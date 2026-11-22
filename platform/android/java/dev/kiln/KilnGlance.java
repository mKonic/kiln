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
import android.os.Bundle;
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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * kiln::glance's RemoteViews translation (glance-appwidget RemoteViewsTranslator, ApplyModifiers,
 * ApplyAction and translators/): the content's elements, normalized and encoded by native code,
 * become nested RemoteViews of kiln's layouts (res/layout/kiln_glance_*), with sizes, padding,
 * colours and corners set at runtime (API 31).
 */
final class KilnGlance {
    static final String ACTION_RUN = "dev.kiln.widget.RUN";
    static final String EXTRA_KEY = "dev.kiln.widget.key", EXTRA_LAMBDA = "dev.kiln.widget.lambda",
            EXTRA_PARAMS = "dev.kiln.widget.params";

    // The element kinds and modifier codes of kiln/glance/Glance.cpp.
    private static final int BOX = 1, ROW = 2, COLUMN = 3, TEXT = 4, IMAGE = 5, BUTTON = 6, SPACER = 7,
            LINEAR_PROGRESS = 8, CIRCULAR_PROGRESS = 9, CHECK_BOX = 10, SWITCH = 11, LAZY_COLUMN = 12, LIST_ITEM = 13,
            RADIO_BUTTON = 14, LAZY_GRID = 15, ANDROID_REMOTE_VIEWS = 16;
    private static final int M_WIDTH = 0, M_HEIGHT = 1, M_PADDING = 2, M_BACKGROUND_COLOR = 3, M_CORNER_RADIUS = 5,
            M_CLICKABLE = 6, M_VISIBILITY = 7, M_SEMANTICS = 8, M_ENABLED = 9, M_APP_WIDGET_BACKGROUND = 10,
            M_CLIP_TO_OUTLINE = 11, M_SELECTABLE_GROUP = 12;
    private static final int D_WRAP = 0, D_FILL = 1, D_EXPAND = 2, D_DP = 3, D_RESOURCE = 4;
    private static final int A_NONE = 0, A_START_ACTIVITY = 1, A_RUN_CALLBACK = 2, A_LAMBDA = 3, A_SEND_BROADCAST = 4,
            A_START_SERVICE = 5;
    private static final int FORMAT = 2;

    private final Context context;
    private final String pkg;
    private final int appWidgetId;
    private final ComponentName receiver;
    private final float density;
    private final boolean rtl;
    private final Map<String, Integer> ids = new HashMap<>();
    private int requests;   // distinct PendingIntents: each tap target its own request
    private boolean inList;   // items of a collection use fill-in intents

    private KilnGlance(Context context, int appWidgetId, ComponentName receiver) {
        this.context = context;
        this.pkg = context.getPackageName();
        this.appWidgetId = appWidgetId;
        this.receiver = receiver;
        this.density = context.getResources().getDisplayMetrics().density;
        this.rtl = context.getResources().getConfiguration().getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
    }

    /** The RemoteViews for one size of a widget's content. */
    static RemoteViews build(Context context, byte[] content, int appWidgetId, ComponentName receiver) {
        return new KilnGlance(context, appWidgetId, receiver).root(new Codec.Reader(content));
    }

    /** A resource by name: this package's, or "android:type/name" for the platform's. */
    private int res(String name, String type) {
        String key = type + "/" + name;
        Integer id = ids.get(key);
        if (id == null) {
            if (name.startsWith("android:")) {
                String rest = name.substring("android:".length());
                int slash = rest.indexOf('/');
                id = context.getResources().getIdentifier(slash >= 0 ? rest.substring(slash + 1) : rest, type, "android");
            } else {
                id = context.getResources().getIdentifier(name, type, pkg);
            }
            ids.put(key, id);
        }
        return id;
    }
    private int view() { return res("kiln_glance_view", "id"); }
    private int wrap() { return res("kiln_glance_wrap", "id"); }
    private RemoteViews layout(String name) { return new RemoteViews(pkg, res("kiln_glance_" + name, "layout")); }

    private static final class Modifiers {
        int widthKind = -1, heightKind = -1;
        float widthDp, heightDp;
        String widthRes = "", heightRes = "";
    }
    private static final class Built {
        RemoteViews views;
        Modifiers size;
        int id;
        // A tap in a collection item, set from the RemoteViews holding this one (applyDeferred).
        Action action;
        boolean compound;
    }
    /** A ColorProvider: fixed (day, night) or a colour resource. */
    private static final class Color {
        int day, night;
        String resource;
    }
    private static final class Action {
        int kind;
        String uri, key, component;
        byte[] params;
        boolean foreground;
    }

    private RemoteViews root(Codec.Reader r) {
        if (r.readInt() != FORMAT) throw new IllegalArgumentException("kiln glance: an unknown format");
        // Each update of a widget alternates two identical root layouts: the launcher then applies
        // it afresh instead of reapplying it to the views it has, which leaves a collection's rows
        // undrawn.
        boolean alt = (generations.merge(appWidgetId, 1, Integer::sum) & 1) == 0;
        RemoteViews root = layout(alt ? "root_alt" : "root");
        add(root, view(), BOX, 0, element(r), 0);
        // A collection's adapter works only from the root RemoteViews (in a nested one the parent
        // it is applied under is not the AppWidgetHostView). Taps in items are fill-in intents on
        // an activity template (LazyListTranslator): an activity, or KilnWidgetTrampoline.
        for (Collection c : collections) {
            root.setRemoteAdapter(c.id, c.items);
            int flags = Intent.FILL_IN_COMPONENT | PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT;
            if (android.os.Build.VERSION.SDK_INT >= 34) flags |= PendingIntent.FLAG_ALLOW_UNSAFE_IMPLICIT_INTENT;
            root.setPendingIntentTemplate(c.id, PendingIntent.getActivity(context, ++requests, new Intent(), flags, c.activityOptions));
        }
        return root;
    }

    /** A list's or grid's items, set on the root. */
    private static final class Collection {
        int id;
        RemoteViews.RemoteCollectionItems items;
        Bundle activityOptions;
    }
    private final List<Collection> collections = new ArrayList<>();
    private int lists, grids;

    /** Adds a child to a container, in the wrapper its parent needs (weights, Box alignment). */
    // Children go in with addStableView (as Glance adds them on API 31): an update the launcher
    // reapplies to the views it has (the same layout) reuses each child by its stable id.
    private void add(RemoteViews parent, int parentId, int parentKind, int parentGravity, Built child, int index) {
        RemoteViews views = child.views;
        if (parentKind == ROW && child.size.widthKind == D_EXPAND) {
            views = wrapped(child.size.heightKind == D_FILL ? "weight_h_match" : "weight_h_wrap", views);
        } else if (parentKind == COLUMN && child.size.heightKind == D_EXPAND) {
            views = wrapped(child.size.widthKind == D_FILL ? "weight_v_match" : "weight_v_wrap", views);
        } else if (parentKind == BOX && parentGravity != 0 && parentGravity != (0x00800003 | 0x30)) {
            RemoteViews align = layout("align");
            align.setInt(wrap(), "setGravity", parentGravity);
            align.addStableView(wrap(), views, 0);
            applyDeferred(align, child);
            views = align;
        } else if (views != child.views) {
            applyDeferred(views, child);
        }
        parent.addStableView(parentId, views, index);
        if (views == child.views) applyDeferred(parent, child);
    }
    private RemoteViews wrapped(String name, RemoteViews child) {
        RemoteViews w = layout(name);
        w.addStableView(wrap(), child, 0);
        return w;
    }

    /**
     * A collection item's tap is set from the RemoteViews holding the element: RemoteViews sets
     * an element's own root only as a tag for the list's item click, which a clickable view (a
     * button, a toggle) would swallow. From API 33 such elements have ids of their own for this.
     */
    private void applyDeferred(RemoteViews container, Built child) {
        if (child.action == null) return;
        applyAction(container, child.id, child.action, child.compound);
        child.action = null;
    }
    private int nextItemViewId = 0x00100000;   // Glance's LazyListItemStartingViewId
    private int forcedId;   // the next element's own id (make)

    private Built element(Codec.Reader r) {
        int kind = r.readInt();
        // The modifiers come first; applied once the view exists.
        int modifierCount = r.readInt();
        int[] modifierKinds = new int[modifierCount];
        Object[] modifierValues = new Object[modifierCount];
        Modifiers size = new Modifiers();
        boolean background = false, selectable = false;
        for (int i = 0; i < modifierCount; i++) {
            int m = r.readInt();
            modifierKinds[i] = m;
            switch (m) {
                case M_WIDTH: case M_HEIGHT: {
                    int k = r.readInt();
                    float dp = r.readFloat();
                    String resource = r.readString();
                    if (m == M_WIDTH) { size.widthKind = k; size.widthDp = dp; size.widthRes = resource; }
                    else { size.heightKind = k; size.heightDp = dp; size.heightRes = resource; }
                    break;
                }
                case M_CORNER_RADIUS: modifierValues[i] = new Object[] { r.readInt(), r.readFloat(), r.readString() }; break;
                case M_PADDING:
                    modifierValues[i] = new Object[] {
                        new float[] { r.readFloat(), r.readFloat(), r.readFloat(), r.readFloat() },
                        new String[] { r.readString(), r.readString(), r.readString(), r.readString() },
                        r.readBoolean(),
                    };
                    break;
                case M_BACKGROUND_COLOR: modifierValues[i] = readColor(r); break;
                case M_CLICKABLE: modifierValues[i] = readAction(r); break;
                case M_VISIBILITY: case M_ENABLED: case M_CLIP_TO_OUTLINE: modifierValues[i] = r.readInt(); break;
                case M_APP_WIDGET_BACKGROUND: background = true; break;
                case M_SELECTABLE_GROUP: selectable = true; break;
                case M_SEMANTICS: {
                    boolean has = r.readBoolean();
                    String d = r.readString();
                    modifierValues[i] = has ? d : null;
                    break;
                }
                default: throw new IllegalArgumentException("kiln glance: an unknown modifier " + m);
            }
        }
        Codec.Reader props = new Codec.Reader(r.readBytes());
        int childCount = r.readInt();

        RemoteViews rv;
        int v = view();
        boolean compound = false;
        boolean hasAction = false;
        for (int k : modifierKinds) hasAction |= k == M_CLICKABLE;
        boolean defer = inList && hasAction && android.os.Build.VERSION.SDK_INT >= 33 && kind != LIST_ITEM;
        forcedId = defer ? nextItemViewId++ : 0;
        switch (kind) {
            case BOX: case ROW: case COLUMN: {
                int gravity = props.readInt();
                String name = kind == BOX ? "box" : selectable ? (kind == ROW ? "radio_row" : "radio_column") : kind == ROW ? "row" : "column";
                rv = make(name, background, v);
                v = madeId;
                if (kind != BOX) rv.setInt(v, "setGravity", gravity);
                for (int i = 0; i < childCount; i++) add(rv, v, kind, gravity, element(r), i);
                childCount = 0;
                break;
            }
            case TEXT: case BUTTON: {
                rv = make(kind == TEXT ? "text" : "button", background, v);
                v = madeId;
                String text = props.readString();
                TextStyle style = readTextStyle(props);
                int maxLines = props.readInt();
                applyText(rv, v, text, style, maxLines, props.readInt());
                break;
            }
            case RADIO_BUTTON: case CHECK_BOX: case SWITCH: {
                // CheckBoxTranslator / SwitchTranslator / RadioButtonTranslator (API 31).
                rv = make(kind == CHECK_BOX ? "checkbox" : kind == SWITCH ? "switch" : "radio", background, v);
                v = madeId;
                boolean checked = props.readBoolean();
                boolean enabled = props.readBoolean();
                String text = props.readString();
                TextStyle style = readTextStyle(props);
                int maxLines = props.readInt();
                rv.setCompoundButtonChecked(v, checked);
                applyText(rv, v, text, style, maxLines, 0x10);   // Gravity.CENTER_VERTICAL
                if (kind == SWITCH) {
                    setCheckable(rv, v, "setThumbTintList", readCheckable(props));
                    setCheckable(rv, v, "setTrackTintList", readCheckable(props));
                } else {
                    setCheckable(rv, v, "setButtonTintList", readCheckable(props));
                }
                if (kind == RADIO_BUTTON) rv.setBoolean(v, "setEnabled", enabled);
                // A radio button's tap is a click; a check box's or switch's a checked change.
                compound = kind != RADIO_BUTTON;
                break;
            }
            case ANDROID_REMOTE_VIEWS: {
                // translateEmittableAndroidRemoteViews: the app's layout in a frame, the children
                // in its container.
                String layoutName = props.readString();
                String container = props.readString();
                rv = make("box", background, v);
                v = madeId;
                RemoteViews custom = new RemoteViews(pkg, res(layoutName, "layout"));
                if (childCount > 0) {
                    int cid = res(container, "id");
                    custom.removeAllViews(cid);
                    for (int i = 0; i < childCount; i++) {
                        Built c = element(r);
                        custom.addStableView(cid, c.views, i);
                        applyDeferred(custom, c);
                    }
                    childCount = 0;
                }
                rv.removeAllViews(v);
                rv.addStableView(v, custom, 0);
                break;
            }
            case IMAGE: {
                String resource = props.readString();
                byte[] bytes = props.readBytes();
                String uri = props.readString();
                boolean hasDescription = props.readBoolean();
                String description = props.readString();
                int scale = props.readInt();
                boolean hasFilter = props.readBoolean();
                Color tint = readColor(props);
                float alpha = props.readFloat();
                String base = scale == 1 ? "image_crop" : scale == 2 ? "image_fill" : "image_fit";
                rv = make(hasDescription ? base : base + "_decorative", background, v);
                v = madeId;
                if (!resource.isEmpty()) {
                    rv.setImageViewResource(v, res(resource, "drawable"));
                } else if (bytes.length > 0) {
                    Bitmap b = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
                    if (b != null) rv.setImageViewBitmap(v, b);
                } else if (!uri.isEmpty()) {
                    rv.setImageViewUri(v, Uri.parse(uri));
                }
                if (hasDescription) rv.setContentDescription(v, description);
                if (hasFilter && tint != null) setColor(rv, v, "setColorFilter", tint);
                rv.setInt(v, "setImageAlpha", Math.round(Math.max(0f, Math.min(alpha, 1f)) * 255));
                // ImageTranslator: Fit with a wrapping side adjusts the view's bounds.
                rv.setBoolean(v, "setAdjustViewBounds", scale == 0 && (size.widthKind == D_WRAP || size.heightKind == D_WRAP));
                break;
            }
            case SPACER: rv = make("spacer", background, v); v = madeId; break;
            case LINEAR_PROGRESS: {
                rv = make("progress_linear", background, v);
                v = madeId;
                float progress = props.readFloat();
                boolean indeterminate = props.readBoolean();
                Color color = readColor(props), bg = readColor(props);
                rv.setProgressBar(v, 100, (int) (progress * 100), indeterminate);
                if (color != null) setColorStateList(rv, v, "setProgressTintList", color);
                if (bg != null) setColorStateList(rv, v, "setProgressBackgroundTintList", bg);
                break;
            }
            case CIRCULAR_PROGRESS: {
                rv = make("progress_circular", background, v);
                v = madeId;
                Color color = readColor(props);
                if (color != null) setColorStateList(rv, v, "setIndeterminateTintList", color);
                break;
            }
            case LAZY_COLUMN: case LAZY_GRID: {
                if (inList) throw new IllegalArgumentException("Glance does not support nested list views.");
                boolean grid = kind == LAZY_GRID;
                int index = grid ? grids++ : lists++;
                if (index >= MAX_COLLECTIONS)
                    throw new IllegalArgumentException("kiln glance: at most " + MAX_COLLECTIONS + (grid ? " LazyVerticalGrids" : " LazyColumns") + " in a widget");
                String name = (grid ? "grid_" : "list_") + index;
                rv = make(name, background, res("kiln_glance_" + name, "id"));
                v = madeId;
                int count = props.readInt();
                float minSize = props.readFloat();
                boolean hasOptions = props.readBoolean();
                String[] options = { props.readString(), props.readString(), props.readString(), props.readString() };
                boolean stable = props.readBoolean();
                if (grid) {
                    // The column count (1 to 5, or as many as fit at the minimum width).
                    rv.setInt(v, "setNumColumns", count > 0 ? count : -1);   // GridView.AUTO_FIT
                    if (count == 0) rv.setIntDimen(v, "setColumnWidth", minSize, TypedValue.COMPLEX_UNIT_DIP);
                }
                RemoteViews.RemoteCollectionItems.Builder items = new RemoteViews.RemoteCollectionItems.Builder().setHasStableIds(stable);
                inList = true;
                for (int i = 0; i < childCount; i++) {
                    Built item = element(r);
                    Long id = itemIds.remove(item);
                    items.addItem(id != null ? id : i, item.views);
                }
                inList = false;
                childCount = 0;
                Collection c = new Collection();
                c.id = v;
                c.items = items.setViewTypeCount(1).build();
                c.activityOptions = hasOptions ? KilnBundles.activityOptions(context, options) : null;
                collections.add(c);
                break;
            }
            case LIST_ITEM: {
                // Normalized to one Box: the item's view (a recycled row reapplies it in place),
                // the row's width unless it sets its own (Glance's items fill the list's width).
                long id = props.readLong();
                Built b = element(r);
                for (int i = 1; i < childCount; i++) element(r);
                // The item's root: its tap a tag that the list's item click reads.
                if (b.action != null) {
                    applyAction(b.views, b.id, b.action, b.compound);
                    b.action = null;
                }
                if (b.size.widthKind < 0) b.views.setViewLayoutWidth(b.id, ViewGroup.LayoutParams.MATCH_PARENT, TypedValue.COMPLEX_UNIT_PX);
                itemIds.put(b, id);
                return b;
            }
            default: throw new IllegalArgumentException("kiln glance: an unknown element " + kind);
        }
        for (int i = 0; i < childCount; i++) element(r);   // children of a leaf: none expected
        Action action = applyModifiers(rv, v, modifierKinds, modifierValues, size, compound, defer);
        // Before API 33, a clickable view in a collection item leaves the tap to the item click.
        if (inList && hasAction && !defer && (kind == BUTTON || kind == CHECK_BOX || kind == SWITCH || kind == RADIO_BUTTON))
            rv.setBoolean(v, "setClickable", false);
        Built b = new Built();
        b.views = rv;
        b.size = size;
        b.id = v;
        b.action = action;
        b.compound = compound;
        return b;
    }
    private final Map<Built, Long> itemIds = new HashMap<>();
    private static final Map<Integer, Integer> generations = new HashMap<>();
    private static final int MAX_COLLECTIONS = 4;

    /**
     * An element's layout. With appWidgetBackground its view is android:id/background (from
     * API 33 any layout takes the id; before, containers have layouts with it); madeId is the id.
     */
    private RemoteViews make(String name, boolean background, int id) {
        madeId = id;
        int layoutId = res("kiln_glance_" + name, "layout");
        int own = forcedId;
        forcedId = 0;
        if (!background && own != 0 && android.os.Build.VERSION.SDK_INT >= 33) {
            madeId = own;
            return new RemoteViews(pkg, layoutId, own);
        }
        if (!background) return new RemoteViews(pkg, layoutId);
        madeId = android.R.id.background;
        if (android.os.Build.VERSION.SDK_INT >= 33) return new RemoteViews(pkg, layoutId, android.R.id.background);
        int withId = res("kiln_glance_" + name + "_background", "layout");
        if (withId != 0) return new RemoteViews(pkg, withId);
        android.util.Log.w("kiln", "widget: appWidgetBackground on a " + name + " needs API 33");
        madeId = id;
        return new RemoteViews(pkg, layoutId);
    }
    private int madeId;

    /** Applies the modifiers; with `defer`, returns the tap instead of applying it. */
    private Action applyModifiers(RemoteViews rv, int v, int[] kinds, Object[] values, Modifiers size, boolean compound, boolean defer) {
        // ApplyModifiers: the last size, tap, corners and visibility win; paddings add up;
        // backgrounds apply in order.
        Action action = null;
        Object[] corner = null;
        int visibility = 0;
        float[] padding = null;   // left, top, right, bottom in px
        Integer enabled = null, clip = null;
        String description = null;
        for (int i = 0; i < kinds.length; i++) {
            Object value = values[i];
            switch (kinds[i]) {
                case M_CLICKABLE: action = (Action) value; break;
                case M_CORNER_RADIUS: corner = (Object[]) value; break;
                case M_VISIBILITY: visibility = (int) value; break;
                case M_PADDING: {
                    Object[] p = (Object[]) value;
                    float[] dp = (float[]) p[0];
                    String[] dimens = (String[]) p[1];
                    boolean absolute = (boolean) p[2];
                    float[] px = new float[4];
                    for (int k = 0; k < 4; k++) px[k] = dp[k] * density + (dimens[k].isEmpty() ? 0 : context.getResources().getDimension(res(dimens[k], "dimen")));
                    // start/end to left/right (toAbsolute).
                    if (!absolute && rtl) { float s = px[0]; px[0] = px[2]; px[2] = s; }
                    if (padding == null) padding = px;
                    else for (int k = 0; k < 4; k++) padding[k] += px[k];
                    break;
                }
                case M_BACKGROUND_COLOR: {
                    Color c = (Color) value;
                    if (c != null) setColor(rv, v, "setBackgroundColor", c);
                    break;
                }
                case M_ENABLED: enabled = (int) value; break;
                case M_CLIP_TO_OUTLINE: clip = (int) value; break;
                case M_SEMANTICS: if (value != null) description = (String) value; break;
                default: break;
            }
        }
        if (size.widthKind >= 0) setDimension(rv, v, true, size.widthKind, size.widthDp, size.widthRes);
        if (size.heightKind >= 0) setDimension(rv, v, false, size.heightKind, size.heightDp, size.heightRes);
        if (action != null && !defer) applyAction(rv, v, action, compound);
        if (corner != null) {
            rv.setBoolean(v, "setClipToOutline", true);
            if ((int) corner[0] == D_RESOURCE) rv.setViewOutlinePreferredRadiusDimen(v, res((String) corner[2], "dimen"));
            else rv.setViewOutlinePreferredRadius(v, (float) corner[1], TypedValue.COMPLEX_UNIT_DIP);
        }
        if (padding != null) rv.setViewPadding(v, Math.round(padding[0]), Math.round(padding[1]), Math.round(padding[2]), Math.round(padding[3]));
        if (clip != null) rv.setBoolean(v, "setClipToOutline", clip != 0);
        if (enabled != null) rv.setBoolean(v, "setEnabled", enabled != 0);
        if (description != null) rv.setContentDescription(v, description);
        rv.setViewVisibility(v, visibility == 0 ? View.VISIBLE : visibility == 1 ? View.INVISIBLE : View.GONE);
        return defer ? action : null;
    }

    private void setDimension(RemoteViews rv, int v, boolean width, int kind, float dp, String resource) {
        if (kind == D_RESOURCE) {
            if (width) rv.setViewLayoutWidthDimen(v, res(resource, "dimen"));
            else rv.setViewLayoutHeightDimen(v, res(resource, "dimen"));
            return;
        }
        float value;
        int unit = TypedValue.COMPLEX_UNIT_PX;
        switch (kind) {
            // Expand: the weight wrapper's; the view fills it.
            case D_FILL: case D_EXPAND: value = ViewGroup.LayoutParams.MATCH_PARENT; break;
            case D_DP: value = dp; unit = TypedValue.COMPLEX_UNIT_DIP; break;
            default: value = ViewGroup.LayoutParams.WRAP_CONTENT; break;
        }
        if (width) rv.setViewLayoutWidth(v, value, unit);
        else rv.setViewLayoutHeight(v, value, unit);
    }

    // --- colours ----------------------------------------------------------------------------

    private static Color readColor(Codec.Reader r) {
        boolean set = r.readBoolean();
        Color c = new Color();
        c.day = r.readInt();
        c.night = r.readInt();
        c.resource = r.readString();
        return set ? c : null;
    }
    /** A method taking a colour int: resolved when the launcher applies it (resources, night mode). */
    private void setColor(RemoteViews rv, int v, String method, Color c) {
        if (!c.resource.isEmpty()) rv.setColor(v, method, res(c.resource, "color"));
        else if (c.day == c.night) rv.setInt(v, method, c.day);
        else rv.setColorInt(v, method, c.day, c.night);
    }
    /** A method taking a ColorStateList. */
    private void setColorStateList(RemoteViews rv, int v, String method, Color c) {
        if (!c.resource.isEmpty()) rv.setColorStateList(v, method, res(c.resource, "color"));
        else rv.setColorStateList(v, method, ColorStateList.valueOf(c.day), ColorStateList.valueOf(c.night));
    }

    // --- text -------------------------------------------------------------------------------

    /** A TextStyle as encoded (none: the platform's). */
    private static final class TextStyle {
        Color color;
        float fontSize;
        int weight, fontStyle, align, decoration;
        String family;
    }
    private static TextStyle readTextStyle(Codec.Reader p) {
        if (!p.readBoolean()) return null;
        TextStyle t = new TextStyle();
        t.color = readColor(p);
        t.fontSize = p.readFloat();
        t.weight = p.readInt();
        t.fontStyle = p.readInt();
        t.align = p.readInt();
        t.decoration = p.readInt();
        t.family = p.readString();
        return t;
    }

    /** TextTranslator.setText: the style as spans, its size, alignment and colour. */
    private void applyText(RemoteViews rv, int v, String text, TextStyle style, int maxLines, int verticalGravity) {
        if (maxLines != Integer.MAX_VALUE) rv.setInt(v, "setMaxLines", maxLines);
        if (style == null) {
            rv.setTextViewText(v, text);
            return;
        }
        SpannableString s = new SpannableString(text);
        int flags = Spanned.SPAN_INCLUSIVE_EXCLUSIVE, n = text.length();
        if (style.fontSize > 0) rv.setTextViewTextSize(v, TypedValue.COMPLEX_UNIT_SP, style.fontSize);
        if (style.decoration > 0) {
            if ((style.decoration & 2) != 0) s.setSpan(new StrikethroughSpan(), 0, n, flags);
            if ((style.decoration & 1) != 0) s.setSpan(new UnderlineSpan(), 0, n, flags);
        }
        if (style.fontStyle >= 0) s.setSpan(new StyleSpan(style.fontStyle == 1 ? Typeface.ITALIC : Typeface.NORMAL), 0, n, flags);
        // Glance's text appearances: Medium and Bold are both a bold text style.
        if (style.weight >= 500) s.setSpan(new StyleSpan(Typeface.BOLD), 0, n, flags);
        if (!style.family.isEmpty()) s.setSpan(new TypefaceSpan(style.family), 0, n, flags);
        if (style.align >= 0) rv.setInt(v, "setGravity", alignGravity(style.align) | verticalGravity);
        rv.setTextViewText(v, s);
        Color color = style.color;
        if (color != null) {
            if (!color.resource.isEmpty()) rv.setColorStateList(v, "setTextColor", res(color.resource, "color"));
            else if (color.day == color.night) rv.setTextColor(v, color.day);
            else rv.setColorInt(v, "setTextColor", color.day, color.night);
        }
    }

    /** A toggle's colours: a colour state list resource, or checked and unchecked colours. */
    private static final class Checkable {
        String resource;
        Color checked, unchecked;
    }
    private static Checkable readCheckable(Codec.Reader p) {
        Checkable c = new Checkable();
        c.resource = p.readString();
        c.checked = readColor(p);
        c.unchecked = readColor(p);
        return c;
    }
    private static final int[][] CHECKED_STATES = { { android.R.attr.state_checked }, { -android.R.attr.state_checked } };
    /** toDayNightColorStateList: the checked and unchecked colours for day and night. */
    private void setCheckable(RemoteViews rv, int v, String method, Checkable c) {
        if (!c.resource.isEmpty()) {
            rv.setColorStateList(v, method, res(c.resource, "color"));
            return;
        }
        if (c.checked == null || c.unchecked == null) return;
        ColorStateList day = new ColorStateList(CHECKED_STATES, new int[] { c.checked.day, c.unchecked.day });
        ColorStateList night = new ColorStateList(CHECKED_STATES, new int[] { c.checked.night, c.unchecked.night });
        rv.setColorStateList(v, method, day, night);
    }
    private static int alignGravity(int align) {
        switch (align) {
            case 0: return 0x03;           // Left
            case 1: return 0x05;           // Right
            case 2: return 0x01;           // Center
            case 5: return 0x00800005;     // End
            default: return 0x00800003;    // Start (Justify: Glance logs it and starts)
        }
    }

    // --- actions ------------------------------------------------------------------------------

    private static Action readAction(Codec.Reader r) {
        Action a = new Action();
        a.kind = r.readInt();
        a.uri = r.readString();
        a.key = r.readString();
        a.component = r.readString();
        a.params = r.readBytes();
        a.foreground = r.readBoolean();
        return a;
    }

    /** The parameters' bytes (glance::ParametersToBytes) as a Bundle, and back. */
    static Bundle paramsToBundle(byte[] bytes) {
        Bundle b = new Bundle();
        if (bytes == null || bytes.length == 0) return b;
        Codec.Reader r = new Codec.Reader(bytes);
        int n = r.readInt();
        for (int i = 0; i < n; i++) {
            String name = r.readString();
            switch (r.readInt()) {
                case 0: b.putBoolean(name, r.readBoolean()); break;
                case 1: b.putInt(name, r.readInt()); break;
                case 2: b.putLong(name, r.readLong()); break;
                case 3: b.putFloat(name, r.readFloat()); break;
                case 4: b.putDouble(name, r.readDouble()); break;
                default: b.putString(name, r.readString()); break;
            }
        }
        return b;
    }
    @SuppressWarnings("deprecation")
    static byte[] bundleToParams(Bundle b) {
        Codec.Writer w = new Codec.Writer();
        List<String> keys = new ArrayList<>();
        if (b != null) for (String k : b.keySet()) {
            Object o = b.get(k);
            if (o instanceof Boolean || o instanceof Integer || o instanceof Long || o instanceof Float || o instanceof Double || o instanceof String) keys.add(k);
        }
        w.writeInt(keys.size());
        for (String k : keys) {
            Object o = b.get(k);
            w.writeString(k);
            if (o instanceof Boolean) { w.writeInt(0); w.writeBoolean((Boolean) o); }
            else if (o instanceof Integer) { w.writeInt(1); w.writeInt((Integer) o); }
            else if (o instanceof Long) { w.writeInt(2); w.writeLong((Long) o); }
            else if (o instanceof Float) { w.writeInt(3); w.writeFloat((Float) o); }
            else if (o instanceof Double) { w.writeInt(4); w.writeDouble((Double) o); }
            else { w.writeInt(5); w.writeString((String) o); }
        }
        return w.toByteArray();
    }

    /** A broadcast to this widget's receiver: a callback, or a lambda of this drawing. */
    private Intent receiverIntent(Action a) {
        Intent i = new Intent(ACTION_RUN).setComponent(receiver)
                .setData(Uri.parse("kiln-widget://" + appWidgetId + "/" + (++requests)))
                .putExtra(android.appwidget.AppWidgetManager.EXTRA_APPWIDGET_ID, appWidgetId)
                .putExtra(EXTRA_PARAMS, paramsToBundle(a.params));
        if (a.kind == A_LAMBDA) i.putExtra(EXTRA_LAMBDA, a.key);
        else i.putExtra(EXTRA_KEY, a.key);
        return i;
    }

    /** Opens the app (its KilnActivity) at a deep link, the parameters as extras. */
    private Intent activityIntent(String uri, byte[] params) {
        Intent i = context.getPackageManager().getLaunchIntentForPackage(pkg);
        if (i == null) i = new Intent().setClassName(pkg, "dev.kiln.KilnActivity");
        i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        if (!uri.isEmpty()) i.setAction(Intent.ACTION_VIEW).setData(Uri.parse(uri));
        i.putExtras(paramsToBundle(params));
        return i;
    }
    private Intent broadcastIntent(Action a) {
        Intent i = a.uri.isEmpty() ? new Intent() : new Intent(a.uri);
        if (!a.component.isEmpty()) i.setComponent(new ComponentName(pkg, a.component));
        return i;
    }
    private Intent serviceIntent(Action a) { return new Intent().setComponent(new ComponentName(pkg, a.component)); }
    /** applyTrampolineIntent: KilnWidgetTrampoline carrying the target. */
    private Intent trampoline(Intent target, int type) {
        return new Intent().setComponent(new ComponentName(pkg, "dev.kiln.KilnWidgetTrampoline"))
                .setData(Uri.parse("kiln-widget-action://" + appWidgetId + "/" + (++requests)))
                .putExtra(KilnWidgetTrampoline.EXTRA_TYPE, type).putExtra(KilnWidgetTrampoline.EXTRA_INTENT, target);
    }
    private void identify(Intent i) {
        if (i.getData() == null) i.setIdentifier("kiln-widget://" + appWidgetId + "/" + (++requests));
    }

    private void applyAction(RemoteViews rv, int v, Action a, boolean compound) {
        if (a.kind == A_NONE) return;
        if (inList) {
            // Collection items: fill-ins merged into the collection's activity template
            // (ApplyAction.getFillInIntentForAction): an activity directly, the rest through the
            // trampoline.
            Intent fill;
            switch (a.kind) {
                case A_START_ACTIVITY: fill = activityIntent(a.uri, a.params); identify(fill); break;
                case A_SEND_BROADCAST: fill = trampoline(broadcastIntent(a), KilnWidgetTrampoline.BROADCAST); break;
                case A_START_SERVICE: fill = trampoline(serviceIntent(a), a.foreground ? KilnWidgetTrampoline.FOREGROUND_SERVICE : KilnWidgetTrampoline.SERVICE); break;
                default: fill = trampoline(receiverIntent(a), KilnWidgetTrampoline.BROADCAST); break;
            }
            if (compound) rv.setOnCheckedChangeResponse(v, RemoteViews.RemoteResponse.fromFillInIntent(fill));
            else rv.setOnClickFillInIntent(v, fill);
            return;
        }
        // A toggle's intent is mutable (the system adds EXTRA_CHECKED), except for lambdas.
        int mutability = compound && a.kind != A_LAMBDA ? PendingIntent.FLAG_MUTABLE : PendingIntent.FLAG_IMMUTABLE;
        int flags = mutability | PendingIntent.FLAG_UPDATE_CURRENT;
        PendingIntent pi;
        switch (a.kind) {
            case A_START_ACTIVITY: {
                Intent i = activityIntent(a.uri, a.params);
                identify(i);
                pi = PendingIntent.getActivity(context, ++requests, i, flags);
                break;
            }
            case A_SEND_BROADCAST: {
                Intent i = broadcastIntent(a);
                identify(i);
                pi = PendingIntent.getBroadcast(context, ++requests, i, flags);
                break;
            }
            case A_START_SERVICE: {
                Intent i = serviceIntent(a);
                identify(i);
                pi = a.foreground ? PendingIntent.getForegroundService(context, ++requests, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT)
                        : PendingIntent.getService(context, ++requests, i, flags);
                break;
            }
            default: pi = PendingIntent.getBroadcast(context, ++requests, receiverIntent(a), flags); break;
        }
        if (compound) rv.setOnCheckedChangeResponse(v, RemoteViews.RemoteResponse.fromPendingIntent(pi));
        else rv.setOnClickPendingIntent(v, pi);
    }
}
