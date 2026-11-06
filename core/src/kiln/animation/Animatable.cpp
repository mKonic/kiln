#include "kiln/animation/Animatable.h"

#include <algorithm>

namespace kiln::anim {

    namespace { float g_Scale = 1.0f; }
    float DurationScale() { return g_Scale; }
    void SetDurationScale(float s) { g_Scale = s < 0 ? 0 : s; }

    Animatable::Animatable(Vec initial, int dims) : m_Value(initial), m_Target(initial), m_Dims(dims) {}

    Vec Animatable::Clamp(const Vec& v, bool& clamped) const {
        Vec out = v;
        clamped = false;
        for (int i = 0; i < m_Dims; ++i) {
            if (m_Lower && out[i] < (*m_Lower)[i]) { out[i] = (*m_Lower)[i]; clamped = true; }
            if (m_Upper && out[i] > (*m_Upper)[i]) { out[i] = (*m_Upper)[i]; clamped = true; }
        }
        return out;
    }

    void Animatable::SnapTo(Vec value) {
        ++m_Generation;
        bool c;
        m_Value.Set(Clamp(value, c));
        m_Target = m_Value.Peek();
        m_Velocity = {};
        m_Running.Set(false);
    }

    void Animatable::Stop() {
        ++m_Generation;
        m_Velocity = {};
        m_Running.Set(false);
    }

    rt::Task<AnimationResult> Animatable::AnimateDecay(float v0, std::shared_ptr<const DecaySpec> decay,
                                                        std::function<void(float)> onFrame) {
        const std::uint64_t gen = ++m_Generation;
        const float from = m_Value.Peek()[0];
        const std::int64_t duration = decay->DurationNanos(from, v0);
        m_Target = { decay->TargetValue(from, v0), 0, 0, 0 };
        m_Running.Set(true);
        std::int64_t start = -1;
        for (;;) {
            const std::int64_t frame = co_await rt::NextFrame{};
            if (gen != m_Generation) co_return { EndReason::Interrupted, m_Value.Peek(), m_Velocity };
            if (start < 0) start = frame;
            // Flings ignore the animator duration scale (DefaultFlingBehavior runs with scale 1).
            const std::int64_t play = frame - start;
            const bool done = play >= duration;
            float value = done ? m_Target[0] : decay->ValueAt(play, from, v0);
            float velocity = done ? 0.0f : decay->VelocityAt(play, from, v0);
            bool clamped;
            Vec v = Clamp({ value, 0, 0, 0 }, clamped);
            m_Velocity = { clamped ? 0.0f : velocity, 0, 0, 0 };
            m_Value.Set(v);
            if (onFrame) onFrame(v[0]);
            if (gen != m_Generation) co_return { EndReason::Interrupted, v, m_Velocity };
            if (clamped) { m_Running.Set(false); co_return { EndReason::BoundReached, v, {} }; }
            if (done) { m_Running.Set(false); co_return { EndReason::Finished, v, {} }; }
        }
    }

    rt::Task<AnimationResult> Animatable::AnimateTo(Vec target, SpecPtr spec, std::optional<Vec> initialVelocity,
                                                     std::function<void(const Vec&)> onFrame) {
        // An interruption-aware spec picks its interruption spec when we are already running.
        if (auto* aware = dynamic_cast<const InterruptionAwareSpec*>(spec.get()))
            spec = m_Running.Peek() ? aware->interruptionSpec : aware->animationSpec;
        const std::uint64_t gen = ++m_Generation;
        const Vec from = m_Value.Peek();
        const Vec v0 = initialVelocity.value_or(m_Velocity);
        m_Target = target;
        m_Running.Set(true);
        const std::int64_t duration = spec->DurationNanos(from, target, v0, m_Dims);
        std::int64_t start = -1;
        for (;;) {
            const std::int64_t frame = co_await rt::NextFrame{};
            if (gen != m_Generation) co_return { EndReason::Interrupted, m_Value.Peek(), m_Velocity };
            if (start < 0) start = frame;
            const float scale = DurationScale();
            const bool finished = scale == 0 || (!spec->Infinite() && double(frame - start) / scale >= double(duration));
            if (finished) {
                bool clamped;
                m_Value.Set(Clamp(target, clamped));
                m_Velocity = {};
                if (onFrame) onFrame(m_Value.Peek());
                m_Running.Set(false);
                co_return { EndReason::Finished, m_Value.Peek(), {} };
            }
            const std::int64_t play = std::int64_t(double(frame - start) / scale);
            bool clamped;
            const Vec v = Clamp(spec->ValueAt(play, from, target, v0, m_Dims), clamped);
            m_Velocity = spec->VelocityAt(play, from, target, v0, m_Dims);
            m_Value.Set(v);
            if (onFrame) onFrame(v);
            if (clamped) {
                m_Velocity = {};
                m_Running.Set(false);
                co_return { EndReason::BoundReached, v, {} };
            }
        }
    }

}
