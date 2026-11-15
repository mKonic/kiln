package dev.kiln;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Base64;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * kiln::alarm's host: AlarmManager (AlarmManagerCompat's calls), each alarm kept in
 * SharedPreferences until it goes off so KilnAlarmRescheduler can set it again after a reboot.
 */
final class KilnAlarms {
    static final String ACTION_FIRE = "dev.kiln.alarm.FIRE";
    static final String EXTRA_ALARM = "dev.kiln.alarm";
    private static final String PREFS = "kiln_alarms";
    // alarm::AlarmKind
    private static final int INEXACT = 0, INEXACT_WHILE_IDLE = 1, WINDOW = 2, EXACT = 3, EXACT_WHILE_IDLE = 4, ALARM_CLOCK = 5;

    private static Context sContext;

    static native void fire(byte[] alarm);

    private KilnAlarms() {}

    static void attach(Context context) {
        if (sContext != null) return;
        sContext = context.getApplicationContext();
        restoreForceStopped(sContext);
    }

    /**
     * A force stop cancels the app's alarms and pending intents (WorkManager's
     * ForceStopRunnable): in a new process, a kept alarm whose pending intent is gone is set again.
     */
    private static void restoreForceStopped(Context c) {
        for (Map.Entry<String, ?> e : c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getAll().entrySet()) {
            if (operation(c, e.getKey(), null, PendingIntent.FLAG_NO_CREATE) != null) continue;
            String[] f = ((String) e.getValue()).split("\\|", 5);
            if (f.length != 5) continue;
            try {
                set(c, Base64.decode(f[3], Base64.NO_WRAP), e.getKey(), Long.parseLong(f[1]), Integer.parseInt(f[0]), Long.parseLong(f[2]), f[4]);
            } catch (RuntimeException ignored) {
                // AlarmManager bugs on some devices (ForceStopRunnable's b/189975360).
            }
        }
    }

    private static AlarmManager manager(Context c) { return c.getSystemService(AlarmManager.class); }

    static boolean canScheduleExact() {
        Context c = sContext;
        if (c == null) return true;
        return Build.VERSION.SDK_INT < 31 || manager(c).canScheduleExactAlarms();
    }

    static void openExactSettings() {
        Context c = sContext;
        if (c == null || Build.VERSION.SDK_INT < 31) return;
        Intent i = new Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, Uri.parse("package:" + c.getPackageName()));
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        c.startActivity(i);
    }

    /** One pending intent per name (the data URI tells them apart). */
    private static PendingIntent operation(Context c, String name, byte[] alarm, int flags) {
        Intent i = new Intent(c, KilnAlarmReceiver.class).setAction(ACTION_FIRE)
                .setData(new Uri.Builder().scheme("kiln-alarm").authority("alarm").appendPath(name).build());
        if (alarm != null) i.putExtra(EXTRA_ALARM, alarm);
        return PendingIntent.getBroadcast(c, 0, i, PendingIntent.FLAG_IMMUTABLE | flags);
    }

    private static PendingIntent show(Context c, String uri) {
        Intent launch = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
        Intent i = new Intent(uri.isEmpty() ? Intent.ACTION_MAIN : Intent.ACTION_VIEW);
        if (launch != null && launch.getComponent() != null) i.setComponent(launch.getComponent());
        if (!uri.isEmpty()) i.setData(Uri.parse(uri));
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(c, 0, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    static boolean schedule(byte[] alarm, String name, long trigger, int kind, long window, String showUri) {
        Context c = sContext;
        if (c == null) return false;
        if (!set(c, alarm, name, trigger, kind, window, showUri)) return false;
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(name, kind + "|" + trigger + "|" + window + "|" + Base64.encodeToString(alarm, Base64.NO_WRAP) + "|" + showUri)
                .apply();
        return true;
    }

    private static boolean set(Context c, byte[] alarm, String name, long trigger, int kind, long window, String showUri) {
        AlarmManager am = manager(c);
        PendingIntent op = operation(c, name, alarm, PendingIntent.FLAG_UPDATE_CURRENT);
        try {
            switch (kind) {
                case INEXACT: am.set(AlarmManager.RTC_WAKEUP, trigger, op); break;
                case INEXACT_WHILE_IDLE: am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, trigger, op); break;
                case WINDOW: am.setWindow(AlarmManager.RTC_WAKEUP, trigger, window, op); break;
                case EXACT: am.setExact(AlarmManager.RTC_WAKEUP, trigger, op); break;
                case EXACT_WHILE_IDLE: am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, trigger, op); break;
                case ALARM_CLOCK: am.setAlarmClock(new AlarmManager.AlarmClockInfo(trigger, show(c, showUri)), op); break;
                default: return false;
            }
            return true;
        } catch (SecurityException e) {
            Log.w("kiln", "alarm " + name + ": not allowed: " + e.getMessage());
            return false;
        }
    }

    static void cancel(String name) {
        Context c = sContext;
        if (c == null) return;
        PendingIntent op = operation(c, name, null, PendingIntent.FLAG_NO_CREATE);
        if (op != null) {
            manager(c).cancel(op);
            op.cancel();
        }
        forget(c, name);
    }

    static void forget(Context c, String name) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(name).apply();
    }

    static byte[][] pending() {
        Context c = sContext;
        if (c == null) return new byte[0][];
        List<byte[]> out = new ArrayList<>();
        for (Object v : c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getAll().values()) {
            String[] f = ((String) v).split("\\|", 5);
            if (f.length == 5) out.add(Base64.decode(f[3], Base64.NO_WRAP));
        }
        return out.toArray(new byte[0][]);
    }

    /** After a reboot, or when exact alarms are allowed again: every kept alarm, set again. */
    static void rescheduleAll(Context c) {
        for (Map.Entry<String, ?> e : c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getAll().entrySet()) {
            String[] f = ((String) e.getValue()).split("\\|", 5);
            if (f.length != 5) continue;
            // A time that passed while the device was off goes off now (AlarmManager fires it at once).
            set(c, Base64.decode(f[3], Base64.NO_WRAP), e.getKey(), Long.parseLong(f[1]), Integer.parseInt(f[0]), Long.parseLong(f[2]), f[4]);
        }
    }
}
