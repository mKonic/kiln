package dev.kiln;

import android.os.Parcel;
import android.text.Annotation;
import android.text.SpannableString;
import android.text.Spanned;
import android.util.Base64;

/**
 * Compose's span styles in clips (AndroidClipboardManager's convertToCharSequence and
 * convertToAnnotatedString with EncodeHelper / DecodeHelper): each SpanStyle is a Parcel, Base64
 * encoded, in an Annotation span keyed "androidx.compose.text.SpanStyle", so styled text copied
 * from kiln pastes styled into Compose apps and back.
 *
 * Native passes the styles flat, per span: INTS ints (start, end, the mask of present fields by
 * field id, the font size and letter spacing unit types, weight, style, synthesis, decoration,
 * then the colour, background and shadow colour as ARGB), FLOATS floats (font size, letter
 * spacing, baseline shift, scale x, skew x, shadow x, y, blur) and one string (font feature
 * settings).
 */
final class KilnClipSpans {
    private KilnClipSpans() {}

    static final int INTS = 12, FLOATS = 8;
    private static final String KEY = "androidx.compose.text.SpanStyle";

    private static final byte UNIT_TYPE_UNSPECIFIED = 0;
    private static final byte COLOR_ID = 1;
    private static final byte FONT_SIZE_ID = 2;
    private static final byte FONT_WEIGHT_ID = 3;
    private static final byte FONT_STYLE_ID = 4;
    private static final byte FONT_SYNTHESIS_ID = 5;
    private static final byte FONT_FEATURE_SETTINGS_ID = 6;
    private static final byte LETTER_SPACING_ID = 7;
    private static final byte BASELINE_SHIFT_ID = 8;
    private static final byte TEXT_GEOMETRIC_TRANSFORM_ID = 9;
    private static final byte BACKGROUND_ID = 10;
    private static final byte TEXT_DECORATION_ID = 11;
    private static final byte SHADOW_ID = 12;

    private static final int BYTE_SIZE = 1;
    private static final int INT_SIZE = 4;
    private static final int FLOAT_SIZE = 4;
    private static final int LONG_SIZE = 8;
    private static final int COLOR_SIZE = LONG_SIZE;
    private static final int TEXT_UNIT_SIZE = BYTE_SIZE + FLOAT_SIZE;
    private static final int FONT_WEIGHT_SIZE = INT_SIZE;
    private static final int FONT_STYLE_SIZE = BYTE_SIZE;
    private static final int FONT_SYNTHESIS_SIZE = BYTE_SIZE;
    private static final int BASELINE_SHIFT_SIZE = FLOAT_SIZE;
    private static final int TEXT_GEOMETRIC_TRANSFORM_SIZE = FLOAT_SIZE * 2;
    private static final int TEXT_DECORATION_SIZE = INT_SIZE;
    private static final int SHADOW_SIZE = COLOR_SIZE + FLOAT_SIZE * 3;

    // Field indexes in the flat arrays.
    private static final int I_START = 0, I_END = 1, I_MASK = 2, I_SIZE_TYPE = 3, I_SPACING_TYPE = 4, I_WEIGHT = 5, I_STYLE = 6,
            I_SYNTHESIS = 7, I_DECORATION = 8, I_COLOR = 9, I_BACKGROUND = 10, I_SHADOW_COLOR = 11;
    private static final int F_SIZE = 0, F_SPACING = 1, F_SHIFT = 2, F_SCALE_X = 3, F_SKEW_X = 4, F_SHADOW_X = 5, F_SHADOW_Y = 6,
            F_SHADOW_BLUR = 7;

    private static boolean has(int mask, byte id) { return (mask & (1 << id)) != 0; }

    /** convertToCharSequence: the text, with one Annotation per span style. */
    static CharSequence encode(String text, int[] ints, float[] floats, String[] strings) {
        int n = ints.length / INTS;
        if (n == 0) return text;
        SpannableString s = new SpannableString(text);
        for (int i = 0; i < n; ++i) {
            int io = i * INTS, fo = i * FLOATS;
            int mask = ints[io + I_MASK];
            Parcel p = Parcel.obtain();
            try {
                if (has(mask, COLOR_ID)) { p.writeByte(COLOR_ID); p.writeLong(android.graphics.Color.pack(ints[io + I_COLOR])); }
                if (has(mask, FONT_SIZE_ID)) { p.writeByte(FONT_SIZE_ID); writeTextUnit(p, ints[io + I_SIZE_TYPE], floats[fo + F_SIZE]); }
                if (has(mask, FONT_WEIGHT_ID)) { p.writeByte(FONT_WEIGHT_ID); p.writeInt(ints[io + I_WEIGHT]); }
                if (has(mask, FONT_STYLE_ID)) { p.writeByte(FONT_STYLE_ID); p.writeByte((byte) ints[io + I_STYLE]); }
                if (has(mask, FONT_SYNTHESIS_ID)) { p.writeByte(FONT_SYNTHESIS_ID); p.writeByte((byte) ints[io + I_SYNTHESIS]); }
                if (has(mask, FONT_FEATURE_SETTINGS_ID)) { p.writeByte(FONT_FEATURE_SETTINGS_ID); p.writeString(strings[i]); }
                if (has(mask, LETTER_SPACING_ID)) {
                    p.writeByte(LETTER_SPACING_ID);
                    writeTextUnit(p, ints[io + I_SPACING_TYPE], floats[fo + F_SPACING]);
                }
                if (has(mask, BASELINE_SHIFT_ID)) { p.writeByte(BASELINE_SHIFT_ID); p.writeFloat(floats[fo + F_SHIFT]); }
                if (has(mask, TEXT_GEOMETRIC_TRANSFORM_ID)) {
                    p.writeByte(TEXT_GEOMETRIC_TRANSFORM_ID);
                    p.writeFloat(floats[fo + F_SCALE_X]);
                    p.writeFloat(floats[fo + F_SKEW_X]);
                }
                if (has(mask, BACKGROUND_ID)) { p.writeByte(BACKGROUND_ID); p.writeLong(android.graphics.Color.pack(ints[io + I_BACKGROUND])); }
                if (has(mask, TEXT_DECORATION_ID)) { p.writeByte(TEXT_DECORATION_ID); p.writeInt(ints[io + I_DECORATION]); }
                if (has(mask, SHADOW_ID)) {
                    p.writeByte(SHADOW_ID);
                    p.writeLong(android.graphics.Color.pack(ints[io + I_SHADOW_COLOR]));
                    p.writeFloat(floats[fo + F_SHADOW_X]);
                    p.writeFloat(floats[fo + F_SHADOW_Y]);
                    p.writeFloat(floats[fo + F_SHADOW_BLUR]);
                }
                String encoded = Base64.encodeToString(p.marshall(), Base64.DEFAULT);
                s.setSpan(new Annotation(KEY, encoded), ints[io + I_START], ints[io + I_END], Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            } finally {
                p.recycle();
            }
        }
        return s;
    }

    private static void writeTextUnit(Parcel p, int type, float value) {
        p.writeByte((byte) type);
        if (type != UNIT_TYPE_UNSPECIFIED) p.writeFloat(value);
    }

    /**
     * convertToAnnotatedString: {text, ints, floats, strings} for the clip text's span styles; the
     * ints and floats are as for encode.
     */
    static Object[] decode(CharSequence text) {
        if (text == null) return null;
        if (!(text instanceof Spanned)) return new Object[] { text.toString(), new int[0], new float[0], new String[0] };
        Spanned spanned = (Spanned) text;
        Annotation[] annotations = spanned.getSpans(0, spanned.length(), Annotation.class);
        java.util.ArrayList<int[]> intList = new java.util.ArrayList<>();
        java.util.ArrayList<float[]> floatList = new java.util.ArrayList<>();
        java.util.ArrayList<String> stringList = new java.util.ArrayList<>();
        for (Annotation span : annotations) {
            if (!KEY.equals(span.getKey())) continue;
            int[] in = new int[INTS];
            float[] fl = new float[FLOATS];
            String[] str = new String[1];
            in[I_START] = spanned.getSpanStart(span);
            in[I_END] = spanned.getSpanEnd(span);
            decodeSpanStyle(span.getValue(), in, fl, str);
            intList.add(in);
            floatList.add(fl);
            stringList.add(str[0]);
        }
        int n = intList.size();
        int[] ints = new int[n * INTS];
        float[] floats = new float[n * FLOATS];
        for (int i = 0; i < n; ++i) {
            System.arraycopy(intList.get(i), 0, ints, i * INTS, INTS);
            System.arraycopy(floatList.get(i), 0, floats, i * FLOATS, FLOATS);
        }
        return new Object[] { text.toString(), ints, floats, stringList.toArray(new String[0]) };
    }

    private static void decodeSpanStyle(String value, int[] in, float[] fl, String[] str) {
        Parcel parcel = Parcel.obtain();
        try {
            byte[] bytes = Base64.decode(value, Base64.DEFAULT);
            parcel.unmarshall(bytes, 0, bytes.length);
            parcel.setDataPosition(0);
            int mask = 0;
            loop:
            while (parcel.dataAvail() > BYTE_SIZE) {
                byte id = parcel.readByte();
                switch (id) {
                    case COLOR_ID:
                        if (parcel.dataAvail() < COLOR_SIZE) break loop;
                        in[I_COLOR] = android.graphics.Color.toArgb(parcel.readLong());
                        break;
                    case FONT_SIZE_ID:
                        if (parcel.dataAvail() < TEXT_UNIT_SIZE) break loop;
                        in[I_SIZE_TYPE] = parcel.readByte();
                        if (in[I_SIZE_TYPE] != UNIT_TYPE_UNSPECIFIED) fl[F_SIZE] = parcel.readFloat();
                        break;
                    case FONT_WEIGHT_ID:
                        if (parcel.dataAvail() < FONT_WEIGHT_SIZE) break loop;
                        in[I_WEIGHT] = parcel.readInt();
                        break;
                    case FONT_STYLE_ID:
                        if (parcel.dataAvail() < FONT_STYLE_SIZE) break loop;
                        in[I_STYLE] = parcel.readByte();
                        break;
                    case FONT_SYNTHESIS_ID:
                        if (parcel.dataAvail() < FONT_SYNTHESIS_SIZE) break loop;
                        in[I_SYNTHESIS] = parcel.readByte();
                        break;
                    case FONT_FEATURE_SETTINGS_ID:
                        str[0] = parcel.readString();
                        break;
                    case LETTER_SPACING_ID:
                        if (parcel.dataAvail() < TEXT_UNIT_SIZE) break loop;
                        in[I_SPACING_TYPE] = parcel.readByte();
                        if (in[I_SPACING_TYPE] != UNIT_TYPE_UNSPECIFIED) fl[F_SPACING] = parcel.readFloat();
                        break;
                    case BASELINE_SHIFT_ID:
                        if (parcel.dataAvail() < BASELINE_SHIFT_SIZE) break loop;
                        fl[F_SHIFT] = parcel.readFloat();
                        break;
                    case TEXT_GEOMETRIC_TRANSFORM_ID:
                        if (parcel.dataAvail() < TEXT_GEOMETRIC_TRANSFORM_SIZE) break loop;
                        fl[F_SCALE_X] = parcel.readFloat();
                        fl[F_SKEW_X] = parcel.readFloat();
                        break;
                    case BACKGROUND_ID:
                        if (parcel.dataAvail() < COLOR_SIZE) break loop;
                        in[I_BACKGROUND] = android.graphics.Color.toArgb(parcel.readLong());
                        break;
                    case TEXT_DECORATION_ID:
                        if (parcel.dataAvail() < TEXT_DECORATION_SIZE) break loop;
                        in[I_DECORATION] = parcel.readInt();
                        break;
                    case SHADOW_ID:
                        if (parcel.dataAvail() < SHADOW_SIZE) break loop;
                        in[I_SHADOW_COLOR] = android.graphics.Color.toArgb(parcel.readLong());
                        fl[F_SHADOW_X] = parcel.readFloat();
                        fl[F_SHADOW_Y] = parcel.readFloat();
                        fl[F_SHADOW_BLUR] = parcel.readFloat();
                        break;
                    default:
                        continue;
                }
                mask |= 1 << id;
            }
            in[I_MASK] = mask;
        } finally {
            parcel.recycle();
        }
    }
}
