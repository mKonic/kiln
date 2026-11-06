package dev.kiln;

import android.os.Bundle;
import android.os.Handler;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.inputmethod.CompletionInfo;
import android.view.inputmethod.CorrectionInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputContentInfo;

/**
 * The IME's connection to the focused kiln text field. Every edit runs synchronously on the main
 * thread against the native field state, so a read right after a write sees it (spec 04 req. 5).
 * A connection becomes inert once closed or replaced (NullableInputConnectionWrapper).
 */
final class KilnInputConnection implements InputConnection {
    private final KilnView view;
    private boolean closed;
    private int batchDepth;

    KilnInputConnection(KilnView view) { this.view = view; }

    private boolean live() { return !closed && view.isCurrentConnection(this); }

    // --- edits -----------------------------------------------------------------------------
    @Override public boolean beginBatchEdit() { if (!live()) return false; batchDepth++; view.beginBatch(); return true; }
    @Override public boolean endBatchEdit() {
        if (!live() || batchDepth == 0) return false;
        batchDepth--;
        view.endBatch();
        return batchDepth > 0;
    }
    @Override public boolean commitText(CharSequence text, int newCursorPosition) {
        if (!live()) return false;
        KilnNative.imeCommitText(text.toString(), newCursorPosition);
        return true;
    }
    @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (!live()) return false;
        KilnNative.imeSetComposingText(text.toString(), newCursorPosition);
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
    @Override public boolean setSelection(int start, int end) { if (!live()) return false; KilnNative.imeSetSelection(start, end); return true; }
    @Override public boolean performEditorAction(int editorAction) { if (!live()) return false; KilnNative.imePerformEditorAction(editorAction); return true; }
    @Override public boolean sendKeyEvent(KeyEvent event) {
        if (!live()) return false;
        // IME key events go back through the view's key dispatch (BaseInputConnection.sendKeyEvent).
        view.dispatchKeyEvent(event);
        return true;
    }
    @Override public boolean performContextMenuAction(int id) {
        if (!live()) return false;
        if (id == android.R.id.selectAll) {
            String t = KilnNative.imeGetText();
            KilnNative.imeSetSelection(0, t.length());
            return true;
        }
        return false;   // cut/copy/paste come with the clipboard and text toolbar
    }

    // --- reads -----------------------------------------------------------------------------
    @Override public CharSequence getTextBeforeCursor(int n, int flags) {
        if (!live()) return null;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        int start = Math.min(s[0], s[1]);
        return t.substring(Math.max(0, start - n), start);
    }
    @Override public CharSequence getTextAfterCursor(int n, int flags) {
        if (!live()) return null;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        int end = Math.max(s[0], s[1]);
        return t.substring(end, Math.min(t.length(), end + n));
    }
    @Override public CharSequence getSelectedText(int flags) {
        if (!live()) return null;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        if (s[0] == s[1]) return null;
        return t.substring(Math.min(s[0], s[1]), Math.max(s[0], s[1]));
    }
    @Override public int getCursorCapsMode(int reqModes) {
        if (!live()) return 0;
        String t = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        return TextUtils.getCapsMode(t, Math.min(s[0], s[1]), reqModes);
    }
    @Override public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
        if (!live()) return null;
        // Samsung's keyboard relies on this (Flutter lessons, spec 16 req. 2).
        ExtractedText e = new ExtractedText();
        e.text = KilnNative.imeGetText();
        int[] s = KilnNative.imeGetSelection();
        e.startOffset = 0;
        e.partialStartOffset = -1;
        e.partialEndOffset = -1;
        e.selectionStart = s[0];
        e.selectionEnd = s[1];
        e.flags = view.isSingleLineInput() ? ExtractedText.FLAG_SINGLE_LINE : 0;
        return e;
    }

    // --- the rest ----------------------------------------------------------------------------
    @Override public boolean commitCompletion(CompletionInfo text) { return false; }
    @Override public boolean commitCorrection(CorrectionInfo correctionInfo) { return live(); }
    @Override public boolean clearMetaKeyStates(int states) { return false; }
    @Override public boolean reportFullscreenMode(boolean enabled) { return false; }
    @Override public boolean performPrivateCommand(String action, Bundle data) { return false; }
    @Override public boolean requestCursorUpdates(int cursorUpdateMode) {
        if (!live()) return false;
        // CURSOR_UPDATE_IMMEDIATE = 1, CURSOR_UPDATE_MONITOR = 2 (RecordingInputConnection).
        view.setCursorMonitoring((cursorUpdateMode & 2) != 0);
        if ((cursorUpdateMode & 1) != 0) view.sendCursorAnchorInfo();
        return true;
    }
    @Override public Handler getHandler() { return null; }
    @Override public void closeConnection() {
        if (closed) return;
        while (batchDepth > 0) endBatchEdit();
        closed = true;
    }
    @Override public boolean commitContent(InputContentInfo inputContentInfo, int flags, Bundle opts) { return false; }
}
