package dev.kiln;

import android.app.ActivityOptions;
import android.app.PendingIntent;
import android.app.RemoteAction;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.os.Handler;
import android.os.LocaleList;
import android.text.TextUtils;
import android.util.Log;
import android.util.SparseArray;
import android.view.Menu;
import android.view.MenuItem;
import android.view.textclassifier.TextClassification;
import android.view.textclassifier.TextClassificationContext;
import android.view.textclassifier.TextClassificationManager;
import android.view.textclassifier.TextClassifier;
import android.view.textclassifier.TextSelection;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

/**
 * The platform services behind the text toolbar's platform items: TextClassifier sessions for
 * smart selection (PlatformSelectionBehaviorsImpl), the classifications the toolbar's actions come
 * from (TextToolbarHelperApi28, TextClassificationHelperApi28), ACTION_PROCESS_TEXT activities
 * (ProcessTextApi23Impl) and Look Up (LookUpText). Classifier calls run on background threads
 * (Dispatchers.IO) with Compose's timeouts; results come back to native on the main thread.
 */
final class KilnTextServices {
    private static final String TAG = "TextClassification";
    private static final long TEXT_CLASSIFIER_INITIALIZATION_TIMEOUT_MILLIS = 300;
    private static final long TEXT_CLASSIFICATION_TIMEOUT_MILLIS = 200;
    // Intent.ACTION_LOOK_UP_TEXT (API 37).
    private static final String ACTION_LOOK_UP_TEXT = "android.intent.action.LOOK_UP_TEXT";

    private final Context context;
    private final Handler main;
    private final ExecutorService io = Executors.newCachedThreadPool(r -> {
        Thread t = new Thread(r, "kiln-textclassifier");
        t.setDaemon(true);
        return t;
    });

    private static final class Session {
        final String widgetType;
        final Object lock = new Object();
        TextClassifier classifier;   // made on first use, off the main thread
        Session(String widgetType) { this.widgetType = widgetType; }
    }
    private final SparseArray<Session> sessions = new SparseArray<>();
    private int lastSession;

    /** A kept classification and its action icons (index 0, or shouldShowIcon). */
    private static final class Classification {
        final TextClassification classification;
        final Drawable[] icons;
        Classification(TextClassification c, Drawable[] icons) { this.classification = c; this.icons = icons; }
    }
    private final SparseArray<Classification> classifications = new SparseArray<>();
    private int lastClassification;

    private List<ResolveInfo> processTextActivities = new ArrayList<>();

    KilnTextServices(Context context, Handler main) {
        this.context = context;
        this.main = main;
    }

    /** Destroys the sessions and stops the background threads. */
    void shutdown() {
        int[] ids = new int[sessions.size()];
        for (int i = 0; i < ids.length; ++i) ids[i] = sessions.keyAt(i);
        for (int id : ids) destroySession(id);
        classifications.clear();
        io.shutdown();
    }

    // --- TextClassifier sessions -----------------------------------------------------------------

    /** type 0: EditableText (WIDGET_TYPE_EDITTEXT), 1: StaticText (WIDGET_TYPE_TEXTVIEW). */
    int createSession(int type) {
        int id = ++lastSession;
        sessions.put(id, new Session(type == 0 ? TextClassifier.WIDGET_TYPE_EDITTEXT : TextClassifier.WIDGET_TYPE_TEXTVIEW));
        return id;
    }

    void destroySession(int id) {
        Session s = sessions.get(id);
        if (s == null) return;
        sessions.remove(id);
        io.execute(() -> {
            synchronized (s.lock) {
                if (s.classifier != null && !s.classifier.isDestroyed()) s.classifier.destroy();
                s.classifier = null;
            }
        });
    }

    /** requireTextClassificationSession's session: created (300 ms at most) when missing or destroyed. */
    private TextClassifier requireSession(Session s) {
        synchronized (s.lock) {
            if (s.classifier != null && !s.classifier.isDestroyed()) return s.classifier;
            Future<TextClassifier> f = io.submit(() -> {
                TextClassificationManager m = context.getSystemService(TextClassificationManager.class);
                TextClassificationContext c = new TextClassificationContext.Builder(context.getPackageName(), s.widgetType).build();
                return m.createTextClassificationSession(c);
            });
            try {
                s.classifier = f.get(TEXT_CLASSIFIER_INITIALIZATION_TIMEOUT_MILLIS, TimeUnit.MILLISECONDS);
            } catch (Exception e) {
                f.cancel(true);
                s.classifier = null;
            }
            return s.classifier;
        }
    }

    private static LocaleList localesOf(String[] tags) {
        if (tags == null || tags.length == 0) return LocaleList.getDefault();
        return LocaleList.forLanguageTags(TextUtils.join(",", tags));
    }

    private Classification keep(TextClassification c) {
        List<RemoteAction> actions = c.getActions();
        Drawable[] icons = new Drawable[actions.size()];
        for (int i = 0; i < icons.length; ++i) {
            RemoteAction a = actions.get(i);
            if ((i == 0 || a.shouldShowIcon()) && a.getIcon() != null) icons[i] = a.getIcon().loadDrawable(context);
        }
        return new Classification(c, icons);
    }

    /** Runs `block` on the session's classifier within the classification timeout; null on a timeout. */
    private <T> T withSession(Session s, java.util.function.Function<TextClassifier, T> block) {
        TextClassifier tc = requireSession(s);
        if (tc == null) return null;
        Future<T> f = io.submit(() -> block.apply(tc));
        try {
            return f.get(TEXT_CLASSIFICATION_TIMEOUT_MILLIS, TimeUnit.MILLISECONDS);
        } catch (Exception e) {
            f.cancel(true);
            return null;
        }
    }

    private static final class Suggestion {
        int start, end;
        Classification classification;
    }

    /** suggestSelectionForLongPressOrDoubleClick; the result reaches native through KilnNative.classifierResult. */
    void suggestSelection(int sessionId, int request, String text, int start, int end, String[] locales) {
        Session s = sessions.get(sessionId);
        if (s == null) { KilnNative.classifierResult(request, false, 0, 0, 0, null, false); return; }
        LocaleList ll = localesOf(locales);
        io.execute(() -> {
            Suggestion r = withSession(s, tc -> {
                TextSelection.Request req = new TextSelection.Request.Builder(text, start, end)
                        .setDefaultLocales(ll).setIncludeTextClassification(true).build();
                TextSelection sel = tc.suggestSelection(req);
                Suggestion out = new Suggestion();
                out.start = sel.getSelectionStartIndex();
                out.end = sel.getSelectionEndIndex();
                TextClassification c = sel.getTextClassification();
                if (c == null) {
                    c = tc.classifyText(new TextClassification.Request.Builder(text, out.start, out.end).setDefaultLocales(ll).build());
                }
                if (c != null) out.classification = keep(c);
                return out;
            });
            main.post(() -> deliver(request, r != null, r == null ? 0 : r.start, r == null ? 0 : r.end, r == null ? null : r.classification));
        });
    }

    void classifyText(int sessionId, int request, String text, int start, int end, String[] locales) {
        Session s = sessions.get(sessionId);
        if (s == null) { KilnNative.classifierResult(request, false, 0, 0, 0, null, false); return; }
        LocaleList ll = localesOf(locales);
        io.execute(() -> {
            Classification r = withSession(s, tc ->
                    keep(tc.classifyText(new TextClassification.Request.Builder(text, start, end).setDefaultLocales(ll).build())));
            main.post(() -> deliver(request, false, 0, 0, r));
        });
    }

    private void deliver(int request, boolean hasRange, int start, int end, Classification c) {
        int handle = 0;
        String[] titles = null;
        boolean legacy = false;
        if (c != null) {
            handle = ++lastClassification;
            classifications.put(handle, c);
            List<RemoteAction> actions = c.classification.getActions();
            titles = new String[actions.size()];
            for (int i = 0; i < titles.length; ++i) titles[i] = String.valueOf(actions.get(i).getTitle());
            legacy = hasLegacyAssistItem(c.classification);
        }
        KilnNative.classifierResult(request, hasRange, start, end, handle, titles, legacy);
    }

    @SuppressWarnings("deprecation")
    private static boolean hasLegacyAssistItem(TextClassification c) {
        return (c.getIcon() != null || !TextUtils.isEmpty(c.getLabel())) && (c.getIntent() != null || c.getOnClickListener() != null);
    }

    void releaseClassification(int handle) { classifications.remove(handle); }

    // --- the toolbar's classification items (TextToolbarHelperApi28) ----------------------------

    void addClassificationItem(Menu menu, int orderId, int handle, int index) {
        Classification c = classifications.get(handle);
        if (c == null) return;
        if (index < 0) { addLegacyMenuItem(menu, orderId, c.classification); return; }
        List<RemoteAction> actions = c.classification.getActions();
        if (index >= actions.size()) return;
        boolean isPrimary = index == 0;
        RemoteAction remoteAction = actions.get(index);
        MenuItem item = menu.add(android.R.id.textAssist, isPrimary ? android.R.id.textAssist : Menu.NONE, orderId, remoteAction.getTitle());
        item.setShowAsAction(isPrimary ? MenuItem.SHOW_AS_ACTION_ALWAYS : MenuItem.SHOW_AS_ACTION_NEVER);
        Drawable icon = c.icons[index];
        if (icon != null) item.setIcon(icon);
        item.setOnMenuItemClickListener(i -> { sendPendingIntent(remoteAction.getActionIntent()); return true; });
    }

    @SuppressWarnings("deprecation")
    private void addLegacyMenuItem(Menu menu, int orderId, TextClassification c) {
        MenuItem item = menu.add(android.R.id.textAssist, android.R.id.textAssist, orderId, c.getLabel());
        item.setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS);
        item.setIcon(c.getIcon());
        item.setOnMenuItemClickListener(i -> {
            PendingIntent pi = PendingIntent.getActivity(context, c.getText().hashCode(), c.getIntent(),
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            sendPendingIntent(pi);
            return true;
        });
    }

    @SuppressWarnings("deprecation")
    private static void sendPendingIntent(PendingIntent pendingIntent) {
        try {
            if (Build.VERSION.SDK_INT >= 34) {
                ActivityOptions options = ActivityOptions.makeBasic();
                if (Build.VERSION.SDK_INT >= 36) {
                    options.setPendingIntentBackgroundActivityStartMode(ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOW_IF_VISIBLE);
                } else {
                    options.setPendingIntentBackgroundActivityStartMode(ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED);
                }
                pendingIntent.send(options.toBundle());
            } else {
                pendingIntent.send();
            }
        } catch (PendingIntent.CanceledException e) {
            Log.e(TAG, "error sending pendingIntent: " + pendingIntent + " error: " + e);
        }
    }

    // --- PROCESS_TEXT and Look Up ----------------------------------------------------------------

    private static Intent createProcessTextIntent() {
        return new Intent().setAction(Intent.ACTION_PROCESS_TEXT).setType("text/plain");
    }

    private boolean hasPermission(ResolveInfo info) {
        if (context.getPackageName().equals(info.activityInfo.packageName)) return true;
        ActivityInfo a = info.activityInfo;
        return a.exported && (a.permission == null || context.checkSelfPermission(a.permission) == PackageManager.PERMISSION_GRANTED);
    }

    /** The activities' labels; a later processText(index) starts one of these. */
    String[] processTextActivities() {
        PackageManager pm = context.getPackageManager();
        List<ResolveInfo> found = new ArrayList<>();
        for (ResolveInfo info : pm.queryIntentActivities(createProcessTextIntent(), 0))
            if (hasPermission(info)) found.add(info);
        processTextActivities = found;
        String[] labels = new String[found.size()];
        for (int i = 0; i < labels.length; ++i) labels[i] = found.get(i).loadLabel(pm).toString();
        return labels;
    }

    void processText(int index, boolean editable, String selected) {
        if (index < 0 || index >= processTextActivities.size()) return;
        ResolveInfo info = processTextActivities.get(index);
        // EXTRA_PROCESS_TEXT_READONLY: whether the text cannot be replaced (TextView's meaning).
        Intent intent = createProcessTextIntent()
                .putExtra(Intent.EXTRA_PROCESS_TEXT_READONLY, !editable)
                .setClassName(info.activityInfo.packageName, info.activityInfo.name)
                .putExtra(Intent.EXTRA_PROCESS_TEXT, selected);
        try {
            context.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "No activity found to handle " + intent, e);
        }
    }

    private static Intent createLookUpTextIntent(String selected) {
        return new Intent(ACTION_LOOK_UP_TEXT).setType("text/plain").putExtra(Intent.EXTRA_TEXT, selected);
    }

    boolean canLookUp(String selected) {
        if (Build.VERSION.SDK_INT < 37) return false;
        return createLookUpTextIntent(selected).resolveActivity(context.getPackageManager()) != null;
    }

    void lookUp(String selected) {
        Intent intent = createLookUpTextIntent(selected);
        try {
            context.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            // The Look Up handler may have been disabled after the menu was shown.
            Log.w("LookUpText", "No activity found to handle " + intent, e);
        }
    }

    // --- the dropdown menu (DefaultTextContextMenuDropdownProvider) -----------------------------

    /** ContextMenuIcons: the action mode drawable attribute of a standard item; 0 for none. */
    static int standardIconAttr(int id) {
        switch (id) {
            case 1: return android.R.attr.actionModeCutDrawable;
            case 2: return android.R.attr.actionModeCopyDrawable;
            case 3: return android.R.attr.actionModePasteDrawable;
            case 4: return android.R.attr.actionModeSelectAllDrawable;
            case 6: return Build.VERSION.SDK_INT >= 37 ? Resources.getSystem().getIdentifier("actionModeLookUpDrawable", "attr", "android") : 0;
            default: return 0;
        }
    }

    /** IconBox: the drawable a theme attribute names, drawn `size` px square; null for none. */
    int[] themeIcon(int attr, int size) {
        if (attr == 0) return null;
        android.content.res.TypedArray a = context.obtainStyledAttributes(new int[] { attr });
        int res = a.getResourceId(0, -1);
        a.recycle();
        if (res == -1) return null;
        return pixels(context.getDrawable(res), size);
    }

    /** A drawable as {width, height, ARGB...} (non-premultiplied), fitted in a square of `size`. */
    static int[] pixels(Drawable d, int size) {
        if (d == null || size <= 0) return null;
        int iw = d.getIntrinsicWidth(), ih = d.getIntrinsicHeight();
        int w = size, h = size;
        if (iw > 0 && ih > 0) {   // ContentScale.Fit
            float s = Math.min(size / (float) iw, size / (float) ih);
            w = Math.max(1, Math.round(iw * s));
            h = Math.max(1, Math.round(ih * s));
        }
        android.graphics.Bitmap b = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.Canvas canvas = new android.graphics.Canvas(b);
        d.setBounds(0, 0, w, h);
        d.draw(canvas);
        int[] out = new int[2 + w * h];
        out[0] = w;
        out[1] = h;
        b.getPixels(out, 2, w, 0, 0, w, h);
        b.recycle();
        return out;
    }

    /** A classification item's title (the action's, or the legacy label for index -1). */
    String classificationLabel(int handle, int index) {
        Classification c = classifications.get(handle);
        if (c == null) return "";
        if (index < 0) { CharSequence l = c.classification.getLabel(); return l != null ? l.toString() : ""; }
        List<RemoteAction> actions = c.classification.getActions();
        return index < actions.size() ? actions.get(index).getTitle().toString() : "";
    }

    /** A classification item's icon (the preloaded action icon, or the legacy icon), drawn. */
    @SuppressWarnings("deprecation")
    int[] classificationIcon(int handle, int index, int size) {
        Classification c = classifications.get(handle);
        if (c == null) return null;
        Drawable d = index < 0 ? c.classification.getIcon() : (index < c.icons.length ? c.icons[index] : null);
        if (d == null) return null;
        // IconBox draws it stretched over the 24 dp box.
        android.graphics.Bitmap b = android.graphics.Bitmap.createBitmap(size, size, android.graphics.Bitmap.Config.ARGB_8888);
        Drawable copy = d.getConstantState() != null ? d.getConstantState().newDrawable().mutate() : d;
        copy.setBounds(0, 0, size, size);
        copy.draw(new android.graphics.Canvas(b));
        int[] out = new int[2 + size * size];
        out[0] = size;
        out[1] = size;
        b.getPixels(out, 2, size, 0, 0, size, size);
        b.recycle();
        return out;
    }

    /** A classification item clicked in the dropdown: its action's PendingIntent (or the legacy intent). */
    @SuppressWarnings("deprecation")
    void performClassification(int handle, int index) {
        Classification c = classifications.get(handle);
        if (c == null) return;
        if (index < 0) {
            TextClassification tc = c.classification;
            if (tc.getIntent() == null) return;
            sendPendingIntent(PendingIntent.getActivity(context, tc.getText().hashCode(), tc.getIntent(),
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
            return;
        }
        List<RemoteAction> actions = c.classification.getActions();
        if (index < actions.size()) sendPendingIntent(actions.get(index).getActionIntent());
    }

    /** computeContextMenuColors: {background, enabled text, disabled text}; 0 for the default. */
    int[] contextMenuColors() {
        android.content.res.TypedArray a = context.obtainStyledAttributes(android.R.style.Widget_PopupMenu, new int[] { android.R.attr.colorBackground });
        int background = a.getColor(0, 0);
        a.recycle();
        a = context.obtainStyledAttributes(android.R.style.TextAppearance_Widget_PopupMenu_Large, new int[] { android.R.attr.textColorPrimary });
        android.content.res.ColorStateList list = a.getColorStateList(0);
        a.recycle();
        int enabled = list != null ? list.getColorForState(new int[] { android.R.attr.state_enabled }, 0) : 0;
        int disabled = list != null ? list.getColorForState(new int[] { -android.R.attr.state_enabled }, 0) : 0;
        return new int[] { background, enabled, disabled };
    }

    /** The platform string for a standard item (TextContextMenuItems' strings). */
    static String standardLabel(Context context, int id) {
        Resources r = context.getResources();
        switch (id) {
            case 1: return r.getString(android.R.string.cut);
            case 2: return r.getString(android.R.string.copy);
            case 3: return r.getString(android.R.string.paste);
            case 4: return r.getString(android.R.string.selectAll);
            case 5: return r.getString(android.R.string.autofill);
            case 6: {
                int s = Resources.getSystem().getIdentifier("lookUp", "string", "android");
                return s != 0 ? Resources.getSystem().getString(s) : "Look up";
            }
            default: return "";
        }
    }

    /** android.R.id for a standard item; 0 for the order id. */
    static int standardItemId(int id) {
        switch (id) {
            case 1: return android.R.id.cut;
            case 2: return android.R.id.copy;
            case 3: return android.R.id.paste;
            case 4: return android.R.id.selectAll;
            case 5: return android.R.id.autofill;
            case 6: return Resources.getSystem().getIdentifier("lookUpText", "id", "android");
            default: return 0;
        }
    }
}
