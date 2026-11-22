package dev.kiln;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProviderInfo;
import android.appwidget.AppWidgetProvider;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.LocaleList;
import android.util.Log;
import android.util.SizeF;
import android.widget.RemoteViews;

import dev.kiln.bridge.Codec;
import dev.kiln.bridge.Completion;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A kiln::glance widget's receiver (GlanceAppWidgetReceiver). Each widget has its own subclass
 * (kiln-pack generates them for WIDGETS; a Gradle app writes {@code class X : KilnWidgetReceiver()})
 * declared with the appwidget-provider info and {@code <meta-data android:name="dev.kiln.widget"
 * android:value="name">}, the name the C++ side registered. Drawing, callbacks and deletions run
 * as native tasks (the widget's state is read, provideGlance runs) while the broadcast is kept
 * alive with goAsync.
 */
public class KilnWidgetReceiver extends AppWidgetProvider {
    private static Context appContext;

    // glance::Draw for each size: the result's bytes are found, failed, the error layout, then
    // each drawn size and its content.
    static native void draw(String widget, int appWidgetId, float[] launcherSizes, float minWidth, float minHeight, Object[] options, Completion done);
    // A callback (key) or a lambda of the widget's drawing (lambda), with its parameters.
    static native void runCallback(String widget, int appWidgetId, String key, String lambda, byte[] params, Completion done);
    static native void deleted(String widget, int appWidgetId, Completion done);
    static native boolean redrawsOnResize(String widget, int sdkInt);

    static void attach(Context context) {
        if (appContext == null) appContext = context.getApplicationContext();
    }

    private static void ensureLoaded(Context context) {
        KilnProcess.ensureLoaded(context);
    }

    /** This receiver's widget name (its dev.kiln.widget meta-data). */
    private String widgetName(Context context) {
        try {
            ActivityInfo info = context.getPackageManager().getReceiverInfo(new ComponentName(context, getClass()), PackageManager.GET_META_DATA);
            if (info.metaData != null) return info.metaData.getString("dev.kiln.widget");
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        return null;
    }

    /** Runs `count` pieces of work, then finishes the broadcast. */
    private static final class Pending {
        private final BroadcastReceiver.PendingResult result;
        private int left;
        Pending(BroadcastReceiver.PendingResult result, int count) {
            this.result = result;
            this.left = count;
            if (count == 0) finish();
        }
        void done() {
            if (--left == 0) finish();
        }
        private void finish() {
            if (result != null) result.finish();
        }
    }

    @Override
    public void onUpdate(Context context, AppWidgetManager manager, int[] ids) {
        ensureLoaded(context);
        String name = widgetName(context);
        Pending p = new Pending(goAsync(), ids.length);
        for (int id : ids) draw(context, manager, new ComponentName(context, getClass()), name, id, p::done);
    }

    @Override
    public void onAppWidgetOptionsChanged(Context context, AppWidgetManager manager, int id, Bundle options) {
        ensureLoaded(context);
        String name = widgetName(context);
        // GlanceAppWidget.resize: Single, and Responsive past API 31, need no redraw.
        if (name == null || !redrawsOnResize(name, Build.VERSION.SDK_INT)) return;
        Pending p = new Pending(goAsync(), 1);
        draw(context, manager, new ComponentName(context, getClass()), name, id, p::done);
    }

    @Override
    public void onDeleted(Context context, int[] ids) {
        ensureLoaded(context);
        String name = widgetName(context);
        if (name == null) return;
        Pending p = new Pending(goAsync(), ids.length);
        for (int id : ids) deleted(name, id, completion(p::done, "delete " + name));
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (KilnGlance.ACTION_RUN.equals(intent.getAction())) {
            ensureLoaded(context);
            String name = widgetName(context);
            int id = intent.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, 0);
            Bundle params = intent.getBundleExtra(KilnGlance.EXTRA_PARAMS);
            if (params == null) params = new Bundle();
            // A toggle's new state (ActionCallbackBroadcastReceiver: ToggleableStateKey).
            if (intent.hasExtra(RemoteViews.EXTRA_CHECKED)) params.putBoolean(RemoteViews.EXTRA_CHECKED, intent.getBooleanExtra(RemoteViews.EXTRA_CHECKED, false));
            Pending p = new Pending(goAsync(), 1);
            final String widget = name;
            runCallback(name, id, intent.getStringExtra(KilnGlance.EXTRA_KEY), intent.getStringExtra(KilnGlance.EXTRA_LAMBDA),
                    KilnGlance.bundleToParams(params), completion(() -> {
                        // Redrawn after its callback, as Glance does.
                        drawAll(context, widget, p::done);
                    }, "callback in " + name));
            return;
        }
        if (Intent.ACTION_LOCALE_CHANGED.equals(intent.getAction()) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) {
            // Redrawn in the new language, as GlanceAppWidgetReceiver does, and after an update of
            // the app (MyPackageReplacedReceiver), whose layouts may have changed.
            ensureLoaded(context);
            Pending p = new Pending(goAsync(), 1);
            drawAll(context, widgetName(context), p::done);
            return;
        }
        super.onReceive(context, intent);
    }

    /** A completion that runs `then` either way, logging a failure. */
    private static Completion completion(Runnable then, String what) {
        return new Completion() {
            @Override public void resume(byte[] value) { then.run(); }
            @Override public void fail(String type, String message) {
                Log.e("kiln", "widget: " + what + ": " + type + ": " + message);
                then.run();
            }
        };
    }

    /** The launcher's sizes for an instance (dp), or estimates. */
    private static ArrayList<SizeF> launcherSizes(Bundle options) {
        ArrayList<SizeF> sizes = sizes(options);
        if (sizes == null || sizes.isEmpty()) sizes = estimateSizes(options);
        return sizes;
    }

    static void draw(Context context, AppWidgetManager manager, ComponentName receiver, String name, int id, Runnable after) {
        if (name == null) {
            Log.e("kiln", "widget: " + receiver.getClassName() + " has no dev.kiln.widget meta-data");
            after.run();
            return;
        }
        Bundle options = manager.getAppWidgetOptions(id);
        ArrayList<SizeF> sizes = launcherSizes(options);
        float[] launcher = new float[sizes.size() * 2];
        for (int i = 0; i < sizes.size(); i++) {
            launcher[2 * i] = sizes.get(i).getWidth();
            launcher[2 * i + 1] = sizes.get(i).getHeight();
        }
        // Glance's appWidgetMinSize: the provider's minimum (its resize minimum when it resizes).
        float minWidth = 0, minHeight = 0;
        AppWidgetProviderInfo info = manager.getAppWidgetInfo(id);
        if (info != null) {
            float density = context.getResources().getDisplayMetrics().density;
            int w = info.minWidth, h = info.minHeight;
            if ((info.resizeMode & AppWidgetProviderInfo.RESIZE_HORIZONTAL) != 0) w = Math.min(w, info.minResizeWidth);
            if ((info.resizeMode & AppWidgetProviderInfo.RESIZE_VERTICAL) != 0) h = Math.min(h, info.minResizeHeight);
            minWidth = w / density;
            minHeight = h / density;
        }
        draw(name, id, launcher, minWidth, minHeight, KilnBundles.fromBundle(options), new Completion() {
            @Override public void resume(byte[] value) {
                try {
                    show(context, manager, receiver, name, id, value);
                } catch (RuntimeException e) {
                    Log.e("kiln", "widget " + name + ": cannot draw", e);
                }
                after.run();
            }
            @Override public void fail(String type, String message) {
                Log.e("kiln", "widget " + name + ": cannot draw: " + type + ": " + message);
                after.run();
            }
        });
    }

    /** A drawing's RemoteViews, or the error view. */
    private static void show(Context context, AppWidgetManager manager, ComponentName receiver, String name, int id, byte[] value) {
        Codec.Reader r = new Codec.Reader(value);
        if (!r.readBoolean()) {
            Log.e("kiln", "widget: no widget named " + name + " (register it with a static glance::WidgetRegistration)");
            return;
        }
        boolean failed = r.readBoolean();
        String errorLayout = r.readString();
        if (failed) {
            if (!errorLayout.isEmpty()) manager.updateAppWidget(id, errorView(context, errorLayout));
            return;
        }
        int count = r.readInt();
        float[] drawnSizes = new float[count * 2];
        byte[][] contents = new byte[count][];
        for (int i = 0; i < count; i++) {
            drawnSizes[2 * i] = r.readFloat();
            drawnSizes[2 * i + 1] = r.readFloat();
            contents[i] = r.readBytes();
        }
        manager.updateAppWidget(id, views(context, receiver, id, drawnSizes, contents));
    }

    private static RemoteViews views(Context context, ComponentName receiver, int id, float[] drawnSizes, byte[][] contents) {
        if (contents.length == 1) return KilnGlance.build(context, contents[0], id, receiver);
        // One layout per size, the launcher choosing (RemoteViews(Map<SizeF, RemoteViews>)).
        Map<SizeF, RemoteViews> map = new HashMap<>();
        for (int i = 0; i < contents.length; i++)
            map.put(new SizeF(drawnSizes[2 * i], drawnSizes[2 * i + 1]), KilnGlance.build(context, contents[i], id, receiver));
        return new RemoteViews(map);
    }

    /** onCompositionError's default: the error layout, its text in the launcher's language. */
    private static RemoteViews errorView(Context context, String layout) {
        int layoutId = context.getResources().getIdentifier(layout, "layout", context.getPackageName());
        RemoteViews rv = new RemoteViews(context.getPackageName(), layoutId);
        if ("kiln_glance_error".equals(layout)) {
            int text = context.getResources().getIdentifier("kiln_glance_error_text", "id", context.getPackageName());
            rv.setTextViewText(text, errorText(context.getResources().getConfiguration().getLocales()));
        }
        return rv;
    }

    /** glance_error_layout_text_v2 in the launcher's language. */
    private static String errorText(LocaleList locales) {
        return KilnGlanceStrings.pick(KilnGlanceStrings.ERROR_TEXT, locales);
    }

    /**
     * Without OPTION_APPWIDGET_SIZES, Glance's estimateSizes: portrait is the minimum width and
     * maximum height, landscape the maximum width and minimum height; none when unknown.
     */
    private static ArrayList<SizeF> estimateSizes(Bundle options) {
        ArrayList<SizeF> out = new ArrayList<>();
        int minHeight = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, 0);
        int maxHeight = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_HEIGHT, 0);
        int minWidth = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH, 0);
        int maxWidth = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_WIDTH, 0);
        if (minHeight == 0 || maxHeight == 0 || minWidth == 0 || maxWidth == 0) return out;
        out.add(new SizeF(minWidth, maxHeight));
        out.add(new SizeF(maxWidth, minHeight));
        return out;
    }

    /** OPTION_APPWIDGET_SIZES (the typed getParcelableArrayList is API 33). */
    @SuppressWarnings("deprecation")
    private static ArrayList<SizeF> sizes(Bundle options) {
        if (Build.VERSION.SDK_INT >= 33) return options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES, SizeF.class);
        return options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES);
    }

    /** The receivers of a widget: this package's with that dev.kiln.widget name. */
    private static List<ComponentName> receivers(Context context, String name) {
        List<ComponentName> out = new ArrayList<>();
        try {
            PackageInfo p = context.getPackageManager().getPackageInfo(context.getPackageName(),
                    PackageManager.GET_RECEIVERS | PackageManager.GET_META_DATA);
            if (p.receivers != null)
                for (ActivityInfo r : p.receivers)
                    if (r.metaData != null && name.equals(r.metaData.getString("dev.kiln.widget"))) out.add(new ComponentName(r.packageName, r.name));
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        return out;
    }

    /** Draws every placed instance, then runs `after`. */
    private static void drawAll(Context c, String name, Runnable after) {
        if (c == null || name == null) {
            after.run();
            return;
        }
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        List<ComponentName> rs = receivers(c, name);
        List<int[]> work = new ArrayList<>();
        for (ComponentName r : rs) for (int id : manager.getAppWidgetIds(r)) work.add(new int[] { rs.indexOf(r), id });
        if (work.isEmpty()) {
            after.run();
            return;
        }
        int[] left = { work.size() };
        for (int[] w : work) draw(c, manager, rs.get(w[0]), name, w[1], () -> {
            if (--left[0] == 0) after.run();
        });
    }

    // --- glance::GlanceHost -----------------------------------------------------------------

    static void updateAll(String name) {
        drawAll(appContext, name, () -> {});
    }

    static void update(String name, int id) {
        Context c = appContext;
        if (c == null || name == null) return;
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        for (ComponentName r : receivers(c, name))
            for (int placed : manager.getAppWidgetIds(r))
                if (placed == id) draw(c, manager, r, name, id, () -> {});
    }

    static int[] appWidgetIds(String name) {
        Context c = appContext;
        if (c == null) return new int[0];
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        List<Integer> all = new ArrayList<>();
        for (ComponentName r : receivers(c, name)) for (int id : manager.getAppWidgetIds(r)) all.add(id);
        int[] out = new int[all.size()];
        for (int i = 0; i < out.length; i++) out[i] = all.get(i);
        return out;
    }

    /** GlanceAppWidgetManager.getAppWidgetSizes: width, height pairs in dp. */
    static float[] appWidgetSizes(int id) {
        Context c = appContext;
        if (c == null) return new float[0];
        ArrayList<SizeF> sizes = launcherSizes(AppWidgetManager.getInstance(c).getAppWidgetOptions(id));
        float[] out = new float[sizes.size() * 2];
        for (int i = 0; i < sizes.size(); i++) {
            out[2 * i] = sizes.get(i).getWidth();
            out[2 * i + 1] = sizes.get(i).getHeight();
        }
        return out;
    }

    /** requestPinGlanceAppWidget: a drawing for the dialog, and the app opened once placed. */
    static boolean requestPin(String name, byte[] preview, String successDeepLink) {
        Context c = appContext;
        if (c == null) return false;
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        List<ComponentName> r = receivers(c, name);
        if (r.isEmpty() || !manager.isRequestPinAppWidgetSupported()) return false;
        Bundle extras = null;
        if (preview != null && preview.length > 0) {
            extras = new Bundle();
            extras.putParcelable(AppWidgetManager.EXTRA_APPWIDGET_PREVIEW, KilnGlance.build(c, preview, 0, r.get(0)));
        }
        PendingIntent success = null;
        if (successDeepLink != null && !successDeepLink.isEmpty()) {
            Intent open = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
            if (open == null) open = new Intent().setClassName(c.getPackageName(), "dev.kiln.KilnActivity");
            open.setAction(Intent.ACTION_VIEW).setData(Uri.parse(successDeepLink)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            success = PendingIntent.getActivity(c, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        }
        return manager.requestPinAppWidget(r.get(0), extras, success);
    }

    /** The provider's minimum size in dp (its resize minimum when it resizes), as width, height. */
    static float[] minimumSize(String name) {
        Context c = appContext;
        if (c == null) return new float[] { 0, 0 };
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        List<ComponentName> r = receivers(c, name);
        for (AppWidgetProviderInfo info : manager.getInstalledProvidersForPackage(c.getPackageName(), null)) {
            if (r.isEmpty() || !info.provider.equals(r.get(0))) continue;
            float density = c.getResources().getDisplayMetrics().density;
            int w = info.minWidth, h = info.minHeight;
            if ((info.resizeMode & AppWidgetProviderInfo.RESIZE_HORIZONTAL) != 0) w = Math.min(w, info.minResizeWidth);
            if ((info.resizeMode & AppWidgetProviderInfo.RESIZE_VERTICAL) != 0) h = Math.min(h, info.minResizeHeight);
            return new float[] { w / density, h / density };
        }
        return new float[] { 0, 0 };
    }

    /** setWidgetPreview (API 35): the generated preview for a category; false when rate-limited. */
    static boolean setWidgetPreview(String name, int category, float[] sizes, byte[][] contents) {
        Context c = appContext;
        if (c == null) return false;
        List<ComponentName> r = receivers(c, name);
        if (r.isEmpty()) return false;
        if (Build.VERSION.SDK_INT < 35) {
            Log.w("kiln", "widget: generated previews need API 35");
            return false;
        }
        return AppWidgetManager.getInstance(c).setWidgetPreview(r.get(0), category, views(c, r.get(0), 0, sizes, contents));
    }

}
