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

}
