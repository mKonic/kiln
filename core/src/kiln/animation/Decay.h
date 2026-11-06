#pragma once

// Decay (fling) animation: Android's spline (animation FlingCalculator.kt + AndroidFlingSpline,
// friction from ViewConfiguration.getScrollFriction) and the exponential decay.

#include <cstdint>

namespace kiln::anim {

    class DecaySpec {
    public:
        virtual ~DecaySpec() = default;
        virtual float TargetValue(float initialValue, float initialVelocity) const = 0;
        virtual float ValueAt(std::int64_t playNanos, float initialValue, float initialVelocity) const = 0;
        virtual float VelocityAt(std::int64_t playNanos, float initialValue, float initialVelocity) const = 0;
        virtual std::int64_t DurationNanos(float initialValue, float initialVelocity) const = 0;
        virtual float AbsVelocityThreshold() const { return 0; }
    };

    // splineBasedDecay(density): the platform scroller's fling curve.
    class SplineDecay final : public DecaySpec {
    public:
        SplineDecay(float density, float scrollFriction = 0.015f);
        float TargetValue(float v0, float vel) const override { return v0 + Distance(vel); }
        float ValueAt(std::int64_t t, float v0, float vel) const override;
        float VelocityAt(std::int64_t t, float v0, float vel) const override;
        std::int64_t DurationNanos(float, float vel) const override;
        float Distance(float velocity) const;   // signed

    private:
        double SplineDeceleration(float velocity) const;
        float m_Friction, m_Magic;
    };

    // exponentialDecay(frictionMultiplier = 1, absVelocityThreshold = 0.1)
    class ExponentialDecay final : public DecaySpec {
    public:
        explicit ExponentialDecay(float frictionMultiplier = 1, float absVelocityThreshold = 0.1f)
            : m_Friction(frictionMultiplier * -4.2f), m_Threshold(absVelocityThreshold) {}
        float TargetValue(float v0, float vel) const override;
        float ValueAt(std::int64_t t, float v0, float vel) const override;
        float VelocityAt(std::int64_t t, float v0, float vel) const override;
        std::int64_t DurationNanos(float v0, float vel) const override;
        float AbsVelocityThreshold() const override { return m_Threshold; }
    private:
        float m_Friction, m_Threshold;
    };

}
