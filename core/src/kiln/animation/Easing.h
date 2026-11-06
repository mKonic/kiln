#pragma once

// Easing curves (animation-core Easing.kt). CubicBezierEasing solves x(t) = fraction for t and
// returns y(t), clamped to the curve's own y extremes, like Compose (which uses ui-graphics'
// exact root finder; this uses Newton with a bisection fallback to the same float precision).

#include <functional>
#include <utility>
#include <vector>

namespace kiln::gfx { class Path; }

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

    // PathEasing: y for x along a path from (0,0) to (1,1) that is a function of x (one y per x).
    Easing PathEasing(const std::vector<std::pair<float, float>>& polyline);
    // From a path in the unit square (PathEasing(path)), flattened to 1e-4.
    Easing PathEasing(const gfx::Path& path);

    // EasingFunctions.kt: the CSS / easings.net set (exact control points) plus the closed-form
    // elastic and bounce curves.
    namespace easing {
        Easing Ease(); Easing EaseIn(); Easing EaseOut(); Easing EaseInOut();
        Easing EaseInSine(); Easing EaseOutSine(); Easing EaseInOutSine();
        Easing EaseInCubic(); Easing EaseOutCubic(); Easing EaseInOutCubic();
        Easing EaseInQuint(); Easing EaseOutQuint(); Easing EaseInOutQuint();
        Easing EaseInCirc(); Easing EaseOutCirc(); Easing EaseInOutCirc();
        Easing EaseInQuad(); Easing EaseOutQuad(); Easing EaseInOutQuad();
        Easing EaseInQuart(); Easing EaseOutQuart(); Easing EaseInOutQuart();
        Easing EaseInExpo(); Easing EaseOutExpo(); Easing EaseInOutExpo();
        Easing EaseInBack(); Easing EaseOutBack(); Easing EaseInOutBack();
        Easing EaseInElastic(); Easing EaseOutElastic(); Easing EaseInOutElastic();
        Easing EaseInBounce(); Easing EaseOutBounce(); Easing EaseInOutBounce();
    }

}
