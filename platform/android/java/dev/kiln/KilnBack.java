package dev.kiln;

import android.os.Build;
import android.view.KeyEvent;
import android.window.BackEvent;
import android.window.OnBackAnimationCallback;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

/**
 * Back bridge (spec 15 req. 2). A platform callback is registered only while native has an
 * enabled handler, so the system back-to-home animation runs otherwise. API 34+ gets the
 * predictive gesture (OnBackAnimationCallback), API 33 completion only, API 31-32 the back key.
 */
final class KilnBack {
    private final KilnView view;
    private boolean enabled;
    private OnBackInvokedDispatcher registeredWith;
    private Object callback;   // OnBackInvokedCallback; typed loosely so API 31 never loads it
    private boolean keyDown;

    KilnBack(KilnView view) { this.view = view; }

    /** From native, synchronously when handler enablement changes. */
    void setEnabled(boolean value) {
        enabled = value;
        sync();
    }

    void onAttached() { sync(); }
    void onDetached() { unregister(); }

    private void sync() {
        if (Build.VERSION.SDK_INT < 33) return;
        if (enabled && view.isAttachedToWindow()) register(); else unregister();
    }

    private void register() {
        OnBackInvokedDispatcher d = view.findOnBackInvokedDispatcher();
        if (d == null || d == registeredWith) return;
        unregister();
        if (callback == null) callback = Build.VERSION.SDK_INT >= 34 ? Api34.create(view) : Api33.create();
        d.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, (OnBackInvokedCallback) callback);
        registeredWith = d;
    }

    private void unregister() {
        if (registeredWith == null) return;
        registeredWith.unregisterOnBackInvokedCallback((OnBackInvokedCallback) callback);
        registeredWith = null;
    }

    /** API 31-32: the back key completes back while a handler is enabled. */
    boolean onKey(KeyEvent e) {
        if (Build.VERSION.SDK_INT >= 33 || e.getKeyCode() != KeyEvent.KEYCODE_BACK) return false;
        if (e.getAction() == KeyEvent.ACTION_DOWN) {
            if (!enabled) return false;
            if (e.getRepeatCount() == 0) keyDown = true;
            return true;
        }
        if (e.getAction() == KeyEvent.ACTION_UP && keyDown) {
            keyDown = false;
            if (!e.isCanceled()) KilnNative.backInvoked();
            return true;
        }
        return false;
    }

    private static final class Api33 {
        static OnBackInvokedCallback create() { return KilnNative::backInvoked; }
    }

    private static final class Api34 {
        static OnBackInvokedCallback create(KilnView view) {
            return new OnBackAnimationCallback() {
                @Override public void onBackStarted(BackEvent e) { send(view, e, true); }
                @Override public void onBackProgressed(BackEvent e) { send(view, e, false); }
                @Override public void onBackCancelled() { KilnNative.backCancelled(); }
                @Override public void onBackInvoked() { KilnNative.backInvoked(); }
            };
        }
        static void send(KilnView view, BackEvent e, boolean started) {
            int[] loc = new int[2];
            view.getLocationInWindow(loc);
            long frameTime = Build.VERSION.SDK_INT >= 36 ? e.getFrameTimeMillis() : 0;
            KilnNative.backEvent(started, e.getTouchX() - loc[0], e.getTouchY() - loc[1], e.getProgress(),
                    e.getSwipeEdge(), frameTime);
        }
    }
}
