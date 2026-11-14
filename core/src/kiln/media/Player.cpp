#include "kiln/media/Player.h"

#include "kiln/runtime/Task.h"

namespace kiln::media {

    namespace {
        kiln::Function<std::unique_ptr<PlayerEngine>()>& Factory() {
            static kiln::Function<std::unique_ptr<PlayerEngine>()> f;
            return f;
        }
    }

    void SetPlayerEngineFactory(kiln::Function<std::unique_ptr<PlayerEngine>()> factory) { Factory() = std::move(factory); }

    // The player's own playWhenReady with audio focus applied over the engine's state
    // (ExoPlayerImplInternal.updatePlayWhenReadyWithAudioFocus), on the main thread.
    struct Player::Shared {
        using Command = AudioFocusManager::PlayerCommand;
        rt::MutableStateFlow<PlayerState> state{ PlayerState{} };
        std::unique_ptr<PlayerEngine> engine;
        PlayerState engineState;
        bool playWhenReady = false;
        PlayWhenReadyChangeReason reason = PlayWhenReadyChangeReason::UserRequest;
        PlaybackSuppression suppression = PlaybackSuppression::None;
        float volume = 1;
        AudioFocusManager focus{ { .setVolumeMultiplier = [this](float) { ApplyVolume(); },
                                   .executePlayerCommand = [this](Command c) { Update(playWhenReady, c, reason); } } };
        AudioBecomingNoisyManager noisy{ [this] { UpdateWithFocus(false, PlayWhenReadyChangeReason::AudioBecomingNoisy); } };

        void ApplyVolume() {
            if (engine) engine->SetVolume(volume * focus.VolumeMultiplier());
        }
        void UpdateWithFocus(bool play, PlayWhenReadyChangeReason why) { Update(play, focus.UpdateAudioFocus(play, engineState.state), why); }
        void Update(bool play, Command c, PlayWhenReadyChangeReason why) {
            play = play && c != Command::DoNotPlay;
            if (c == Command::DoNotPlay) why = PlayWhenReadyChangeReason::AudioFocusLoss;
            else if (why == PlayWhenReadyChangeReason::AudioFocusLoss) why = PlayWhenReadyChangeReason::UserRequest;
            PlaybackSuppression suppressed = suppression;
            if (c == Command::WaitForCallback) suppressed = PlaybackSuppression::TransientAudioFocusLoss;
            else if (suppressed == PlaybackSuppression::TransientAudioFocusLoss) suppressed = PlaybackSuppression::None;
            if (play == playWhenReady && suppressed == suppression && why == reason) return;
            playWhenReady = play;
            reason = why;
            suppression = suppressed;
            if (engine) engine->SetPlayWhenReady(playWhenReady && suppression == PlaybackSuppression::None);
            Publish();
        }
        void OnEngineState(const PlayerState& s) {
            const bool changed = s.state != engineState.state;
            engineState = s;
            // Preparing asks for focus; stopping or failing gives it back.
            if (changed) UpdateWithFocus(playWhenReady, reason);
            Publish();
        }
        void Publish() {
            PlayerState s = engineState;
            s.playWhenReady = playWhenReady;
            s.playWhenReadyChangeReason = reason;
            s.suppression = suppression;
            if (!playWhenReady || suppression != PlaybackSuppression::None) s.isPlaying = false;
            state.SetValue(s);
        }
    };

    Player::Player() : m_Shared(std::make_shared<Shared>()) {
        if (Factory()) m_Shared->engine = Factory()();
        if (!m_Shared->engine) return;
        // Reports from the decoding thread reach the state on this (the main) thread.
        rt::Dispatcher* main = &rt::Dispatcher::Main();
        std::weak_ptr<Shared> weak = m_Shared;
        m_Shared->engine->onState = [main, weak](const PlayerState& s) {
            main->PostFromAnyThread([weak, s] {
                if (auto shared = weak.lock()) shared->OnEngineState(s);
            });
        };
    }

    Player::~Player() = default;

    const PlayerState& Player::State() const { return m_Shared->state.Value(); }
    rt::MutableStateFlow<PlayerState>& Player::StateFlow() { return m_Shared->state; }

    void Player::SetMediaUri(std::string uri) {
        if (m_Shared->engine) {
            m_Shared->engine->SetMediaUri(std::move(uri));
        } else if (!uri.empty()) {
            m_Shared->state.Update([](PlayerState s) {
                s.state = PlaybackState::Idle;
                s.error = "no media engine on this platform";
                return s;
            });
        }
    }
    void Player::Play() { SetPlayWhenReady(true, PlayWhenReadyChangeReason::UserRequest); }
    void Player::Pause() { SetPlayWhenReady(false, PlayWhenReadyChangeReason::UserRequest); }
    void Player::SetPlayWhenReady(bool play, PlayWhenReadyChangeReason reason) { m_Shared->UpdateWithFocus(play, reason); }
    void Player::SeekTo(std::int64_t ms) {
        if (m_Shared->engine) m_Shared->engine->SeekTo(ms < 0 ? 0 : ms);
    }
    void Player::SetRepeat(bool repeat) {
        if (m_Shared->engine) m_Shared->engine->SetRepeat(repeat);
    }
    void Player::SetVolume(float volume) {
        m_Shared->volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
        m_Shared->ApplyVolume();
    }
    void Player::SetSurface(ui::PlatformSurface surface) {
        if (m_Shared->engine) m_Shared->engine->SetSurface(surface);
    }
    void Player::SetAudioAttributes(AudioAttributes attributes, bool handleAudioFocus) {
        m_Shared->focus.SetAudioAttributes(handleAudioFocus ? std::optional(attributes) : std::nullopt);
        if (m_Shared->engine) m_Shared->engine->SetAudioAttributes(attributes);
        m_Shared->UpdateWithFocus(m_Shared->playWhenReady, m_Shared->reason);
    }
    void Player::SetHandleAudioBecomingNoisy(bool handle) { m_Shared->noisy.SetEnabled(handle); }

    FrameRelease DecideFrameRelease(const FrameTiming& t) {
        // shouldForceRelease: the first frame always shows (even paused); later, a late frame
        // shows when none has for 100 ms (MediaCodecVideoRenderer.shouldForceRenderOutputBuffer).
        if (!t.firstFrameRendered) return FrameRelease::Immediately;
        if (!t.started) return FrameRelease::TryAgainLater;
        if (t.earlyUs < kLateThresholdUs && t.sinceLastReleaseUs > 100'000) return FrameRelease::Immediately;
        if (t.earlyUs < kLateThresholdUs && !t.lastFrame) return FrameRelease::Drop;
        if (t.earlyUs > kEarlySchedulingThresholdUs) return FrameRelease::TryAgainLater;
        return FrameRelease::Scheduled;
    }

}
