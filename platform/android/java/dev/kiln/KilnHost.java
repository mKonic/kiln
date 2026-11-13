package dev.kiln;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;

/**
 * kiln in an activity of the app's own: a KilnView placed in any layout (a CoordinatorLayout under
 * an app bar, a pane of a larger screen), instead of KilnActivity's whole window.
 *
 * <pre>
 * KilnView kiln = KilnHost.create(this, "notes", savedInstanceState);
 * container.addView(kiln);
 * </pre>
 *
 * It loads the app's native library, makes the view, and follows the activity: its lifecycle
 * reaches the content (LocalLifecycleOwner), and the content's saved state goes into the
 * activity's. One KilnView per process, as with KilnActivity. Activity results and permission
 * requests from the content need KilnActivity, which receives them.
 */
public final class KilnHost {
    private static final String SAVED_STATE_KEY = "SaveableStateRegistry:kiln";
    private static final int ON_START = 1, ON_RESUME = 2, ON_PAUSE = 3, ON_STOP = 4, ON_DESTROY = 5;

    private KilnHost() {}

    /** Call from onCreate; libraryName is the app's native library ("notes" for libnotes.so). */
    public static KilnView create(Activity activity, String libraryName, Bundle savedInstanceState) {
        System.loadLibrary(libraryName);
        byte[] saved = savedInstanceState != null ? savedInstanceState.getByteArray(SAVED_STATE_KEY) : null;
        final KilnView view = new KilnView(activity, saved);
        // The view may cover only part of the window: popups get windows of their own, so a menu
        // can reach past the view as it does from a ComposeView.
        KilnNative.setPopupWindows(0, true);
        activity.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            // As KilnActivity (and androidx's ReportFragment): start and resume after the
            // activity's own callback, pause, stop and destroy before it.
            @Override public void onActivityPostStarted(Activity a) { view.onHostStart(); KilnNative.lifecycleEvent(ON_START); }
            @Override public void onActivityPostResumed(Activity a) { KilnNative.lifecycleEvent(ON_RESUME); }
            @Override public void onActivityPrePaused(Activity a) { KilnNative.lifecycleEvent(ON_PAUSE); }
            @Override public void onActivityPreStopped(Activity a) { KilnNative.lifecycleEvent(ON_STOP); view.onHostStop(); }
            @Override public void onActivityPreDestroyed(Activity a) {
                KilnNative.lifecycleEvent(ON_DESTROY);
                view.release();
                KilnNative.destroy();
                a.unregisterActivityLifecycleCallbacks(this);
            }
            @Override public void onActivitySaveInstanceState(Activity a, Bundle out) { out.putByteArray(SAVED_STATE_KEY, view.saveState()); }
            @Override public void onActivityCreated(Activity a, Bundle b) {}
            @Override public void onActivityStarted(Activity a) {}
            @Override public void onActivityResumed(Activity a) {}
            @Override public void onActivityPaused(Activity a) {}
            @Override public void onActivityStopped(Activity a) {}
            @Override public void onActivityDestroyed(Activity a) {}
        });
        return view;
    }
}
