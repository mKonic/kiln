package dev.kiln;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.fonts.Font;
import android.graphics.text.PositionedGlyphs;
import android.graphics.text.TextRunShaper;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.TypedValue;

import java.text.Bidi;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Objects;

/**
 * Platform text path (architecture 3.5), M0 prototype: StaticLayout for line breaking and
 * metrics, TextRunShaper for glyph ids/positions/fonts per bidi run, Canvas.drawGlyphs to
 * rasterise each glyph once into RGBA atlas pages that native uploads to the GPU.
 *
 * Called from native on the main thread. Everything here is cached per (font, glyph, size).
 */
final class KilnText {
    static final int PAGE_SIZE = 1024;
    private static final int GLYPH_FLOATS = 10;

    private final Context context;
    private final TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glyphPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Page> pages = new ArrayList<>();
    private final HashMap<GlyphKey, float[]> slots = new HashMap<>();
    private final RectF bounds = new RectF();
    private final int[] oneGlyph = new int[1];
    private final HashMap<Integer, Layout> layouts = new HashMap<>();
    private int nextHandle = 1;
    private final float[] onePosition = new float[2];

    static final class Page {
        final Bitmap bitmap = Bitmap.createBitmap(PAGE_SIZE, PAGE_SIZE, Bitmap.Config.ARGB_8888);
        final Canvas canvas = new Canvas(bitmap);
        int shelfX, shelfY, shelfH;
        int dirtyY0 = PAGE_SIZE, dirtyY1 = 0;
        boolean reported;
    }

    private static final class GlyphKey {
        final Font font; final int glyph; final int sizeQ;
        GlyphKey(Font font, int glyph, int sizeQ) { this.font = font; this.glyph = glyph; this.sizeQ = sizeQ; }
        @Override public boolean equals(Object o) {
            if (!(o instanceof GlyphKey)) return false;
            GlyphKey k = (GlyphKey) o;
            return glyph == k.glyph && sizeQ == k.sizeQ && font.equals(k.font);
        }
        @Override public int hashCode() { return Objects.hash(font, glyph, sizeQ); }
    }

    KilnText(Context context) {
        this.context = context;
        glyphPaint.setColor(Color.WHITE);
    }

    /**
     * Lays out text at sizeSp wrapped to widthPx. Returns
     * [width, height, lineCount, then per glyph: page, x, y, w, h, u, v, uw, vh, color].
     */
    float[] layout(String text, float sizeSp, int widthPx) {
        final float px = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, sizeSp,
                context.getResources().getDisplayMetrics());   // nonlinear font scale on 34+
        // A StaticLayout keeps the paint it was built with and measures through it later, so each
        // layout gets its own: reusing one TextPaint broke cursor queries after another layout.
        final TextPaint paint = new TextPaint(this.paint);
        paint.setTextSize(px);
        glyphPaint.setTextSize(px);
        final int width = widthPx > 0 ? widthPx : Integer.MAX_VALUE / 4;
        StaticLayout layout = StaticLayout.Builder.obtain(text, 0, text.length(), paint, width)
                .setIncludePad(false)                       // Compose default (07 §D)
                .setUseLineSpacingFromFallbacks(true)
                .setBreakStrategy(Layout.BREAK_STRATEGY_HIGH_QUALITY)
                .setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE)
                .build();

        ArrayList<float[]> glyphs = new ArrayList<>();
        float maxWidth = 0;
        for (int line = 0; line < layout.getLineCount(); line++) {
            final int start = layout.getLineStart(line);
            final int end = layout.getLineVisibleEnd(line);
            maxWidth = Math.max(maxWidth, layout.getLineWidth(line));
            if (end <= start) continue;
            final float baseline = layout.getLineBaseline(line);
            final boolean paraRtl = layout.getParagraphDirection(line) == Layout.DIR_RIGHT_TO_LEFT;
            Bidi bidi = new Bidi(text.substring(start, end),
                    paraRtl ? Bidi.DIRECTION_RIGHT_TO_LEFT : Bidi.DIRECTION_LEFT_TO_RIGHT);
            final int runs = bidi.getRunCount();
            Integer[] order = new Integer[runs];
            byte[] levels = new byte[runs];
            for (int r = 0; r < runs; r++) { order[r] = r; levels[r] = (byte) bidi.getRunLevel(r); }
            Bidi.reorderVisually(levels, 0, order, 0, runs);
            float x = layout.getLineLeft(line);
            for (int v = 0; v < runs; v++) {
                final int r = order[v];
                final int rs = start + bidi.getRunStart(r), re = start + bidi.getRunLimit(r);
                final boolean rtl = (bidi.getRunLevel(r) & 1) != 0;
                PositionedGlyphs pg = TextRunShaper.shapeTextRun(text, rs, re - rs, start, end - start,
                        x, baseline, rtl, paint);
                for (int i = 0; i < pg.glyphCount(); i++)
                    addGlyph(glyphs, pg.getFont(i), pg.getGlyphId(i), pg.getGlyphX(i), pg.getGlyphY(i), px);
                x += pg.getAdvance();
            }
        }

        // The Layout stays alive for cursor/offset queries until native releases the handle.
        final int handle = nextHandle++;
        if (nextHandle >= (1 << 23)) nextHandle = 1;   // stays exact as a float
        layouts.put(handle, layout);
        float[] out = new float[6 + glyphs.size() * GLYPH_FLOATS];
        out[0] = widthPx > 0 ? Math.min(maxWidth, widthPx) : maxWidth;
        out[1] = layout.getHeight();
        out[2] = layout.getLineCount();
        out[3] = handle;
        out[4] = layout.getLineBaseline(0);
        out[5] = layout.getLineBaseline(Math.max(0, layout.getLineCount() - 1));
        for (int i = 0; i < glyphs.size(); i++)
            System.arraycopy(glyphs.get(i), 0, out, 6 + i * GLYPH_FLOATS, GLYPH_FLOATS);
        return out;
    }

    /** [x, top, bottom] of the caret before UTF-16 offset (getPrimaryHorizontal, line bounds). */
    float[] cursorRect(int handle, int offset) {
        Layout l = layouts.get(handle);
        if (l == null) return new float[] { 0, 0, 0 };
        offset = Math.max(0, Math.min(offset, l.getText().length()));
        final int line = l.getLineForOffset(offset);
        return new float[] { l.getPrimaryHorizontal(offset), l.getLineTop(line), l.getLineBottom(line) };
    }

    int offsetForPosition(int handle, float x, float y) {
        Layout l = layouts.get(handle);
        if (l == null) return 0;
        final int line = l.getLineForVertical((int) y);
        return l.getOffsetForHorizontal(line, x);
    }

    void release(int handle) { layouts.remove(handle); }

    private void addGlyph(ArrayList<float[]> out, Font font, int glyph, float gx, float gy, float px) {
        float[] slot = rasterize(font, glyph, px);
        if (slot == null) return;
        // slot: page, left, top, w, h, u, v, uw, vh, color (left/top relative to the pen)
        out.add(new float[] { slot[0], gx + slot[1], gy + slot[2], slot[3], slot[4],
                              slot[5], slot[6], slot[7], slot[8], slot[9] });
    }

    private float[] rasterize(Font font, int glyph, float px) {
        GlyphKey key = new GlyphKey(font, glyph, Math.round(px * 4));
        if (slots.containsKey(key)) return slots.get(key);
        glyphPaint.setTextSize(px);
        font.getGlyphBounds(glyph, glyphPaint, bounds);
        final int left = (int) Math.floor(bounds.left) - 1, top = (int) Math.floor(bounds.top) - 1;
        final int w = (int) Math.ceil(bounds.right) + 1 - left, h = (int) Math.ceil(bounds.bottom) + 1 - top;
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

    // --- atlas sync, called by native after layout ---------------------------------------

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
}
