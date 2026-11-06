#include "kiln/foundation/EdgeEffect.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    namespace {
        constexpr double VelocityThreshold = 0.01;
        constexpr double ValueThreshold = 0.001;
        constexpr double LinearVelocityTakeOver = 200;
        constexpr double LinearDistanceTakeOver = 8.0;
        constexpr double NaturalFrequency = 24.657;
        constexpr double DampingRatio = 0.98;
        constexpr float OnAbsorbVelocityAdjustment = 13;
        constexpr float LinearStretchIntensity = 0.016f;
        constexpr float ExpStretchIntensity = 0.016f;
        constexpr float ScrollDistAffectedByExpStretch = 0.33f;
        float Signum(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }
    }

    void EdgeEffect::OnPull(float delta, float, std::int64_t now) {
        if (m_State != State::Pull) m_PullDistance = m_Distance;
        m_State = State::Pull;
        m_StartTime = now;
        m_PullDistance += delta;
        m_PullDistance = std::min(1.0f, m_PullDistance);
        m_Distance = std::max(0.0f, m_PullDistance);
        m_Velocity = 0;
        if (m_Distance == 0) m_State = State::Idle;
    }

    float EdgeEffect::OnPullDistance(float delta, float displacement, std::int64_t now) {
        const float finalDistance = std::max(0.0f, delta + m_Distance);
        const float applied = finalDistance - m_Distance;
        if (applied == 0 && m_Distance == 0) return 0;
        OnPull(applied, displacement, now);
        return applied;
    }

    void EdgeEffect::OnRelease(std::int64_t now) {
        m_PullDistance = 0;
        if (m_State != State::Pull && m_State != State::PullDecay) return;
        m_State = State::Recede;
        m_Velocity = 0;
        m_StartTime = now;
    }

    void EdgeEffect::OnAbsorb(int velocity, std::int64_t now) {
        m_State = State::Recede;
        m_Velocity = float(velocity) * OnAbsorbVelocityAdjustment;
        m_StartTime = now;
    }

    bool EdgeEffect::AtEquilibrium() const {
        const double displacement = double(m_Distance) * m_Height;
        return displacement < 0 || (std::abs(double(m_Velocity)) < VelocityThreshold && displacement < ValueThreshold);
    }

    void EdgeEffect::UpdateSpring(std::int64_t now) {
        const float dt = float(now - m_StartTime) / 1000.0f;
        if (dt < 0.001f) return;
        m_StartTime = now;
        if (std::abs(m_Velocity) <= LinearVelocityTakeOver && std::abs(m_Distance * m_Height) < LinearDistanceTakeOver &&
            Signum(m_Velocity) == -Signum(m_Distance)) {
            m_Velocity = Signum(m_Velocity) * float(LinearVelocityTakeOver);
            const float target = m_Distance + (m_Velocity * dt / m_Height);
            if (Signum(target) != Signum(m_Distance)) { m_Distance = 0; m_Velocity = 0; }
            else m_Distance = target;
            return;
        }
        const double damped = NaturalFrequency * std::sqrt(1 - DampingRatio * DampingRatio);
        const double cosC = double(m_Distance) * m_Height;
        const double sinC = (1 / damped) * (DampingRatio * NaturalFrequency * m_Distance * m_Height + m_Velocity);
        const double e = std::exp(-DampingRatio * NaturalFrequency * dt);
        const double distance = e * (cosC * std::cos(damped * dt) + sinC * std::sin(damped * dt));
        const double velocity = distance * (-NaturalFrequency) * DampingRatio +
                                e * (-damped * cosC * std::sin(damped * dt) + damped * sinC * std::cos(damped * dt));
        m_Distance = float(distance) / m_Height;
        m_Velocity = float(velocity);
        if (m_Distance > 1) { m_Distance = 1; m_Velocity = 0; }
        if (AtEquilibrium()) { m_Distance = 0; m_Velocity = 0; }
    }

    bool EdgeEffect::Update(std::int64_t now) {
        if (m_State == State::Recede) UpdateSpring(now);
        bool oneLastFrame = false;
        if (m_State == State::Recede && m_Distance == 0 && m_Velocity == 0) { m_State = State::Idle; oneLastFrame = true; }
        return m_State != State::Idle || oneLastFrame;
    }

    float EdgeEffect::DampStretchVector(float v) {
        const float sign = v > 0 ? 1.0f : -1.0f;
        const float over = std::abs(v);
        const float linear = LinearStretchIntensity * over;
        const double scalar = std::exp(1.0) / ScrollDistAffectedByExpStretch;
        const double expo = ExpStretchIntensity * (1 - std::exp(-over * scalar));
        return sign * float(linear + expo);
    }

}
