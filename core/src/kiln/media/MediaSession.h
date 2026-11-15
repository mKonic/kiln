#pragma once

// A media session for a Player (media3 session: MediaSession, MediaSessionService,
// MediaNotificationManager and DefaultMediaNotificationProvider): the system's media controls
// (the notification, the lock screen, quick settings, Bluetooth and headset buttons, Android
// Auto) show what plays and control it, and playback goes on with the app in the background,
// in a media foreground service while the user listens.
//
//   media::Player& Music() { static media::Player p; return p; }   // outlives the activity
//   static media::MediaSession session(Music(), { .onSkipToNext = [] { NextTrack(); } });   // in a function
//   session.SetMetadata({ .title = "Song", .artist = "Band", .artworkUri = "asset:///cover.jpg" });
//
// The app declares android.permission.FOREGROUND_SERVICE and FOREGROUND_SERVICE_MEDIA_PLAYBACK
// (kiln-pack and kiln's manifests declare dev.kiln.KilnMediaService).

#include "kiln/media/Player.h"
#include "kiln/runtime/Task.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media {

    // What the controls show is the current item's MediaMetadata (kiln/media/MediaItem.h), with
    // SetMetadata's fields over it.

    // CommandButton (media3): a button in the system's media controls and the notification, for a
    // custom command (onCustomCommand gets its action) or a player command. Where it goes is its
    // slots' first that is free (back, forward, else the overflow); unset, by its icon or command.
    struct CommandButton {
        enum Icon : int {
            Undefined = 0,
            Play = 0xe037,
            Pause = 0xe034,
            Stop = 0xe047,
            Next = 0xe044,
            Previous = 0xe045,
            SkipForward = 0xf6f4,
            SkipForward5 = 0xe058,
            SkipForward10 = 0xe056,
            SkipForward15 = 0xfe056,
            SkipForward30 = 0xe057,
            SkipBack = 0xe042,
            SkipBack5 = 0xe05b,
            SkipBack10 = 0xe059,
            SkipBack15 = 0xfe059,
            SkipBack30 = 0xe05a,
            FastForward = 0xe01f,
            Rewind = 0xe020,
            RepeatAll = 0xe040,
            RepeatOne = 0xe041,
            RepeatOff = 0xfe040,
            ShuffleOn = 0xe043,
            ShuffleOff = 0xfe044,
            ShuffleStar = 0xfe043,
            HeartFilled = 0xfe87d,
            HeartUnfilled = 0xe87d,
            StarFilled = 0xfe838,
            StarUnfilled = 0xe838,
            BookmarkFilled = 0xfe866,
            BookmarkUnfilled = 0xe866,
            ThumbUpFilled = 0xfe8dc,
            ThumbUpUnfilled = 0xe8dc,
            ThumbDownFilled = 0xfe8db,
            ThumbDownUnfilled = 0xe8db,
            FlagFilled = 0xfe153,
            FlagUnfilled = 0xe153,
            Plus = 0xe145,
            Minus = 0xe15b,
            PlaylistAdd = 0xe03b,
            PlaylistRemove = 0xeb80,
            QueueAdd = 0xe05c,
            QueueNext = 0xe066,
            QueueRemove = 0xe067,
            Block = 0xe14b,
            PlusCircleFilled = 0xfe147,
            PlusCircleUnfilled = 0xe147,
            MinusCircleFilled = 0xfe148,
            MinusCircleUnfilled = 0xfe149,
            CheckCircleFilled = 0xfe86c,
            CheckCircleUnfilled = 0xe86c,
            PlaybackSpeed = 0xe068,
            PlaybackSpeed05 = 0xf4e2,
            PlaybackSpeed08 = 0xff4e2,
            PlaybackSpeed10 = 0xefcd,
            PlaybackSpeed12 = 0xf4e1,
            PlaybackSpeed15 = 0xf4e0,
            PlaybackSpeed18 = 0xff4e0,
            PlaybackSpeed20 = 0xf4eb,
            Settings = 0xe8b8,
            Quality = 0xe429,
            Subtitles = 0xe048,
            SubtitlesOff = 0xef72,
            ClosedCaptions = 0xe01c,
            ClosedCaptionsOff = 0xf1dc,
            Sync = 0xe627,
            Share = 0xe80d,
            VolumeUp = 0xe050,
            VolumeDown = 0xe04d,
            VolumeOff = 0xe04f,
            Artist = 0xe01a,
            Album = 0xe019,
            Radio = 0xe51e,
            Signal = 0xf048,
            Feed = 0xe0e5,
        };
        static constexpr int kSlotCentral = 1, kSlotBack = 2, kSlotForward = 3, kSlotBackSecondary = 4, kSlotForwardSecondary = 5,
                             kSlotOverflow = 6;
        int icon = Undefined;
        std::string iconResource;     // an app drawable (res/drawable) instead of the icon's
        std::string displayName;
        std::string customAction;     // a custom command, or
        std::optional<PlayerCommand> playerCommand;
        std::vector<int> slots;       // in order of preference; empty: getDefaultSlot
        bool enabled = true;
        // DefaultMediaNotificationProvider.COMMAND_KEY_COMPACT_VIEW_INDEX: its place (0-2) in
        // the collapsed notification; -1: by its slot (back, central, forward).
        int compactViewIndex = -1;
        bool operator==(const CommandButton&) const = default;
        int FirstSlot() const;        // slots[0], else the default
    };
    // The drawable media3 ships for an icon (kiln's res), "" for none.
    const char* CommandButtonIconDrawable(int icon);

    struct MediaSessionOptions {
        // The previous / next buttons' actions, instead of the player's SeekToPrevious /
        // SeekToNext (which show the buttons as media3 does: previous with any item, next with a
        // next item). Set, they always show.
        kiln::Function<void()> onSkipToPrevious, onSkipToNext;
        // Tapping the notification opens the app at this deep link (empty: its launch screen).
        std::string sessionActivityUri;
        // The notification and the foreground service (MediaSessionService); false: the session
        // only (controls on connected devices), and playback stops with the app.
        bool notification = true;
        // A command button's custom action, from the notification or a controller (onCustomCommand).
        kiln::Function<void(const std::string& action)> onCustomCommand;
        // The user rated the current item (the controls show the rating kind of its userRating).
        kiln::Function<void(const Rating& rating)> onSetRating;
        // A controller asks to play or prepare media by id, URI or search (Callback.onAddMediaItems):
        // return the items to play (with their URIs), or nullopt to refuse. Unset: refused, as
        // media3's default (a request's URI is only in requestMetadata, not the item's uri).
        kiln::Function<std::optional<std::vector<MediaItem>>(std::vector<MediaItem> requested)> onAddMediaItems;
    };

    // A button as the platform shows it (its drawable resolved).
    struct SessionButton {
        std::string drawable, displayName, customAction;
        int playerCommand = 0;   // PlayerCommand, when not a custom action
        int icon = 0;            // CommandButton::Icon (EXTRAS_KEY_COMMAND_BUTTON_ICON_COMPAT)
        bool operator==(const SessionButton&) const = default;
    };
    // A queue item (QueueItem: MediaDescription of a playlist item; its id is the index).
    struct QueueEntry {
        std::string mediaId, title, subtitle, description, iconUri;
        bool operator==(const QueueEntry&) const = default;
    };

    // What the platform's session and notification show (MediaSession / MediaStyle).
    struct MediaSessionSnapshot {
        MediaMetadata metadata;
        PlaybackState state = PlaybackState::Idle;
        bool playWhenReady = false, isPlaying = false;
        std::int64_t positionMs = 0, durationMs = -1, bufferedPositionMs = 0;
        float playbackSpeed = 1;          // the player's; the platform's state moves at it while playing
        int activeQueueItemId = -1;       // the current item's index (QueueItem id), -1 unknown
        std::string mediaId;              // the current item's (PLAYBACK_STATE_EXTRAS_KEY_MEDIA_ID)
        bool canSkipToPrevious = false, canSkipToNext = false;
        bool canSeek = false;             // there is an item: seek, rewind, fast forward, skip to an item
        // PlaybackState actions (MediaSessionLegacyStub.createPlaybackStateCompat): the player's
        // available commands, set rating always, skip to previous / next unless a custom button
        // takes their slot.
        std::uint64_t actions = 0;
        std::string error, sessionActivityUri;
        // DefaultMediaNotificationProvider.getMediaButtons and the compact view's indices.
        std::vector<SessionButton> notificationButtons;
        std::vector<int> compactView;
        // The custom layout's custom actions (PlaybackState custom actions); a custom back /
        // forward button replaces skip to previous / next.
        std::vector<SessionButton> customActions;
        bool customBack = false, customForward = false;
        std::vector<QueueEntry> queue;
        std::string queueTitle;
        int ratingType = 0;              // RatingCompat style: 0 none, 1 heart, 2 thumbs, 3-5 stars, 6 percentage
        std::optional<Rating> userRating;
        std::map<std::string, std::string> extras;
        bool showNotification = false;   // MediaNotificationManager.shouldShowNotification
        bool foreground = false;         // shouldRunInForeground
        bool operator==(const MediaSessionSnapshot&) const = default;
    };

    // The platform's media session (Android installs it), called on the main thread.
    struct MediaSessionHost {
        kiln::Function<void(int id, const MediaSessionSnapshot& snapshot)> update;
        kiln::Function<void(int id)> release;
    };
    void SetMediaSessionHost(MediaSessionHost host);

    // From the platform's controls (MediaSession.Callback, the notification's actions).
    // positionMs: SeekTo's position, SkipToQueueItem's id; speed: SetPlaybackSpeed's.
    enum class MediaSessionCommand {
        Play, Pause, Stop, SkipToPrevious, SkipToNext, SeekTo, NotificationDismissed,
        Prepare, Rewind, FastForward, SkipToQueueItem, SetPlaybackSpeed,
        PlayerCommand,   // a notification button's player command (positionMs: the PlayerCommand)
    };
    void OnMediaSessionCommand(int id, MediaSessionCommand command, std::int64_t positionMs = 0, float speed = 0);
    void OnMediaSessionCustomAction(int id, const std::string& action);
    void OnMediaSessionSetRating(int id, const Rating& rating);
    // onPlayFrom* / onPrepareFrom*: one of the three is set.
    void OnMediaSessionMediaRequest(int id, const std::string& mediaId, const std::string& uri, const std::string& query, bool play);

    // MediaSessionService.DEFAULT_FOREGROUND_SERVICE_TIMEOUT_MS: how long the service stays in
    // the foreground after the user stops listening (paused).
    inline constexpr std::int64_t kForegroundServiceTimeoutMs = 600'000;

    class MediaSession {
    public:
        MediaSession(Player& player, MediaSessionOptions options = {});
        ~MediaSession();
        MediaSession(const MediaSession&) = delete;
        MediaSession& operator=(const MediaSession&) = delete;

        void SetMetadata(MediaMetadata metadata);
        // setMediaButtonPreferences: the buttons the controls show besides play / pause (a back or
        // forward one replaces previous / next). SetCustomLayout: the same, as media3's older name.
        void SetMediaButtonPreferences(std::vector<CommandButton> buttons);
        void SetCustomLayout(std::vector<CommandButton> buttons) { SetMediaButtonPreferences(std::move(buttons)); }
        const std::vector<CommandButton>& MediaButtonPreferences() const { return m_Buttons; }
        // setSessionExtras: given to controllers.
        void SetSessionExtras(std::map<std::string, std::string> extras);
        const std::map<std::string, std::string>& SessionExtras() const { return m_Extras; }
        // setSessionActivity: the deep link the notification opens (empty: the launch screen).
        void SetSessionActivity(std::string uri);
        const std::string& SessionActivity() const { return m_Options.sessionActivityUri; }
        const MediaMetadata& Metadata() const { return m_Metadata; }
        // What the platform was last given.
        const MediaSessionSnapshot& Snapshot() const { return m_Last; }
        int Id() const { return m_Id; }

    private:
        friend void OnMediaSessionCommand(int, MediaSessionCommand, std::int64_t, float);
        friend void OnMediaSessionCustomAction(int, const std::string&);
        friend void OnMediaSessionSetRating(int, const Rating&);
        friend void OnMediaSessionMediaRequest(int, const std::string&, const std::string&, const std::string&, bool);
        void OnPlayerCommand(PlayerCommand c);
        void OnMediaRequest(MediaItem item, bool play);
        void Publish();
        void OnCommand(MediaSessionCommand c, std::int64_t positionMs, float speed);

        Player& m_Player;
        MediaSessionOptions m_Options;
        MediaMetadata m_Metadata;
        std::vector<CommandButton> m_Buttons;
        std::map<std::string, std::string> m_Extras;
        int m_Id;
        bool m_WasDismissed = false, m_HasBeenPrepared = false, m_UserEngaged = false;
        rt::Job m_State, m_Timeout;
        bool m_TimeoutPending = false;
        MediaSessionSnapshot m_Last;
        rt::Dispatcher::Clock::time_point m_LastAt{};
        bool m_Published = false;
    };

}
