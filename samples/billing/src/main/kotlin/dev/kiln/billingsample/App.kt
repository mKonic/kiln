package dev.kiln.billingsample

import android.app.Activity
import android.app.Application
import android.os.Bundle
import java.lang.ref.WeakReference

class App : Application() {
    /** The activity in front, which Google Play's purchase screen opens over. */
    private var resumed = WeakReference<Activity>(null)

    override fun onCreate() {
        super.onCreate()
        registerActivityLifecycleCallbacks(object : ActivityLifecycleCallbacks {
            override fun onActivityResumed(activity: Activity) { resumed = WeakReference(activity) }
            override fun onActivityPaused(activity: Activity) { if (resumed.get() === activity) resumed.clear() }
            override fun onActivityCreated(activity: Activity, state: Bundle?) {}
            override fun onActivityStarted(activity: Activity) {}
            override fun onActivityStopped(activity: Activity) {}
            override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) {}
            override fun onActivityDestroyed(activity: Activity) {}
        })
        BillingCpp.register(PlayBilling(this) { resumed.get() })
    }
}
