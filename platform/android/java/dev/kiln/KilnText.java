package dev.kiln;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.fonts.Font;
import android.graphics.text.LineBreakConfig;
import android.graphics.text.PositionedGlyphs;
import android.graphics.text.TextRunShaper;
import android.os.Build;
import android.os.LocaleList;
import android.text.Layout;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.StaticLayout;
import android.text.TextDirectionHeuristic;
import android.text.TextDirectionHeuristics;
import android.text.TextPaint;
import android.text.TextUtils;
import android.text.style.LeadingMarginSpan;
import android.text.style.LineHeightSpan;
import android.text.style.MetricAffectingSpan;

import java.text.Bidi;
import java.text.BreakIterator;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;
import java.util.Objects;
import java.util.PriorityQueue;

/**
 * Platform text path (architecture 3.5). Native sends one paragraph: text, the paragraph style
 * and resolved character runs. Like Compose's AndroidParagraph this builds a Spannable (one
 * metric span per run, the line height span, the indent span) for a StaticLayout; glyphs come
 * from TextRunShaper per line, bidi run and style run, and are rasterised once each into RGBA
 * atlas pages with Canvas.drawGlyphs.
 *
 * Called from native on the main thread.
 */
final class KilnText {
    static final int PAGE_SIZE = 1024;

    // Request layout (mirrors AndroidText.cpp).
    private static final int P_INTS = 13, R_INTS = 8, P_FLOATS = 6, R_FLOATS = 6, R_STRS = 4;
    // Result layout.
    private static final int HEADER = 12, GLYPH = 11, LINE = 14, SEGMENT = 5, METRICS = 4, PLACEHOLDER = 4;
    // After the runs: placeholder count, then [start, end, widthUnit, heightUnit, align] and
    // [width, height] each (unit 0: px, 1: em of the font size).
    private static final int PH_INTS = 5, PH_FLOATS = 2;

    private final Context context;
    private final Paint glyphPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Page> pages = new ArrayList<>();
    private final HashMap<GlyphKey, float[]> slots = new HashMap<>();
    private final HashMap<String, Typeface> typefaces = new HashMap<>();
    private final RectF bounds = new RectF();
    private final int[] oneGlyph = new int[1];
    private final float[] onePosition = new float[2];
    private final HashMap<Integer, Laid> layouts = new HashMap<>();
    private int nextHandle = 1;

    static final class Page {
        final Bitmap bitmap = Bitmap.createBitmap(PAGE_SIZE, PAGE_SIZE, Bitmap.Config.ARGB_8888);
        final Canvas canvas = new Canvas(bitmap);
        int shelfX, shelfY, shelfH;
        int dirtyY0 = PAGE_SIZE, dirtyY1 = 0;
        boolean reported;
    }

    private static final class GlyphKey {
        final Font font; final int glyph, sizeQ, skewQ, scaleQ; final boolean bold;
        GlyphKey(Font font, int glyph, Paint p, boolean bold) {
            this.font = font; this.glyph = glyph; this.bold = bold;
            sizeQ = Math.round(p.getTextSize() * 4);
            skewQ = Math.round(p.getTextSkewX() * 100);
            scaleQ = Math.round(p.getTextScaleX() * 100);
        }
        @Override public boolean equals(Object o) {
            if (!(o instanceof GlyphKey)) return false;
            GlyphKey k = (GlyphKey) o;
            return glyph == k.glyph && sizeQ == k.sizeQ && skewQ == k.skewQ && scaleQ == k.scaleQ && bold == k.bold && font.equals(k.font);
        }
        @Override public int hashCode() { return Objects.hash(font, glyph, sizeQ, skewQ, scaleQ, bold); }
    }

    /** One resolved run: a paint and what drawing needs beyond it. */
    private static final class Run {
        int start, end;
        TextPaint paint;
        boolean fakeBold, fakeItalic;
    }

    /** The span carrying a run's paint into StaticLayout's measurement. */
    private static final class RunSpan extends MetricAffectingSpan {
        final Run run;
        RunSpan(Run run) { this.run = run; }
        @Override public void updateMeasureState(TextPaint tp) { apply(tp); }
        @Override public void updateDrawState(TextPaint tp) { apply(tp); }
        private void apply(TextPaint tp) {
            final TextPaint p = run.paint;
            tp.setTypeface(p.getTypeface());
            tp.setTextSize(p.getTextSize());
            tp.setLetterSpacing(p.getLetterSpacing());
            tp.setTextScaleX(p.getTextScaleX());
            tp.setTextSkewX(p.getTextSkewX());
            tp.setFontFeatureSettings(p.getFontFeatureSettings());
            tp.setTextLocales(p.getTextLocales());
            tp.baselineShift = p.baselineShift;
        }
    }

    /** A laid-out paragraph kept for queries. */
    private static final class Laid {
        StaticLayout layout;
        CharSequence text;
        int lineCount;
        int topPadding, bottomPadding;
        Paint.FontMetricsInt lastLineMetrics;
        float[] lineTop, lineBottom;
        Locale locale;
    }

    KilnText(Context context) {
        this.context = context;
        glyphPaint.setColor(Color.WHITE);
    }

    // --- request parsing ---------------------------------------------------------------------

    private Run makeRun(int[] ints, float[] floats, String[] strs, int index) {
        final int io = P_INTS + index * R_INTS, fo = P_FLOATS + index * R_FLOATS, so = index * R_STRS;
        Run run = new Run();
        run.start = ints[io];
        run.end = ints[io + 1];
        final int kind = ints[io + 2], weight = ints[io + 3];
        final boolean italic = ints[io + 4] != 0;
        run.fakeBold = ints[io + 5] != 0;
        run.fakeItalic = ints[io + 6] != 0;
        TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        p.setTypeface(typeface(kind, strs[so], weight, italic, strs[so + 1]));
        p.setTextSize(floats[fo]);
        final float lsPx = floats[fo + 1], lsEm = floats[fo + 2];
        if (!Float.isNaN(lsEm)) p.setLetterSpacing(lsEm);
        else if (!Float.isNaN(lsPx) && p.getTextSize() > 0) p.setLetterSpacing(lsPx / p.getTextSize());
        p.setTextScaleX(floats[fo + 4]);
        p.setTextSkewX(floats[fo + 5]);
        if (!strs[so + 2].isEmpty()) p.setFontFeatureSettings(strs[so + 2]);
        if (!strs[so + 3].isEmpty()) p.setTextLocales(LocaleList.forLanguageTags(strs[so + 3]));
        // BaselineShiftSpan: a multiple of the ascent (negative is up).
        final float shift = floats[fo + 3];
        if (shift != 0) p.baselineShift = (int) Math.ceil(p.ascent() * shift);
        run.paint = p;
        return run;
    }

    // Downloadable fonts by query: loaded, or null while loading / after failing.
    private final HashMap<String, Typeface> downloaded = new HashMap<>();
    private final java.util.HashSet<String> requested = new java.util.HashSet<>();
    private final android.os.Handler mainHandler = new android.os.Handler(android.os.Looper.getMainLooper());

    /** FontsContract.requestFonts; on arrival native lays text out again. */
    private Typeface downloadable(String spec) {
        Typeface tf = downloaded.get(spec);
        if (tf != null || requested.contains(spec)) return tf;
        requested.add(spec);
        final String[] parts = spec.split("\n", -1);
        if (parts.length < 4) return null;
        java.util.ArrayList<byte[]> certs = new java.util.ArrayList<>();
        for (String c : parts[3].split(",")) if (!c.isEmpty()) certs.add(android.util.Base64.decode(c.trim(), android.util.Base64.DEFAULT));
        java.util.ArrayList<java.util.List<byte[]>> certSets = new java.util.ArrayList<>();
        for (byte[] c : certs) certSets.add(java.util.Collections.singletonList(c));
        android.provider.FontRequest request = new android.provider.FontRequest(parts[0], parts[1], parts[2], certSets);
        android.provider.FontsContract.requestFonts(context, request, mainHandler, null, new android.provider.FontsContract.FontRequestCallback() {
            @Override public void onTypefaceRetrieved(Typeface typeface) {
                downloaded.put(spec, typeface);
                typefaces.clear();   // drop the fallbacks made while loading
                KilnNative.fontsChanged();
            }
            @Override public void onTypefaceRequestFailed(int reason) {
                android.util.Log.w("kiln", "downloadable font failed (" + reason + "): " + parts[2]);
            }
        });
        return null;
    }

    private Typeface typeface(int kind, String name, int weight, boolean italic, String variation) {
        final String key = kind + "|" + name + "|" + weight + "|" + italic + "|" + variation;
        Typeface cached = typefaces.get(key);
        if (cached != null) return cached;
        Typeface base;
        switch (kind) {
            case 8: {
                base = downloadable(name);
                if (base == null) return Typeface.create(Typeface.DEFAULT, Math.max(1, Math.min(1000, weight)), italic);   // not cached
                break;
            }
            case 1: base = Typeface.SANS_SERIF; break;
            case 2: base = Typeface.SERIF; break;
            case 3: base = Typeface.MONOSPACE; break;
            case 4: base = Typeface.create("cursive", Typeface.NORMAL); break;
            case 5: base = Typeface.create(name, Typeface.NORMAL); break;
            case 6: case 7: {
                Typeface.Builder b = kind == 6 ? new Typeface.Builder(context.getAssets(), name) : new Typeface.Builder(name);
                if (!variation.isEmpty()) b.setFontVariationSettings(variation);
                base = b.build();
                if (base == null) base = Typeface.DEFAULT;
                break;
            }
            default: base = Typeface.DEFAULT;
        }
        Typeface tf = Typeface.create(base, Math.max(1, Math.min(1000, weight)), italic);
        typefaces.put(key, tf);
        return tf;
    }

    // TextAlignmentAdapter: Left and Right are the hidden ALIGN_LEFT / ALIGN_RIGHT.
    private static Layout.Alignment alignLeft, alignRight;
    static {
        try { alignLeft = Layout.Alignment.valueOf("ALIGN_LEFT"); } catch (IllegalArgumentException e) { alignLeft = Layout.Alignment.ALIGN_NORMAL; }
        try { alignRight = Layout.Alignment.valueOf("ALIGN_RIGHT"); } catch (IllegalArgumentException e) { alignRight = Layout.Alignment.ALIGN_OPPOSITE; }
    }

    private static Layout.Alignment alignment(int textAlign) {
        switch (textAlign) {
            case 1: return alignLeft;
            case 2: return alignRight;
            case 3: return Layout.Alignment.ALIGN_CENTER;
            case 6: return Layout.Alignment.ALIGN_OPPOSITE;
            default: return Layout.Alignment.ALIGN_NORMAL;   // Start, Justify
        }
    }

    private static TextDirectionHeuristic direction(int textDirection) {
        switch (textDirection) {
            case 1: return TextDirectionHeuristics.LTR;
            case 2: return TextDirectionHeuristics.RTL;
            case 5: return TextDirectionHeuristics.FIRSTSTRONG_RTL;
            case 4: default: return TextDirectionHeuristics.FIRSTSTRONG_LTR;
        }
    }

    // --- layout ------------------------------------------------------------------------------

    /** A Spannable and paints for a request. */
    private static final class Built {
        SpannableString text;
        TextPaint basePaint;
        Run[] runs;
        LineHeightStyleSpan lineHeightSpan;
        PlaceholderSpan[] placeholders = new PlaceholderSpan[0];
    }

    private Built build(String text, int[] ints, float[] floats, String[] strs) {
        Built b = new Built();
        final int nRuns = ints[0];
        b.basePaint = makeRun(ints, floats, strs, 0).paint;
        b.runs = new Run[nRuns];
        b.text = new SpannableString(text);
        final int len = text.length();
        for (int i = 0; i < nRuns; i++) {
            b.runs[i] = makeRun(ints, floats, strs, i + 1);
            if (b.runs[i].end > b.runs[i].start)
                b.text.setSpan(new RunSpan(b.runs[i]), b.runs[i].start, b.runs[i].end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        final int spanFlags = len == 0 ? Spanned.SPAN_INCLUSIVE_INCLUSIVE : Spanned.SPAN_EXCLUSIVE_EXCLUSIVE;
        final float lineHeight = floats[2];
        if (!Float.isNaN(lineHeight) && lineHeight > 0) {
            final boolean includePad = ints[12] != 0, hasStyle = ints[7] != 0;
            if (includePad && !hasStyle) {
                b.text.setSpan(new LegacyLineHeightSpan(lineHeight), 0, len, spanFlags);
            } else {
                final int endIndex = len == 0 || text.charAt(len - 1) == '\n' ? len + 1 : len;
                final int trim = ints[5];
                b.lineHeightSpan = new LineHeightStyleSpan(lineHeight, 0, endIndex, (trim & 0x1) != 0, (trim & 0x10) != 0,
                        floats[3], ints[6]);
                b.text.setSpan(b.lineHeightSpan, 0, len, spanFlags);
            }
        }
        final int first = (int) Math.ceil(floats[4]), rest = (int) Math.ceil(floats[5]);
        if (first != 0 || rest != 0) b.text.setSpan(new LeadingMarginSpan.Standard(first, rest), 0, len, spanFlags);
        final int pio = P_INTS + (nRuns + 1) * R_INTS, pfo = P_FLOATS + (nRuns + 1) * R_FLOATS;
        final int nPh = pio < ints.length ? ints[pio] : 0;
        b.placeholders = new PlaceholderSpan[nPh];
        for (int i = 0; i < nPh; i++) {
            final int io = pio + 1 + i * PH_INTS, fo = pfo + i * PH_FLOATS;
            PlaceholderSpan ph = new PlaceholderSpan(floats[fo], ints[io + 2], floats[fo + 1], ints[io + 3], ints[io + 4]);
            ph.start = ints[io];
            ph.end = ints[io + 1];
            b.placeholders[i] = ph;
            if (ph.end > ph.start) b.text.setSpan(ph, ph.start, ph.end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return b;
    }

    private StaticLayout staticLayout(CharSequence text, TextPaint paint, int width, int[] ints, int maxLines,
                                      TextUtils.TruncateAt ellipsize) {
        StaticLayout.Builder b = StaticLayout.Builder.obtain(text, 0, text.length(), paint, width)
                .setAlignment(alignment(ints[3]))
                .setTextDirection(direction(ints[4]))
                .setMaxLines(maxLines)
                .setEllipsize(ellipsize)
                .setEllipsizedWidth(width)
                .setIncludePad(ints[12] != 0)
                .setUseLineSpacingFromFallbacks(true)
                .setBreakStrategy(ints[8] == 2 ? Layout.BREAK_STRATEGY_HIGH_QUALITY : ints[8] == 3 ? Layout.BREAK_STRATEGY_BALANCED : Layout.BREAK_STRATEGY_SIMPLE)
                .setHyphenationFrequency(ints[11] == 2 ? (Build.VERSION.SDK_INT >= 33 ? Layout.HYPHENATION_FREQUENCY_FULL_FAST : Layout.HYPHENATION_FREQUENCY_FULL) : Layout.HYPHENATION_FREQUENCY_NONE)
                .setJustificationMode(ints[3] == 4 ? Layout.JUSTIFICATION_MODE_INTER_WORD : Layout.JUSTIFICATION_MODE_NONE);
        if (Build.VERSION.SDK_INT >= 33) {
            final int strictness = ints[9], word = ints[10];
            LineBreakConfig.Builder lb = new LineBreakConfig.Builder();
            lb.setLineBreakStyle(strictness == 2 ? LineBreakConfig.LINE_BREAK_STYLE_LOOSE : strictness == 3 ? LineBreakConfig.LINE_BREAK_STYLE_NORMAL
                    : strictness == 4 ? LineBreakConfig.LINE_BREAK_STYLE_STRICT : LineBreakConfig.LINE_BREAK_STYLE_NONE);
            lb.setLineBreakWordStyle(word == 2 ? LineBreakConfig.LINE_BREAK_WORD_STYLE_PHRASE : LineBreakConfig.LINE_BREAK_WORD_STYLE_NONE);
            b.setLineBreakConfig(lb.build());
        }
        return b.build();
    }

    /**
     * Lays out one paragraph. Returns the header, then glyphs, lines, run segments and run
     * metrics (see AndroidText.cpp).
     */
    float[] layout(String text, int[] ints, float[] floats, String[] strs) {
        Built b = build(text, ints, floats, strs);
        final float widthPx = floats[0];
        final int width = widthPx > 0 ? (int) Math.ceil(widthPx) : Integer.MAX_VALUE / 4;
        final int maxLines = Math.max(1, ints[1]);
        final int overflow = ints[2];
        final TextUtils.TruncateAt ellipsize = overflow == 1 ? TextUtils.TruncateAt.END
                : overflow == 3 ? TextUtils.TruncateAt.START : overflow == 4 ? TextUtils.TruncateAt.MIDDLE : null;
        StaticLayout layout = staticLayout(b.text, b.basePaint, width, ints, maxLines, ellipsize);
        // Ellipsis when the lines do not fit the height (end ellipsis only).
        final float maxHeight = floats[1];
        if (overflow == 1 && maxLines > 1 && !Float.isInfinite(maxHeight) && layout.getHeight() > maxHeight) {
            int fit = 0;
            while (fit < layout.getLineCount() && layout.getLineBottom(fit) <= maxHeight) fit++;
            if (fit != maxLines) layout = staticLayout(b.text, b.basePaint, width, ints, Math.max(1, fit), ellipsize);
        }

        Laid laid = new Laid();
        laid.layout = layout;
        laid.text = b.text;
        laid.locale = b.basePaint.getTextLocale();
        final int lineCount = Math.max(1, Math.min(layout.getLineCount(), maxLines));
        laid.lineCount = lineCount;
        final int lastLine = lineCount - 1;
        final boolean didExceed = lineCount >= maxLines
                && (layout.getEllipsisCount(lastLine) > 0 || layout.getLineEnd(lastLine) != text.length());

        // TextLayout: paddings from the line height span, and the empty last line's metrics.
        // (Font padding paddings are zero: fallback line spacing is on.)
        int topPadding = 0, bottomPadding = 0;
        if (b.lineHeightSpan != null) {
            final LineHeightStyleSpan s = b.lineHeightSpan;
            if (s.firstAscentDiff < 0) topPadding = Math.abs(s.firstAscentDiff);
            // As in Compose's getLineHeightPaddings, which compares against the first diff.
            if (s.lastDescentDiff < 0) bottomPadding = Math.max(topPadding, Math.abs(s.lastDescentDiff));
        }
        laid.topPadding = topPadding;
        laid.bottomPadding = bottomPadding;
        laid.lineTop = new float[lineCount];
        laid.lineBottom = new float[lineCount];
        // Line bounds before the last line metrics (getLineHeight in TextLayout's init).
        for (int i = 0; i < lineCount; i++) {
            laid.lineTop[i] = layout.getLineTop(i) + (i == 0 ? 0 : topPadding);
            laid.lineBottom[i] = topPadding + layout.getLineBottom(i) + (i == lastLine ? bottomPadding : 0);
        }
        Paint.FontMetricsInt lastMetrics = null;
        if (layout.getLineStart(lastLine) == layout.getLineEnd(lastLine) && b.lineHeightSpan != null) {
            SpannableString empty = new SpannableString("​");
            final LineHeightStyleSpan s = b.lineHeightSpan;
            empty.setSpan(s.copy(0, empty.length(), lastLine != 0 && s.trimLastLineBottom ? false : s.trimLastLineBottom),
                    0, empty.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            StaticLayout tmp = StaticLayout.Builder.obtain(empty, 0, empty.length(), b.basePaint, Integer.MAX_VALUE)
                    .setTextDirection(direction(ints[4])).setIncludePad(ints[12] != 0).setUseLineSpacingFromFallbacks(true).build();
            lastMetrics = new Paint.FontMetricsInt();
            lastMetrics.ascent = tmp.getLineAscent(0);
            lastMetrics.descent = tmp.getLineDescent(0);
            lastMetrics.top = tmp.getLineTop(0);
            lastMetrics.bottom = tmp.getLineBottom(0);
        }
        final int lastLineExtra = lastMetrics != null ? lastMetrics.bottom - (int) (laid.lineBottom[lastLine] - laid.lineTop[lastLine]) : 0;
        laid.lastLineMetrics = lastMetrics;
        if (lastMetrics != null)
            laid.lineBottom[lastLine] = (lastLine > 0 ? layout.getLineBottom(lastLine - 1) : 0) + lastMetrics.bottom;
        final float height = (didExceed ? layout.getLineBottom(lastLine) : layout.getHeight()) + topPadding + bottomPadding + lastLineExtra;

        // Glyphs, segments.
        final char[] chars = text.toCharArray();
        ArrayList<float[]> glyphs = new ArrayList<>();
        ArrayList<float[]> segments = new ArrayList<>();
        float[] baselines = new float[lineCount];
        float maxWidth = 0;
        for (int line = 0; line < lineCount; line++) {
            final int start = layout.getLineStart(line);
            final int ellipsisCount = layout.getEllipsisCount(line);
            final int ellipsisStart = start + layout.getEllipsisStart(line);
            if (ellipsisCount > 0) {
                for (int k = ellipsisStart; k < ellipsisStart + ellipsisCount && k < chars.length; k++)
                    chars[k] = k == ellipsisStart ? '…' : '﻿';
            }
            final int end = ellipsisCount > 0 ? Math.min(chars.length, ellipsisStart + 1) : layout.getLineVisibleEnd(line);
            final float baseline = topPadding + (line == lastLine && lastMetrics != null
                    ? laid.lineTop[line] - lastMetrics.ascent : layout.getLineBaseline(line));
            baselines[line] = baseline;
            maxWidth = Math.max(maxWidth, layout.getLineWidth(line));
            if (end <= start) continue;
            shapeLine(b, layout, chars, line, start, end, baseline, glyphs, segments);
        }

        // The Layout stays alive for queries until native releases the handle.
        final int handle = nextHandle++;
        if (nextHandle >= (1 << 23)) nextHandle = 1;   // stays exact as a float
        layouts.put(handle, laid);

        final int nRuns = b.runs.length;
        final int nPh = b.placeholders.length;
        float[] out = new float[HEADER + glyphs.size() * GLYPH + lineCount * LINE + segments.size() * SEGMENT + nRuns * METRICS + nPh * PLACEHOLDER];
        out[0] = widthPx > 0 ? Math.min(maxWidth, widthPx) : maxWidth;
        out[1] = height;
        out[2] = lineCount;
        out[3] = handle;
        out[4] = baselines.length > 0 ? baselines[0] : 0;
        out[5] = baselines.length > 0 ? baselines[lastLine] : 0;
        out[6] = didExceed ? 1 : 0;
        out[7] = glyphs.size();
        out[8] = lineCount;
        out[9] = segments.size();
        out[10] = nRuns;
        out[11] = nPh;
        int o = HEADER;
        for (float[] g : glyphs) { System.arraycopy(g, 0, out, o, GLYPH); o += GLYPH; }
        for (int line = 0; line < lineCount; line++) {
            out[o] = laid.lineTop[line];
            out[o + 1] = laid.lineBottom[line];
            out[o + 2] = baselines[line];
            out[o + 3] = line == lastLine && lastMetrics != null ? lastMetrics.ascent : layout.getLineAscent(line);
            out[o + 4] = line == lastLine && lastMetrics != null ? lastMetrics.descent : layout.getLineDescent(line);
            out[o + 5] = layout.getLineLeft(line);
            out[o + 6] = layout.getLineRight(line);
            out[o + 7] = layout.getLineStart(line);
            out[o + 8] = layout.getLineEnd(line);
            out[o + 9] = layout.getLineVisibleEnd(line);
            out[o + 10] = layout.getEllipsisStart(line);
            out[o + 11] = layout.getEllipsisCount(line);
            out[o + 12] = layout.getParagraphDirection(line) == Layout.DIR_RIGHT_TO_LEFT ? 1 : 0;
            o += LINE;
        }
        for (float[] s : segments) { System.arraycopy(s, 0, out, o, SEGMENT); o += SEGMENT; }
        for (Run run : b.runs) {
            out[o] = run.paint.getUnderlinePosition();
            out[o + 1] = run.paint.getUnderlineThickness();
            out[o + 2] = run.paint.getStrikeThruPosition();
            out[o + 3] = run.paint.getStrikeThruThickness();
            o += METRICS;
        }
        for (PlaceholderSpan ph : b.placeholders) {
            final float[] r = placeholderRect(laid, ph, maxLines);
            for (int k = 0; k < 4; k++) out[o + k] = r == null ? Float.NaN : r[k];
            o += PLACEHOLDER;
        }
        return out;
    }

    private int runAt(Run[] runs, int offset) {
        int lo = 0, hi = runs.length - 1;
        while (lo < hi) {
            final int mid = (lo + hi + 1) >>> 1;
            if (runs[mid].start <= offset) lo = mid; else hi = mid - 1;
        }
        return lo;
    }

    private static final class Piece { int run, start, end; boolean space, placeholder; PositionedGlyphs glyphs; float advance; }

    private static PlaceholderSpan placeholderAt(Built b, int offset) {
        for (PlaceholderSpan p : b.placeholders) if (offset >= p.start && offset < p.end) return p;
        return null;
    }

    private void shapeLine(Built b, StaticLayout layout, char[] chars, int line, int start, int end, float baseline,
                           ArrayList<float[]> glyphs, ArrayList<float[]> segments) {
        final boolean paraRtl = layout.getParagraphDirection(line) == Layout.DIR_RIGHT_TO_LEFT;
        Bidi bidi = new Bidi(chars, start, null, 0, end - start,
                paraRtl ? Bidi.DIRECTION_RIGHT_TO_LEFT : Bidi.DIRECTION_LEFT_TO_RIGHT);
        final int nBidi = bidi.getRunCount();
        Integer[] order = new Integer[nBidi];
        byte[] levels = new byte[nBidi];
        for (int r = 0; r < nBidi; r++) { order[r] = r; levels[r] = (byte) bidi.getRunLevel(r); }
        Bidi.reorderVisually(levels, 0, order, 0, nBidi);

        // Justified lines (not a paragraph's last line) stretch their spaces.
        final boolean justify = layout.getJustificationMode() == Layout.JUSTIFICATION_MODE_INTER_WORD
                && line < layout.getLineCount() - 1 && chars[Math.max(start, layout.getLineEnd(line) - 1)] != '\n';
        final Run[] runs = b.runs;
        ArrayList<ArrayList<Piece>> visual = new ArrayList<>();
        float natural = 0;
        int spaces = 0;
        for (int v = 0; v < nBidi; v++) {
            final int r = order[v];
            final int rs = start + bidi.getRunStart(r), re = start + bidi.getRunLimit(r);
            final boolean rtl = (bidi.getRunLevel(r) & 1) != 0;
            ArrayList<Piece> pieces = new ArrayList<>();
            int pos = rs;
            while (pos < re) {
                PlaceholderSpan ph = placeholderAt(b, pos);
                if (ph != null) {
                    Piece p = new Piece();
                    p.run = runs.length > 0 ? runAt(runs, pos) : 0;
                    p.start = pos;
                    p.end = Math.min(re, ph.end);
                    p.placeholder = true;
                    p.advance = pos == ph.start ? ph.widthPx : 0;
                    natural += p.advance;
                    pieces.add(p);
                    pos = p.end;
                    continue;
                }
                final int ri = runs.length > 0 ? runAt(runs, pos) : -1;
                int next = ri >= 0 ? Math.min(re, Math.max(pos + 1, runs[ri].end)) : re;
                for (PlaceholderSpan other : b.placeholders)
                    if (other.start > pos && other.start < next) next = other.start;
                boolean space = false;
                if (justify) {
                    // Split so each space is its own piece.
                    if (chars[pos] == ' ') { next = pos + 1; space = true; }
                    else for (int k = pos + 1; k < next; k++) if (chars[k] == ' ') { next = k; break; }
                }
                Piece p = new Piece();
                p.run = Math.max(0, ri);
                p.start = pos;
                p.end = next;
                p.space = space;
                final TextPaint paint = ri >= 0 ? runs[ri].paint : b.basePaint;
                p.glyphs = TextRunShaper.shapeTextRun(chars, pos, next - pos, start, end - start, 0, 0, rtl, paint);
                p.advance = p.glyphs.getAdvance();
                natural += p.advance;
                if (space) spaces++;
                pieces.add(p);
                pos = next;
            }
            if (rtl) java.util.Collections.reverse(pieces);   // logical -> visual
            visual.add(pieces);
        }
        final float extra = justify && spaces > 0
                ? Math.max(0, (layout.getParagraphRight(line) - layout.getParagraphLeft(line)) - natural) / spaces : 0;
        float x = justify ? layout.getParagraphLeft(line) : layout.getLineLeft(line);
        for (ArrayList<Piece> pieces : visual) {
            for (Piece p : pieces) {
                if (p.placeholder) { x += p.advance; continue; }
                final Run run = runs.length > 0 ? runs[p.run] : null;
                final TextPaint paint = run != null ? run.paint : b.basePaint;
                final float shift = paint.baselineShift;
                for (int i = 0; i < p.glyphs.glyphCount(); i++)
                    addGlyph(glyphs, p.glyphs.getFont(i), p.glyphs.getGlyphId(i), x + p.glyphs.getGlyphX(i),
                            baseline + shift + p.glyphs.getGlyphY(i), paint, run, p.run);
                final float w = p.advance + (p.space ? extra : 0);
                segments.add(new float[] { p.run, line, x, x + w, shift });
                x += w;
            }
        }
    }

    private void addGlyph(ArrayList<float[]> out, Font font, int glyph, float gx, float gy, TextPaint paint, Run run, int runIndex) {
        float[] slot = rasterize(font, glyph, paint, run != null && run.fakeBold, run != null && run.fakeItalic);
        if (slot == null) return;
        // slot: page, left, top, w, h, u, v, uw, vh, color (left/top relative to the pen)
        out.add(new float[] { slot[0], gx + slot[1], gy + slot[2], slot[3], slot[4],
                              slot[5], slot[6], slot[7], slot[8], slot[9], runIndex });
    }

    private float[] rasterize(Font font, int glyph, TextPaint paint, boolean fakeBold, boolean fakeItalic) {
        glyphPaint.setTextSize(paint.getTextSize());
        glyphPaint.setTextScaleX(paint.getTextScaleX());
        glyphPaint.setTextSkewX(paint.getTextSkewX() + (fakeItalic ? -0.25f : 0));
        glyphPaint.setFakeBoldText(fakeBold);
        GlyphKey key = new GlyphKey(font, glyph, glyphPaint, fakeBold);
        if (slots.containsKey(key)) return slots.get(key);
        font.getGlyphBounds(glyph, glyphPaint, bounds);
        // Fake bold strokes outside the outline; skew is not in the bounds on every release.
        final int pad = 1 + (fakeBold ? (int) Math.ceil(paint.getTextSize() / 24) : 0);
        final float skewPad = Math.abs(glyphPaint.getTextSkewX()) * Math.max(Math.abs(bounds.top), Math.abs(bounds.bottom));
        final int left = (int) Math.floor(bounds.left - skewPad) - pad, top = (int) Math.floor(bounds.top) - pad;
        final int w = (int) Math.ceil(bounds.right + skewPad) + pad - left, h = (int) Math.ceil(bounds.bottom) + pad - top;
        if (bounds.isEmpty() || w <= 0 || h <= 0 || w >= PAGE_SIZE || h >= PAGE_SIZE) {
            slots.put(key, null);
            return null;
        }
        Page page = allocate(w, h);
        final int ox = page.shelfX, oy = page.shelfY;
        page.shelfX += w + 1;
        page.shelfH = Math.max(page.shelfH, h);
        oneGlyph[0] = glyph;
        onePosition[0] = ox - left;
        onePosition[1] = oy - top;
        page.canvas.save();
        page.canvas.clipRect(ox, oy, ox + w, oy + h);
        page.canvas.drawGlyphs(oneGlyph, 0, onePosition, 0, 1, font, glyphPaint);
        page.canvas.restore();
        page.dirtyY0 = Math.min(page.dirtyY0, oy);
        page.dirtyY1 = Math.max(page.dirtyY1, oy + h);

        float[] slot = new float[] { pages.indexOf(page), left, top, w, h,
                (float) ox / PAGE_SIZE, (float) oy / PAGE_SIZE, (float) w / PAGE_SIZE, (float) h / PAGE_SIZE,
                isColor(page.bitmap, ox, oy, w, h) ? 1 : 0 };
        slots.put(key, slot);
        return slot;
    }

    /** [min, max] intrinsic widths (LayoutIntrinsics). */
    float[] intrinsics(String text, int[] ints, float[] floats, String[] strs) {
        Built b = build(text, ints, floats, strs);
        final CharSequence cs = b.text;
        final TextPaint paint = b.basePaint;
        float max = (float) Math.ceil(Layout.getDesiredWidth(cs, paint));
        boolean letterSpacing = paint.getLetterSpacing() != 0;
        for (Run r : b.runs) letterSpacing |= r.paint.getLetterSpacing() != 0;
        if (max != 0 && letterSpacing) max += 0.5f;
        // The ten longest line-break segments, by characters; the widest of them.
        BreakIterator it = BreakIterator.getLineInstance(paint.getTextLocale());
        it.setText(text);
        PriorityQueue<int[]> longest = new PriorityQueue<>(10, (l, r) -> (l[1] - l[0]) - (r[1] - r[0]));
        int start = 0, end = it.next();
        while (end != BreakIterator.DONE) {
            if (longest.size() < 10) longest.add(new int[] { start, end });
            else {
                int[] min = longest.peek();
                if (min != null && (min[1] - min[0]) < (end - start)) { longest.poll(); longest.add(new int[] { start, end }); }
            }
            start = end;
            end = it.next();
        }
        float min = 0;
        for (int[] range : longest) min = Math.max(min, Layout.getDesiredWidth(cs, range[0], range[1], paint));
        return new float[] { min, max };
    }

    /** AndroidParagraph.placeholderRects: [l, t, r, b], or null when ellipsized or cut off. */
    private float[] placeholderRect(Laid l, PlaceholderSpan span, int maxLines) {
        if (!span.laidOut || span.end <= span.start) return null;
        final Layout layout = l.layout;
        final int line = layout.getLineForOffset(span.start);
        if (line >= maxLines || line >= l.lineCount) return null;
        final boolean ellipsized = layout.getEllipsisCount(line) > 0 && span.end > layout.getLineStart(line) + layout.getEllipsisStart(line);
        if (ellipsized || span.end > layout.getLineEnd(line)) return null;
        final boolean ltrLine = layout.getParagraphDirection(line) == Layout.DIR_LEFT_TO_RIGHT;
        final boolean rtlChar = layout.isRtlCharAt(span.start);
        float left, right;
        if (ltrLine && !rtlChar) { left = layout.getPrimaryHorizontal(span.start); right = left + span.widthPx; }
        else if (ltrLine) { right = layout.getSecondaryHorizontal(span.start); left = right - span.widthPx; }
        else if (rtlChar) { right = layout.getPrimaryHorizontal(span.start); left = right - span.widthPx; }
        else { left = layout.getSecondaryHorizontal(span.start); right = left + span.widthPx; }
        final float baseline = lineBaseline(l, line);
        final float top;
        switch (span.verticalAlign) {
            case PlaceholderSpan.ALIGN_ABOVE_BASELINE: top = baseline - span.heightPx; break;
            case PlaceholderSpan.ALIGN_TOP: top = l.lineTop[line]; break;
            case PlaceholderSpan.ALIGN_BOTTOM: top = l.lineBottom[line] - span.heightPx; break;
            case PlaceholderSpan.ALIGN_CENTER: top = (l.lineTop[line] + l.lineBottom[line] - span.heightPx) / 2; break;
            case PlaceholderSpan.ALIGN_TEXT_TOP: top = span.fontMetrics.ascent + baseline; break;
            case PlaceholderSpan.ALIGN_TEXT_BOTTOM: top = span.fontMetrics.descent + baseline - span.heightPx; break;
            default: top = (span.fontMetrics.ascent + span.fontMetrics.descent - span.heightPx) / 2f + baseline; break;
        }
        return new float[] { left, top, right, top + span.heightPx };
    }

    private static float lineBaseline(Laid l, int line) {
        if (line == l.lineCount - 1 && l.lastLineMetrics != null) return l.topPadding + l.lineTop[line] - l.lastLineMetrics.ascent;
        return l.topPadding + l.layout.getLineBaseline(line);
    }

    /** PlaceholderSpan: reserves width and height for inline content; draws nothing. */
    static final class PlaceholderSpan extends android.text.style.ReplacementSpan {
        static final int ALIGN_ABOVE_BASELINE = 0, ALIGN_TOP = 1, ALIGN_BOTTOM = 2, ALIGN_CENTER = 3,
                ALIGN_TEXT_TOP = 4, ALIGN_TEXT_BOTTOM = 5, ALIGN_TEXT_CENTER = 6;
        final float width, height;
        final int widthUnit, heightUnit, verticalAlign;   // unit 0: px, 1: em
        int start, end;
        int widthPx, heightPx;
        boolean laidOut;
        Paint.FontMetricsInt fontMetrics;

        PlaceholderSpan(float width, int widthUnit, float height, int heightUnit, int verticalAlign) {
            this.width = width; this.widthUnit = widthUnit; this.height = height; this.heightUnit = heightUnit;
            this.verticalAlign = verticalAlign;
        }

        @Override
        public int getSize(Paint paint, CharSequence text, int start, int end, Paint.FontMetricsInt fm) {
            laidOut = true;
            final float fontSize = paint.getTextSize();
            fontMetrics = paint.getFontMetricsInt();
            widthPx = (int) Math.ceil(widthUnit == 1 ? width * fontSize : width);
            heightPx = (int) Math.ceil(heightUnit == 1 ? height * fontSize : height);
            if (fm != null) {
                fm.ascent = fontMetrics.ascent;
                fm.descent = fontMetrics.descent;
                fm.leading = fontMetrics.leading;
                switch (verticalAlign) {
                    case ALIGN_TOP: case ALIGN_TEXT_TOP:
                        if (fm.ascent + heightPx > fm.descent) fm.descent = fm.ascent + heightPx;
                        break;
                    case ALIGN_BOTTOM: case ALIGN_TEXT_BOTTOM:
                        if (fm.ascent > fm.descent - heightPx) fm.ascent = fm.descent - heightPx;
                        break;
                    case ALIGN_CENTER: case ALIGN_TEXT_CENTER:
                        if (fm.descent - fm.ascent < heightPx) {
                            fm.ascent -= (heightPx - (fm.descent - fm.ascent)) / 2;
                            fm.descent = fm.ascent + heightPx;
                        }
                        break;
                    default:
                        if (fm.ascent > -heightPx) fm.ascent = -heightPx;
                }
                fm.top = Math.min(fontMetrics.top, fm.ascent);
                fm.bottom = Math.max(fontMetrics.bottom, fm.descent);
            }
            return widthPx;
        }

        @Override
        public void draw(Canvas canvas, CharSequence text, int start, int end, float x, int top, int y, int bottom, Paint paint) {}
    }

    // --- queries -----------------------------------------------------------------------------

    float horizontal(int handle, int offset, boolean primary) {
        Laid l = layouts.get(handle);
        if (l == null) return 0;
        offset = clampOffset(l, offset);
        return primary ? l.layout.getPrimaryHorizontal(offset) : l.layout.getSecondaryHorizontal(offset);
    }

    private static int clampOffset(Laid l, int offset) { return Math.max(0, Math.min(offset, l.text.length())); }
    private static int lineFor(Laid l, int offset) { return Math.min(l.layout.getLineForOffset(offset), l.lineCount - 1); }

    /** [x, top, bottom] of the caret before UTF-16 offset. */
    float[] cursorRect(int handle, int offset) {
        Laid l = layouts.get(handle);
        if (l == null) return new float[] { 0, 0, 0 };
        offset = clampOffset(l, offset);
        final int line = lineFor(l, offset);
        return new float[] { l.layout.getPrimaryHorizontal(offset), l.lineTop[line], l.lineBottom[line] };
    }

    int offsetForPosition(int handle, float x, float y) {
        Laid l = layouts.get(handle);
        if (l == null) return 0;
        final int line = Math.min(l.layout.getLineForVertical((int) (y - l.topPadding)), l.lineCount - 1);
        return l.layout.getOffsetForHorizontal(line, x);
    }

    /** [left, top, right, bottom] of the character at offset. */
    float[] boundingBox(int handle, int offset) {
        Laid l = layouts.get(handle);
        if (l == null) return new float[] { 0, 0, 0, 0 };
        offset = clampOffset(l, offset);
        final int line = lineFor(l, offset);
        final float a = l.layout.getPrimaryHorizontal(offset);
        float b;
        if (offset + 1 <= l.text.length() && lineFor(l, offset + 1) == line && offset < l.layout.getLineEnd(line))
            b = l.layout.isRtlCharAt(offset) ? l.layout.getSecondaryHorizontal(offset + 1) : l.layout.getPrimaryHorizontal(offset + 1);
        else b = a;
        if (l.layout.isRtlCharAt(offset) && b == a && offset < l.text.length())
            b = a - Layout.getDesiredWidth(l.text, offset, offset + 1, l.layout.getPaint());
        return new float[] { Math.min(a, b), l.lineTop[line], Math.max(a, b), l.lineBottom[line] };
    }

    /** Rectangles [l, t, r, b]* of the selection path for [start, end). */
    float[] selectionRects(int handle, int start, int end) {
        Laid l = layouts.get(handle);
        if (l == null) return new float[0];
        Path path = new Path();
        l.layout.getSelectionPath(clampOffset(l, start), clampOffset(l, end), path);
        // The path is made of rectangles; approximate() lists each contour's points, a new
        // contour starting where two consecutive points share a fraction.
        float[] pts = path.approximate(0.5f);
        ArrayList<float[]> rects = new ArrayList<>();
        float[] cur = null;
        for (int i = 0; i + 2 < pts.length; i += 3) {
            if (i > 0 && pts[i] == pts[i - 3] && cur != null) { rects.add(cur); cur = null; }
            final float x = pts[i + 1], y = pts[i + 2];
            if (cur == null) cur = new float[] { x, y, x, y };
            else { cur[0] = Math.min(cur[0], x); cur[1] = Math.min(cur[1], y); cur[2] = Math.max(cur[2], x); cur[3] = Math.max(cur[3], y); }
        }
        if (cur != null) rects.add(cur);
        ArrayList<float[]> kept = new ArrayList<>();
        for (float[] r : rects) if (r[2] > r[0] && r[3] > r[1]) kept.add(r);
        float[] out = new float[kept.size() * 4];
        for (int i = 0; i < kept.size(); i++) {
            float[] r = kept.get(i);
            out[i * 4] = r[0]; out[i * 4 + 1] = r[1] + l.topPadding; out[i * 4 + 2] = r[2]; out[i * 4 + 3] = r[3] + l.topPadding;
        }
        return out;
    }

    /** BreakIterator.getCharacterInstance preceding / following a UTF-16 offset; -1 at the ends. */
    int characterBreak(String text, int offset, boolean following) {
        BreakIterator it = BreakIterator.getCharacterInstance();
        it.setText(text);
        offset = Math.max(0, Math.min(offset, text.length()));
        int r = following ? it.following(offset) : it.preceding(offset);
        return r == BreakIterator.DONE ? -1 : r;
    }

    /** String.toUpperCase for a locale tag (the default locale when empty): AllCaps. */
    String upperCase(String text, String localeTag) {
        Locale locale = localeTag == null || localeTag.isEmpty() ? Locale.getDefault() : Locale.forLanguageTag(localeTag);
        return text.toUpperCase(locale);
    }

    /** BreakIterator.getWordInstance (the default locale) preceding / following; -1 at the ends. */
    int wordBreak(String text, int offset, boolean following) {
        BreakIterator it = BreakIterator.getWordInstance(Locale.getDefault());
        it.setText(text);
        offset = Math.max(0, Math.min(offset, text.length()));
        int r = following ? it.following(offset) : it.preceding(offset);
        return r == BreakIterator.DONE ? -1 : r;
    }

    /** (start << 32) | end of the word around offset. */
    long wordBoundary(int handle, int offset) {
        Laid l = layouts.get(handle);
        if (l == null) return 0;
        final String s = l.text.toString();
        offset = clampOffset(l, offset);
        BreakIterator it = BreakIterator.getWordInstance(l.locale != null ? l.locale : Locale.getDefault());
        it.setText(s);
        // On a word: its bounds; just after one (at its end): that word.
        int at = offset;
        if ((at == s.length() || !Character.isLetterOrDigit(s.charAt(at))) && at > 0 && Character.isLetterOrDigit(s.charAt(at - 1))) at--;
        int end = at >= s.length() ? s.length() : it.following(at);
        if (end == BreakIterator.DONE) end = s.length();
        int start = it.preceding(Math.min(end, s.length()));
        if (start == BreakIterator.DONE || start > at) start = at;
        if (end < offset) end = offset;
        return ((long) start << 32) | (end & 0xffffffffL);
    }

    boolean isRtlCharAt(int handle, int offset) {
        Laid l = layouts.get(handle);
        return l != null && offset < l.text.length() && l.layout.isRtlCharAt(clampOffset(l, offset));
    }

    void release(int handle) { layouts.remove(handle); }

    // --- atlas -------------------------------------------------------------------------------

    private Page allocate(int w, int h) {
        Page page = pages.isEmpty() ? null : pages.get(pages.size() - 1);
        if (page != null && page.shelfX + w + 1 > PAGE_SIZE) {
            page.shelfY += page.shelfH + 1;
            page.shelfX = 0;
            page.shelfH = 0;
        }
        if (page == null || page.shelfY + h + 1 > PAGE_SIZE) {
            page = new Page();
            pages.add(page);
        }
        return page;
    }

    /** A glyph drawn in white that came out with any non-grey pixel is a colour glyph. */
    private static boolean isColor(Bitmap bitmap, int x, int y, int w, int h) {
        int[] px = new int[w * h];
        bitmap.getPixels(px, 0, w, x, y, w, h);
        for (int c : px) {
            if (Color.alpha(c) == 0) continue;
            if (Color.red(c) != Color.green(c) || Color.green(c) != Color.blue(c)) return true;
        }
        return false;
    }

    int pageCount() { return pages.size(); }
    Bitmap pageBitmap(int index) { return pages.get(index).bitmap; }
    boolean pageIsNew(int index) {
        Page p = pages.get(index);
        boolean fresh = !p.reported;
        p.reported = true;
        return fresh;
    }
    /** Returns [y0, y1) of the rows changed since the last call, and clears it. */
    long takeDirtyRows(int index) {
        Page p = pages.get(index);
        if (p.dirtyY1 <= p.dirtyY0) return 0;
        long rows = ((long) p.dirtyY0 << 32) | p.dirtyY1;
        p.dirtyY0 = PAGE_SIZE;
        p.dirtyY1 = 0;
        return rows;
    }

    // --- line height spans (ui-text android/style) -------------------------------------------

    /** LineHeightStyleSpan: places the extra line height by topRatio, trims the ends, by mode. */
    static final class LineHeightStyleSpan implements LineHeightSpan {
        final float lineHeight;
        final int startIndex, endIndex;
        final boolean trimFirstLineTop, trimLastLineBottom;
        final float topRatio;
        final int mode;   // 0 Fixed, 1 Minimum, 2 Tight, 3 PerLine
        private int firstAscent = Integer.MIN_VALUE, ascent = Integer.MIN_VALUE, descent = Integer.MIN_VALUE, lastDescent = Integer.MIN_VALUE;
        int firstAscentDiff, lastDescentDiff;

        LineHeightStyleSpan(float lineHeight, int startIndex, int endIndex, boolean trimFirstLineTop, boolean trimLastLineBottom,
                            float topRatio, int mode) {
            this.lineHeight = lineHeight; this.startIndex = startIndex; this.endIndex = endIndex;
            this.trimFirstLineTop = trimFirstLineTop; this.trimLastLineBottom = trimLastLineBottom;
            this.topRatio = topRatio; this.mode = mode;
        }

        LineHeightStyleSpan copy(int startIndex, int endIndex, boolean trimFirstLineTop) {
            return new LineHeightStyleSpan(lineHeight, startIndex, endIndex, trimFirstLineTop, trimLastLineBottom, topRatio, mode);
        }

        @Override
        public void chooseHeight(CharSequence text, int start, int end, int spanstartv, int lineHeightIn, Paint.FontMetricsInt fm) {
            if (mode == 3) { chooseHeightPerLine(start, end, fm); return; }
            final int currentHeight = fm.descent - fm.ascent;
            if (currentHeight <= 0) return;
            final boolean isFirstLine = start == startIndex, isLastLine = end == endIndex;
            if (isFirstLine && isLastLine && trimFirstLineTop && trimLastLineBottom && mode != 2) return;
            if (firstAscent == Integer.MIN_VALUE) calculateTargetMetrics(fm);
            fm.ascent = isFirstLine ? firstAscent : ascent;
            fm.descent = isLastLine ? lastDescent : descent;
        }

        private void calculateTargetMetrics(Paint.FontMetricsInt fm) {
            final int currentHeight = fm.descent - fm.ascent;
            final int ceiled = (int) Math.ceil(lineHeight);
            final int diff = ceiled - currentHeight;
            if (mode == 1 && diff <= 0) {
                ascent = fm.ascent; descent = fm.descent;
                firstAscent = ascent; lastDescent = descent;
                firstAscentDiff = 0; lastDescentDiff = 0;
                return;
            }
            final float ascentRatio = topRatio == -1f ? Math.abs((float) fm.ascent) / currentHeight : topRatio;
            final int descentDiff = diff <= 0 ? (int) Math.ceil(diff * ascentRatio) : (int) Math.ceil(diff * (1f - ascentRatio));
            descent = fm.descent + descentDiff;
            ascent = descent - ceiled;
            if (mode == 0 || diff >= 0) {
                firstAscent = trimFirstLineTop ? fm.ascent : ascent;
                lastDescent = trimLastLineBottom ? fm.descent : descent;
                firstAscentDiff = fm.ascent - firstAscent;
                lastDescentDiff = lastDescent - fm.descent;
            } else if (mode == 2) {
                firstAscent = trimFirstLineTop ? Math.max(fm.ascent, ascent) : Math.min(fm.ascent, ascent);
                lastDescent = trimLastLineBottom ? Math.min(fm.descent, descent) : Math.max(fm.descent, descent);
                firstAscentDiff = 0;
                lastDescentDiff = 0;
            }
        }

        private void chooseHeightPerLine(int start, int end, Paint.FontMetricsInt fm) {
            final int naturalAscent = fm.ascent, naturalDescent = fm.descent;
            final int currentHeight = naturalDescent - naturalAscent;
            if (currentHeight <= 0) return;
            final boolean isFirstLine = start == startIndex, isLastLine = end == endIndex;
            if (isFirstLine && isLastLine && trimFirstLineTop && trimLastLineBottom) return;
            final int ceiled = (int) Math.ceil(lineHeight);
            final int diff = ceiled - currentHeight;
            if (diff <= 0) {
                if (isFirstLine) firstAscentDiff = 0;
                if (isLastLine) lastDescentDiff = 0;
                return;
            }
            final float ascentRatio = topRatio == -1f ? Math.abs((float) naturalAscent) / currentHeight : topRatio;
            final int descentDiff = (int) Math.ceil(diff * (1f - ascentRatio));
            final int targetDescent = naturalDescent + descentDiff;
            final int targetAscent = targetDescent - ceiled;
            final int finalAscent = isFirstLine && trimFirstLineTop ? naturalAscent : Math.min(naturalAscent, targetAscent);
            final int finalDescent = isLastLine && trimLastLineBottom ? naturalDescent : Math.max(naturalDescent, targetDescent);
            if (isFirstLine) firstAscentDiff = naturalAscent - finalAscent;
            if (isLastLine) lastDescentDiff = finalDescent - naturalDescent;
            fm.ascent = finalAscent;
            fm.descent = finalDescent;
        }
    }

    /** LineHeightSpan (includeFontPadding without a LineHeightStyle): scales ascent and descent. */
    static final class LegacyLineHeightSpan implements LineHeightSpan {
        final float lineHeight;
        LegacyLineHeightSpan(float lineHeight) { this.lineHeight = lineHeight; }
        @Override
        public void chooseHeight(CharSequence text, int start, int end, int spanstartv, int lineHeightIn, Paint.FontMetricsInt fm) {
            final int currentHeight = fm.descent - fm.ascent;
            if (currentHeight <= 0) return;
            final int ceiled = (int) Math.ceil(lineHeight);
            final float ratio = ceiled * 1f / currentHeight;
            fm.descent = (int) Math.ceil(fm.descent * ratio);
            fm.ascent = fm.descent - ceiled;
        }
    }
}
