package dev.kiln;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Bundle;

import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.Set;

/**
 * lifecycle::ProcessLifecycleOwner's source on Android: every activity of the process
 * (ProcessLifecycleOwner.attach's ActivityLifecycleCallbacks, the post-started / post-resumed
 * ones as on API 29+). Activities are tracked by identity, so one that started before kiln was
 * loaded (a Gradle app's own) never stops or pauses below zero.
 */
final class KilnProcessLifecycle {
    private static boolean sAttached;
    private static final Set<Activity> sStarted = Collections.newSetFromMap(new IdentityHashMap<>());
    private static final Set<Activity> sResumed = Collections.newSetFromMap(new IdentityHashMap<>());

    private static native void started();
    private static native void resumed();
    private static native void paused();
    private static native void stopped();

    private KilnProcessLifecycle() {}

    static void attach(Context context) {
        if (sAttached) return;
        sAttached = true;
        Application app = (Application) context.getApplicationContext();
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityPostStarted(Activity a) { if (sStarted.add(a)) started(); }
            @Override public void onActivityPostResumed(Activity a) { if (sResumed.add(a)) resumed(); }
            @Override public void onActivityPaused(Activity a) { if (sResumed.remove(a)) paused(); }
            @Override public void onActivityStopped(Activity a) { if (sStarted.remove(a)) stopped(); }
            @Override public void onActivityCreated(Activity a, Bundle b) {}
            @Override public void onActivityStarted(Activity a) {}
            @Override public void onActivityResumed(Activity a) {}
            @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
            @Override public void onActivityDestroyed(Activity a) {
                if (sResumed.remove(a)) paused();
                if (sStarted.remove(a)) stopped();
            }
        });
    }
}
