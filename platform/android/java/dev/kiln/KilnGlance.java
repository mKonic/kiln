package dev.kiln;

import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.net.Uri;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.StrikethroughSpan;
import android.text.style.StyleSpan;
import android.text.style.TypefaceSpan;
import android.text.style.UnderlineSpan;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RemoteViews;

import dev.kiln.bridge.Codec;

import java.util.HashMap;
import java.util.Map;

/**
 * kiln::glance's RemoteViews translation (glance-appwidget RemoteViewsTranslator and
 * translators/): the content's elements, encoded by native code, become nested RemoteViews of
 * kiln's layouts (res/layout/kiln_glance_*), with sizes, padding and corners set at runtime (API 31).
 */
final class KilnGlance {
    static final String ACTION_RUN = "dev.kiln.widget.RUN";
    static final String EXTRA_KEY = "dev.kiln.widget.key", EXTRA_PARAMS = "dev.kiln.widget.params",
            EXTRA_START = "dev.kiln.widget.start";

    // The element kinds and modifier codes of kiln/glance/Glance.cpp.
    private static final int BOX = 1, ROW = 2, COLUMN = 3, TEXT = 4, IMAGE = 5, BUTTON = 6, SPACER = 7,
            LINEAR_PROGRESS = 8, CIRCULAR_PROGRESS = 9, CHECK_BOX = 10, SWITCH = 11, LAZY_COLUMN = 12, LIST_ITEM = 13;
    private static final int M_WIDTH = 0, M_HEIGHT = 1, M_PADDING = 2, M_BACKGROUND_COLOR = 3, M_BACKGROUND_RESOURCE = 4,
            M_CORNER_RADIUS = 5, M_CLICKABLE = 6, M_VISIBILITY = 7, M_CONTENT_DESCRIPTION = 8, M_ENABLED = 9;
    private static final int D_WRAP = 0, D_FILL = 1, D_EXPAND = 2, D_DP = 3;
    private static final int A_NONE = 0, A_START_ACTIVITY = 1, A_RUN_CALLBACK = 2;

    private final Context context;
    private final String pkg;
    private final int appWidgetId;
    private final ComponentName receiver;
    private final float density;
    private final Map<String, Integer> ids = new HashMap<>();
    private int requests;   // distinct PendingIntents: each tap target its own request
    private boolean inList;   // items of a collection use fill-in intents

    private KilnGlance(Context context, int appWidgetId, ComponentName receiver) {
        this.context = context;
        this.pkg = context.getPackageName();
        this.appWidgetId = appWidgetId;
        this.receiver = receiver;
        this.density = context.getResources().getDisplayMetrics().density;
    }

    /** The RemoteViews for one size of a widget's content. */
    static RemoteViews build(Context context, byte[] content, int appWidgetId, ComponentName receiver) {
        return new KilnGlance(context, appWidgetId, receiver).root(new Codec.Reader(content));
    }

    private int res(String name, String type) {
        String key = type + "/" + name;
        Integer id = ids.get(key);
        if (id == null) {
            id = context.getResources().getIdentifier(name, type, pkg);
            ids.put(key, id);
        }
        return id;
    }
    private int view() { return res("kiln_glance_view", "id"); }
    private int wrap() { return res("kiln_glance_wrap", "id"); }
    private RemoteViews layout(String name) { return new RemoteViews(pkg, res("kiln_glance_" + name, "layout")); }
    // Children go in with addStableView (as Glance adds them on API 31): an update the launcher
    // reapplies to the views it has (the same layout) reuses each child by its stable id, and
    // drops the rest, instead of adding them again.
    private RemoteViews container(String name, int id) { return layout(name); }

    private static final class Modifiers {
        int widthKind = -1, heightKind = -1;
        float widthDp, heightDp;
    }
    private static final class Built {
        RemoteViews views;
        Modifiers size;
    }

    private RemoteViews root(Codec.Reader r) {
        if (r.readInt() != 1) throw new IllegalArgumentException("kiln glance: an unknown format");
        // Each update of a widget alternates two identical root layouts: the launcher then applies
        // it afresh instead of reapplying it to the views it has, which leaves a collection's rows
        // undrawn.
        boolean alt = (generations.merge(appWidgetId, 1, Integer::sum) & 1) == 0;
        RemoteViews root = container(alt ? "root_alt" : "root", view());
        int count = r.readInt();
        for (int i = 0; i < count; i++) add(root, BOX, 0, element(r), i);
        // A collection's adapter works only from the root RemoteViews (in a nested one the parent
        // it is applied under is not the AppWidgetHostView).
        for (int i = 0; i < lists.size(); i++) {
            int id = res("kiln_glance_list_" + i, "id");
            root.setRemoteAdapter(id, lists.get(i));
            Intent template = new Intent(ACTION_RUN).setComponent(receiver);
            root.setPendingIntentTemplate(id, PendingIntent.getBroadcast(context, ++requests, template,
                    PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
        }
        return root;
    }

    /** Adds a child to a container, in the wrapper its parent needs (weights, Box alignment). */
    private void add(RemoteViews parent, int parentKind, int parentGravity, Built child, int index) {
        RemoteViews views = child.views;
        if (parentKind == ROW && child.size.widthKind == D_EXPAND) {
            views = wrapped(child.size.heightKind == D_FILL ? "weight_h_match" : "weight_h_wrap", views);
        } else if (parentKind == COLUMN && child.size.heightKind == D_EXPAND) {
            views = wrapped(child.size.widthKind == D_FILL ? "weight_v_match" : "weight_v_wrap", views);
        } else if (parentKind == BOX && parentGravity != 0 && parentGravity != (0x00800003 | 0x30)) {
            RemoteViews align = container("align", wrap());
            align.setInt(wrap(), "setGravity", parentGravity);
            align.addStableView(wrap(), views, 0);
            views = align;
        }
        parent.addStableView(view(), views, index);
    }
    private RemoteViews wrapped(String layout, RemoteViews child) {
        RemoteViews w = container(layout, wrap());
        w.addStableView(wrap(), child, 0);
        return w;
    }

    private Built element(Codec.Reader r) {
        int kind = r.readInt();
        // The modifiers come first; applied once the view exists.
        int modifierCount = r.readInt();
        int[] modifierKinds = new int[modifierCount];
        Object[] modifierValues = new Object[modifierCount];
        Modifiers size = new Modifiers();
        for (int i = 0; i < modifierCount; i++) {
            int m = r.readInt();
            modifierKinds[i] = m;
            switch (m) {
                case M_WIDTH: case M_HEIGHT: {
                    int k = r.readInt();
                    float dp = r.readFloat();
                    if (m == M_WIDTH) { size.widthKind = k; size.widthDp = dp; } else { size.heightKind = k; size.heightDp = dp; }
                    break;
                }
                case M_PADDING: case M_CORNER_RADIUS:
                    modifierValues[i] = new float[] { r.readFloat(), r.readFloat(), r.readFloat(), r.readFloat() };
                    break;
                case M_BACKGROUND_COLOR: modifierValues[i] = readColor(r); break;
                case M_BACKGROUND_RESOURCE: case M_CONTENT_DESCRIPTION: modifierValues[i] = r.readString(); break;
                case M_CLICKABLE: modifierValues[i] = readAction(r); break;
                case M_VISIBILITY: case M_ENABLED: modifierValues[i] = r.readInt(); break;
                default: throw new IllegalArgumentException("kiln glance: an unknown modifier " + m);
            }
        }
        Codec.Reader props = new Codec.Reader(r.readBytes());
        int childCount = r.readInt();

        RemoteViews rv;
        int v = view();
        switch (kind) {
            case BOX: case ROW: case COLUMN: {
                int gravity = props.readInt();
                rv = container(kind == BOX ? "box" : kind == ROW ? "row" : "column", v);
                if (kind != BOX) rv.setInt(v, "setGravity", gravity);
                for (int i = 0; i < childCount; i++) add(rv, kind, gravity, element(r), i);
                childCount = 0;
                break;
            }
            case TEXT: {
                rv = layout("text");
                String text = props.readString();
                float sizeSp = props.readFloat();
                int[] color = readColor(props);
                int weight = props.readInt();
                boolean italic = props.readBoolean();
                int decoration = props.readInt();
                int gravity = props.readInt();
                int maxLines = props.readInt();
                rv.setTextViewText(v, styled(text, weight, italic, decoration));
                if (sizeSp > 0) rv.setTextViewTextSize(v, TypedValue.COMPLEX_UNIT_SP, sizeSp);
                if (color != null) rv.setColorInt(v, "setTextColor", color[0], color[1]);
                rv.setInt(v, "setGravity", gravity);
                if (maxLines > 0) rv.setInt(v, "setMaxLines", maxLines);
                break;
            }
            case IMAGE: {
                String resource = props.readString();
                byte[] bytes = props.readBytes();
                String description = props.readString();
                int scale = props.readInt();
                int[] tint = readColor(props);
                float alpha = props.readFloat();
                rv = layout(scale == 1 ? "image_crop" : scale == 2 ? "image_fill" : "image_fit");
                if (!resource.isEmpty()) {
                    rv.setImageViewResource(v, res(resource, "drawable"));
                } else if (bytes.length > 0) {
                    Bitmap b = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
                    if (b != null) rv.setImageViewBitmap(v, b);
                }
                if (!description.isEmpty()) rv.setContentDescription(v, description);
                if (tint != null) rv.setColorInt(v, "setColorFilter", tint[0], tint[1]);
                if (alpha < 1) rv.setInt(v, "setImageAlpha", Math.round(alpha * 255));
                break;
            }
            case BUTTON: {
                rv = layout("button");
                rv.setTextViewText(v, props.readString());
                Object[] action = readAction(props);
                boolean enabled = props.readBoolean();
                int[] background = readColor(props), content = readColor(props);
                applyAction(rv, v, action);
                rv.setBoolean(v, "setEnabled", enabled);
                if (background != null) rv.setColorStateList(v, "setBackgroundTintList", ColorStateList.valueOf(background[0]), ColorStateList.valueOf(background[1]));
                if (content != null) rv.setColorInt(v, "setTextColor", content[0], content[1]);
                break;
            }
            case SPACER: rv = layout("spacer"); break;
            case LINEAR_PROGRESS: {
                rv = layout("progress_linear");
                float progress = props.readFloat();
                int[] color = readColor(props), background = readColor(props);
                rv.setProgressBar(v, 1000, progress < 0 ? 0 : Math.round(Math.min(progress, 1f) * 1000), progress < 0);
                if (color != null) {
                    rv.setColorStateList(v, "setProgressTintList", ColorStateList.valueOf(color[0]), ColorStateList.valueOf(color[1]));
                    rv.setColorStateList(v, "setIndeterminateTintList", ColorStateList.valueOf(color[0]), ColorStateList.valueOf(color[1]));
                }
                if (background != null) rv.setColorStateList(v, "setProgressBackgroundTintList", ColorStateList.valueOf(background[0]), ColorStateList.valueOf(background[1]));
                break;
            }
            case CIRCULAR_PROGRESS: {
                rv = layout("progress_circular");
                int[] color = readColor(props);
                if (color != null) rv.setColorStateList(v, "setIndeterminateTintList", ColorStateList.valueOf(color[0]), ColorStateList.valueOf(color[1]));
                break;
            }
            case CHECK_BOX: case SWITCH: {
                rv = layout(kind == CHECK_BOX ? "checkbox" : "switch");
                boolean checked = props.readBoolean();
                Object[] action = readAction(props);
                String text = props.readString();
                int[] color = readColor(props);
                rv.setCompoundButtonChecked(v, checked);
                if (!text.isEmpty()) rv.setTextViewText(v, text);
                if (color != null) rv.setColorInt(v, "setTextColor", color[0], color[1]);
                if ((int) action[0] == A_RUN_CALLBACK) {
                    // The system adds EXTRA_CHECKED to a mutable intent.
                    Intent i = callbackIntent((String) action[2], (String) action[3]);
                    if (inList) {
                        rv.setOnCheckedChangeResponse(v, RemoteViews.RemoteResponse.fromFillInIntent(i));
                    } else {
                        PendingIntent pi = PendingIntent.getBroadcast(context, ++requests, i, PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
                        rv.setOnCheckedChangeResponse(v, RemoteViews.RemoteResponse.fromPendingIntent(pi));
                    }
                }
                break;
            }
            case LAZY_COLUMN: {
                if (lists.size() >= MAX_LISTS) throw new IllegalArgumentException("kiln glance: at most " + MAX_LISTS + " LazyColumns in a widget");
                v = res("kiln_glance_list_" + lists.size(), "id");
                rv = layout("list_" + lists.size());
                RemoteViews.RemoteCollectionItems.Builder items = new RemoteViews.RemoteCollectionItems.Builder().setHasStableIds(true);
                boolean wasInList = inList;
                inList = true;
                for (int i = 0; i < childCount; i++) {
                    Built item = element(r);
                    Long id = itemIds.remove(item);
                    items.addItem(id != null ? id : i, item.views);
                }
                inList = wasInList;
                childCount = 0;
                // Set on the root (root()); taps in items are fill-in intents on a template that
                // broadcasts to the receiver.
                lists.add(items.setViewTypeCount(1).build());
                break;
            }
            case LIST_ITEM: {
                long id = props.readLong();
                Built b = new Built();
                if (childCount == 1) {
                    // One element: it is the item's view (a recycled row reapplies it in place),
                    // the row's width unless it sets its own (Glance's items fill the list's width).
                    b = element(r);
                    if (b.size.widthKind < 0) b.views.setViewLayoutWidth(view(), ViewGroup.LayoutParams.MATCH_PARENT, TypedValue.COMPLEX_UNIT_PX);
                } else {
                    rv = container("item", wrap());
                    for (int i = 0; i < childCount; i++) rv.addStableView(wrap(), element(r).views, i);
                    applyModifiers(rv, wrap(), modifierKinds, modifierValues, size);
                    b.views = rv;
                    b.size = size;
                }
                itemIds.put(b, id);
                return b;
            }
            default: throw new IllegalArgumentException("kiln glance: an unknown element " + kind);
        }
        for (int i = 0; i < childCount; i++) element(r);   // children of a leaf: none expected
        applyModifiers(rv, v, modifierKinds, modifierValues, size);
        Built b = new Built();
        b.views = rv;
        b.size = size;
        return b;
    }
    private final Map<Built, Long> itemIds = new HashMap<>();
    private static final Map<Integer, Integer> generations = new HashMap<>();
    private static final int MAX_LISTS = 4;
    private final java.util.List<RemoteViews.RemoteCollectionItems> lists = new java.util.ArrayList<>();

    private void applyModifiers(RemoteViews rv, int v, int[] kinds, Object[] values, Modifiers size) {
        // Sizes (ApplyModifiersApi31Impl.setViewWidth/Height); Expand is the wrapper's, so the view fills it.
        if (size.widthKind >= 0) setDimension(rv, v, true, size.widthKind, size.widthDp);
        if (size.heightKind >= 0) setDimension(rv, v, false, size.heightKind, size.heightDp);
        for (int i = 0; i < kinds.length; i++) {
            Object value = values[i];
            switch (kinds[i]) {
                case M_PADDING: {
                    float[] p = (float[]) value;
                    boolean rtl = context.getResources().getConfiguration().getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
                    rv.setViewPadding(v, px(rtl ? p[2] : p[0]), px(p[1]), px(rtl ? p[0] : p[2]), px(p[3]));
                    break;
                }
                case M_CORNER_RADIUS:
                    rv.setBoolean(v, "setClipToOutline", true);
                    rv.setViewOutlinePreferredRadius(v, ((float[]) value)[0], TypedValue.COMPLEX_UNIT_DIP);
                    break;
                case M_BACKGROUND_COLOR: {
                    int[] c = (int[]) value;
                    if (c != null) rv.setColorInt(v, "setBackgroundColor", c[0], c[1]);
                    break;
                }
                case M_BACKGROUND_RESOURCE: rv.setInt(v, "setBackgroundResource", res((String) value, "drawable")); break;
                case M_CLICKABLE: applyAction(rv, v, (Object[]) value); break;
                case M_VISIBILITY: {
                    int vis = (int) value;
                    rv.setViewVisibility(v, vis == 0 ? View.VISIBLE : vis == 1 ? View.INVISIBLE : View.GONE);
                    break;
                }
                case M_CONTENT_DESCRIPTION: rv.setContentDescription(v, (String) value); break;
                case M_ENABLED: rv.setBoolean(v, "setEnabled", (int) value != 0); break;
                default: break;
            }
        }
    }

    private void setDimension(RemoteViews rv, int v, boolean width, int kind, float dp) {
        float value;
        int unit = TypedValue.COMPLEX_UNIT_PX;
        switch (kind) {
            case D_FILL: case D_EXPAND: value = ViewGroup.LayoutParams.MATCH_PARENT; break;
            case D_DP: value = dp; unit = TypedValue.COMPLEX_UNIT_DIP; break;
            default: value = ViewGroup.LayoutParams.WRAP_CONTENT; break;
        }
        if (width) rv.setViewLayoutWidth(v, value, unit);
        else rv.setViewLayoutHeight(v, value, unit);
    }

    private int px(float dp) { return Math.round(dp * density); }

    private static int[] readColor(Codec.Reader r) {
        boolean set = r.readBoolean();
        int day = r.readInt(), night = r.readInt();
        return set ? new int[] { day, night } : null;
    }
    private static Object[] readAction(Codec.Reader r) {
        return new Object[] { r.readInt(), r.readString(), r.readString(), r.readString() };
    }

    private Intent callbackIntent(String key, String params) {
        return new Intent(ACTION_RUN).setComponent(receiver)
                .setData(Uri.parse("kiln-widget://" + appWidgetId + "/" + (++requests)))
                .putExtra(android.appwidget.AppWidgetManager.EXTRA_APPWIDGET_ID, appWidgetId)
                .putExtra(EXTRA_KEY, key).putExtra(EXTRA_PARAMS, params);
    }

    /** Opens the app (its KilnActivity) at a deep link. */
    private Intent activityIntent(String uri) {
        Intent i = context.getPackageManager().getLaunchIntentForPackage(pkg);
        if (i == null) i = new Intent().setClassName(pkg, "dev.kiln.KilnActivity");
        i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        if (!uri.isEmpty()) i.setAction(Intent.ACTION_VIEW).setData(Uri.parse(uri));
        return i;
    }

    private void applyAction(RemoteViews rv, int v, Object[] a) {
        int kind = (int) a[0];
        if (kind == A_NONE) return;
        if (inList) {
            // Collection items: fill-ins merged into the list's template (a broadcast); a deep
            // link travels as an extra and the receiver opens it.
            Intent fill = kind == A_START_ACTIVITY
                    ? new Intent().putExtra(EXTRA_START, (String) a[1]).putExtra(android.appwidget.AppWidgetManager.EXTRA_APPWIDGET_ID, appWidgetId)
                    : callbackIntent((String) a[2], (String) a[3]);
            rv.setOnClickFillInIntent(v, fill);
            return;
        }
        PendingIntent pi = kind == A_START_ACTIVITY
                ? PendingIntent.getActivity(context, ++requests, activityIntent((String) a[1]), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT)
                : PendingIntent.getBroadcast(context, ++requests, callbackIntent((String) a[2], (String) a[3]), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        rv.setOnClickPendingIntent(v, pi);
    }

    static Intent openFromList(Context context, String uri) {
        return new KilnGlance(context, 0, null).activityIntent(uri);
    }

    /** Bold, medium, italic, underline and strike-through as spans (TextTranslator). */
    private static CharSequence styled(String text, int weight, boolean italic, int decoration) {
        SpannableString s = new SpannableString(text);
        int flags = Spanned.SPAN_INCLUSIVE_EXCLUSIVE, n = text.length();
        if (weight >= 700 && italic) s.setSpan(new StyleSpan(Typeface.BOLD_ITALIC), 0, n, flags);
        else if (weight >= 700) s.setSpan(new StyleSpan(Typeface.BOLD), 0, n, flags);
        else if (italic) s.setSpan(new StyleSpan(Typeface.ITALIC), 0, n, flags);
        if (weight >= 500 && weight < 700) s.setSpan(new TypefaceSpan("sans-serif-medium"), 0, n, flags);
        if ((decoration & 1) != 0) s.setSpan(new UnderlineSpan(), 0, n, flags);
        if ((decoration & 2) != 0) s.setSpan(new StrikethroughSpan(), 0, n, flags);
        return s;
    }
}
