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
    @Override public boolean commitCompletion(CompletionInfo text) { return false; }
    @Override public boolean commitCorrection(CorrectionInfo correctionInfo) { return true; }
    @Override public boolean clearMetaKeyStates(int states) { return false; }
    @Override public boolean reportFullscreenMode(boolean enabled) { return false; }
    // InputConnectionCompat's wrapper hands its own commands on; nothing else is handled.
    @Override public boolean performPrivateCommand(String action, Bundle data) { return true; }
    @Override public Handler getHandler() { return null; }
    // receiveContent is not ported: no content is accepted (the terminal connection's answer).
    @Override public boolean commitContent(InputContentInfo inputContentInfo, int flags, Bundle opts) { return false; }

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
