#pragma once

// Animation specs (animation-core AnimationSpec.kt, FloatAnimationSpec.kt, SpringSimulation.kt,
// SpringEstimation.kt). Values are vectors of up to 4 floats (AnimationVector1D..4D); a spring
// runs one simulation per component and lasts as long as the slowest.

#include "kiln/animation/Easing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <limits>
#include <vector>

namespace kiln::anim {

    using Vec = std::array<float, 4>;

    namespace spring {
        constexpr float StiffnessHigh = 10000, StiffnessMedium = 1500, StiffnessMediumLow = 400,
                        StiffnessLow = 200, StiffnessVeryLow = 50;
        constexpr float DampingRatioHighBouncy = 0.2f, DampingRatioMediumBouncy = 0.5f,
                        DampingRatioLowBouncy = 0.75f, DampingRatioNoBouncy = 1.0f;
        constexpr float DefaultDisplacementThreshold = 0.01f;
    }
    constexpr int DefaultDurationMillis = 300;

    // A spec for `dims` components.
    class AnimationSpec {
    public:
        virtual ~AnimationSpec() = default;
        virtual Vec ValueAt(std::int64_t playNanos, const Vec& from, const Vec& to, const Vec& v0, int dims) const = 0;
        virtual Vec VelocityAt(std::int64_t playNanos, const Vec& from, const Vec& to, const Vec& v0, int dims) const = 0;
        virtual std::int64_t DurationNanos(const Vec& from, const Vec& to, const Vec& v0, int dims) const = 0;
        virtual bool Infinite() const { return false; }
    };
    using SpecPtr = std::shared_ptr<const AnimationSpec>;

    // spring(dampingRatio = 1, stiffness = 1500, visibilityThreshold = 0.01 per component)
    SpecPtr Spring(float dampingRatio = spring::DampingRatioNoBouncy, float stiffness = spring::StiffnessMedium,
                   float visibilityThreshold = spring::DefaultDisplacementThreshold);
    // tween(durationMillis = 300, delayMillis = 0, easing = FastOutSlowIn)
    SpecPtr Tween(int durationMillis = DefaultDurationMillis, int delayMillis = 0, Easing easing = FastOutSlowIn());
    SpecPtr Snap(int delayMillis = 0);
    // InterruptionAwareAnimationSpec: `animation` normally, `interruption` when it starts while
    // an animation is already running (Animatable.animateTo, animate*AsState, Transition).
    class InterruptionAwareSpec final : public AnimationSpec {
    public:
        InterruptionAwareSpec(SpecPtr animation, SpecPtr interruption)
            : animationSpec(std::move(animation)), interruptionSpec(interruption ? std::move(interruption) : animationSpec) {}
        Vec ValueAt(std::int64_t t, const Vec& f, const Vec& to, const Vec& v0, int d) const override { return animationSpec->ValueAt(t, f, to, v0, d); }
        Vec VelocityAt(std::int64_t t, const Vec& f, const Vec& to, const Vec& v0, int d) const override { return animationSpec->VelocityAt(t, f, to, v0, d); }
        std::int64_t DurationNanos(const Vec& f, const Vec& to, const Vec& v0, int d) const override { return animationSpec->DurationNanos(f, to, v0, d); }
        const SpecPtr animationSpec, interruptionSpec;
    };
    inline SpecPtr InterruptionAware(SpecPtr animation, SpecPtr interruption = nullptr) {
        return std::make_shared<InterruptionAwareSpec>(std::move(animation), std::move(interruption));
    }
    enum class RepeatMode : std::uint8_t { Restart, Reverse };
    SpecPtr InfiniteRepeatable(SpecPtr duration, RepeatMode mode = RepeatMode::Restart, std::int64_t initialOffsetNanos = 0);

    // The single-dimension spring simulation, exposed for decay/snapping code and tests.
    // keyframes { durationMillis; value at ms using easing }: each keyframe's easing applies to the
    // segment starting at it (default Linear); missing 0 / duration keyframes use the start and
    // target values. Velocity reads as 0, as Compose's keyframes effectively report.
    //
    // ArcMode (keyframe `using arcMode`): from a keyframe on, pairs of components (x, y) travel a
    // quarter ellipse instead of a line, curving below or above the straight path.
    enum class ArcMode : std::uint8_t { Linear = 0, Below = 4, Above = 5 };
    struct Keyframe { int timeMillis; Vec value; Easing easing = Linear(); ArcMode arcMode = ArcMode::Linear; };
    SpecPtr Keyframes(int durationMillis, std::vector<Keyframe> keyframes, int delayMillis = 0);
    // ArcAnimationSpec: from start to target along a quarter ellipse (per component pair).
    SpecPtr ArcAnimation(ArcMode mode = ArcMode::Below, int durationMillis = DefaultDurationMillis, int delayMillis = 0,
                         Easing easing = FastOutSlowIn());
    // keyframesWithSpline: a smooth monotone cubic through the keyframes (each keyframe's easing
    // still shapes the time to the next one). periodicBias in [0, 1], when given, matches the end
    // slopes so a repeating animation loops without a kink.
    SpecPtr KeyframesWithSpline(int durationMillis, std::vector<Keyframe> keyframes, int delayMillis = 0,
                                float periodicBias = std::numeric_limits<float>::quiet_NaN());
    // repeatable(iterations, animation, repeatMode, initialStartOffset) for duration-based specs.
    SpecPtr Repeatable(int iterations, SpecPtr duration, RepeatMode mode = RepeatMode::Restart, std::int64_t initialOffsetNanos = 0);

    // StartOffset(offsetMillis, type): Delay holds the first iteration back, FastForward starts it
    // part-way through; pass the result as initialOffsetNanos.
    enum class StartOffsetType : std::uint8_t { Delay, FastForward };
    constexpr std::int64_t StartOffset(int offsetMillis, StartOffsetType type = StartOffsetType::Delay) {
        return std::int64_t(offsetMillis) * 1000000 * (type == StartOffsetType::Delay ? -1 : 1);
    }

    struct SpringMotion { float value, velocity; };
    SpringMotion SpringStep(float dampingRatio, float stiffness, float from, float velocity, float target, double seconds);
    std::int64_t EstimateSpringDurationNanos(float stiffness, float dampingRatio, float initialVelocity,
                                             float initialDisplacement, float delta);

}
