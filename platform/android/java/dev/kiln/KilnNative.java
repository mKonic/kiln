package dev.kiln;

import android.view.Surface;

/** JNI entry points into libkiln (statically linked into the app's library). */
final class KilnNative {
    private KilnNative() {}

    static native void create(KilnView view, KilnText text, android.content.res.AssetManager assets, float density, float fontScale, byte[] savedState);
    static native byte[] saveState();
    static native void setVisible(boolean visible);
    static native void destroy();
    static native void setDensity(float density, float fontScale);
    /** Configuration.fontWeightAdjustment (the bold text setting). */
    static native void setFontWeightAdjustment(int adjustment);
    /** Downloadable fonts arrived: lay text out again. */
    static native void fontsChanged();
    static native void textToolbarItem(int item);
    static native void textToolbarDestroyed();
    static native void setAnimationScale(float scale);
    static native void setLayoutDirection(boolean rtl);
    static native void touchEvent(int action, int actionIndex, long eventTime, int[] ids, float[] xy,
                                  int[] toolTypes, int buttons, int metaState, int classification, float hscroll, float vscroll,
                                  long[] historyTimes, float[] historyXy, float[] pan);
    static native boolean keyEvent(int action, int keyCode, int metaState, int unicode, int repeatCount, long eventTime);
    static native boolean keyEventPreIme(int action, int keyCode, int metaState, int unicode, int repeatCount, long eventTime);
    static native boolean rotary(float verticalPixels, float horizontalPixels, long eventTime, int deviceId);
    static native boolean indirectPointer(int action, int actionIndex, long eventTime, int[] ids, float[] xy,
                                          float[] pressures, long[] historyTimes, float[] historyXy, int axis);
    static native void indirectFling(boolean next);
    static native float[] focusedRect();
    static native boolean hasFocusableContent();
    static native float[] focusSearch(int direction, float left, float top, float right, float bottom);
    static native boolean isBetterFocusCandidate(int direction, float[] proposed, float[] current, float[] focused);
    static native boolean hostRequestFocus(int direction, boolean hasRect, float left, float top, float right,
                                           float bottom, boolean hostHasFocus);
    static native void hostFocusChanged(boolean focused);
    static native void setWindowInsets(int[] data);
    static native void setInsetsAnimation(int type, boolean running, int[] sourceTarget, float fraction, float alpha, long durationMillis);

    // InputConnection -> the active native text input session.
    static native void imeCommitText(String text, int newCursorPosition);
    static native void imeSetComposingText(String text, int newCursorPosition);
    static native void imeSetComposingRegion(int start, int end);
    static native void imeFinishComposingText();
    static native void imeDeleteSurroundingText(int before, int after, boolean codePoints);
    static native void imeSetSelection(int start, int end);
    static native void imePerformEditorAction(int action);
    static native String imeGetText();
    static native int[] imeGetSelection();
    static native float[] imeGetCaret();

    // Back: started/progressed gesture events, cancel, completion.
    static native void backEvent(boolean started, float x, float y, float progress, int edge, long frameTimeMillis);
    static native void backCancelled();
    static native void backInvoked();

    // Semantics for KilnAccessibility.
    static native int[] semNode(int id);
    static native int[] semScrollEvents();
    static native String[] semStrings(int id);
    /** Text spans: [kind (0 URL, 1 clickable, 2 TTS), start, end]* on the node's text. */
    static native int[] semSpans(int id);
    static native String semSpanValue(int id, int index);
    static native void semSpanClick(int id, int index);
    static native boolean semAction(int id, int action, String argument);
    static native int semHitTest(float x, float y);
    static native void setViewConfiguration(long longPressTimeout, long doubleTapTimeout, float touchSlop,
                                            float maxFling, float minFling);
    static native void surfaceCreated(Surface surface);
    static native void surfaceChanged(int width, int height);
    static native void surfaceDestroyed();
}
