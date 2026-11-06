package dev.kiln;

import android.view.Surface;

/** JNI entry points into libkiln (statically linked into the app's library). */
final class KilnNative {
    private KilnNative() {}

    static native void create(KilnView view, KilnText text, float density, float fontScale);
    static native void setVisible(boolean visible);
    static native void setDensity(float density, float fontScale);
    static native void setAnimationScale(float scale);
    static native void surfaceCreated(Surface surface);
    static native void surfaceChanged(int width, int height);
    static native void surfaceDestroyed();
}
