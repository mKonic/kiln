package dev.kiln;

import android.app.AlarmManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Sets kiln::alarm's alarms again after a reboot (AlarmManager forgets them) and when the user
 * allows exact alarms again (WorkManager's RescheduleReceiver). Only the system sends these.
 */
public class KilnAlarmRescheduler extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String a = intent.getAction();
        if (Intent.ACTION_BOOT_COMPLETED.equals(a) || AlarmManager.ACTION_SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED.equals(a)) {
            KilnAlarms.attach(context);
            KilnAlarms.rescheduleAll(context);
        }
    }
}
