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

    Player::Player() {
        if (Factory()) m_Engine = Factory()();
        if (!m_Engine) return;
        // Reports from the decoding thread reach the state on this (the main) thread.
        rt::Dispatcher* main = &rt::Dispatcher::Main();
        std::weak_ptr<Shared> weak = m_Shared;
        m_Engine->onState = [main, weak](const PlayerState& s) {
            main->PostFromAnyThread([weak, s] {
                if (auto shared = weak.lock()) shared->state.SetValue(s);
            });
        };
    }

    Player::~Player() = default;

    void Player::SetMediaUri(std::string uri) {
        if (m_Engine) {
            m_Engine->SetMediaUri(std::move(uri));
        } else if (!uri.empty()) {
            m_Shared->state.Update([](PlayerState s) {
                s.state = PlaybackState::Idle;
                s.error = "no media engine on this platform";
                return s;
            });
        }
    }
    void Player::Play() {
        if (m_Engine) m_Engine->SetPlayWhenReady(true);
    }
    void Player::Pause() {
        if (m_Engine) m_Engine->SetPlayWhenReady(false);
    }
    void Player::SeekTo(std::int64_t ms) {
        if (m_Engine) m_Engine->SeekTo(ms < 0 ? 0 : ms);
    }
    void Player::SetRepeat(bool repeat) {
        if (m_Engine) m_Engine->SetRepeat(repeat);
    }
    void Player::SetVolume(float volume) {
        if (m_Engine) m_Engine->SetVolume(volume < 0 ? 0 : volume > 1 ? 1 : volume);
    }
    void Player::SetSurface(ui::PlatformSurface surface) {
        if (m_Engine) m_Engine->SetSurface(surface);
    }

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
