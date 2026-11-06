#pragma once

// android.widget.EdgeEffect, stretch behaviour (Android 12+, frameworks/base EdgeEffect.java):
// pulls accumulate a normalised distance (0..1 of the container), release and absorb run the
// damped spring back to 0 (natural frequency 24.657, damping 0.98, linear take-over near rest),
// and the drawn stretch is dampStretchVector(distance).

#include <cstdint>

namespace kiln::foundation {

    class EdgeEffect {
    public:
        void SetSize(float width, float height) { m_Width = width; m_Height = height; }
        bool IsFinished() const { return m_State == State::Idle; }
        void Finish() { m_State = State::Idle; m_Distance = 0; m_Velocity = 0; }
        // onPull / onPullDistance (returns the distance change actually applied).
        void OnPull(float deltaDistance, float displacement, std::int64_t nowMillis);
        float OnPullDistance(float deltaDistance, float displacement, std::int64_t nowMillis);
        float Distance() const { return m_Distance; }
        void OnRelease(std::int64_t nowMillis);
        void OnAbsorb(int velocity, std::int64_t nowMillis);
        // draw(): advances the recede spring; true while it still needs frames.
        bool Update(std::int64_t nowMillis);
        // The stretch for the RenderNode: dampStretchVector of the normalised distance.
        static float DampStretchVector(float normalized);
        float StretchAmount() const { return DampStretchVector(m_Distance); }

    private:
        enum class State : std::uint8_t { Idle, Pull, Absorb, Recede, PullDecay };
        void UpdateSpring(std::int64_t nowMillis);
        bool AtEquilibrium() const;
        State m_State = State::Idle;
        float m_Distance = 0, m_PullDistance = 0, m_Velocity = 0;
        float m_Width = 0, m_Height = 0;
        std::int64_t m_StartTime = 0;
    };

}
