#pragma once

// Playing audio and video: a Player in media3's model (Player, ExoPlayer's renderers), decoding
// on its own thread. On Android it reads with AMediaExtractor, decodes with AMediaCodec into the
// surface of a VideoSurface (kiln/foundation/VideoSurface.h) and plays sound through AAudio, the
// video's frames released against the audio clock the way media3's VideoFrameReleaseControl
// releases them.
//
//   auto& player = c.RememberNew<media::Player>({}, {});   // released when it leaves the composition
//   player.SetMediaUri("asset:///intro.mp4");
//   player.Play();
//   VideoSurface(FillMax(Modifier{}, Axis::Horizontal), player);
//   const media::PlayerState& s = rt::CollectAsState(player.StateFlow(), ...);
//
// Sources: asset:///path, file:///path or an absolute path, content:// URIs, http(s):// URLs
// (android.permission.INTERNET). Where there is no engine (the desktop), the state reports an
// error.

#include "kiln/media/AudioFocus.h"
#include "kiln/runtime/Flow.h"
#include "kiln/ui/ExternalSurface.h"
#include <kiln/Function.h>

#include <cstdint>
#include <memory>
#include <string>

namespace kiln::media {

    // Player.STATE_*.
    enum class PlaybackState {
        Idle,        // no media, or it failed (error)
        Buffering,   // preparing, or catching up after a seek
        Ready,       // can play at once
        Ended,       // played to the end
    };

    // Player.PLAYBACK_SUPPRESSION_REASON_*: why a player that would play does not.
    enum class PlaybackSuppression { None, TransientAudioFocusLoss };

    // Player.PLAY_WHEN_READY_CHANGE_REASON_*: what last changed playWhenReady.
    enum class PlayWhenReadyChangeReason { UserRequest, AudioFocusLoss, AudioBecomingNoisy };

    struct PlayerState {
        PlaybackState state = PlaybackState::Idle;
        bool playWhenReady = false;   // Play() was called (and not Pause(), nor lost to another app)
        PlayWhenReadyChangeReason playWhenReadyChangeReason = PlayWhenReadyChangeReason::UserRequest;
        PlaybackSuppression suppression = PlaybackSuppression::None;
        bool isPlaying = false;       // ready, playing and not suppressed
        std::int64_t positionMs = 0;
        std::int64_t durationMs = -1; // unknown
        bool hasVideo = false, hasAudio = false;
        int videoWidth = 0, videoHeight = 0;
        int rotationDegrees = 0;      // how the video is meant to be turned (clockwise)
        std::string error;            // why playback stopped, when it failed
        bool operator==(const PlayerState&) const = default;
    };

    // What decodes: the platform's engine. Its methods are called on the main thread; it reports
    // its state from any thread.
    class PlayerEngine {
    public:
        virtual ~PlayerEngine() = default;
        virtual void SetMediaUri(std::string uri) = 0;
        virtual void SetPlayWhenReady(bool play) = 0;
        virtual void SeekTo(std::int64_t positionMs) = 0;
        virtual void SetRepeat(bool repeat) = 0;
        virtual void SetVolume(float volume) = 0;
        // The stream's usage and content type (AAudio's), before it opens.
        virtual void SetAudioAttributes(AudioAttributes) {}
        // The video's output; nativeWindow null: none (frames are decoded and dropped).
        virtual void SetSurface(ui::PlatformSurface surface) = 0;
        kiln::Function<void(const PlayerState&)> onState;
    };
    // The platform's engines (Android installs one; tests install their own).
    void SetPlayerEngineFactory(kiln::Function<std::unique_ptr<PlayerEngine>()> factory);

    class Player {
    public:
        Player();
        ~Player();
        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        // Prepares the media (Buffering, then Ready); empty stops and clears it.
        void SetMediaUri(std::string uri);
        void Play();
        void Pause();
        void SeekTo(std::int64_t positionMs);
        // Starts again from the beginning at the end.
        void SetRepeat(bool repeat);
        // 0 to 1.
        void SetVolume(float volume);
        void SetSurface(ui::PlatformSurface surface);
        // What the sound is; with handleAudioFocus the player asks for audio focus before it
        // plays and follows its changes (ExoPlayer.setAudioAttributes). Only media and game
        // usages can be handled.
        void SetAudioAttributes(AudioAttributes attributes, bool handleAudioFocus);
        // Pauses when audio would come out of the speaker (headphones unplugged).
        void SetHandleAudioBecomingNoisy(bool handle);

        // The engine's state, updated on the main thread (the position about four times a second
        // while it plays).
        const PlayerState& State() const;
        rt::MutableStateFlow<PlayerState>& StateFlow();
        // Play() / Pause() from elsewhere (a media session's buttons): the reason they report.
        void SetPlayWhenReady(bool playWhenReady, PlayWhenReadyChangeReason reason);

    private:
        struct Shared;
        std::shared_ptr<Shared> m_Shared;
    };

    // VideoFrameReleaseControl.getFrameReleaseAction with MediaCodecVideoRenderer's thresholds:
    // what to do with a decoded frame that is earlyUs ahead of the clock (negative: late).
    enum class FrameRelease {
        Immediately,     // show it now (the first frame, or long without one)
        Scheduled,       // show it at now + earlyUs
        Drop,            // too late
        TryAgainLater,   // too early (or paused after the first frame)
    };
    inline constexpr std::int64_t kEarlySchedulingThresholdUs = 50'000;
    inline constexpr std::int64_t kLateThresholdUs = -30'000;
    struct FrameTiming {
        std::int64_t earlyUs = 0;
        bool started = false;              // the clock runs (playing)
        bool firstFrameRendered = false;   // since the media was set or seeked
        bool lastFrame = false;            // the stream's last (never dropped)
        std::int64_t sinceLastReleaseUs = 0;
    };
    FrameRelease DecideFrameRelease(const FrameTiming& t);

}
