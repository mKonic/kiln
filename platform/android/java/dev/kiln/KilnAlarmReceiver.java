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
        KilnProcess.ensureLoaded(context);
        // One-shot: forgotten before its handler runs (which may schedule it again).
        KilnAlarms.forget(context, intent.getData().getLastPathSegment());
        KilnAlarms.fire(alarm);
    }
}
