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
            EXTRA_PARAMS = "dev.kiln.widget.params", EXTRA_FORWARD = "dev.kiln.widget.forward",
            EXTRA_FORWARD_KIND = "dev.kiln.widget.forwardKind";
    static final int FORWARD_ACTIVITY = 1, FORWARD_BROADCAST = 2, FORWARD_SERVICE = 3, FORWARD_FOREGROUND_SERVICE = 4;

    // The element kinds and modifier codes of kiln/glance/Glance.cpp.
    private static final int BOX = 1, ROW = 2, COLUMN = 3, TEXT = 4, IMAGE = 5, BUTTON = 6, SPACER = 7,
            LINEAR_PROGRESS = 8, CIRCULAR_PROGRESS = 9, CHECK_BOX = 10, SWITCH = 11, LAZY_COLUMN = 12, LIST_ITEM = 13;
    private static final int M_WIDTH = 0, M_HEIGHT = 1, M_PADDING = 2, M_BACKGROUND_COLOR = 3, M_CORNER_RADIUS = 5,
            M_CLICKABLE = 6, M_VISIBILITY = 7, M_SEMANTICS = 8, M_ENABLED = 9, M_CLIP_TO_OUTLINE = 11;
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
        add(root, BOX, 0, element(r), 0);
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
    // Children go in with addStableView (as Glance adds them on API 31): an update the launcher
    // reapplies to the views it has (the same layout) reuses each child by its stable id.
    private void add(RemoteViews parent, int parentKind, int parentGravity, Built child, int index) {
        RemoteViews views = child.views;
        if (parentKind == ROW && child.size.widthKind == D_EXPAND) {
            views = wrapped(child.size.heightKind == D_FILL ? "weight_h_match" : "weight_h_wrap", views);
        } else if (parentKind == COLUMN && child.size.heightKind == D_EXPAND) {
            views = wrapped(child.size.widthKind == D_FILL ? "weight_v_match" : "weight_v_wrap", views);
        } else if (parentKind == BOX && parentGravity != 0 && parentGravity != (0x00800003 | 0x30)) {
            RemoteViews align = layout("align");
            align.setInt(wrap(), "setGravity", parentGravity);
            align.addStableView(wrap(), views, 0);
            views = align;
        }
        parent.addStableView(view(), views, index);
    }
    private RemoteViews wrapped(String name, RemoteViews child) {
        RemoteViews w = layout(name);
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
        switch (kind) {
            case BOX: case ROW: case COLUMN: {
                int gravity = props.readInt();
                rv = layout(kind == BOX ? "box" : kind == ROW ? "row" : "column");
                if (kind != BOX) rv.setInt(v, "setGravity", gravity);
                for (int i = 0; i < childCount; i++) add(rv, kind, gravity, element(r), i);
                childCount = 0;
                break;
            }
            case TEXT: case BUTTON: {
                rv = layout(kind == TEXT ? "text" : "button");
                setText(rv, v, props);
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
                rv = layout(hasDescription ? base : base + "_decorative");
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
            case SPACER: rv = layout("spacer"); break;
            case LINEAR_PROGRESS: {
                rv = layout("progress_linear");
                float progress = props.readFloat();
                Color color = readColor(props), background = readColor(props);
                rv.setProgressBar(v, 1000, progress < 0 ? 0 : Math.round(Math.min(progress, 1f) * 1000), progress < 0);
                if (color != null) {
                    setColorStateList(rv, v, "setProgressTintList", color);
                    setColorStateList(rv, v, "setIndeterminateTintList", color);
                }
                if (background != null) setColorStateList(rv, v, "setProgressBackgroundTintList", background);
                break;
            }
            case CIRCULAR_PROGRESS: {
                rv = layout("progress_circular");
                Color color = readColor(props);
                if (color != null) setColorStateList(rv, v, "setIndeterminateTintList", color);
                break;
            }
            case CHECK_BOX: case SWITCH: {
                rv = layout(kind == CHECK_BOX ? "checkbox" : "switch");
                boolean checked = props.readBoolean();
                String text = props.readString();
                Color color = readColor(props);
                rv.setCompoundButtonChecked(v, checked);
                if (!text.isEmpty()) rv.setTextViewText(v, text);
                if (color != null) setColorStateList(rv, v, "setTextColor", color);
                compound = true;
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
                // Normalized to one Box: the item's view (a recycled row reapplies it in place),
                // the row's width unless it sets its own (Glance's items fill the list's width).
                long id = props.readLong();
                Built b = element(r);
                for (int i = 1; i < childCount; i++) element(r);
                if (b.size.widthKind < 0) b.views.setViewLayoutWidth(view(), ViewGroup.LayoutParams.MATCH_PARENT, TypedValue.COMPLEX_UNIT_PX);
                itemIds.put(b, id);
                return b;
            }
            default: throw new IllegalArgumentException("kiln glance: an unknown element " + kind);
        }
        for (int i = 0; i < childCount; i++) element(r);   // children of a leaf: none expected
        applyModifiers(rv, v, modifierKinds, modifierValues, size, compound);
        Built b = new Built();
        b.views = rv;
        b.size = size;
        return b;
    }
    private final Map<Built, Long> itemIds = new HashMap<>();
    private static final Map<Integer, Integer> generations = new HashMap<>();
    private static final int MAX_LISTS = 4;
    private final List<RemoteViews.RemoteCollectionItems> lists = new ArrayList<>();

    private void applyModifiers(RemoteViews rv, int v, int[] kinds, Object[] values, Modifiers size, boolean compound) {
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
        if (action != null) applyAction(rv, v, action, compound);
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

    /** TextTranslator.setText: the style as spans, its size, alignment and colour. */
    private void setText(RemoteViews rv, int v, Codec.Reader p) {
        String text = p.readString();
        boolean hasStyle = p.readBoolean();
        Color color = null;
        float fontSize = 0;
        int weight = -1, fontStyle = -1, align = -1, decoration = -1;
        String family = "";
        if (hasStyle) {
            color = readColor(p);
            fontSize = p.readFloat();
            weight = p.readInt();
            fontStyle = p.readInt();
            align = p.readInt();
            decoration = p.readInt();
            family = p.readString();
        }
        int maxLines = p.readInt();
        int verticalGravity = p.readInt();
        if (maxLines != Integer.MAX_VALUE) rv.setInt(v, "setMaxLines", maxLines);
        if (!hasStyle) {
            rv.setTextViewText(v, text);
            return;
        }
        SpannableString s = new SpannableString(text);
        int flags = Spanned.SPAN_INCLUSIVE_EXCLUSIVE, n = text.length();
        if (fontSize > 0) rv.setTextViewTextSize(v, TypedValue.COMPLEX_UNIT_SP, fontSize);
        if (decoration > 0) {
            if ((decoration & 2) != 0) s.setSpan(new StrikethroughSpan(), 0, n, flags);
            if ((decoration & 1) != 0) s.setSpan(new UnderlineSpan(), 0, n, flags);
        }
        if (fontStyle >= 0) s.setSpan(new StyleSpan(fontStyle == 1 ? Typeface.ITALIC : Typeface.NORMAL), 0, n, flags);
        // Glance's text appearances: Medium and Bold are both a bold text style.
        if (weight >= 500) s.setSpan(new StyleSpan(Typeface.BOLD), 0, n, flags);
        if (!family.isEmpty()) s.setSpan(new TypefaceSpan(family), 0, n, flags);
        if (align >= 0) rv.setInt(v, "setGravity", alignGravity(align) | verticalGravity);
        rv.setTextViewText(v, s);
        if (color != null) {
            if (!color.resource.isEmpty()) rv.setColorStateList(v, "setTextColor", res(color.resource, "color"));
            else if (color.day == color.night) rv.setTextColor(v, color.day);
            else rv.setColorInt(v, "setTextColor", color.day, color.night);
        }
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
    private void identify(Intent i) {
        if (i.getData() == null) i.setIdentifier("kiln-widget://" + appWidgetId + "/" + (++requests));
    }

    private void applyAction(RemoteViews rv, int v, Action a, boolean compound) {
        if (a.kind == A_NONE) return;
        if (inList) {
            // Collection items: fill-ins merged into the list's template (a broadcast to the
            // receiver), which runs callbacks and lambdas and forwards the rest.
            Intent fill;
            if (a.kind == A_RUN_CALLBACK || a.kind == A_LAMBDA) {
                fill = receiverIntent(a);
            } else {
                Intent target;
                int forward;
                switch (a.kind) {
                    case A_START_ACTIVITY: target = activityIntent(a.uri, a.params); forward = FORWARD_ACTIVITY; break;
                    case A_SEND_BROADCAST: target = broadcastIntent(a); forward = FORWARD_BROADCAST; break;
                    default: target = serviceIntent(a); forward = a.foreground ? FORWARD_FOREGROUND_SERVICE : FORWARD_SERVICE; break;
                }
                fill = new Intent().putExtra(EXTRA_FORWARD, target).putExtra(EXTRA_FORWARD_KIND, forward)
                        .putExtra(android.appwidget.AppWidgetManager.EXTRA_APPWIDGET_ID, appWidgetId);
                identify(fill);
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
