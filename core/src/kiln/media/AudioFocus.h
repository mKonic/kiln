#pragma once

// Audio focus and "becoming noisy" (media3 common/audio AudioFocusManager and
// AudioBecomingNoisyManager): a Player that handles audio focus asks for it before it plays,
// pauses when another app takes it, waits through a call or a navigation prompt and ducks under a
// notification; with becoming-noisy handling it pauses when the headphones are unplugged.
//
//   player.SetAudioAttributes({ .usage = media::AudioUsage::Media, .contentType = media::AudioContentType::Music }, true);
//   player.SetHandleAudioBecomingNoisy(true);

#include <kiln/Function.h>

#include <optional>

namespace kiln::media {

    // AudioAttributes.USAGE_* (the platform's values).
    enum class AudioUsage {
        Unknown = 0,
        Media = 1,
        VoiceCommunication = 2,
        VoiceCommunicationSignalling = 3,
        Alarm = 4,
        Notification = 5,
        NotificationRingtone = 6,
        NotificationCommunicationRequest = 7,
        NotificationCommunicationInstant = 8,
        NotificationCommunicationDelayed = 9,
        NotificationEvent = 10,
        AssistanceAccessibility = 11,
        AssistanceNavigationGuidance = 12,
        AssistanceSonification = 13,
        Game = 14,
        Assistant = 16,
    };
    // AudioAttributes.CONTENT_TYPE_*.
    enum class AudioContentType { Unknown = 0, Speech = 1, Music = 2, Movie = 3, Sonification = 4 };

    struct AudioAttributes {
        AudioUsage usage = AudioUsage::Media;
        AudioContentType contentType = AudioContentType::Unknown;
        // C.ALLOW_CAPTURE_BY_*: who may record the sound (1 all, 2 the system, 3 no one).
        int allowedCapturePolicy = 1;
        // C.SPATIALIZATION_BEHAVIOR_*: 0 auto (spatialized when the device can), 1 never.
        int spatializationBehavior = 0;
        bool operator==(const AudioAttributes&) const = default;
    };

    // AudioManager's values.
    inline constexpr int kAudioFocusNone = 0, kAudioFocusGain = 1, kAudioFocusGainTransient = 2,
                         kAudioFocusGainTransientMayDuck = 3, kAudioFocusGainTransientExclusive = 4;
    inline constexpr int kAudioFocusLoss = -1, kAudioFocusLossTransient = -2, kAudioFocusLossTransientCanDuck = -3;
    inline constexpr int kAudioFocusRequestFailed = 0, kAudioFocusRequestGranted = 1, kAudioFocusRequestDelayed = 2;

    // convertAudioAttributesToFocusGain: the focus a usage asks for (none without attributes).
    int FocusGainFor(const std::optional<AudioAttributes>& attributes);

    enum class PlaybackState;

    // AudioFocusManager: decides, for the player's playWhenReady and state, whether it may play,
    // and follows the platform's focus changes.
    class AudioFocusManager {
    public:
        enum class PlayerCommand { DoNotPlay = -1, WaitForCallback = 0, PlayWhenReady = 1 };
        static constexpr float kVolumeMultiplierDuck = 0.2f;

        struct Control {
            kiln::Function<void(float)> setVolumeMultiplier;
            kiln::Function<void(PlayerCommand)> executePlayerCommand;
        };
        explicit AudioFocusManager(Control control);
        ~AudioFocusManager();
        AudioFocusManager(const AudioFocusManager&) = delete;
        AudioFocusManager& operator=(const AudioFocusManager&) = delete;

        float VolumeMultiplier() const { return m_VolumeMultiplier; }
        // Null: the player does not handle audio focus. Only media and game usages may be
        // handled automatically (as media3 checks).
        void SetAudioAttributes(std::optional<AudioAttributes> attributes);
        PlayerCommand UpdateAudioFocus(bool playWhenReady, PlaybackState state);
        void Release();
        // The platform's focus listener (AudioManager.AUDIOFOCUS_*).
        void OnFocusChange(int focusChange);

    private:
        enum class State { NotRequested, NoFocus, HaveFocus, LossTransient, LossTransientDuck };
        PlayerCommand RequestAudioFocus();
        void AbandonAudioFocusIfHeld();
        void SetState(State s);
        void Execute(PlayerCommand c);
        bool WillPauseWhenDucked() const;

        Control m_Control;
        std::optional<AudioAttributes> m_Attributes;
        State m_State = State::NotRequested;
        int m_FocusGain = kAudioFocusNone;
        float m_VolumeMultiplier = 1;
        int m_Id;
    };

    struct StreamVolume {
        int volume = 0;
        bool muted = false;
        int minVolume = 0, maxVolume = 0;
        bool operator==(const StreamVolume&) const = default;
    };
    // AudioManager.STREAM_*, ADJUST_* and C.VOLUME_FLAG_*.
    inline constexpr int kStreamVoiceCall = 0, kStreamSystem = 1, kStreamRing = 2, kStreamMusic = 3, kStreamAlarm = 4, kStreamNotification = 5,
                         kStreamDtmf = 8, kStreamAccessibility = 10;
    inline constexpr int kAdjustLower = -1, kAdjustRaise = 1, kAdjustMute = -100, kAdjustUnmute = 100;
    inline constexpr int kVolumeFlagShowUi = 1, kVolumeFlagAllowRingerModes = 2, kVolumeFlagPlaySound = 4, kVolumeFlagRemoveSoundAndVibrate = 8,
                         kVolumeFlagVibrate = 16;
    // AudioAttributes.getVolumeControlStream (Util.getStreamTypeForAudioUsage).
    int VolumeControlStream(const AudioAttributes& attributes);

    // The platform's audio services, called on the main thread. Without a host (the desktop,
    // tests) focus is always granted and nothing becomes noisy.
    struct AudioHost {
        // AudioManager.requestAudioFocus(AudioFocusRequest) for client `id`, accepting delayed
        // gains; returns AUDIOFOCUS_REQUEST_*. Changes come back through OnAudioFocusChange.
        kiln::Function<int(int id, int gain, AudioAttributes attributes, bool willPauseWhenDucked)> requestFocus;
        kiln::Function<void(int id)> abandonFocus;
        // A receiver for ACTION_AUDIO_BECOMING_NOISY for client `id`, or none.
        kiln::Function<void(int id, bool enabled)> setNoisyReceiver;
        // A stream's volume (AudioManager.getStreamVolume, isStreamMute, getStreamMinVolume,
        // getStreamMaxVolume), setStreamVolume and adjustStreamVolume (ADJUST_*), and a receiver
        // for its changes (VOLUME_CHANGED_ACTION, reported through OnStreamVolumeChanged).
        kiln::Function<StreamVolume(int stream)> streamVolume;
        kiln::Function<void(int stream, int volume, int flags)> setStreamVolume;
        kiln::Function<void(int stream, int direction, int flags)> adjustStreamVolume;
        kiln::Function<void(int id, bool enabled)> setVolumeChangedReceiver;
        // A partial wake lock and a high-performance Wi-Fi lock held for client `id`, or not.
        kiln::Function<void(int id, bool wake, bool wifi)> setLocks;
    };
    void SetAudioHost(AudioHost host);
    // From the platform, on the main thread.
    void OnAudioFocusChange(int id, int focusChange);
    void OnAudioBecomingNoisy(int id);
    void OnStreamVolumeChanged(int id);
    // A new client id for the host's per-client calls, and the playback locks it holds.
    int NewAudioClientId();
    void SetPlaybackLocks(int id, bool wake, bool wifi);

    // StreamVolumeManager: a stream's volume on the device, followed while it lives.
    class StreamVolumeManager {
    public:
        // onChanged(volume, muted) and onStreamTypeChanged as media3's listener.
        StreamVolumeManager(int streamType, kiln::Function<void(int, bool)> onVolumeChanged, kiln::Function<void(int)> onStreamTypeChanged);
        ~StreamVolumeManager();
        StreamVolumeManager(const StreamVolumeManager&) = delete;
        StreamVolumeManager& operator=(const StreamVolumeManager&) = delete;
        void SetStreamType(int streamType);
        int StreamType() const { return m_State.streamType; }
        int MinVolume() const { return m_State.volume.minVolume; }
        int MaxVolume() const { return m_State.volume.maxVolume; }
        int Volume() const { return m_State.volume.volume; }
        bool IsMuted() const { return m_State.volume.muted; }
        void SetVolume(int volume, int flags);
        void IncreaseVolume(int flags);
        void DecreaseVolume(int flags);
        void SetMuted(bool muted, int flags);
        void OnSystemChange();   // the receiver heard a change

    private:
        struct State { int streamType = kStreamMusic; StreamVolume volume; };
        State Generate(int streamType) const;
        void Apply(State next);
        State m_State;
        int m_VolumeBeforeMute = 0;
        kiln::Function<void(int, bool)> m_OnVolume;
        kiln::Function<void(int)> m_OnStreamType;
        int m_Id;
    };

    // AudioBecomingNoisyManager: calls back when audio is about to come out of the speaker
    // (headphones unplugged, a Bluetooth headset gone), while enabled.
    class AudioBecomingNoisyManager {
    public:
        explicit AudioBecomingNoisyManager(kiln::Function<void()> onAudioBecomingNoisy);
        ~AudioBecomingNoisyManager();
        AudioBecomingNoisyManager(const AudioBecomingNoisyManager&) = delete;
        AudioBecomingNoisyManager& operator=(const AudioBecomingNoisyManager&) = delete;
        void SetEnabled(bool enabled);
        bool Enabled() const { return m_Enabled; }

    private:
        kiln::Function<void()> m_Listener;
        bool m_Enabled = false;
        int m_Id;
    };

}
