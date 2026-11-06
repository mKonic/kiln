#pragma once

// Animatable (animation-core Animatable.kt + SuspendAnimation.kt): a value animated by
// coroutines on the frame clock. Starting an animation (or SnapTo/Stop) interrupts the running
// one, which then returns Interrupted; the new one starts from the current value and velocity,
// so springs stay continuous. Frame time is divided by the animator duration scale (0 finishes
// immediately), the MotionDurationScale Compose reads from ANIMATOR_DURATION_SCALE.

#include "kiln/animation/Decay.h"
#include "kiln/animation/Spec.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"

#include <functional>
#include <optional>

namespace kiln::anim {

    // Set by the host from Settings.Global.ANIMATOR_DURATION_SCALE.
    float DurationScale();
    void SetDurationScale(float scale);

    enum class EndReason : std::uint8_t { Finished, Interrupted, BoundReached };
    struct AnimationResult { EndReason reason; Vec endValue; Vec endVelocity; };

    class Animatable {
    public:
        explicit Animatable(Vec initial, int dims = 1);
        explicit Animatable(float initial) : Animatable(Vec{ initial, 0, 0, 0 }, 1) {}

        // Reading subscribes the reader (composition, layout or draw).
        const Vec& Value() const { return m_Value.Get(); }
        float Float() const { return m_Value.Get()[0]; }
        const Vec& Velocity() const { return m_Velocity; }
        const Vec& Target() const { return m_Target; }
        bool IsRunning() const { return m_Running.Get(); }
        int Dims() const { return m_Dims; }

        rt::Task<AnimationResult> AnimateTo(Vec target, SpecPtr spec = Spring(), std::optional<Vec> initialVelocity = {},
                                            std::function<void(const Vec&)> onFrame = {});
        rt::Task<AnimationResult> AnimateTo(float target, SpecPtr spec = Spring()) {
            return AnimateTo(Vec{ target, 0, 0, 0 }, std::move(spec));
        }
        // 1D decay from the current value (animateDecay): runs until the decay ends, a bound is
        // reached, or another animation interrupts.
        rt::Task<AnimationResult> AnimateDecay(float initialVelocity, std::shared_ptr<const DecaySpec> decay,
                                               std::function<void(float)> onFrame = {});
        void SnapTo(Vec value);
        void SnapTo(float value) { SnapTo(Vec{ value, 0, 0, 0 }); }
        void Stop();
        // Values outside the bounds stop the animation at the bound (BoundReached).
        void UpdateBounds(std::optional<Vec> lower, std::optional<Vec> upper) { m_Lower = lower; m_Upper = upper; }

    private:
        Vec Clamp(const Vec& v, bool& clamped) const;
        rt::State<Vec, rt::NeverEqual> m_Value;
        rt::State<bool> m_Running{ false };
        Vec m_Velocity{};
        Vec m_Target{};
        int m_Dims;
        std::uint64_t m_Generation = 0;
        std::optional<Vec> m_Lower, m_Upper;
    };

}
