#include "kiln/graphics/Path.h"
#include <memory>
#include "kiln/animation/Easing.h"

#include <algorithm>
#include <cmath>

namespace kiln::anim {

    namespace {
        double Bez(double p1, double p2, double t) {   // 1D cubic with P0 = 0, P3 = 1
            const double u = 1 - t;
            return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
        }
        double BezD(double p1, double p2, double t) {
            const double u = 1 - t;
            return 3 * u * u * p1 + 6 * u * t * (p2 - p1) + 3 * t * t * (1 - p2);
        }
    }

    Easing Easing::CubicBezier(float a, float b, float c, float d) {
        // y extremes over t in [0, 1]: endpoints plus roots of y'(t).
        double lo = 0, hi = 1;
        {
            const double A = 3 * (1 - 3 * d + 3 * b) , B = 6 * (d - 2 * b), C = 3 * b;   // y'(t) = At^2 + Bt + C
            auto consider = [&](double t) { if (t > 0 && t < 1) { const double y = Bez(b, d, t); lo = std::min(lo, y); hi = std::max(hi, y); } };
            if (std::abs(A) < 1e-12) { if (std::abs(B) > 1e-12) consider(-C / B); }
            else {
                const double disc = B * B - 4 * A * C;
                if (disc >= 0) { const double s = std::sqrt(disc); consider((-B + s) / (2 * A)); consider((-B - s) / (2 * A)); }
            }
        }
        const float minY = float(lo), maxY = float(hi);
        return Easing([=](float fraction) {
            if (!(fraction > 0 && fraction < 1)) return fraction;
            const double x = std::max(double(fraction), 1.1920929e-7);   // OneUlpAt1
            double t = x;                                                // Newton from t = x
            for (int i = 0; i < 8; ++i) {
                const double err = Bez(a, c, t) - x, dx = BezD(a, c, t);
                if (std::abs(err) < 1e-9) break;
                if (std::abs(dx) < 1e-9) break;
                t = std::clamp(t - err / dx, 0.0, 1.0);
            }
            if (std::abs(Bez(a, c, t) - x) > 1e-7) {                     // bisection fallback
                double lo2 = 0, hi2 = 1;
                for (int i = 0; i < 60; ++i) {
                    t = (lo2 + hi2) / 2;
                    if (Bez(a, c, t) < x) lo2 = t; else hi2 = t;
                }
            }
            return std::clamp(float(Bez(b, d, t)), minY, maxY);
        });
    }

    Easing FastOutSlowIn() { static const Easing e = Easing::CubicBezier(0.4f, 0.0f, 0.2f, 1.0f); return e; }
    Easing LinearOutSlowIn() { static const Easing e = Easing::CubicBezier(0.0f, 0.0f, 0.2f, 1.0f); return e; }
    Easing FastOutLinearIn() { static const Easing e = Easing::CubicBezier(0.4f, 0.0f, 1.0f, 1.0f); return e; }
    Easing Linear() { static const Easing e; return e; }

    Easing PathEasing(const std::vector<std::pair<float, float>>& pts) {
        auto p = std::make_shared<std::vector<std::pair<float, float>>>(pts);
        return Easing([p](float f) {
            if (f <= 0) return 0.0f;
            if (f >= 1) return 1.0f;
            const auto& v = *p;
            auto it = std::lower_bound(v.begin(), v.end(), f, [](const auto& a, float x) { return a.first < x; });
            if (it == v.begin()) return it->second;
            if (it == v.end()) return v.back().second;
            const auto& [x1, y1] = *it;
            const auto& [x0, y0] = *(it - 1);
            return x1 > x0 ? y0 + (y1 - y0) * (f - x0) / (x1 - x0) : y1;
        });
    }

    Easing PathEasing(const gfx::Path& path) {
        std::vector<std::pair<float, float>> pts;
        for (const auto& poly : path.Flatten({}, 1e-4f))
            for (const auto& q : poly.points) pts.push_back({ q.x, q.y });
        std::stable_sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return PathEasing(pts);
    }

    namespace easing {
        namespace {
            Easing Cb(float a, float b, float c, float d) { return Easing::CubicBezier(a, b, c, d); }
            constexpr float kPi = 3.14159265358979f;
            float OutBounce(float f) {
                const float n1 = 7.5625f, d1 = 2.75f;
                if (f < 1 / d1) return n1 * f * f;
                if (f < 2 / d1) { const float x = f - 1.5f / d1; return n1 * x * x + 0.75f; }
                if (f < 2.5f / d1) { const float x = f - 2.25f / d1; return n1 * x * x + 0.9375f; }
                const float x = f - 2.625f / d1;
                return n1 * x * x + 0.984375f;
            }
        }
#define KILN_CB(name, a, b, c, d) Easing name() { static const Easing e = Cb(a, b, c, d); return e; }
        KILN_CB(Ease, 0.25f, 0.1f, 0.25f, 1.0f) KILN_CB(EaseOut, 0.0f, 0.0f, 0.58f, 1.0f)
        KILN_CB(EaseIn, 0.42f, 0.0f, 1.0f, 1.0f) KILN_CB(EaseInOut, 0.42f, 0.0f, 0.58f, 1.0f)
        KILN_CB(EaseInSine, 0.12f, 0, 0.39f, 0) KILN_CB(EaseOutSine, 0.61f, 1, 0.88f, 1) KILN_CB(EaseInOutSine, 0.37f, 0, 0.63f, 1)
        KILN_CB(EaseInCubic, 0.32f, 0, 0.67f, 0) KILN_CB(EaseOutCubic, 0.33f, 1, 0.68f, 1) KILN_CB(EaseInOutCubic, 0.65f, 0, 0.35f, 1)
        KILN_CB(EaseInQuint, 0.64f, 0, 0.78f, 0) KILN_CB(EaseOutQuint, 0.22f, 1, 0.36f, 1) KILN_CB(EaseInOutQuint, 0.83f, 0, 0.17f, 1)
        KILN_CB(EaseInCirc, 0.55f, 0, 1, 0.45f) KILN_CB(EaseOutCirc, 0, 0.55f, 0.45f, 1) KILN_CB(EaseInOutCirc, 0.85f, 0, 0.15f, 1)
        KILN_CB(EaseInQuad, 0.11f, 0, 0.5f, 0) KILN_CB(EaseOutQuad, 0.5f, 1, 0.89f, 1) KILN_CB(EaseInOutQuad, 0.45f, 0, 0.55f, 1)
        KILN_CB(EaseInQuart, 0.5f, 0, 0.75f, 0) KILN_CB(EaseOutQuart, 0.25f, 1, 0.5f, 1) KILN_CB(EaseInOutQuart, 0.76f, 0, 0.24f, 1)
        KILN_CB(EaseInExpo, 0.7f, 0, 0.84f, 0) KILN_CB(EaseOutExpo, 0.16f, 1, 0.3f, 1) KILN_CB(EaseInOutExpo, 0.87f, 0, 0.13f, 1)
        KILN_CB(EaseInBack, 0.36f, 0, 0.66f, -0.56f) KILN_CB(EaseOutBack, 0.34f, 1.56f, 0.64f, 1) KILN_CB(EaseInOutBack, 0.68f, -0.6f, 0.32f, 1.6f)
#undef KILN_CB
        Easing EaseInElastic() {
            return Easing([](float f) {
                if (f == 0 || f == 1) return f;
                return -std::pow(2.0f, 10 * f - 10) * std::sin((f * 10 - 10.75f) * (2 * kPi / 3));
            });
        }
        Easing EaseOutElastic() {
            return Easing([](float f) {
                if (f == 0 || f == 1) return f;
                return std::pow(2.0f, -10 * f) * std::sin((f * 10 - 0.75f) * (2 * kPi / 3)) + 1;
            });
        }
        Easing EaseInOutElastic() {
            return Easing([](float f) {
                if (f == 0 || f == 1) return f;
                const float c5 = 2 * kPi / 4.5f;
                if (f <= 0.5f) return -(std::pow(2.0f, 20 * f - 10) * std::sin((20 * f - 11.125f) * c5)) / 2;
                return std::pow(2.0f, -20 * f + 10) * std::sin((20 * f - 11.125f) * c5) / 2 + 1;
            });
        }
        Easing EaseOutBounce() { return Easing(OutBounce); }
        Easing EaseInBounce() { return Easing([](float f) { return 1 - OutBounce(1 - f); }); }
        Easing EaseInOutBounce() {
            return Easing([](float f) { return f < 0.5f ? (1 - OutBounce(1 - 2 * f)) / 2 : (1 + OutBounce(2 * f - 1)) / 2; });
        }
    }

}
