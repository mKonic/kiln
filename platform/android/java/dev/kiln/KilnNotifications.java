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

    /** A Person from {name, key, uri, icon, "true" bot, "true" important}. */
    static android.app.Person person(Context context, String[] p) {
        android.app.Person.Builder b = new android.app.Person.Builder().setName(p[0]);
        if (!p[1].isEmpty()) b.setKey(p[1]);
        if (!p[2].isEmpty()) b.setUri(p[2]);
        if (!p[3].isEmpty()) {
            int id = context.getResources().getIdentifier(p[3], "drawable", context.getPackageName());
            if (id == 0) id = context.getResources().getIdentifier(p[3], "mipmap", context.getPackageName());
            if (id != 0) b.setIcon(Icon.createWithResource(context, id));
        }
        return b.setBot("true".equals(p[4])).setImportant("true".equals(p[5])).build();
    }

    /** A bubble's activity: the launcher activity at `data`, mutable as bubbles need (API 31). */
    private static PendingIntent bubbleIntent(String data) {
        Intent launch = sContext.getPackageManager().getLaunchIntentForPackage(sContext.getPackageName());
        Intent i = new Intent(Intent.ACTION_VIEW);
        if (launch != null && launch.getComponent() != null) i.setComponent(launch.getComponent());
        if (!data.isEmpty()) i.setData(Uri.parse(data));
        return PendingIntent.getActivity(sContext, ++sRequestCode, i, PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    /**
     * extra: {String[] {shortcutId, locusId, category}, Object[] people, messaging or null,
     * bubble or null}. messaging: {String[] user, conversation title, "true" group, Object[]
     * messages of {text, timestamp, String[] person or null}}. bubble: {shortcutId, data, icon,
     * desired height dp, "true" auto expand, "true" suppress notification}.
     */
    private static void applyExtra(Notification.Builder b, Object[] extra) {
        if (extra == null) return;
        String[] ids = (String[]) extra[0];
        if (!ids[0].isEmpty()) b.setShortcutId(ids[0]);
        if (!ids[1].isEmpty()) b.setLocusId(new android.content.LocusId(ids[1]));
        if (!ids[2].isEmpty()) b.setCategory(ids[2]);
        for (Object p : (Object[]) extra[1]) b.addPerson(person(sContext, (String[]) p));
        if (extra[2] != null) {
            Object[] m = (Object[]) extra[2];
            Notification.MessagingStyle style = new Notification.MessagingStyle(person(sContext, (String[]) m[0]));
            if (!((String) m[1]).isEmpty()) style.setConversationTitle((String) m[1]);
            style.setGroupConversation("true".equals(m[2]));
            for (Object o : (Object[]) m[3]) {
                Object[] msg = (Object[]) o;
                android.app.Person sender = msg[2] == null ? null : person(sContext, (String[]) msg[2]);
                style.addMessage(new Notification.MessagingStyle.Message((String) msg[0], Long.parseLong((String) msg[1]), sender));
            }
            b.setStyle(style);
        }
        if (extra[3] != null) {
            String[] bm = (String[]) extra[3];
            Notification.BubbleMetadata.Builder mb = !bm[0].isEmpty()
                    ? new Notification.BubbleMetadata.Builder(bm[0])
                    : new Notification.BubbleMetadata.Builder(bubbleIntent(bm[1]), icon(bm[2]));
            mb.setAutoExpandBubble("true".equals(bm[4])).setSuppressNotification("true".equals(bm[5]));
            int height = Integer.parseInt(bm[3]);
            if (height != 0) mb.setDesiredHeight(height);
            b.setBubbleMetadata(mb.build());
        }
    }

    /**
     * strings: channel, title, text, bigText, smallIcon, data, tag, group; ints: id, progress,
     * progressMax, flags (1 autoCancel, 2 ongoing, 4 onlyAlertOnce, 8 indeterminate, 16 group summary); actions: title,
     * data, icon per action.
     */
    static boolean notify(String[] s, int[] n, long when, String[] actions, Object[] extra) {
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
            applyExtra(b, extra);
            nm.notify(s[6].isEmpty() ? null : s[6], n[0], b.build());
            return true;
        } catch (IllegalArgumentException | SecurityException e) {
            android.util.Log.w("kiln", "notification: " + e.getMessage());
            return false;
        }
    }
}
