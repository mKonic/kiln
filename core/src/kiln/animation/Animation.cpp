#include "kiln/animation/Animation.h"

namespace kiln::anim {

    struct AnimationRunner {
        // animateTo / animateDecay's loop: samples `animation` on each frame (the play time
        // scaled by the animator duration scale for spec animations), updating the state and the
        // scope, until it finishes, the block cancels it or a newer animation of the state starts.
        static rt::Task<> Run(AnimationState& state, std::shared_ptr<const Animation> animation, bool scaled, std::function<void(AnimationScope&)> block) {
            const std::uint64_t gen = state.NextGeneration();
            AnimationScope scope;
            scope.value = state.Value();
            scope.velocity = state.Velocity();
            scope.targetValue = animation->TargetValue();
            state.SetRunning(true);
            std::int64_t start = -1;
            for (;;) {
                const std::int64_t frame = co_await rt::NextFrame{};
                if (gen != state.Generation()) co_return;   // a newer animation took over
                if (start < 0) { start = frame; scope.startTimeNanos = frame; }
                const float durationScale = scaled ? DurationScale() : 1.0f;
                const std::int64_t play = durationScale == 0 ? animation->DurationNanos() : std::int64_t(double(frame - start) / durationScale);
                const bool finished = !animation->IsInfinite() && animation->IsFinishedFromNanos(play);
                scope.value = animation->ValueFromNanos(play);
                scope.velocity = animation->VelocityFromNanos(play);
                scope.lastFrameTimeNanos = frame;
                if (finished) { scope.finishedTimeNanos = frame; scope.m_Running = false; }
                state.Set(scope.value, scope.velocity);
                state.SetFrameTimes(frame, scope.finishedTimeNanos);
                if (block) block(scope);
                if (!scope.IsRunning()) {
                    if (!finished) state.Set(scope.value, {});   // cancelled: keep the value, drop the velocity
                    state.SetRunning(false);
                    co_return;
                }
            }
        }
    };

    rt::Task<> AnimateTo(AnimationState& state, Vec target, SpecPtr spec, bool, std::function<void(AnimationScope&)> block) {
        // From the state's velocity (sequentialAnimation only moves Compose's start time to the
        // last frame's, which the frame clock here gives anyway).
        const Vec v0 = state.Velocity();
        auto animation = std::make_shared<TargetBasedAnimation>(std::move(spec), state.Value(), target, v0, state.Dims());
        co_await AnimationRunner::Run(state, animation, true, std::move(block));
    }

    rt::Task<> AnimateDecay(AnimationState& state, std::shared_ptr<const DecaySpec> decay, bool, std::function<void(AnimationScope&)> block) {
        auto animation = std::make_shared<DecayAnimation>(std::move(decay), state.Value()[0], state.Velocity()[0]);
        co_await AnimationRunner::Run(state, animation, false, std::move(block));
    }

    Vec DeferredTargetAnimation::UpdateTarget(Vec target, SpecPtr spec) {
        if (!m_Animatable) {
            m_Animatable = std::make_shared<Animatable>(target, m_Dims);
            m_PendingTarget.Set(target);
            return target;
        }
        if (!m_PendingTarget.Peek() || !(*m_PendingTarget.Peek() == target)) {
            m_PendingTarget.Set(target);
            auto a = m_Animatable;
            m_Job = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Animatable> a, Vec t, SpecPtr s) -> rt::Task<> { co_await a->AnimateTo(t, s); }(a, target, std::move(spec)));
        }
        return m_Animatable->Value();
    }

}
