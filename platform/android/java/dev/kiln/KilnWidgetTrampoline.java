package dev.kiln;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.StrictMode;
import android.util.Log;
import android.widget.RemoteViews;

/**
 * Runs a widget list item's tap that is not an activity (Glance's InvisibleActionTrampolineActivity
 * and launchTrampolineAction): the list's template is an activity PendingIntent, so a callback,
 * a broadcast or a service goes through this invisible activity, which sends or starts it and
 * finishes.
 */
public final class KilnWidgetTrampoline extends Activity {
    static final String EXTRA_TYPE = "dev.kiln.widget.trampolineType", EXTRA_INTENT = "dev.kiln.widget.trampolineIntent";
    static final int BROADCAST = 1, SERVICE = 2, FOREGROUND_SERVICE = 3;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            launch(getIntent());
        } catch (RuntimeException e) {
            Log.e("kiln", "widget: a list item's action failed", e);
        }
        finish();
    }

    @SuppressWarnings("deprecation")
    private void launch(Intent intent) {
        Intent action = Build.VERSION.SDK_INT >= 33 ? intent.getParcelableExtra(EXTRA_INTENT, Intent.class) : intent.getParcelableExtra(EXTRA_INTENT);
        if (action == null) throw new IllegalArgumentException("List adapter activity trampoline invoked without specifying target intent.");
        if (intent.hasExtra(RemoteViews.EXTRA_CHECKED)) action.putExtra(RemoteViews.EXTRA_CHECKED, intent.getBooleanExtra(RemoteViews.EXTRA_CHECKED, false));
        // allowUnsafeIntentLaunch: the target intent came out of this one.
        StrictMode.VmPolicy previous = StrictMode.getVmPolicy();
        StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder(previous).permitUnsafeIntentLaunch().build());
        try {
            switch (intent.getIntExtra(EXTRA_TYPE, 0)) {
                case BROADCAST: sendBroadcast(action); break;
                case SERVICE: startService(action); break;
                case FOREGROUND_SERVICE: startForegroundService(action); break;
                default: throw new IllegalArgumentException("List adapter activity trampoline invoked without trampoline type");
            }
        } finally {
            StrictMode.setVmPolicy(previous);
        }
    }
}
