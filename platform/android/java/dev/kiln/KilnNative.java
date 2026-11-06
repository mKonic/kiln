package dev.kiln;

import android.view.Surface;

/** JNI entry points into libkiln (statically linked into the app's library). */
final class KilnNative {
    private KilnNative() {}

    static native void create(KilnView view, KilnText text, float density, float fontScale, byte[] savedState);
    static native byte[] saveState();
    static native void setVisible(boolean visible);
    static native void destroy();
    static native void setDensity(float density, float fontScale);
    static native void setAnimationScale(float scale);
    static native void touchEvent(int action, int actionIndex, long eventTime, int[] ids, float[] xy,
                                  int[] toolTypes, int buttons, int classification, float hscroll, float vscroll);
    static native boolean keyEvent(int action, int keyCode, int metaState, int unicode, int repeatCount, long eventTime);
    static native void hostFocusChanged(boolean focused);
    static native void setInsets(int type, int left, int top, int right, int bottom);
    static native void setImeState(boolean visible, boolean animating);

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
    static native String[] semStrings(int id);
    static native boolean semAction(int id, int action, String argument);
    static native int semHitTest(float x, float y);
    static native void setViewConfiguration(long longPressTimeout, long doubleTapTimeout, float touchSlop,
                                            float maxFling, float minFling);
    static native void surfaceCreated(Surface surface);
    static native void surfaceChanged(int width, int height);
    static native void surfaceDestroyed();
}
