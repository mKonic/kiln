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
//
// A playlist follows media3's Player: SetMediaItems, then Prepare (SetMediaUri is one item,
// prepared); the items play one after another, with repeat and shuffle modes, next / previous
// as BasePlayer has them, and playback speed.

#include "kiln/media/AudioFocus.h"
#include "kiln/media/MediaItem.h"
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

    // Player.REPEAT_MODE_*.
    enum class RepeatMode { Off, One, All };
    // Player.MEDIA_ITEM_TRANSITION_REASON_*.
    enum class MediaItemTransitionReason { Repeat, Auto, Seek, PlaylistChanged };
    // Player.DISCONTINUITY_REASON_*.
    enum class DiscontinuityReason { AutoTransition, Seek, SeekAdjustment, Skip, Remove, Internal, SilenceSkip };

    // Player.PositionInfo: where playback was or is.
    struct PositionInfo {
        int mediaItemIndex = 0;
        std::int64_t positionMs = 0;
        std::string mediaId;
        bool operator==(const PositionInfo&) const = default;
    };

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
        VideoSize videoSize;          // as decoded (videoWidth / videoHeight, with the pixel aspect ratio)
        std::string error;            // why playback stopped, when it failed (playerError's message)
        std::optional<PlaybackException> playerError;
        bool isLoading = false;               // the source is being read
        std::int64_t bufferedPositionMs = 0;  // how far it is read ahead (in the current item)
        // The playlist (getCurrentMediaItemIndex, getMediaItemCount ...).
        int currentMediaItemIndex = 0;
        int mediaItemCount = 0;
        std::string currentMediaId;
        MediaMetadata mediaMetadata;          // the current item's (getMediaMetadata)
        bool hasNextMediaItem = false, hasPreviousMediaItem = false;
        RepeatMode repeatMode = RepeatMode::Off;
        bool shuffleModeEnabled = false;
        PlaybackParameters playbackParameters;
        bool operator==(const PlayerState&) const = default;
    };

    // Player.Listener's discrete events (the rest of its callbacks are PlayerState changes).
    struct PlayerListener {
        kiln::Function<void(const std::optional<MediaItem>& item, MediaItemTransitionReason reason)> onMediaItemTransition;
        kiln::Function<void(const PositionInfo& oldPosition, const PositionInfo& newPosition, DiscontinuityReason reason)> onPositionDiscontinuity;
        kiln::Function<void(const PlaybackException& error)> onPlayerError;
        kiln::Function<void(const MediaMetadata& metadata)> onPlaylistMetadataChanged;
    };

    // One item for the engine to play (empty uri: stop and release).
    struct EngineLoad {
        std::string uri, mimeType;
        std::int64_t startPositionMs = 0;                 // where to start (in the media)
        std::int64_t clipStartMs = 0;                     // where a repeat starts again
        std::int64_t endPositionMs = kTimeEndOfSource;   // the clipped end
        std::uint64_t loadId = 0;                         // reported back with its states
    };

    // What decodes: the platform's engine. Its methods are called on the main thread; it reports
    // its state from any thread.
    class PlayerEngine {
    public:
        virtual ~PlayerEngine() = default;
        virtual void Load(EngineLoad load) = 0;
        virtual void SetPlayWhenReady(bool play) = 0;
        virtual void SeekTo(std::int64_t positionMs) = 0;
        virtual void SetRepeat(bool repeat) = 0;
        virtual void SetVolume(float volume) = 0;
        // The stream's usage and content type (AAudio's), before it opens.
        virtual void SetAudioAttributes(AudioAttributes) {}
        // The video's output; nativeWindow null: none (frames are decoded and dropped).
        virtual void SetSurface(ui::PlatformSurface surface) = 0;
        // Speed (the audio time-stretched, its pitch kept unless pitch says otherwise).
        virtual void SetPlaybackParameters(PlaybackParameters) {}
        // The state of the load it belongs to; looped: repeat started the item again.
        kiln::Function<void(const PlayerState&, std::uint64_t loadId)> onState;
        kiln::Function<void(std::uint64_t loadId)> onLooped;
    };
    // The platform's engines (Android installs one; tests install their own).
    void SetPlayerEngineFactory(kiln::Function<std::unique_ptr<PlayerEngine>()> factory);

    class Player {
    public:
        Player();
        ~Player();
        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        // One item, prepared (setMediaItem + prepare); empty clears the playlist.
        void SetMediaUri(std::string uri);

        // --- the playlist (Player's) ---------------------------------------------------------
        // resetPosition: start at the first item's default position; otherwise keep the current
        // index and position.
        void SetMediaItems(std::vector<MediaItem> items, bool resetPosition = true);
        // startIndex kIndexUnset with startPositionMs kTimeUnset: as resetPosition false.
        void SetMediaItems(std::vector<MediaItem> items, int startIndex, std::int64_t startPositionMs);
        void SetMediaItem(MediaItem item, bool resetPosition = true);
        void SetMediaItem(MediaItem item, std::int64_t startPositionMs);
        void AddMediaItem(MediaItem item);
        void AddMediaItem(int index, MediaItem item);
        void AddMediaItems(std::vector<MediaItem> items);
        void AddMediaItems(int index, std::vector<MediaItem> items);
        void MoveMediaItem(int currentIndex, int newIndex);
        void MoveMediaItems(int fromIndex, int toIndex, int newIndex);
        void ReplaceMediaItem(int index, MediaItem item);
        void ReplaceMediaItems(int fromIndex, int toIndex, std::vector<MediaItem> items);
        void RemoveMediaItem(int index);
        void RemoveMediaItems(int fromIndex, int toIndex);
        void ClearMediaItems();
        int MediaItemCount() const;
        const MediaItem& MediaItemAt(int index) const;
        int CurrentMediaItemIndex() const;
        std::optional<MediaItem> CurrentMediaItem() const;
        int NextMediaItemIndex() const;       // kIndexUnset at the end (with the repeat mode)
        int PreviousMediaItemIndex() const;
        bool HasNextMediaItem() const;
        bool HasPreviousMediaItem() const;
        void SetPlaylistMetadata(MediaMetadata metadata);
        const MediaMetadata& PlaylistMetadata() const;

        // Loads the playlist (Idle to Buffering; Ended when it is empty); Stop goes back to Idle,
        // keeping the playlist and the position.
        void Prepare();
        void Stop();
        void Play();
        void Pause();

        // --- seeking (BasePlayer's) ----------------------------------------------------------
        void SeekTo(std::int64_t positionMs);
        void SeekTo(int mediaItemIndex, std::int64_t positionMs);
        void SeekToDefaultPosition();
        void SeekToDefaultPosition(int mediaItemIndex);
        void SeekBack();
        void SeekForward();
        void SeekToNextMediaItem();
        void SeekToPreviousMediaItem();
        // The next item, or nothing at the end.
        void SeekToNext();
        // The previous item within the first MaxSeekToPreviousPositionMs of this one; otherwise
        // the start of this one.
        void SeekToPrevious();
        void SetSeekBackIncrementMs(std::int64_t ms);       // 5000
        void SetSeekForwardIncrementMs(std::int64_t ms);    // 15000
        void SetMaxSeekToPreviousPositionMs(std::int64_t ms);   // 3000
        std::int64_t SeekBackIncrementMs() const;
        std::int64_t SeekForwardIncrementMs() const;
        std::int64_t MaxSeekToPreviousPositionMs() const;

        void SetRepeatMode(RepeatMode mode);
        RepeatMode GetRepeatMode() const;
        // Starts again at the end: RepeatMode One, or Off.
        void SetRepeat(bool repeat);
        void SetShuffleModeEnabled(bool enabled);
        bool ShuffleModeEnabled() const;
        // ExoPlayer.setShuffleOrder: its length must be the playlist's.
        void SetShuffleOrder(ShuffleOrder order);

        void SetPlaybackParameters(PlaybackParameters parameters);
        void SetPlaybackSpeed(float speed);
        const PlaybackParameters& GetPlaybackParameters() const;

        // 0 to 1.
        void SetVolume(float volume);
        float Volume() const;
        void Mute();     // volume 0, remembering it
        void Unmute();   // the volume before Mute

        int AddListener(PlayerListener listener);
        void RemoveListener(int id);
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
