package dev.kiln;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Icon;
import android.net.Uri;

/** ui::Notifications over NotificationManager. Taps and actions open the app at a deep link. */
final class KilnNotifications {
    private static Context sContext;
    private static int sRequestCode;

    private KilnNotifications() {}

    static void attach(Context context) { sContext = context.getApplicationContext(); }

    private static NotificationManager manager() { return sContext.getSystemService(NotificationManager.class); }

    static boolean createChannel(String id, String name, String description, int importance) {
        NotificationManager nm = manager();
        if (nm == null) return false;
        NotificationChannel c = new NotificationChannel(id, name, importance);
        if (!description.isEmpty()) c.setDescription(description);
        nm.createNotificationChannel(c);
        return true;
    }
    static void deleteChannel(String id) { NotificationManager nm = manager(); if (nm != null) nm.deleteNotificationChannel(id); }
    static boolean enabled() { NotificationManager nm = manager(); return nm != null && nm.areNotificationsEnabled(); }
    static void cancel(int id, String tag) { NotificationManager nm = manager(); if (nm != null) nm.cancel(tag.isEmpty() ? null : tag, id); }
    static void cancelAll() { NotificationManager nm = manager(); if (nm != null) nm.cancelAll(); }

    /** The app's launcher activity, at `data`; a running app gets it through onNewIntent. */
    private static PendingIntent open(String data) {
        Intent launch = sContext.getPackageManager().getLaunchIntentForPackage(sContext.getPackageName());
        Intent i = new Intent(data.isEmpty() ? Intent.ACTION_MAIN : Intent.ACTION_VIEW);
        if (launch != null && launch.getComponent() != null) i.setComponent(launch.getComponent());
        if (!data.isEmpty()) i.setData(Uri.parse(data));
        else i.addCategory(Intent.CATEGORY_LAUNCHER);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        return PendingIntent.getActivity(sContext, ++sRequestCode, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private static Icon icon(String name) {
        int id = 0;
        if (!name.isEmpty()) {
            id = sContext.getResources().getIdentifier(name, "drawable", sContext.getPackageName());
            if (id == 0) id = sContext.getResources().getIdentifier(name, "mipmap", sContext.getPackageName());
        }
        if (id == 0) id = sContext.getApplicationInfo().icon;
        // No launcher icon either: a notification without one cannot be posted.
        if (id == 0) id = sContext.getResources().getIdentifier("kiln_ic_notification", "drawable", sContext.getPackageName());
        return Icon.createWithResource(sContext, id);
    }

    /**
     * strings: channel, title, text, bigText, smallIcon, data, tag, group; ints: id, progress,
     * progressMax, flags (1 autoCancel, 2 ongoing, 4 onlyAlertOnce, 8 indeterminate, 16 group summary); actions: title,
     * data, icon per action.
     */
    static boolean notify(String[] s, int[] n, long when, String[] actions) {
        NotificationManager nm = manager();
        if (nm == null || !nm.areNotificationsEnabled()) return false;
        try {
            Notification.Builder b = new Notification.Builder(sContext, s[0]).setSmallIcon(icon(s[4]));
            if (!s[1].isEmpty()) b.setContentTitle(s[1]);
            if (!s[2].isEmpty()) b.setContentText(s[2]);
            if (!s[3].isEmpty()) b.setStyle(new Notification.BigTextStyle().bigText(s[3]));
            b.setContentIntent(open(s[5]));
            b.setAutoCancel((n[3] & 1) != 0).setOngoing((n[3] & 2) != 0).setOnlyAlertOnce((n[3] & 4) != 0);
            if (n[2] > 0 || (n[3] & 8) != 0) b.setProgress(n[2], n[1], (n[3] & 8) != 0);
            if (!s[7].isEmpty()) b.setGroup(s[7]);
            if ((n[3] & 16) != 0) b.setGroupSummary(true);
            // NotificationCompat shows the time by default; the platform's builder does not
            // for apps that target Android 7 and later.
            b.setShowWhen(true);
            if (when != 0) b.setWhen(when);
            for (int i = 0; i + 2 < actions.length; i += 3)
                b.addAction(new Notification.Action.Builder(icon(actions[i + 2]), actions[i], open(actions[i + 1])).build());
            nm.notify(s[6].isEmpty() ? null : s[6], n[0], b.build());
            return true;
        } catch (IllegalArgumentException | SecurityException e) {
            android.util.Log.w("kiln", "notification: " + e.getMessage());
            return false;
        }
    }
}
