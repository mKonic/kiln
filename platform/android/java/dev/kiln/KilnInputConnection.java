package dev.kiln;

import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.text.Spanned;
import android.text.TextUtils;
import android.text.style.BackgroundColorSpan;
import android.text.style.ForegroundColorSpan;
import android.text.style.StrikethroughSpan;
import android.text.style.StyleSpan;
import android.text.style.TypefaceSpan;
import android.text.style.UnderlineSpan;
import android.view.KeyEvent;
import android.view.inputmethod.CompletionInfo;
import android.view.inputmethod.CorrectionInfo;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputContentInfo;
import android.view.inputmethod.TextAttribute;

import java.util.ArrayList;

/**
 * The IME's connection to the focused kiln text field (Compose's StatelessInputConnection). Edits
 * run synchronously on the main thread against the native field; edits inside a batch apply
 * together when the outermost batch ends. A closed or replaced connection is inert
 * (NullableInputConnectionWrapper).
 */
final class KilnInputConnection implements InputConnection {
    private final KilnView view;
    private boolean closed;
    private int batchDepth;

    KilnInputConnection(KilnView view) { this.view = view; }

    private boolean live() { return !closed && view.isCurrentConnection(this); }

    // --- edits -----------------------------------------------------------------------------
    @Override public boolean beginBatchEdit() {
        if (!live()) return false;
        batchDepth++;
        return KilnNative.imeBeginBatchEdit();
    }
    @Override public boolean endBatchEdit() {
        if (!live() || batchDepth == 0) return false;
        batchDepth--;
        return KilnNative.imeEndBatchEdit();
    }
    @Override public void closeConnection() {
        if (closed) return;
        // The queued edits go with the connection.
        while (batchDepth > 0) { batchDepth--; KilnNative.imeEndBatchEdit(); }
        closed = true;
    }

    @Override public boolean commitText(CharSequence text, int newCursorPosition) {
        if (!live()) return false;
        if (text == null) return true;
        KilnNative.imeCommitText(text.toString(), newCursorPosition, false);
        return true;
    }
    @Override public boolean commitText(CharSequence text, int newCursorPosition, TextAttribute textAttribute) {
        if (!live()) return false;
        KilnNative.imeCommitText(text.toString(), newCursorPosition, isTextSuggestionSelected(textAttribute));
        return true;
    }
    @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (!live()) return false;
        if (text == null) return true;
        ArrayList<String> families = new ArrayList<>();
        int[] spans = text instanceof Spanned ? annotations((Spanned) text, families) : null;
        KilnNative.imeSetComposingText(text.toString(), newCursorPosition, spans, families.toArray(new String[0]), false);
        return true;
    }
    @Override public boolean setComposingText(CharSequence text, int newCursorPosition, TextAttribute textAttribute) {
        if (!live()) return false;
        KilnNative.imeSetComposingText(text.toString(), newCursorPosition, null, new String[0], isTextSuggestionSelected(textAttribute));
        return true;
    }
    @Override public boolean setComposingRegion(int start, int end) { if (!live()) return false; KilnNative.imeSetComposingRegion(start, end); return true; }
    @Override public boolean finishComposingText() { if (!live()) return false; KilnNative.imeFinishComposingText(); return true; }
    @Override public boolean deleteSurroundingText(int before, int after) {
        if (!live()) return false;
        KilnNative.imeDeleteSurroundingText(before, after, false);
        return true;
    }
    @Override public boolean deleteSurroundingTextInCodePoints(int before, int after) {
        if (!live()) return false;
        KilnNative.imeDeleteSurroundingText(before, after, true);
        return true;
    }
    @Override public boolean setSelection(int start, int end) {
        if (!live()) return false;
        KilnNative.imeSetSelection(start, end);   // also ends the direct touch interaction
        return true;
    }
    @Override public boolean sendKeyEvent(KeyEvent event) {
        if (!live()) return false;
        view.sendKeyEventFromInputMethod(event);
        return true;
    }
    @Override public boolean performEditorAction(int editorAction) {
        if (!live()) return false;
        KilnNative.imePerformEditorAction(editorAction);
        return true;
    }
    @Override public boolean performContextMenuAction(int id) {
        if (!live()) return false;
        if (id == android.R.id.selectAll) {
            KilnNative.imeSetSelection(0, KilnNative.imeGetText().length());
        } else if (id == android.R.id.cut) {
            sendSynthesizedKeyEvent(KeyEvent.KEYCODE_CUT);
        } else if (id == android.R.id.copy) {
            sendSynthesizedKeyEvent(KeyEvent.KEYCODE_COPY);
        } else if (id == android.R.id.paste) {
            sendSynthesizedKeyEvent(KeyEvent.KEYCODE_PASTE);
        }
        // startSelectingText, stopSelectingText, copyUrl and switchInputMethod are not supported.
        return false;
    }
    private void sendSynthesizedKeyEvent(int code) {
        sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, code));
        sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, code));
    }

    // --- reads -----------------------------------------------------------------------------
    @Override public CharSequence getTextBeforeCursor(int n, int flags) {
        if (!live()) return null;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        return t.substring(Math.max(0, s[0] - Math.max(0, n)), s[0]);
    }
    @Override public CharSequence getTextAfterCursor(int n, int flags) {
        if (!live()) return null;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        return t.substring(s[1], (int) Math.min(t.length(), (long) s[1] + Math.max(0, n)));
    }
    @Override public CharSequence getSelectedText(int flags) {
        if (!live()) return null;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        if (s[0] == s[1]) return null;
        return t.substring(s[0], s[1]);
    }
    @Override public int getCursorCapsMode(int reqModes) {
        if (!live()) return 0;
        return TextUtils.getCapsMode(KilnNative.imeGetText(), KilnNative.imeGetSelection()[0], reqModes);
    }
    @Override public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
        if (!live()) return null;
        if ((flags & GET_EXTRACTED_TEXT_MONITOR) != 0) view.requestExtractedTextUpdates(request != null ? request.token : 0);
        return extractedText(KilnNative.imeGetText(), KilnNative.imeGetSelection());
    }
    /** TextFieldCharSequence.toExtractedText: the whole text, single line when it has no '\n'. */
    static ExtractedText extractedText(String text, int[] sel) {
        ExtractedText e = new ExtractedText();
        e.text = text;
        e.startOffset = 0;
        e.partialEndOffset = text.length();
        e.partialStartOffset = -1;
        e.selectionStart = sel[0];
        e.selectionEnd = sel[1];
        e.flags = text.indexOf('\n') >= 0 ? 0 : ExtractedText.FLAG_SINGLE_LINE;
        return e;
    }

    // --- the rest ----------------------------------------------------------------------------
    @Override public boolean requestCursorUpdates(int cursorUpdateMode) {
        if (!live()) return false;
        KilnNative.imeRequestCursorUpdates(resolveCursorUpdateFilters(cursorUpdateMode));
        return true;
    }
    /** CursorAnchorInfoController.requestUpdates: the filters an API level has, all when none. */
    static int resolveCursorUpdateFilters(int mode) {
        final int marker = CURSOR_UPDATE_FILTER_INSERTION_MARKER, chars = CURSOR_UPDATE_FILTER_CHARACTER_BOUNDS,
                editor = CURSOR_UPDATE_FILTER_EDITOR_BOUNDS, lines = CURSOR_UPDATE_FILTER_VISIBLE_LINE_BOUNDS;
        final int modes = mode & (CURSOR_UPDATE_IMMEDIATE | CURSOR_UPDATE_MONITOR);
        if (Build.VERSION.SDK_INT < 33) return modes | marker | chars;
        int filters = mode & (marker | chars | editor | (Build.VERSION.SDK_INT >= 34 ? lines : 0));
        if (filters == 0) filters = marker | chars | editor | (Build.VERSION.SDK_INT >= 34 ? lines : 0);
        return modes | filters;
    }
    // --- handwriting gestures (API 34) -------------------------------------------------------
    @Override public void performHandwritingGesture(android.view.inputmethod.HandwritingGesture gesture,
                                                    java.util.concurrent.Executor executor, java.util.function.IntConsumer consumer) {
        int result = HANDWRITING_GESTURE_RESULT_UNSUPPORTED;
        if (live() && Build.VERSION.SDK_INT >= 34) {
            float[] g = gestureData(gesture);
            if (g != null) {
                String insert = gesture instanceof android.view.inputmethod.InsertGesture
                        ? ((android.view.inputmethod.InsertGesture) gesture).getTextToInsert() : "";
                result = KilnNative.imePerformHandwritingGesture(g, insert, gesture.getFallbackText());
            }
        }
        if (consumer == null) return;
        final int r = result;
        if (executor != null) executor.execute(() -> consumer.accept(r));
        else consumer.accept(r);
    }
    @Override public boolean previewHandwritingGesture(android.view.inputmethod.PreviewableHandwritingGesture gesture,
                                                       android.os.CancellationSignal cancellationSignal) {
        if (!live() || Build.VERSION.SDK_INT < 34) return false;
        float[] g = gestureData((android.view.inputmethod.HandwritingGesture) gesture);
        if (g == null || !KilnNative.imePreviewHandwritingGesture(g)) return false;
        if (cancellationSignal != null) cancellationSignal.setOnCancelListener(KilnNative::imeCancelHandwritingPreview);
        return true;
    }
    /**
     * A gesture as (type, granularity, area l t r b, end area l t r b, point x y, end point x y)
     * in view pixels; types: 0 select, 1 delete, 2 select range, 3 delete range, 4 join or split,
     * 5 insert, 6 remove space; granularity 1 word, else character.
     */
    private float[] gestureData(android.view.inputmethod.HandwritingGesture g) {
        if (Build.VERSION.SDK_INT < 34) return null;
        int[] loc = new int[2];
        view.getLocationOnScreen(loc);
        float[] d = new float[14];
        android.graphics.RectF a = null, b = null;
        android.graphics.PointF p = null, q = null;
        int granularity = 0;
        if (g instanceof android.view.inputmethod.SelectGesture) {
            d[0] = 0; a = ((android.view.inputmethod.SelectGesture) g).getSelectionArea();
            granularity = ((android.view.inputmethod.SelectGesture) g).getGranularity();
        } else if (g instanceof android.view.inputmethod.DeleteGesture) {
            d[0] = 1; a = ((android.view.inputmethod.DeleteGesture) g).getDeletionArea();
            granularity = ((android.view.inputmethod.DeleteGesture) g).getGranularity();
        } else if (g instanceof android.view.inputmethod.SelectRangeGesture) {
            android.view.inputmethod.SelectRangeGesture r = (android.view.inputmethod.SelectRangeGesture) g;
            d[0] = 2; a = r.getSelectionStartArea(); b = r.getSelectionEndArea(); granularity = r.getGranularity();
        } else if (g instanceof android.view.inputmethod.DeleteRangeGesture) {
            android.view.inputmethod.DeleteRangeGesture r = (android.view.inputmethod.DeleteRangeGesture) g;
            d[0] = 3; a = r.getDeletionStartArea(); b = r.getDeletionEndArea(); granularity = r.getGranularity();
        } else if (g instanceof android.view.inputmethod.JoinOrSplitGesture) {
            d[0] = 4; p = ((android.view.inputmethod.JoinOrSplitGesture) g).getJoinOrSplitPoint();
        } else if (g instanceof android.view.inputmethod.InsertGesture) {
            d[0] = 5; p = ((android.view.inputmethod.InsertGesture) g).getInsertionPoint();
        } else if (g instanceof android.view.inputmethod.RemoveSpaceGesture) {
            android.view.inputmethod.RemoveSpaceGesture r = (android.view.inputmethod.RemoveSpaceGesture) g;
            d[0] = 6; p = r.getStartPoint(); q = r.getEndPoint();
        } else {
            return null;
        }
        d[1] = granularity == android.view.inputmethod.HandwritingGesture.GRANULARITY_WORD ? 1 : 0;
        // Screen to view pixels.
        if (a != null) { d[2] = a.left - loc[0]; d[3] = a.top - loc[1]; d[4] = a.right - loc[0]; d[5] = a.bottom - loc[1]; }
        if (b != null) { d[6] = b.left - loc[0]; d[7] = b.top - loc[1]; d[8] = b.right - loc[0]; d[9] = b.bottom - loc[1]; }
        if (p != null) { d[10] = p.x - loc[0]; d[11] = p.y - loc[1]; }
        if (q != null) { d[12] = q.x - loc[0]; d[13] = q.y - loc[1]; }
        return d;
    }

    @Override public boolean commitCompletion(CompletionInfo text) { return false; }
    @Override public boolean commitCorrection(CorrectionInfo correctionInfo) { return true; }
    @Override public boolean clearMetaKeyStates(int states) { return false; }
    @Override public boolean reportFullscreenMode(boolean enabled) { return false; }
    // InputConnectionCompat's wrapper hands its own commands on; nothing else is handled.
    @Override public boolean performPrivateCommand(String action, Bundle data) { return true; }
    @Override public Handler getHandler() { return null; }
    /**
     * commitContent (InputConnectionCompat's listener): with the read permission granted, the
     * content goes to the receiveContent modifiers around the field as a one-item clip.
     */
    @Override public boolean commitContent(InputContentInfo info, int flags, Bundle opts) {
        if (!live() || !view.acceptsContent()) return false;
        int permission = 0;
        if ((flags & INPUT_CONTENT_GRANT_READ_URI_PERMISSION) != 0) {
            try {
                info.requestPermission();
            } catch (Exception e) {
                return false;   // can't insert content from the IME: requestPermission() failed
            }
            permission = view.holdContentPermission(info);
        }
        android.content.ClipDescription d = info.getDescription();
        String[] types = new String[d.getMimeTypeCount()];
        for (int i = 0; i < types.length; i++) types[i] = d.getMimeType(i);
        CharSequence label = d.getLabel();
        android.net.Uri link = info.getLinkUri();
        return KilnNative.imeCommitContent(info.getContentUri().toString(), types, label != null ? label.toString() : "",
                link != null ? link.toString() : null, permission);
    }

    private static boolean isTextSuggestionSelected(TextAttribute a) {
        return a != null && Build.VERSION.SDK_INT >= 37 && a.isTextSuggestionSelected();
    }

    /**
     * Spanned.toAnnotationList: the IME's composing spans as (kind, start, end, value) quads.
     * Kinds: 1 background, 2 foreground (ARGB value), 3 line-through, 4 bold, 5 italic, 6 bold
     * italic, 7 underline, 8 font family (value: index into `families`; generic names or a
     * system family that is not the default).
     */
    private static int[] annotations(Spanned s, ArrayList<String> families) {
        Object[] spans = s.getSpans(0, s.length(), Object.class);
        int[] out = new int[spans.length * 4];
        int n = 0;
        for (Object span : spans) {
            int kind = 0, value = 0;
            if (span instanceof BackgroundColorSpan) { kind = 1; value = ((BackgroundColorSpan) span).getBackgroundColor(); }
            else if (span instanceof ForegroundColorSpan) { kind = 2; value = ((ForegroundColorSpan) span).getForegroundColor(); }
            else if (span instanceof StrikethroughSpan) kind = 3;
            else if (span instanceof StyleSpan) {
                int style = ((StyleSpan) span).getStyle();
                kind = style == Typeface.BOLD ? 4 : style == Typeface.ITALIC ? 5 : style == Typeface.BOLD_ITALIC ? 6 : 0;
            } else if (span instanceof UnderlineSpan) kind = 7;
            else if (span instanceof TypefaceSpan) {
                String family = ((TypefaceSpan) span).getFamily();
                if (family != null && !family.isEmpty() && isKnownFamily(family)) { kind = 8; value = families.size(); families.add(family); }
            }
            if (kind == 0) continue;
            out[n++] = kind;
            out[n++] = s.getSpanStart(span);
            out[n++] = s.getSpanEnd(span);
            out[n++] = value;
        }
        return n == 0 ? null : java.util.Arrays.copyOf(out, n);
    }
    private static boolean isKnownFamily(String family) {
        switch (family) {
            case "cursive": case "monospace": case "sans-serif": case "serif": return true;
            default:
                // optionalFontFamilyFromName: a family the system resolves to something else
                // than the default.
                Typeface t = Typeface.create(family, Typeface.NORMAL);
                return t != Typeface.DEFAULT && t != Typeface.create(Typeface.DEFAULT, Typeface.NORMAL);
        }
    }
}
