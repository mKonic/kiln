package dev.kiln;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** An alarm went off: its handler runs (the app's library loaded first in a cold process). */
public class KilnAlarmReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (!KilnAlarms.ACTION_FIRE.equals(intent.getAction())) return;
        byte[] alarm = intent.getByteArrayExtra(KilnAlarms.EXTRA_ALARM);
        if (alarm == null || intent.getData() == null) return;
        // One-shot: forgotten before its handler runs (which may schedule it again), and before
        // a new process sets kept alarms again after a force stop.
        KilnAlarms.forget(context, intent.getData().getLastPathSegment());
        KilnProcess.ensureLoaded(context);
        KilnAlarms.fire(alarm);
    }
}
