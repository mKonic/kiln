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
#include <string>

namespace kiln::media {

    // What the controls show is the current item's MediaMetadata (kiln/media/MediaItem.h), with
    // SetMetadata's fields over it.

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
    };

    // What the platform's session and notification show (MediaSession / MediaStyle).
    struct MediaSessionSnapshot {
        MediaMetadata metadata;
        PlaybackState state = PlaybackState::Idle;
        bool playWhenReady = false, isPlaying = false;
        std::int64_t positionMs = 0, durationMs = -1;
        bool canSkipToPrevious = false, canSkipToNext = false;
        std::string error, sessionActivityUri;
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
    enum class MediaSessionCommand { Play, Pause, Stop, SkipToPrevious, SkipToNext, SeekTo, NotificationDismissed };
    void OnMediaSessionCommand(int id, MediaSessionCommand command, std::int64_t positionMs = 0);

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
        const MediaMetadata& Metadata() const { return m_Metadata; }
        // What the platform was last given.
        const MediaSessionSnapshot& Snapshot() const { return m_Last; }
        int Id() const { return m_Id; }

    private:
        friend void OnMediaSessionCommand(int, MediaSessionCommand, std::int64_t);
        void Publish();
        void OnCommand(MediaSessionCommand c, std::int64_t positionMs);

        Player& m_Player;
        MediaSessionOptions m_Options;
        MediaMetadata m_Metadata;
        int m_Id;
        bool m_WasDismissed = false, m_HasBeenPrepared = false, m_UserEngaged = false;
        rt::Job m_State, m_Timeout;
        bool m_TimeoutPending = false;
        MediaSessionSnapshot m_Last;
        rt::Dispatcher::Clock::time_point m_LastAt{};
        bool m_Published = false;
    };

}
