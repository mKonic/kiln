package dev.kiln;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.Icon;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.os.Bundle;
import android.media.session.PlaybackState;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.util.SparseArray;

import java.io.InputStream;

/**
 * kiln::media's platform side: audio focus (AudioFocusManager's requests), the becoming-noisy
 * receiver, and media sessions with their MediaStyle notification (media3's MediaSession,
 * MediaNotificationManager and DefaultMediaNotificationProvider over the platform's
 * android.media.session). KilnMediaService keeps playback in the foreground.
 */
final class KilnMedia {
    static final String CHANNEL = "kiln_media";
    static final int NOTIFICATION_ID = 1001;   // DefaultMediaNotificationProvider.DEFAULT_NOTIFICATION_ID
    static final String ACTION_COMMAND = "dev.kiln.media.COMMAND";
    // media::MediaSessionCommand
    static final int PLAY = 0, PAUSE = 1, STOP = 2, PREVIOUS = 3, NEXT = 4, SEEK = 5, DISMISSED = 6,
            PREPARE = 7, REWIND = 8, FAST_FORWARD = 9, SKIP_TO_QUEUE_ITEM = 10, SET_SPEED = 11;

    private static Context sContext;
    private static final Handler sMain = new Handler(Looper.getMainLooper());

    static native void focusChanged(int id, int change);
    static native void becomingNoisy(int id);
    static native void sessionCommand(int id, int command, long positionMs, float speed);

    private KilnMedia() {}

    static void attach(Context context) {
        if (sContext == null) sContext = context.getApplicationContext();
    }

    static Context context() { return sContext; }

    private static AudioManager audio() { return sContext.getSystemService(AudioManager.class); }

    // --- the system's caption settings (CaptioningManager) ------------------------------------
    private static android.view.accessibility.CaptioningManager captioning() {
        return sContext == null ? null : sContext.getSystemService(android.view.accessibility.CaptioningManager.class);
    }
    static boolean captioningEnabled() {
        android.view.accessibility.CaptioningManager c = captioning();
        return c != null && c.isEnabled();
    }
    /** Util.getLocaleLanguageTag of the captions' locale, or null. */
    static String captioningLocale() {
        android.view.accessibility.CaptioningManager c = captioning();
        java.util.Locale l = c == null ? null : c.getLocale();
        return l == null ? null : l.toLanguageTag();
    }
    static float captioningFontScale() {
        android.view.accessibility.CaptioningManager c = captioning();
        return c == null ? 1f : c.getFontScale();
    }
    /**
     * CaptionStyleCompat.createFromCaptionStyle: {foreground, background, window, edgeType,
     * edgeColor}, each -1 in has[] where the user set none (the default applies).
     */
    static int[] captioningStyle() {
        android.view.accessibility.CaptioningManager c = captioning();
        if (c == null) return null;
        android.view.accessibility.CaptioningManager.CaptionStyle s = c.getUserStyle();
        return new int[] {
            s.hasForegroundColor() ? 1 : 0, s.foregroundColor, s.hasBackgroundColor() ? 1 : 0, s.backgroundColor,
            s.hasWindowColor() ? 1 : 0, s.windowColor, s.hasEdgeType() ? 1 : 0, s.edgeType, s.hasEdgeColor() ? 1 : 0, s.edgeColor,
        };
    }
    /** The caption typeface's family name, as the setting stores it ("sans-serif-condensed" ...). */
    static String captioningTypeface() {
        if (sContext == null) return null;
        return android.provider.Settings.Secure.getString(sContext.getContentResolver(), "accessibility_captioning_typeface");
    }

    // --- audio focus -------------------------------------------------------------------------

    private static final SparseArray<AudioFocusRequest> sFocus = new SparseArray<>();

    static int requestFocus(int id, int gain, int usage, int contentType, boolean pauseWhenDucked) {
        if (sContext == null) return AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        AudioFocusRequest r = sFocus.get(id);
        if (r == null) {
            AudioAttributes a = new AudioAttributes.Builder().setUsage(usage).setContentType(contentType).build();
            r = new AudioFocusRequest.Builder(gain)
                    .setAudioAttributes(a)
                    .setWillPauseWhenDucked(pauseWhenDucked)
                    .setAcceptsDelayedFocusGain(true)
                    .setOnAudioFocusChangeListener(change -> focusChanged(id, change), sMain)
                    .build();
            sFocus.put(id, r);
        }
        return audio().requestAudioFocus(r);
    }

    static void abandonFocus(int id) {
        AudioFocusRequest r = sFocus.get(id);
        if (r == null || sContext == null) return;
        audio().abandonAudioFocusRequest(r);
        sFocus.remove(id);
    }

    // --- becoming noisy ----------------------------------------------------------------------

    private static final SparseArray<BroadcastReceiver> sNoisy = new SparseArray<>();

    static void setNoisyReceiver(int id, boolean enabled) {
        if (sContext == null) return;
        BroadcastReceiver r = sNoisy.get(id);
        if (enabled && r == null) {
            r = new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) sMain.post(() -> becomingNoisy(id));
                }
            };
            sNoisy.put(id, r);
            // A protected system broadcast: no export flag.
            sContext.registerReceiver(r, new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
        } else if (!enabled && r != null) {
            sContext.unregisterReceiver(r);
            sNoisy.remove(id);
        }
    }

    // --- sessions ------------------------------------------------------------------------------

    private static final class Session {
        final int id;
        final MediaSession session;
        Notification notification;
        boolean show, foreground;
        String artworkUri = "";
        Bitmap artwork;

        Session(int id) {
            this.id = id;
            session = new MediaSession(sContext, "kiln-" + id);
            session.setCallback(new MediaSession.Callback() {
                @Override public void onPlay() { sessionCommand(id, PLAY, 0, 0); }
                @Override public void onPause() { sessionCommand(id, PAUSE, 0, 0); }
                @Override public void onStop() { sessionCommand(id, STOP, 0, 0); }
                @Override public void onSkipToPrevious() { sessionCommand(id, PREVIOUS, 0, 0); }
                @Override public void onSkipToNext() { sessionCommand(id, NEXT, 0, 0); }
                @Override public void onSeekTo(long pos) { sessionCommand(id, SEEK, pos, 0); }
                @Override public void onPrepare() { sessionCommand(id, PREPARE, 0, 0); }
                @Override public void onRewind() { sessionCommand(id, REWIND, 0, 0); }
                @Override public void onFastForward() { sessionCommand(id, FAST_FORWARD, 0, 0); }
                @Override public void onSkipToQueueItem(long queueId) { sessionCommand(id, SKIP_TO_QUEUE_ITEM, queueId, 0); }
                @Override public void onSetPlaybackSpeed(float speed) { sessionCommand(id, SET_SPEED, 0, speed); }
            }, sMain);
            session.setActive(true);
        }
    }

    private static final SparseArray<Session> sSessions = new SparseArray<>();

    /**
     * strings: title, artist, album, artworkUri, error, sessionActivityUri, mediaId; state:
     * media::PlaybackState (0 idle, 1 buffering, 2 ready, 3 ended).
     */
    static void update(int id, String[] s, int state, boolean playWhenReady, boolean isPlaying, long positionMs,
                       long durationMs, long bufferedMs, float speed, int queueItemId, boolean canPrevious,
                       boolean canNext, boolean canSeek, boolean show, boolean foreground) {
        if (sContext == null) return;
        Session x = sSessions.get(id);
        if (x == null) {
            x = new Session(id);
            sSessions.put(id, x);
        }
        if (!s[3].equals(x.artworkUri)) {
            x.artworkUri = s[3];
            x.artwork = s[3].isEmpty() ? null : loadBitmap(s[3]);
        }
        MediaMetadata.Builder m = new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, s[0])
                .putString(MediaMetadata.METADATA_KEY_ARTIST, s[1])
                .putString(MediaMetadata.METADATA_KEY_ALBUM, s[2]);
        if (durationMs >= 0) m.putLong(MediaMetadata.METADATA_KEY_DURATION, durationMs);
        if (x.artwork != null) m.putBitmap(MediaMetadata.METADATA_KEY_ART, x.artwork);
        x.session.setMetadata(m.build());

        // MediaSessionLegacyStub's conversion of the player's state.
        int platformState;
        if (!s[4].isEmpty()) platformState = PlaybackState.STATE_ERROR;
        else if (state == 0) platformState = PlaybackState.STATE_NONE;
        else if (state == 1) platformState = playWhenReady ? PlaybackState.STATE_BUFFERING : PlaybackState.STATE_PAUSED;
        else if (state == 3) platformState = PlaybackState.STATE_STOPPED;
        else platformState = playWhenReady ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED;
        // convertCommandToPlaybackStateActions: play or pause by Util.shouldShowPlayButton.
        boolean showPlay = !playWhenReady || state == 0 || state == 3;
        long actions = (showPlay ? PlaybackState.ACTION_PLAY : PlaybackState.ACTION_PAUSE) | PlaybackState.ACTION_PLAY_PAUSE
                | PlaybackState.ACTION_STOP | PlaybackState.ACTION_PREPARE | PlaybackState.ACTION_SET_PLAYBACK_SPEED;
        if (canSeek) actions |= PlaybackState.ACTION_SEEK_TO | PlaybackState.ACTION_REWIND | PlaybackState.ACTION_FAST_FORWARD
                | PlaybackState.ACTION_SKIP_TO_QUEUE_ITEM;
        if (canPrevious) actions |= PlaybackState.ACTION_SKIP_TO_PREVIOUS;
        if (canNext) actions |= PlaybackState.ACTION_SKIP_TO_NEXT;
        // The position moves at the playback speed while playing; the speed itself in "EXO_SPEED".
        Bundle extras = new Bundle();
        extras.putFloat("EXO_SPEED", speed);
        if (!s[6].isEmpty()) extras.putString("androidx.media.PlaybackStateCompat.Extras.KEY_MEDIA_ID", s[6]);
        PlaybackState.Builder p = new PlaybackState.Builder()
                .setState(platformState, positionMs, isPlaying ? speed : 0f, SystemClock.elapsedRealtime())
                .setActions(actions)
                .setBufferedPosition(bufferedMs)
                .setActiveQueueItemId(queueItemId < 0 ? MediaSession.QueueItem.UNKNOWN_ID : queueItemId)
                .setExtras(extras);
        if (!s[4].isEmpty()) p.setErrorMessage(s[4]);
        x.session.setPlaybackState(p.build());
        x.session.setSessionActivity(open(s[5]));

        x.show = show;
        x.foreground = foreground;
        x.notification = show ? buildNotification(x, s, playWhenReady, canPrevious, canNext) : null;
        KilnMediaService.sync();
    }

    static void release(int id) {
        Session x = sSessions.get(id);
        if (x == null) return;
        sSessions.remove(id);
        x.session.setActive(false);
        x.session.release();
        NotificationManager nm = sContext.getSystemService(NotificationManager.class);
        if (nm != null) nm.cancel(NOTIFICATION_ID + id);
        KilnMediaService.sync();
    }

    /** The sessions' notifications to show: id, notification, whether it keeps the service in the foreground. */
    static int sessionCount() { return sSessions.size(); }
    static int sessionId(int i) { return sSessions.keyAt(i); }
    static Notification notification(int i) { return sSessions.valueAt(i).notification; }
    static boolean foreground(int i) { return sSessions.valueAt(i).foreground; }

    private static PendingIntent command(int id, int command) {
        Intent i = new Intent(sContext, KilnMediaService.class).setAction(ACTION_COMMAND)
                .putExtra("id", id).putExtra("command", command);
        int code = id * 16 + command;
        return command == PLAY
                ? PendingIntent.getForegroundService(sContext, code, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT)
                : PendingIntent.getService(sContext, code, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    /** DefaultMediaNotificationProvider: previous, play / pause and next, all in the compact view. */
    private static Notification buildNotification(Session x, String[] s, boolean playWhenReady, boolean canPrevious, boolean canNext) {
        NotificationManager nm = sContext.getSystemService(NotificationManager.class);
        if (nm != null && nm.getNotificationChannel(CHANNEL) == null)
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Now playing", NotificationManager.IMPORTANCE_LOW));
        Notification.Builder b = new Notification.Builder(sContext, CHANNEL);
        int n = 0;
        if (canPrevious) {
            b.addAction(new Notification.Action.Builder(Icon.createWithResource(sContext, android.R.drawable.ic_media_previous), "Previous", command(x.id, PREVIOUS)).build());
            n++;
        }
        b.addAction(playWhenReady
                ? new Notification.Action.Builder(Icon.createWithResource(sContext, android.R.drawable.ic_media_pause), "Pause", command(x.id, PAUSE)).build()
                : new Notification.Action.Builder(Icon.createWithResource(sContext, android.R.drawable.ic_media_play), "Play", command(x.id, PLAY)).build());
        n++;
        if (canNext) {
            b.addAction(new Notification.Action.Builder(Icon.createWithResource(sContext, android.R.drawable.ic_media_next), "Next", command(x.id, NEXT)).build());
            n++;
        }
        int[] compact = new int[n];
        for (int i = 0; i < n; i++) compact[i] = i;
        Notification.MediaStyle style = new Notification.MediaStyle().setMediaSession(x.session.getSessionToken()).setShowActionsInCompactView(compact);
        b.setContentTitle(s[0]).setContentText(s[1]).setSubText(s[2].isEmpty() ? null : s[2]);
        if (x.artwork != null) b.setLargeIcon(x.artwork);
        return b.setContentIntent(open(s[5]))
                .setDeleteIntent(command(x.id, DISMISSED))
                .setOnlyAlertOnce(true)
                .setSmallIcon(smallIcon())
                .setStyle(style)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOngoing(false)
                .setShowWhen(false)
                .setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
                .build();
    }

    /** media3's small icon for media notifications: a note (kiln's res), whatever the app's icon. */
    private static int smallIcon() {
        int id = sContext.getResources().getIdentifier("kiln_ic_media", "drawable", sContext.getPackageName());
        return id != 0 ? id : android.R.drawable.ic_media_play;
    }

    /** The app's launcher activity, at `data` (KilnNotifications.open). */
    private static PendingIntent open(String data) {
        Intent launch = sContext.getPackageManager().getLaunchIntentForPackage(sContext.getPackageName());
        Intent i = new Intent(data.isEmpty() ? Intent.ACTION_MAIN : Intent.ACTION_VIEW);
        if (launch != null && launch.getComponent() != null) i.setComponent(launch.getComponent());
        if (!data.isEmpty()) i.setData(Uri.parse(data));
        else i.addCategory(Intent.CATEGORY_LAUNCHER);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        return PendingIntent.getActivity(sContext, 0, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    /** asset:///, file:///, an absolute path or content://, scaled down to at most 512 px (the
     *  metadata crosses binder, whose transactions are limited to 1 MB). */
    private static Bitmap loadBitmap(String uri) {
        try {
            InputStream in;
            if (uri.startsWith("asset:///")) in = sContext.getAssets().open(uri.substring(9));
            else if (uri.startsWith("/")) in = new java.io.FileInputStream(uri);
            else if (uri.startsWith("file://")) in = new java.io.FileInputStream(Uri.parse(uri).getPath());
            else in = sContext.getContentResolver().openInputStream(Uri.parse(uri));
            if (in == null) return null;
            Bitmap b;
            try (InputStream s = in) {
                b = BitmapFactory.decodeStream(s);
            }
            if (b == null) return null;
            int max = Math.max(b.getWidth(), b.getHeight());
            if (max > 512) b = Bitmap.createScaledBitmap(b, b.getWidth() * 512 / max, b.getHeight() * 512 / max, true);
            return b;
        } catch (Exception e) {
            Log.w("kiln", "media session: cannot load artwork " + uri + ": " + e);
            return null;
        }
    }
}
