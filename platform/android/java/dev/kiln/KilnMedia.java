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
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaDescription;
import android.media.MediaMetadata;
import android.media.Rating;
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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

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
            PREPARE = 7, REWIND = 8, FAST_FORWARD = 9, SKIP_TO_QUEUE_ITEM = 10, SET_SPEED = 11, PLAYER_COMMAND = 12;
    static final int PLAY_PAUSE = 1;      // media::PlayerCommand::PlayPause
    static final int ICON_PLAY = 0xe037;  // CommandButton.ICON_PLAY

    private static Context sContext;
    private static final Handler sMain = new Handler(Looper.getMainLooper());

    static native void focusChanged(int id, int change);
    static native void becomingNoisy(int id);
    static native void volumeChanged(int id);
    static native void sessionCommand(int id, int command, long positionMs, float speed);
    static native void customAction(int id, String action);
    static native void setRating(int id, int style, boolean rated, boolean liked, float value);
    static native void mediaRequest(int id, String mediaId, String uri, String query, boolean play);

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

    // --- device volume (StreamVolumeManager) ---------------------------------------------------
    /** {volume, muted, min, max} of an AudioManager stream (AudioManagerCompat). */
    static int[] streamVolume(int stream) {
        AudioManager a = sContext == null ? null : audio();
        if (a == null) return new int[] { 0, 0, 0, 0 };
        int volume, max = a.getStreamMaxVolume(stream), min = a.getStreamMinVolume(stream);
        try {
            volume = a.getStreamVolume(stream);
        } catch (RuntimeException e) {   // some devices throw for some streams
            volume = max;
        }
        return new int[] { volume, a.isStreamMute(stream) ? 1 : 0, min, max };
    }
    static void setStreamVolume(int stream, int volume, int flags) {
        if (sContext != null) audio().setStreamVolume(stream, volume, flags);
    }
    static void adjustStreamVolume(int stream, int direction, int flags) {
        if (sContext != null) audio().adjustStreamVolume(stream, direction, flags);
    }
    private static final SparseArray<BroadcastReceiver> sVolume = new SparseArray<>();
    static void setVolumeReceiver(int id, boolean enabled) {
        if (sContext == null) return;
        BroadcastReceiver r = sVolume.get(id);
        if (enabled && r == null) {
            r = new BroadcastReceiver() {
                @Override public void onReceive(Context context, Intent intent) { sMain.post(() -> volumeChanged(id)); }
            };
            sVolume.put(id, r);
            // AudioManager.VOLUME_CHANGED_ACTION (hidden; media3 uses it too): a system broadcast.
            sContext.registerReceiver(r, new IntentFilter("android.media.VOLUME_CHANGED_ACTION"));
        } else if (!enabled && r != null) {
            sContext.unregisterReceiver(r);
            sVolume.remove(id);
        }
    }

    // --- wake and Wi-Fi locks (WakeLockManager, WifiLockManager) -----------------------------------
    private static final SparseArray<android.os.PowerManager.WakeLock> sWake = new SparseArray<>();
    private static final SparseArray<android.net.wifi.WifiManager.WifiLock> sWifi = new SparseArray<>();
    @SuppressWarnings("deprecation")
    static void setLocks(int id, boolean wake, boolean wifi) {
        if (sContext == null) return;
        if (sContext.checkSelfPermission(android.Manifest.permission.WAKE_LOCK) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            if (wake || wifi) android.util.Log.w("kiln", "media: WAKE_LOCK permission not granted, can't keep playback awake");
            return;
        }
        android.os.PowerManager.WakeLock w = sWake.get(id);
        if (wake && w == null) {
            w = sContext.getSystemService(android.os.PowerManager.class).newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "ExoPlayer:WakeLockManager");
            w.setReferenceCounted(false);
            sWake.put(id, w);
        }
        if (w != null) {
            if (wake) w.acquire();
            else w.release();
        }
        android.net.wifi.WifiManager.WifiLock l = sWifi.get(id);
        if (wifi && l == null) {
            android.net.wifi.WifiManager m = sContext.getApplicationContext().getSystemService(android.net.wifi.WifiManager.class);
            if (m != null) {
                l = m.createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "ExoPlayer:WifiLockManager");
                l.setReferenceCounted(false);
                sWifi.put(id, l);
            }
        }
        if (l != null) {
            if (wifi) l.acquire();
            else l.release();
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
        // From layout(): the notification's buttons (drawable, name, custom action, player
        // command) and icons, the compact view, the custom actions, the queue, the rating, extras.
        String[] buttons = new String[0], custom = new String[0], queue = null, extras = null;
        int[] buttonIcons = new int[0], customIcons = new int[0], compact = new int[0];
        String queueTitle = null;
        int ratingType = -1;
        float[] rating = new float[0];
        long actions;

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
                @Override public void onCustomAction(String action, Bundle extras) { customAction(id, action); }
                @Override public void onSetRating(Rating r) {
                    if (r == null) return;
                    int style = r.getRatingStyle();
                    boolean rated = r.isRated();
                    boolean liked = rated && (style == Rating.RATING_HEART ? r.hasHeart() : style == Rating.RATING_THUMB_UP_DOWN && r.isThumbUp());
                    float value = !rated ? 0 : style == Rating.RATING_PERCENTAGE ? r.getPercentRating()
                            : style >= Rating.RATING_3_STARS && style <= Rating.RATING_5_STARS ? r.getStarRating() : 0;
                    setRating(id, style, rated, liked, value);
                }
                @Override public void onPlayFromMediaId(String mediaId, Bundle extras) { mediaRequest(id, nz(mediaId), "", "", true); }
                @Override public void onPlayFromSearch(String query, Bundle extras) { mediaRequest(id, "", "", nz(query), true); }
                @Override public void onPlayFromUri(Uri uri, Bundle extras) { mediaRequest(id, "", uri == null ? "" : uri.toString(), "", true); }
                @Override public void onPrepareFromMediaId(String mediaId, Bundle extras) { mediaRequest(id, nz(mediaId), "", "", false); }
                @Override public void onPrepareFromSearch(String query, Bundle extras) { mediaRequest(id, "", "", nz(query), false); }
                @Override public void onPrepareFromUri(Uri uri, Bundle extras) { mediaRequest(id, "", uri == null ? "" : uri.toString(), "", false); }
            }, sMain);
            session.setActive(true);
        }
    }

    private static final SparseArray<Session> sSessions = new SparseArray<>();

    private static String nz(String s) { return s == null ? "" : s; }

    private static Session session(int id) {
        Session x = sSessions.get(id);
        if (x == null) {
            x = new Session(id);
            sSessions.put(id, x);
        }
        return x;
    }

    /**
     * Before update(): buttons (4 a button: drawable, name, custom action, player command) with
     * their icons, the compact view, the custom layout's custom actions (3 each: drawable,
     * name, action) and icons, the queue (5 an item: mediaId, title, subtitle, description,
     * iconUri), its title, the RatingCompat style, the user rating (empty or style, rated,
     * liked, value), extras (key, value pairs) and the PlaybackState actions.
     */
    static void layout(int id, String[] buttons, int[] buttonIcons, int[] compact, String[] custom, int[] customIcons,
                       String[] queue, String queueTitle, int ratingType, float[] rating, String[] extras, long actions) {
        if (sContext == null) return;
        Session x = session(id);
        x.buttons = buttons;
        x.buttonIcons = buttonIcons;
        x.compact = compact;
        x.custom = custom;
        x.customIcons = customIcons;
        x.rating = rating;
        x.actions = actions;
        // MediaSessionLegacyStub: the queue (QueueItem ids are the indices) and its title.
        if (!Arrays.equals(queue, x.queue)) {
            x.queue = queue;
            List<MediaSession.QueueItem> items = new ArrayList<>();
            for (int i = 0; i + 4 < queue.length; i += 5) {
                MediaDescription.Builder d = new MediaDescription.Builder().setMediaId(queue[i].isEmpty() ? null : queue[i])
                        .setTitle(queue[i + 1]).setSubtitle(queue[i + 2]).setDescription(queue[i + 3]);
                if (!queue[i + 4].isEmpty()) d.setIconUri(Uri.parse(queue[i + 4]));
                items.add(new MediaSession.QueueItem(d.build(), i / 5));
            }
            x.session.setQueue(items.isEmpty() ? null : items);
        }
        if (!queueTitle.equals(x.queueTitle)) {
            x.queueTitle = queueTitle;
            x.session.setQueueTitle(queueTitle.isEmpty() ? null : queueTitle);
        }
        if (ratingType != x.ratingType) {
            x.ratingType = ratingType;
            x.session.setRatingType(ratingType);
        }
        if (!Arrays.equals(extras, x.extras)) {
            x.extras = extras;
            Bundle b = new Bundle();
            for (int i = 0; i + 1 < extras.length; i += 2) b.putString(extras[i], extras[i + 1]);
            x.session.setExtras(b);
        }
    }

    /** LegacyConversions.convertToRatingCompat. */
    private static Rating toRating(float[] r) {
        if (r.length != 4) return null;
        int style = (int) r[0];
        if (r[1] == 0) return Rating.newUnratedRating(style);
        if (style == Rating.RATING_HEART) return Rating.newHeartRating(r[2] != 0);
        if (style == Rating.RATING_THUMB_UP_DOWN) return Rating.newThumbRating(r[2] != 0);
        if (style == Rating.RATING_PERCENTAGE) return Rating.newPercentageRating(r[3]);
        return Rating.newStarRating(style, r[3]);
    }

    /**
     * strings: title, artist, album, artworkUri, error, sessionActivityUri, mediaId; state:
     * media::PlaybackState (0 idle, 1 buffering, 2 ready, 3 ended).
     */
    static void update(int id, String[] s, int state, boolean playWhenReady, boolean isPlaying, long positionMs,
                       long durationMs, long bufferedMs, float speed, int queueItemId, boolean canPrevious,
                       boolean canNext, boolean canSeek, boolean show, boolean foreground) {
        if (sContext == null) return;
        Session x = session(id);
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
        Rating userRating = toRating(x.rating);
        if (userRating != null) m.putRating(MediaMetadata.METADATA_KEY_USER_RATING, userRating);
        x.session.setMetadata(m.build());

        // MediaSessionLegacyStub's conversion of the player's state.
        int platformState;
        if (!s[4].isEmpty()) platformState = PlaybackState.STATE_ERROR;
        else if (state == 0) platformState = PlaybackState.STATE_NONE;
        else if (state == 1) platformState = playWhenReady ? PlaybackState.STATE_BUFFERING : PlaybackState.STATE_PAUSED;
        else if (state == 3) platformState = PlaybackState.STATE_STOPPED;
        else platformState = playWhenReady ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED;
        long actions = x.actions;   // computed as MediaSessionLegacyStub does, by the native side
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
        // The custom layout's custom commands, with their icon in the action's extras.
        for (int i = 0, j = 0; i + 2 < x.custom.length; i += 3, j++) {
            Bundle a = new Bundle();
            if (j < x.customIcons.length && x.customIcons[j] != 0)
                a.putInt("androidx.media3.session.EXTRAS_KEY_COMMAND_BUTTON_ICON_COMPAT", x.customIcons[j]);
            p.addCustomAction(new PlaybackState.CustomAction.Builder(x.custom[i + 2], x.custom[i + 1], drawable(x.custom[i]))
                    .setExtras(a).build());
        }
        if (!s[4].isEmpty()) p.setErrorMessage(s[4]);
        x.session.setPlaybackState(p.build());
        x.session.setSessionActivity(open(s[5]));

        x.show = show;
        x.foreground = foreground;
        x.notification = show ? buildNotification(x, s) : null;
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

    /** A notification button's intent: its player command, or its custom action
     *  (DefaultActionFactory); play starts the service in the foreground. */
    private static PendingIntent button(int id, int index, String customAction, int playerCommand, boolean play) {
        Intent i = new Intent(sContext, KilnMediaService.class).setAction(ACTION_COMMAND).putExtra("id", id);
        if (!customAction.isEmpty()) {
            i.putExtra("custom", customAction);
            i.setData(Uri.parse("kiln-media:" + id + "/" + Uri.encode(customAction)));   // one PendingIntent an action
        } else {
            i.putExtra("command", PLAYER_COMMAND).putExtra("position", (long) playerCommand);
        }
        int code = 65536 + id * 64 + index;
        int flags = PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT;
        return play ? PendingIntent.getForegroundService(sContext, code, i, flags) : PendingIntent.getService(sContext, code, i, flags);
    }

    /** A media3_icon_* drawable (kiln's res) or the app's own, by name. */
    private static int drawable(String name) {
        int id = name.isEmpty() ? 0 : sContext.getResources().getIdentifier(name, "drawable", sContext.getPackageName());
        return id != 0 ? id : android.R.drawable.ic_menu_more;
    }

    /** DefaultMediaNotificationProvider: getMediaButtons' buttons, the compact view from their slots. */
    private static Notification buildNotification(Session x, String[] s) {
        NotificationManager nm = sContext.getSystemService(NotificationManager.class);
        if (nm != null && nm.getNotificationChannel(CHANNEL) == null)
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Now playing", NotificationManager.IMPORTANCE_LOW));
        Notification.Builder b = new Notification.Builder(sContext, CHANNEL);
        for (int i = 0, j = 0; i + 3 < x.buttons.length; i += 4, j++) {
            int command = Integer.parseInt(x.buttons[i + 3]);
            boolean play = command == PLAY_PAUSE && j < x.buttonIcons.length && x.buttonIcons[j] == ICON_PLAY;
            b.addAction(new Notification.Action.Builder(Icon.createWithResource(sContext, drawable(x.buttons[i])), x.buttons[i + 1],
                    button(x.id, j, x.buttons[i + 2], command, play)).build());
        }
        Notification.MediaStyle style = new Notification.MediaStyle().setMediaSession(x.session.getSessionToken()).setShowActionsInCompactView(x.compact);
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

    // --- decoder support (MediaCodecUtil / MediaCodecInfo.isFormatSupported) -----------------
    private static MediaCodecInfo[] sDecoders;

    // 2: a decoder plays the format (C.FORMAT_HANDLED); 1: a decoder takes the type but not these
    // properties (FORMAT_EXCEEDS_CAPABILITIES); 0: none takes the type. -1 for unknown values.
    static synchronized int decoderSupport(String mime, String codecs, int width, int height, float frameRate, int channels, int sampleRate) {
        if (sDecoders == null) sDecoders = new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos();
        int best = 0;
        for (MediaCodecInfo info : sDecoders) {
            if (info.isEncoder()) continue;
            String type = null;
            for (String t : info.getSupportedTypes()) if (t.equalsIgnoreCase(mime)) type = t;
            if (type == null) continue;
            best = 1;
            MediaCodecInfo.CodecCapabilities caps;
            try {
                caps = info.getCapabilitiesForType(type);
            } catch (IllegalArgumentException e) {
                continue;
            }
            if (formatSupported(caps, mime, codecs, width, height, frameRate, channels, sampleRate)) return 2;
        }
        return best;
    }

    private static boolean formatSupported(MediaCodecInfo.CodecCapabilities caps, String mime, String codecs, int width, int height, float frameRate,
                                           int channels, int sampleRate) {
        MediaCodecInfo.VideoCapabilities video = caps.getVideoCapabilities();
        if (video != null && width > 0 && height > 0) {
            // isVideoSizeAndRateSupportedV21: the size and rate, either way round.
            boolean sized = frameRate > 0
                ? video.areSizeAndRateSupported(width, height, frameRate) || video.areSizeAndRateSupported(height, width, frameRate)
                : video.isSizeSupported(width, height) || video.isSizeSupported(height, width);
            if (!sized) return false;
        }
        MediaCodecInfo.AudioCapabilities audio = caps.getAudioCapabilities();
        if (audio != null) {
            if (sampleRate > 0 && !audio.isSampleRateSupported(sampleRate)) return false;
            if (channels > 0 && audio.getMaxInputChannelCount() < channels) return false;
        }
        // isCodecProfileAndLevelSupported for H.264 (avc1.PPCCLL): a profile the decoder has, at
        // the level or above.
        if ("video/avc".equalsIgnoreCase(mime) && codecs != null && codecs.length() >= 11 && (codecs.startsWith("avc1.") || codecs.startsWith("avc3."))) {
            int profile, level;
            try {
                profile = avcProfile(Integer.parseInt(codecs.substring(5, 7), 16));
                level = avcLevel(Integer.parseInt(codecs.substring(9, 11), 16));
            } catch (NumberFormatException e) {
                return true;
            }
            if (profile == 0 || level == 0) return true;
            for (MediaCodecInfo.CodecProfileLevel pl : caps.profileLevels)
                if (pl.profile == profile && pl.level >= level) return true;
            return caps.profileLevels.length == 0;
        }
        return true;
    }

    private static int avcProfile(int profileIdc) {
        switch (profileIdc) {
            case 66: return MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline;
            case 77: return MediaCodecInfo.CodecProfileLevel.AVCProfileMain;
            case 88: return MediaCodecInfo.CodecProfileLevel.AVCProfileExtended;
            case 100: return MediaCodecInfo.CodecProfileLevel.AVCProfileHigh;
            case 110: return MediaCodecInfo.CodecProfileLevel.AVCProfileHigh10;
            case 122: return MediaCodecInfo.CodecProfileLevel.AVCProfileHigh422;
            case 244: return MediaCodecInfo.CodecProfileLevel.AVCProfileHigh444;
            default: return 0;
        }
    }

    private static int avcLevel(int levelIdc) {
        switch (levelIdc) {
            case 10: return MediaCodecInfo.CodecProfileLevel.AVCLevel1;
            case 11: return MediaCodecInfo.CodecProfileLevel.AVCLevel11;
            case 12: return MediaCodecInfo.CodecProfileLevel.AVCLevel12;
            case 13: return MediaCodecInfo.CodecProfileLevel.AVCLevel13;
            case 20: return MediaCodecInfo.CodecProfileLevel.AVCLevel2;
            case 21: return MediaCodecInfo.CodecProfileLevel.AVCLevel21;
            case 22: return MediaCodecInfo.CodecProfileLevel.AVCLevel22;
            case 30: return MediaCodecInfo.CodecProfileLevel.AVCLevel3;
            case 31: return MediaCodecInfo.CodecProfileLevel.AVCLevel31;
            case 32: return MediaCodecInfo.CodecProfileLevel.AVCLevel32;
            case 40: return MediaCodecInfo.CodecProfileLevel.AVCLevel4;
            case 41: return MediaCodecInfo.CodecProfileLevel.AVCLevel41;
            case 42: return MediaCodecInfo.CodecProfileLevel.AVCLevel42;
            case 50: return MediaCodecInfo.CodecProfileLevel.AVCLevel5;
            case 51: return MediaCodecInfo.CodecProfileLevel.AVCLevel51;
            case 52: return MediaCodecInfo.CodecProfileLevel.AVCLevel52;
            case 60: return MediaCodecInfo.CodecProfileLevel.AVCLevel6;
            case 61: return MediaCodecInfo.CodecProfileLevel.AVCLevel61;
            case 62: return MediaCodecInfo.CodecProfileLevel.AVCLevel62;
            default: return 0;
        }
    }
}
