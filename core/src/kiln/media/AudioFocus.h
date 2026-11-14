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

    // The platform's audio services, called on the main thread. Without a host (the desktop,
    // tests) focus is always granted and nothing becomes noisy.
    struct AudioHost {
        // AudioManager.requestAudioFocus(AudioFocusRequest) for client `id`, accepting delayed
        // gains; returns AUDIOFOCUS_REQUEST_*. Changes come back through OnAudioFocusChange.
        kiln::Function<int(int id, int gain, AudioAttributes attributes, bool willPauseWhenDucked)> requestFocus;
        kiln::Function<void(int id)> abandonFocus;
        // A receiver for ACTION_AUDIO_BECOMING_NOISY for client `id`, or none.
        kiln::Function<void(int id, bool enabled)> setNoisyReceiver;
    };
    void SetAudioHost(AudioHost host);
    // From the platform, on the main thread.
    void OnAudioFocusChange(int id, int focusChange);
    void OnAudioBecomingNoisy(int id);

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
