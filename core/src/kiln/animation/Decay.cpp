#include "kiln/animation/Decay.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kiln::anim {

    namespace {
        // AndroidFlingSpline (100 samples), as in animation/FlingCalculator and the platform Scroller.
        constexpr float kInflection = 0.35f, kStartTension = 0.5f, kEndTension = 1.0f;
        constexpr float kP1 = kStartTension * kInflection, kP2 = 1.0f - kEndTension * (1.0f - kInflection);
        constexpr int kSamples = 100;

        struct Spline {
            std::array<float, kSamples + 1> positions{}, times{};
            Spline() {
                float xMin = 0, yMin = 0;
                for (int i = 0; i < kSamples; ++i) {
                    const float alpha = float(i) / kSamples;
                    float xMax = 1, x, tx, coef;
                    for (;;) {
                        x = xMin + (xMax - xMin) / 2.0f;
                        coef = 3.0f * x * (1.0f - x);
                        tx = coef * ((1.0f - x) * kP1 + x * kP2) + x * x * x;
                        if (std::abs(tx - alpha) < 1e-5f) break;
                        if (tx > alpha) xMax = x; else xMin = x;
                    }
                    positions[i] = coef * ((1.0f - x) * kStartTension + x) + x * x * x;
                    float yMax = 1, y, dy;
                    for (;;) {
                        y = yMin + (yMax - yMin) / 2.0f;
                        coef = 3.0f * y * (1.0f - y);
                        dy = coef * ((1.0f - y) * kStartTension + y) + y * y * y;
                        if (std::abs(dy - alpha) < 1e-5f) break;
                        if (dy > alpha) yMax = y; else yMin = y;
                    }
                    times[i] = coef * ((1.0f - y) * kP1 + y * kP2) + y * y * y;
                }
                positions[kSamples] = times[kSamples] = 1.0f;
            }
            // flingPosition(time) -> (distanceCoefficient, velocityCoefficient)
            std::pair<float, float> At(float time) const {
                const int index = int(kSamples * time);
                float distance = 1, velocity = 0;
                if (index < kSamples) {
                    const float tInf = float(index) / kSamples, tSup = float(index + 1) / kSamples;
                    const float dInf = positions[index], dSup = positions[index + 1];
                    velocity = (dSup - dInf) / (tSup - tInf);
                    distance = dInf + (time - tInf) * velocity;
                }
                return { distance, velocity };
            }
        };
        const Spline& TheSpline() { static const Spline s; return s; }

        const float kDecelerationRate = float(std::log(0.78) / std::log(0.9));
        float Sign(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }
    }

    SplineDecay::SplineDecay(float density, float friction)
        : m_Friction(friction), m_Magic(9.80665f * 39.37f * density * 160.0f * 0.84f) {}

    double SplineDecay::SplineDeceleration(float velocity) const {
        return std::log(kInflection * std::abs(velocity) / double(m_Friction * m_Magic));
    }

    float SplineDecay::Distance(float velocity) const {
        const double l = SplineDeceleration(velocity);
        const double decelMinusOne = kDecelerationRate - 1.0;
        return float(m_Friction * m_Magic * std::exp(kDecelerationRate / decelMinusOne * l)) * Sign(velocity);
    }

    std::int64_t SplineDecay::DurationNanos(float, float velocity) const {
        const double l = SplineDeceleration(velocity);
        return std::int64_t(1e9 * std::exp(l / (kDecelerationRate - 1.0)));
    }

    float SplineDecay::ValueAt(std::int64_t t, float v0, float vel) const {
        const std::int64_t d = DurationNanos(v0, vel);
        const float pos = d > 0 ? float(double(t) / d) : 1.0f;
        return v0 + std::abs(Distance(vel)) * Sign(vel) * TheSpline().At(pos).first;
    }

    float SplineDecay::VelocityAt(std::int64_t t, float v0, float vel) const {
        const std::int64_t d = DurationNanos(v0, vel);
        const float pos = d > 0 ? float(double(t) / d) : 1.0f;
        return TheSpline().At(pos).second * Sign(vel) * std::abs(Distance(vel)) / float(d) * 1e9f;
    }

    float ExponentialDecay::TargetValue(float v0, float vel) const {
        if (std::abs(vel) <= m_Threshold) return v0;
        const double duration = std::log(std::abs(m_Threshold / vel)) / m_Friction * 1000;
        return v0 - vel / m_Friction + vel / m_Friction * float(std::exp(m_Friction * duration / 1000.0));
    }
    float ExponentialDecay::ValueAt(std::int64_t t, float v0, float vel) const {
        const float dt = float(double(t) / 1e9);
        return v0 - vel / m_Friction + vel / m_Friction * std::exp(m_Friction * dt);
    }
    float ExponentialDecay::VelocityAt(std::int64_t t, float, float vel) const {
        return vel * std::exp(float(double(t) / 1e9) * m_Friction);
    }
    std::int64_t ExponentialDecay::DurationNanos(float, float vel) const {
        return std::max<std::int64_t>(0, std::int64_t(1e9 * std::log(m_Threshold / std::abs(vel)) / m_Friction));
    }

}
