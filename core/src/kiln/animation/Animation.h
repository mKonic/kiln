#pragma once

// The low-level animation pieces of animation-core (Animation.kt, AnimationState.kt,
// SuspendAnimation.kt, DeferredTargetAnimation.kt):
//  - Animation: a stateless animation from a start to its end, sampled by play time
//    (TargetBasedAnimation for specs, DecayAnimation for decays).
//  - AnimationState: a value and velocity that animateTo / animateDecay update frame by frame,
//    with an AnimationScope the frame block sees (and can cancel the animation from).
//  - DeferredTargetAnimation: an animation whose target is only known during measure or
//    placement; updateTarget starts (or retargets) it and returns the value to use now.

#include "kiln/animation/Animatable.h"
#include <kiln/Function.h>
#include "kiln/animation/Decay.h"
#include "kiln/animation/Spec.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::anim {

    class Animation {
    public:
        virtual ~Animation() = default;
        virtual std::int64_t DurationNanos() const = 0;
        virtual Vec ValueFromNanos(std::int64_t playTimeNanos) const = 0;
        // getVelocityFromNanos / getVelocityVectorFromNanos.
        virtual Vec VelocityFromNanos(std::int64_t playTimeNanos) const = 0;
        virtual Vec TargetValue() const = 0;
        virtual bool IsInfinite() const { return false; }
        bool IsFinishedFromNanos(std::int64_t playTimeNanos) const { return playTimeNanos >= DurationNanos(); }
    };

    // TargetBasedAnimation(animationSpec, initialValue, targetValue, initialVelocity).
    class TargetBasedAnimation final : public Animation {
    public:
        TargetBasedAnimation(SpecPtr spec, Vec initialValue, Vec targetValue, Vec initialVelocity = {}, int dims = 1)
            : m_Spec(std::move(spec)), m_Initial(initialValue), m_Target(targetValue), m_V0(initialVelocity), m_Dims(dims),
              m_Duration(m_Spec->DurationNanos(m_Initial, m_Target, m_V0, m_Dims)) {}
        TargetBasedAnimation(SpecPtr spec, float initialValue, float targetValue, float initialVelocity = 0)
            : TargetBasedAnimation(std::move(spec), Vec{ initialValue }, Vec{ targetValue }, Vec{ initialVelocity }, 1) {}
        std::int64_t DurationNanos() const override { return m_Duration; }
        Vec ValueFromNanos(std::int64_t t) const override { return IsFinishedFromNanos(t) && !IsInfinite() ? m_Target : m_Spec->ValueAt(t, m_Initial, m_Target, m_V0, m_Dims); }
        Vec VelocityFromNanos(std::int64_t t) const override { return IsFinishedFromNanos(t) && !IsInfinite() ? Vec{} : m_Spec->VelocityAt(t, m_Initial, m_Target, m_V0, m_Dims); }
        Vec TargetValue() const override { return m_Target; }
        bool IsInfinite() const override { return m_Spec->Infinite(); }
        Vec InitialValue() const { return m_Initial; }

    private:
        SpecPtr m_Spec;
        Vec m_Initial, m_Target, m_V0;
        int m_Dims;
        std::int64_t m_Duration;
    };

    // DecayAnimation(animationSpec, initialValue, initialVelocity): one dimension.
    class DecayAnimation final : public Animation {
    public:
        DecayAnimation(std::shared_ptr<const DecaySpec> decay, float initialValue, float initialVelocity)
            : m_Decay(std::move(decay)), m_Initial(initialValue), m_V0(initialVelocity), m_Duration(m_Decay->DurationNanos(initialValue, initialVelocity)) {}
        std::int64_t DurationNanos() const override { return m_Duration; }
        Vec ValueFromNanos(std::int64_t t) const override { return { IsFinishedFromNanos(t) ? m_Decay->TargetValue(m_Initial, m_V0) : m_Decay->ValueAt(t, m_Initial, m_V0) }; }
        Vec VelocityFromNanos(std::int64_t t) const override { return { IsFinishedFromNanos(t) ? 0.0f : m_Decay->VelocityAt(t, m_Initial, m_V0) }; }
        Vec TargetValue() const override { return { m_Decay->TargetValue(m_Initial, m_V0) }; }

    private:
        std::shared_ptr<const DecaySpec> m_Decay;
        float m_Initial, m_V0;
        std::int64_t m_Duration;
    };

    // AnimationState: an animated value and its velocity; observable.
    class AnimationState {
    public:
        explicit AnimationState(Vec initial, Vec initialVelocity = {}, int dims = 1) : m_Value(initial), m_Velocity(initialVelocity), m_Dims(dims) {}
        explicit AnimationState(float initial, float initialVelocity = 0) : AnimationState(Vec{ initial }, Vec{ initialVelocity }, 1) {}
        const Vec& Value() const { return m_Value.Get(); }
        float Float() const { return Value()[0]; }
        const Vec& Velocity() const { return m_Velocity; }
        bool IsRunning() const { return m_Running.Get(); }
        std::int64_t LastFrameTimeNanos() const { return m_LastFrame; }
        std::int64_t FinishedTimeNanos() const { return m_Finished; }   // -1 while unfinished
        int Dims() const { return m_Dims; }
        // internal (the animate functions)
        void Set(const Vec& value, const Vec& velocity) { m_Value.Set(value); m_Velocity = velocity; }
        void SetRunning(bool r) { if (m_Running.Peek() != r) m_Running.Set(r); }
        void SetFrameTimes(std::int64_t last, std::int64_t finished) { m_LastFrame = last; m_Finished = finished; }
        std::uint64_t NextGeneration() { return ++m_Generation; }
        std::uint64_t Generation() const { return m_Generation; }

    private:
        rt::State<Vec, rt::NeverEqual> m_Value;
        Vec m_Velocity;
        rt::State<bool> m_Running{ false };
        std::int64_t m_LastFrame = -1, m_Finished = -1;
        std::uint64_t m_Generation = 0;
        int m_Dims;
    };

    // AnimationScope: the frame block's view of a running animation.
    class AnimationScope {
    public:
        Vec value, velocity, targetValue;
        std::int64_t startTimeNanos = 0, lastFrameTimeNanos = 0, finishedTimeNanos = -1;
        bool IsRunning() const { return m_Running; }
        // cancelAnimation: ends the animation after this frame (its state keeps the value).
        void CancelAnimation() { m_Running = false; }

    private:
        friend struct AnimationRunner;
        bool m_Running = true;
    };

    // AnimationState.animateTo(targetValue, animationSpec, sequentialAnimation, block): from the
    // state's value (and velocity, unless sequentialAnimation keeps the last one), frame by frame.
    rt::Task<> AnimateTo(AnimationState& state, Vec targetValue, SpecPtr spec = Spring(), bool sequentialAnimation = false,
                         kiln::Function<void(AnimationScope&)> block = {});
    inline rt::Task<> AnimateTo(AnimationState& state, float targetValue, SpecPtr spec = Spring(), bool sequentialAnimation = false,
                                kiln::Function<void(AnimationScope&)> block = {}) {
        return AnimateTo(state, Vec{ targetValue }, std::move(spec), sequentialAnimation, std::move(block));
    }
    // AnimationState.animateDecay(animationSpec, sequentialAnimation, block): from its value and velocity.
    rt::Task<> AnimateDecay(AnimationState& state, std::shared_ptr<const DecaySpec> decay, bool sequentialAnimation = false,
                            kiln::Function<void(AnimationScope&)> block = {});
    // calculateTargetValue(initialValue, initialVelocity): where a decay ends.
    inline float CalculateTargetValue(const DecaySpec& decay, float initialValue, float initialVelocity) { return decay.TargetValue(initialValue, initialVelocity); }

    // InfiniteAnimationPolicy (ui.platform): decides how animations that never end take frames
    // (a test pauses them, so the UI can go idle). Compose finds it in the coroutine context; here
    // the current policy is set for the thread (null: none, frames as usual).
    class InfiniteAnimationPolicy {
    public:
        virtual ~InfiniteAnimationPolicy() = default;
        virtual rt::Task<std::int64_t> OnInfiniteOperation(kiln::Function<rt::Task<std::int64_t>()> block) = 0;
    };
    void SetInfiniteAnimationPolicy(InfiniteAnimationPolicy* policy);
    InfiniteAnimationPolicy* CurrentInfiniteAnimationPolicy();
    // withInfiniteAnimationFrameNanos / Millis: the next frame's time, through the policy.
    rt::Task<std::int64_t> WithInfiniteAnimationFrameNanos();
    rt::Task<std::int64_t> WithInfiniteAnimationFrameMillis();

    // DeferredTargetAnimation (one to four dimensions).
    class DeferredTargetAnimation {
    public:
        explicit DeferredTargetAnimation(int dims = 1) : m_Dims(dims) {}
        // The target being animated to, null before the first updateTarget.
        const std::optional<Vec>& PendingTarget() const { return m_PendingTarget.Get(); }
        bool IsIdle() const { const auto& t = PendingTarget(); return !m_Animatable || (t && *t == m_Animatable->Target() && !m_Animatable->IsRunning()); }
        // updateTarget(target, coroutineScope, animationSpec): the first target snaps; a new
        // one animates there. Returns the current value.
        Vec UpdateTarget(Vec target, SpecPtr spec = Spring());
        float UpdateTarget(float target, SpecPtr spec = Spring()) { return UpdateTarget(Vec{ target }, std::move(spec))[0]; }

    private:
        int m_Dims;
        std::shared_ptr<Animatable> m_Animatable;
        rt::State<std::optional<Vec>> m_PendingTarget{ std::nullopt };
        rt::Job m_Job;
    };

}
