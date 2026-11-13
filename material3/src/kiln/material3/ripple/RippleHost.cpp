#include "kiln/material3/ripple/RippleHost.h"
#include <kiln/Function.h>

#include "kiln/animation/Easing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace kiln::material3::ripple {

    std::int64_t UptimeMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // --- RippleDrawable (patterned) --------------------------------------------------------------

    void RippleDrawable::SetPressedState(bool pressed) {
        // setRippleActive(enabled && pressed): startPatternedAnimation / exitPatternedAnimation
        // take effect at the next draw.
        if (m_RippleActive == pressed) return;
        m_RippleActive = pressed;
        if (pressed) m_AddRipple = true;
        else m_ExitingAnimation = true;
        Invalidate();
    }

    void RippleDrawable::SetHotspot(float x, float y) {
        m_PendingX = x;
        m_PendingY = y;
        m_HasPending = true;   // the patterned style never has an mRipple
    }

    int RippleDrawable::ComputedRadius() const {
        if (m_MaxRadius >= 0) return m_MaxRadius;
        const float halfWidth = m_Width / 2.0f, halfHeight = m_Height / 2.0f;
        return int(std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight));
    }

    void RippleDrawable::SetBounds(int width, int height) {
        if (width == m_Width && height == m_Height) return;
        m_Width = width;
        m_Height = height;
        // onHotspotBoundsChanged
        const float radius = float(ComputedRadius());
        for (Session& s : m_Sessions) {
            s.radius = radius;
            s.width = float(width);
            s.height = float(height);
            s.originX = float(width >> 1);   // Rect.centerX(): (left + right) >> 1
            s.originY = float(height >> 1);
        }
        Invalidate();
    }

    float RippleDrawable::Progress(const Session& s, std::int64_t now) {
        if (s.exitStart && now >= *s.exitStart) {
            const float f = std::clamp(float(now - *s.exitStart) / float(kExitDuration), 0.0f, 1.0f);
            return s.exitFrom + (1.0f - s.exitFrom) * f;   // LINEAR_INTERPOLATOR, to 1
        }
        static const anim::Easing fastOutSlowIn = anim::Easing::CubicBezier(0.4f, 0, 0.2f, 1);   // PathInterpolator
        const float f = std::clamp(float(now - s.start) / float(kEnterDuration), 0.0f, 1.0f);
        return 0.5f * fastOutSlowIn.Transform(f);   // 0 to 0.5
    }

    float RippleDrawable::NoisePhase(const Session& s, std::int64_t now) {
        // The loop animator: linear over NOISE_ANIMATION_DURATION, in float like the render thread.
        const float f = std::clamp(float(now - s.start) / float(kNoiseDuration), 0.0f, 1.0f);
        return s.noiseFrom + (s.noiseTo - s.noiseFrom) * f;
    }

    void RippleDrawable::Draw(ui::DrawScope& s, std::int64_t now) {
        // onSessionEnd removed the sessions whose exit animators finished.
        std::erase_if(m_Sessions, [now](const Session& x) { return x.end && now >= *x.end; });
        s.Save();
        if (m_Bounded) s.ClipRect({ 0, 0, float(m_Width), float(m_Height) });
        const bool addRipple = m_AddRipple, shouldExit = m_ExitingAnimation;
        m_AddRipple = m_ExitingAnimation = false;
        if (addRipple && int(m_Sessions.size()) <= kMaxRipples) {
            Session session;
            if (m_HasPending) {
                session.touchX = m_PendingX;
                session.touchY = m_PendingY;
                m_HasPending = false;
            } else {
                session.touchX = m_Width / 2.0f;   // exactCenterX
                session.touchY = m_Height / 2.0f;
            }
            session.originX = float(m_Width >> 1);
            session.originY = float(m_Height >> 1);
            session.radius = float(ComputedRadius());
            session.width = float(m_Width);
            session.height = float(m_Height);
            session.color = m_Color;
            session.start = now;
            // The noise phase starts at the creation time and runs MAX_NOISE_PHASE (7000 / 214) past it.
            session.noiseFrom = float(now);
            session.noiseTo = float(now + kNoiseDuration / 214);
            m_Sessions.push_back(session);
        }
        if (shouldExit) {
            for (Session& x : m_Sessions) {
                // exitHardware: from the current progress to 1, delayed until the enter finishes.
                const std::int64_t begin = now + std::max<std::int64_t>(kEnterDuration - (now - x.start), 0);
                x.exitFrom = Progress(x, begin);
                x.exitStart = begin;
                // A second exit restarts the animator; the first one's end still ends the session.
                if (!x.end) x.end = begin + kExitDuration;
            }
        }
        for (const Session& x : m_Sessions) {
            ui::DrawScope::Ripple r;
            r.origin = { x.originX, x.originY };
            r.touch = { x.touchX, x.touchY };
            r.radius = x.radius;
            r.progress = Progress(x, now);
            r.maxRadius = x.radius * 2.3f;   // RippleShader.setRadius
            r.resolution = { x.width, x.height };
            r.turbulencePhase = NoisePhase(x, now);
            r.color = x.color;
            s.DrawRipple(r);
        }
        s.Restore();
    }

    // --- RippleHostView --------------------------------------------------------------------------

    void RippleHostView::AddRipple(Offset press, bool bounded, Size size, int radius, Color color, float alpha,
                                   kiln::Function<void()> onInvalidateRipple) {
        // A RippleDrawable cannot change between bounded and unbounded: a new one replaces it.
        if (!m_Ripple || m_Bounded != bounded) {
            m_Ripple = std::make_unique<RippleDrawable>(bounded);
            m_Ripple->invalidate = [this] { if (m_OnInvalidateRipple) m_OnInvalidateRipple(); };
            m_RippleColor = Color::Unspecified();
            m_Bounded = bounded;
        }
        m_OnInvalidateRipple = std::move(onInvalidateRipple);
        SetRippleProperties(size, radius, color, alpha);
        if (bounded) m_Ripple->SetHotspot(press.x, press.y);
        // Unbounded ripples start at the (integer) centre of the bounds.
        else m_Ripple->SetHotspot(float(m_Ripple->Width() >> 1), float(m_Ripple->Height() >> 1));
        SetRippleState(true);
    }

    void RippleHostView::SetRippleProperties(Size size, int radius, Color color, float alpha) {
        if (!m_Ripple) return;
        // The radius goes first: only a bounds change updates running ripples.
        if (m_Ripple->Radius() != radius) m_Ripple->SetRadius(radius);
        // UnprojectedRipple.setColor: the alpha as given (the platform clamps nothing in the
        // patterned style), packed to ARGB.
        const Color c = color.WithAlpha(std::min(alpha, 1.0f));
        if (m_RippleColor != c) {
            m_RippleColor = c;
            m_Ripple->SetColor(c);
        }
        m_Ripple->SetBounds(int(std::lround(size.width)), int(std::lround(size.height)));
    }

    void RippleHostView::RunReset() {
        m_ResetPending = false;
        if (m_Ripple) m_Ripple->SetPressedState(false);
    }

    void RippleHostView::SetRippleState(bool pressed) {
        const std::int64_t now = UptimeMillis();
        if (m_ResetPending) {
            m_Reset.Cancel();
            RunReset();
        }
        const std::int64_t sinceLastChange = now - m_LastStateChange.value_or(0);
        // A release in the same few milliseconds as the press would end the ripple without its
        // minimum duration: post it instead.
        if (!pressed && sinceLastChange < kMinimumRippleStateChangeTime) {
            m_ResetPending = true;
            m_Reset = rt::Launch(rt::Dispatcher::Main(), [](RippleHostView* self) -> rt::Task<> {
                co_await rt::Delay(kResetRippleDelayDuration);
                self->RunReset();
            }(this));
        } else if (m_Ripple) {
            m_Ripple->SetPressedState(pressed);
        }
        m_LastStateChange = now;
    }

    void RippleHostView::DisposeRipple() {
        m_OnInvalidateRipple = nullptr;
        if (m_ResetPending) {
            m_Reset.Cancel();
            RunReset();
        } else if (m_Ripple) {
            m_Ripple->SetPressedState(false);
        }
        if (m_Ripple) m_Ripple->Hide();
        m_Frames.Cancel();
    }

    void RippleHostView::Draw(ui::DrawScope& s) {
        if (!m_Ripple) return;
        m_Ripple->Draw(s, rt::Dispatcher::Main().LastFrameTimeNanos() / 1'000'000);
        // The render thread animates running ripples; here each frame draws them again.
        if (m_Ripple->Animating() && !m_Frames.Active()) {
            m_Frames = rt::Launch(rt::Dispatcher::Main(), [](RippleHostView* self) -> rt::Task<> {
                co_await rt::NextFrame();
                if (self->m_OnInvalidateRipple) self->m_OnInvalidateRipple();
            }(this));
        }
    }

    // --- RippleContainer -------------------------------------------------------------------------

    RippleContainer::RippleContainer() {
        // One host to start with; more are made as nodes need them.
        m_Hosts.push_back(std::make_unique<RippleHostView>());
        m_Unused.push_back(m_Hosts.back().get());
        m_NextHostIndex = 1;
    }

    RippleHostKey* RippleContainer::KeyOf(const RippleHostView* host) const {
        for (const auto& [k, h] : m_Map) if (h == host) return k;
        return nullptr;
    }

    RippleHostView& RippleContainer::GetRippleHostView(RippleHostKey& key) {
        for (const auto& [k, h] : m_Map) if (k == &key) return *h;
        RippleHostView* host = nullptr;
        if (!m_Unused.empty()) {
            host = m_Unused.front();
            m_Unused.erase(m_Unused.begin());
        } else {
            if (m_NextHostIndex > int(m_Hosts.size()) - 1) {
                m_Hosts.push_back(std::make_unique<RippleHostView>());
                host = m_Hosts.back().get();
            } else {
                // Reuse a host round-robin: the node that had it loses its ripple.
                host = m_Hosts[std::size_t(m_NextHostIndex)].get();
                if (RippleHostKey* existing = KeyOf(host)) {
                    existing->OnResetRippleHostView();
                    std::erase_if(m_Map, [existing](const auto& e) { return e.first == existing; });
                    host->DisposeRipple();
                }
            }
            m_NextHostIndex = m_NextHostIndex < kMaxRippleHosts - 1 ? m_NextHostIndex + 1 : 0;
        }
        m_Map.push_back({ &key, host });
        return *host;
    }

    void RippleContainer::DisposeRippleIfNeeded(RippleHostKey& key) {
        key.OnResetRippleHostView();
        auto it = std::find_if(m_Map.begin(), m_Map.end(), [&key](const auto& e) { return e.first == &key; });
        if (it == m_Map.end()) return;
        RippleHostView* host = it->second;
        host->DisposeRipple();
        m_Map.erase(it);
        m_Unused.push_back(host);
    }

    std::shared_ptr<RippleContainer> RippleContainer::For(const void* window) {
        static std::unordered_map<const void*, std::weak_ptr<RippleContainer>> containers;
        std::erase_if(containers, [](const auto& e) { return e.second.expired(); });
        auto& slot = containers[window];
        auto container = slot.lock();
        if (!container) {
            container = std::make_shared<RippleContainer>();
            slot = container;
        }
        return container;
    }

}
