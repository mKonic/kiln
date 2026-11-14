package dev.kiln;

import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.util.Xml;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Locale;

import org.xmlpull.v1.XmlPullParser;

/**
 * The app's Android resources for kiln's StringResource, PluralStringResource, PainterResource
 * and the other resource reads (ui res/*Resources.android.kt). Names are looked up with
 * getIdentifier (cached); the activity's Resources follow configuration changes handled
 * in-process. Vector drawables are read as Compose's XmlVectorParser reads them (typed arrays of
 * the framework's VectorDrawable attributes, resolved against the theme) and handed to native
 * code as resolved elements (see kiln/ui/AppResources.h).
 */
final class KilnResources {
    private static Context sContext;
    private static final HashMap<String, Integer> sIds = new HashMap<>();

    private KilnResources() {}

    static void attach(Context context) {
        sContext = context;
        synchronized (sIds) { sIds.clear(); }
    }

    private static int id(String type, String name) {
        if (sContext == null) return 0;
        String key = type + '/' + name;
        synchronized (sIds) {
            Integer cached = sIds.get(key);
            if (cached != null) return cached;
            int id = sContext.getResources().getIdentifier(name, type, sContext.getPackageName());
            sIds.put(key, id);
            return id;
        }
    }

    private static Resources res() { return sContext.getResources(); }

    static String string(String name) {
        int id = id("string", name);
        return id == 0 ? null : res().getString(id);
    }

    static String quantity(String name, int count) {
        int id = id("plurals", name);
        return id == 0 ? null : res().getQuantityString(id, count);
    }

    static String[] stringArray(String name) {
        int id = id("array", name);
        return id == 0 ? null : res().getStringArray(id);
    }

    static int[] intArray(String name) {
        int id = id("array", name);
        return id == 0 ? null : res().getIntArray(id);
    }

    /** The colour as ARGB in the low 32 bits, bit 32 set when the app has it. */
    static long color(String name) {
        int id = id("color", name);
        if (id == 0) return 0;
        return (1L << 32) | (res().getColor(id, sContext.getTheme()) & 0xffffffffL);
    }

    /** getDimension in pixels; NaN when the app has none. */
    static float dimension(String name) {
        int id = id("dimen", name);
        return id == 0 ? Float.NaN : res().getDimension(id);
    }

    /** [found, value] */
    static int[] integer(String name) {
        int id = id("integer", name);
        return id == 0 ? new int[] { 0, 0 } : new int[] { 1, res().getInteger(id) };
    }

    static int[] bool(String name) {
        int id = id("bool", name);
        return id == 0 ? new int[] { 0, 0 } : new int[] { 1, res().getBoolean(id) ? 1 : 0 };
    }

    static String format(String pattern, Object[] args, String localeTag) {
        Locale locale = localeTag == null || localeTag.isEmpty() ? Locale.getDefault() : Locale.forLanguageTag(localeTag);
        try {
            return String.format(locale, pattern, args);
        } catch (RuntimeException e) {
            return pattern;
        }
    }

    /** A bitmap drawable's encoded bytes; null for XML drawables or a missing name. */
    static byte[] bitmap(String name) {
        int id = drawableId(name);
        if (id == 0) return null;
        TypedValue value = new TypedValue();
        res().getValue(id, value, true);
        if (value.string != null && value.string.toString().endsWith(".xml")) return null;
        try (InputStream in = res().openRawResource(id)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } catch (Exception e) {
            return null;
        }
    }

    private static int drawableId(String name) {
        int id = id("drawable", name);
        return id != 0 ? id : id("mipmap", name);
    }

    // The framework's VectorDrawable attributes (AndroidVectorResources.kt) by element.
    private static final int FLOAT = 0, DIMEN = 1, COLOR = 2, STRING = 3, INT = 4, BOOL = 5;
    private static final Object[][] VECTOR = {
        { "name", android.R.attr.name, STRING }, { "tint", android.R.attr.tint, COLOR },
        { "tintMode", android.R.attr.tintMode, INT }, { "autoMirrored", android.R.attr.autoMirrored, BOOL },
        { "viewportWidth", android.R.attr.viewportWidth, FLOAT }, { "viewportHeight", android.R.attr.viewportHeight, FLOAT },
        { "width", android.R.attr.width, DIMEN }, { "height", android.R.attr.height, DIMEN },
        { "alpha", android.R.attr.alpha, FLOAT },
    };
    private static final Object[][] GROUP = {
        { "name", android.R.attr.name, STRING }, { "rotation", android.R.attr.rotation, FLOAT },
        { "pivotX", android.R.attr.pivotX, FLOAT }, { "pivotY", android.R.attr.pivotY, FLOAT },
        { "scaleX", android.R.attr.scaleX, FLOAT }, { "scaleY", android.R.attr.scaleY, FLOAT },
        { "translateX", android.R.attr.translateX, FLOAT }, { "translateY", android.R.attr.translateY, FLOAT },
    };
    private static final Object[][] PATH = {
        { "name", android.R.attr.name, STRING }, { "pathData", android.R.attr.pathData, STRING },
        { "fillColor", android.R.attr.fillColor, COLOR }, { "strokeColor", android.R.attr.strokeColor, COLOR },
        { "strokeWidth", android.R.attr.strokeWidth, FLOAT }, { "strokeAlpha", android.R.attr.strokeAlpha, FLOAT },
        { "fillAlpha", android.R.attr.fillAlpha, FLOAT }, { "strokeLineCap", android.R.attr.strokeLineCap, INT },
        { "strokeLineJoin", android.R.attr.strokeLineJoin, INT }, { "strokeMiterLimit", android.R.attr.strokeMiterLimit, FLOAT },
        { "trimPathStart", android.R.attr.trimPathStart, FLOAT }, { "trimPathEnd", android.R.attr.trimPathEnd, FLOAT },
        { "trimPathOffset", android.R.attr.trimPathOffset, FLOAT }, { "fillType", android.R.attr.fillType, INT },
    };
    private static final Object[][] CLIP = {
        { "name", android.R.attr.name, STRING }, { "pathData", android.R.attr.pathData, STRING },
    };
    private static final Object[][] GRADIENT = {
        { "type", android.R.attr.type, INT }, { "startX", android.R.attr.startX, FLOAT }, { "startY", android.R.attr.startY, FLOAT },
        { "endX", android.R.attr.endX, FLOAT }, { "endY", android.R.attr.endY, FLOAT },
        { "centerX", android.R.attr.centerX, FLOAT }, { "centerY", android.R.attr.centerY, FLOAT },
        { "gradientRadius", android.R.attr.gradientRadius, FLOAT }, { "tileMode", android.R.attr.tileMode, INT },
        { "startColor", android.R.attr.startColor, COLOR }, { "centerColor", android.R.attr.centerColor, COLOR },
        { "endColor", android.R.attr.endColor, COLOR },
    };

    /**
     * A vector drawable as resolved elements, flattened: "\u0001tag" opens an element (its
     * attributes follow as name, value pairs), "\u0002group" ends a group. Null when the name is
     * not a vector drawable.
     */
    static String[] vector(String name) {
        int id = drawableId(name);
        if (id == 0) return null;
        TypedValue value = new TypedValue();
        res().getValue(id, value, true);
        if (value.string == null || !value.string.toString().endsWith(".xml")) return null;
        ArrayList<String> out = new ArrayList<>();
        try (XmlResourceParser parser = res().getXml(id)) {
            AttributeSet attrs = Xml.asAttributeSet(parser);
            int type;
            boolean first = true;
            while ((type = parser.next()) != XmlPullParser.END_DOCUMENT) {
                if (type == XmlPullParser.START_TAG) {
                    String tag = parser.getName();
                    if (first && !"vector".equals(tag)) return null;
                    first = false;
                    Object[][] spec = "vector".equals(tag) ? VECTOR : "group".equals(tag) ? GROUP
                            : "path".equals(tag) ? PATH : "clip-path".equals(tag) ? CLIP : null;
                    if (spec == null) continue;
                    out.add("\u0001" + tag);
                    read(attrs, spec, out);
                } else if (type == XmlPullParser.END_TAG && "group".equals(parser.getName())) {
                    out.add("\u0002group");
                }
            }
        } catch (Exception e) {
            return null;
        }
        return out.toArray(new String[0]);
    }

    /** Reads `spec`'s attributes with the theme (obtainStyledAttributes wants ids ascending). */
    private static void read(AttributeSet attrs, Object[][] spec, ArrayList<String> out) {
        Object[][] sorted = spec.clone();
        Arrays.sort(sorted, (a, b) -> Integer.compare((Integer) a[1], (Integer) b[1]));
        int[] ids = new int[sorted.length];
        for (int i = 0; i < sorted.length; ++i) ids[i] = (Integer) sorted[i][1];
        TypedArray a = sContext.getTheme().obtainStyledAttributes(attrs, ids, 0, 0);
        try {
            for (int i = 0; i < sorted.length; ++i) {
                if (!a.hasValue(i)) continue;
                String key = (String) sorted[i][0];
                String v;
                switch ((Integer) sorted[i][2]) {
                    case FLOAT: v = Float.toString(a.getFloat(i, 0)); break;
                    case DIMEN: v = a.getDimension(i, 0) + "px"; break;
                    case STRING: v = a.getString(i); break;
                    case INT: v = Integer.toString(a.getInt(i, 0)); break;
                    case BOOL: v = Boolean.toString(a.getBoolean(i, false)); break;
                    default: v = complexColor(a, i); break;
                }
                if (v == null) continue;
                out.add(key);
                out.add(v);
            }
        } finally {
            a.recycle();
        }
    }

    private static String hex(int argb) { return String.format(Locale.ROOT, "#%08x", argb); }

    /** getNamedComplexColor: a colour, a colour state list's default, or a gradient. */
    private static String complexColor(TypedArray a, int index) {
        TypedValue tv = new TypedValue();
        if (!a.getValue(index, tv)) return null;
        if (tv.type >= TypedValue.TYPE_FIRST_COLOR_INT && tv.type <= TypedValue.TYPE_LAST_COLOR_INT) return hex(tv.data);
        if (tv.type == TypedValue.TYPE_ATTRIBUTE) return null;
        if (tv.resourceId == 0) return null;
        try (XmlResourceParser parser = res().getXml(tv.resourceId)) {
            int type;
            while ((type = parser.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {}
            if (type != XmlPullParser.START_TAG) return null;
            if ("gradient".equals(parser.getName())) return gradient(parser);
            ColorStateList list = res().getColorStateList(tv.resourceId, sContext.getTheme());
            return hex(list.getDefaultColor());
        } catch (Exception e) {
            return null;
        }
    }

    /** GradientColorInflaterCompat: the gradient element and its items, serialized. */
    private static String gradient(XmlResourceParser parser) throws Exception {
        AttributeSet attrs = Xml.asAttributeSet(parser);
        ArrayList<String> g = new ArrayList<>();
        read(attrs, GRADIENT, g);
        HashMap<String, String> m = new HashMap<>();
        for (int i = 0; i + 1 < g.size(); i += 2) m.put(g.get(i), g.get(i + 1));
        StringBuilder stops = new StringBuilder();
        int depth = parser.getDepth();
        int type;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT && (type != XmlPullParser.END_TAG || parser.getDepth() > depth)) {
            if (type != XmlPullParser.START_TAG || !"item".equals(parser.getName())) continue;
            TypedArray a = sContext.getTheme().obtainStyledAttributes(Xml.asAttributeSet(parser),
                    new int[] { android.R.attr.color, android.R.attr.offset }, 0, 0);
            if (stops.length() > 0) stops.append(',');
            stops.append(hex(a.getColor(0, 0))).append('@').append(a.getFloat(1, 0));
            a.recycle();
        }
        if (stops.length() == 0) {
            stops.append(m.containsKey("startColor") ? m.get("startColor") : "#00000000").append("@0");
            if (m.containsKey("centerColor")) stops.append(',').append(m.get("centerColor")).append("@0.5");
            stops.append(',').append(m.containsKey("endColor") ? m.get("endColor") : "#00000000").append("@1");
        }
        String t = m.getOrDefault("type", "0");
        String kind = "1".equals(t) ? "radial" : "2".equals(t) ? "sweep" : "linear";
        String tile = m.getOrDefault("tileMode", "0");
        String tileName = "1".equals(tile) ? "repeat" : "2".equals(tile) ? "mirror" : "clamp";
        return "gradient:" + kind + ";" + m.getOrDefault("startX", "0") + ";" + m.getOrDefault("startY", "0") + ";"
                + m.getOrDefault("endX", "0") + ";" + m.getOrDefault("endY", "0") + ";" + m.getOrDefault("centerX", "0") + ";"
                + m.getOrDefault("centerY", "0") + ";" + m.getOrDefault("gradientRadius", "0") + ";" + tileName + ";" + stops;
    }
}
