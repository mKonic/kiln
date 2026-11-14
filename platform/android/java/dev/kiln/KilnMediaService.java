package dev.kiln;

import android.app.ForegroundServiceStartNotAllowedException;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.util.Log;

/**
 * kiln::media's MediaSessionService: posts the sessions' notifications, in the foreground (a
 * mediaPlayback foreground service) while the user listens and for the timeout after, so the
 * process keeps playing with the app in the background; runs the notification's buttons.
 * Manifests declare it with android:foregroundServiceType="mediaPlayback"; the app declares
 * android.permission.FOREGROUND_SERVICE and FOREGROUND_SERVICE_MEDIA_PLAYBACK.
 */
public class KilnMediaService extends Service {
    private static KilnMediaService current;
    private static boolean starting;
    private boolean inForeground;

    @Override
    public void onCreate() {
        super.onCreate();
        current = this;
        KilnMedia.attach(this);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        starting = false;
        apply();
        // startForegroundService needs startForeground, even when playback paused meanwhile.
        if (!inForeground) {
            for (int i = 0; i < KilnMedia.sessionCount(); i++) {
                Notification n = KilnMedia.notification(i);
                if (n == null) continue;
                try {
                    startForeground(KilnMedia.NOTIFICATION_ID + KilnMedia.sessionId(i), n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
                    stopForeground(STOP_FOREGROUND_DETACH);
                } catch (ForegroundServiceStartNotAllowedException | SecurityException e) {
                    Log.w("kiln", "media session: cannot run in the foreground: " + e);
                }
                break;
            }
        }
        if (intent != null && KilnMedia.ACTION_COMMAND.equals(intent.getAction())) {
            int id = intent.getIntExtra("id", 0), command = intent.getIntExtra("command", -1);
            if (KilnMedia.sessionCount() > 0) KilnMedia.sessionCommand(id, command, 0, 0);
        }
        if (KilnMedia.sessionCount() == 0) stopSelf();   // the process was restarted: nothing plays
        return START_NOT_STICKY;
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        // MediaSessionService.onTaskRemoved: swiped away while not playing, the service goes.
        if (!anyForeground()) stopSelf();
    }

    @Override
    public void onDestroy() {
        if (current == this) current = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    private static boolean anyForeground() {
        for (int i = 0; i < KilnMedia.sessionCount(); i++)
            if (KilnMedia.notification(i) != null && KilnMedia.foreground(i)) return true;
        return false;
    }
    private static boolean anyShown() {
        for (int i = 0; i < KilnMedia.sessionCount(); i++)
            if (KilnMedia.notification(i) != null) return true;
        return false;
    }

    /** After a session changed: start, update or stop the service (MediaNotificationManager.updateNotificationInternal). */
    static void sync() {
        if (current != null) {
            current.apply();
            return;
        }
        if (!anyForeground() || starting) {
            postWithoutService();
            return;
        }
        Context c = KilnMedia.context();
        if (c == null) return;
        try {
            starting = true;
            c.startForegroundService(new Intent(c, KilnMediaService.class));
        } catch (IllegalStateException e) {
            starting = false;
            // ForegroundServiceStartNotAllowedException: started from the background; the
            // notification is still posted, without the service.
            Log.w("kiln", "media session: cannot start the media service: " + e);
            postWithoutService();
        }
    }

    private static void postWithoutService() {
        Context c = KilnMedia.context();
        NotificationManager nm = c == null ? null : c.getSystemService(NotificationManager.class);
        if (nm == null) return;
        for (int i = 0; i < KilnMedia.sessionCount(); i++) {
            Notification n = KilnMedia.notification(i);
            int id = KilnMedia.NOTIFICATION_ID + KilnMedia.sessionId(i);
            if (n != null) nm.notify(id, n);
            else nm.cancel(id);
        }
    }

    private void apply() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        int foregroundId = -1;
        Notification foreground = null;
        for (int i = 0; i < KilnMedia.sessionCount(); i++) {
            Notification n = KilnMedia.notification(i);
            int id = KilnMedia.NOTIFICATION_ID + KilnMedia.sessionId(i);
            if (n != null && KilnMedia.foreground(i) && foreground == null) {
                foreground = n;
                foregroundId = id;
            } else if (n != null) {
                nm.notify(id, n);
            } else {
                nm.cancel(id);
            }
        }
        if (foreground != null) {
            try {
                startForeground(foregroundId, foreground, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
                inForeground = true;
            } catch (ForegroundServiceStartNotAllowedException | SecurityException e) {
                Log.w("kiln", "media session: cannot run in the foreground: " + e);
                nm.notify(foregroundId, foreground);
            }
        } else if (inForeground) {
            // Paused past the timeout: the notification stays, the service leaves the foreground.
            stopForeground(anyShown() ? STOP_FOREGROUND_DETACH : STOP_FOREGROUND_REMOVE);
            inForeground = false;
        }
        if (!anyShown()) stopSelf();
    }
}
