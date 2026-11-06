#pragma once

// Easing curves (animation-core Easing.kt). CubicBezierEasing solves x(t) = fraction for t and
// returns y(t), clamped to the curve's own y extremes, like Compose (which uses ui-graphics'
// exact root finder; this uses Newton with a bisection fallback to the same float precision).

#include <functional>

namespace kiln::anim {

    class Easing {
    public:
        Easing() : m_Fn([](float f) { return f; }) {}
        explicit Easing(std::function<float(float)> fn) : m_Fn(std::move(fn)) {}
        float Transform(float fraction) const { return m_Fn(fraction); }
        static Easing CubicBezier(float a, float b, float c, float d);
    private:
        std::function<float(float)> m_Fn;
    };

    Easing FastOutSlowIn();      // (0.4, 0, 0.2, 1)
    Easing LinearOutSlowIn();    // (0, 0, 0.2, 1)
    Easing FastOutLinearIn();    // (0.4, 0, 1, 1)
    Easing Linear();

}
