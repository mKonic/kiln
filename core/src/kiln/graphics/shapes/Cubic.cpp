#include "kiln/graphics/shapes/Cubic.h"

#include <algorithm>

namespace kiln::gfx::shapes {

    bool CollinearIsh(float ax, float ay, float bx, float by, float cx, float cy, float tolerance) {
        const Point ab = Point{ bx - ax, by - ay }.Rotate90();
        const Point ac{ cx - ax, cy - ay };
        const float dot = std::abs(ab.Dot(ac));
        const float relative = tolerance * ab.Distance() * ac.Distance();
        return dot < tolerance || dot < relative;
    }

    float FindMinimum(float v0, float v1, float tolerance, const std::function<float(float)>& f) {
        float a = v0, b = v1;
        while (b - a > tolerance) {
            const float c1 = (2 * a + b) / 3, c2 = (2 * b + a) / 3;
            if (f(c1) < f(c2)) b = c2; else a = c1;
        }
        return (a + b) / 2;
    }

    Point Cubic::PointOnCurve(float t) const {
        const float u = 1 - t;
        return { Anchor0X() * (u * u * u) + Control0X() * (3 * t * u * u) + Control1X() * (3 * t * t * u) + Anchor1X() * (t * t * t),
                 Anchor0Y() * (u * u * u) + Control0Y() * (3 * t * u * u) + Control1Y() * (3 * t * t * u) + Anchor1Y() * (t * t * t) };
    }

    bool Cubic::ZeroLength() const {
        return std::abs(Anchor0X() - Anchor1X()) < DistanceEpsilon && std::abs(Anchor0Y() - Anchor1Y()) < DistanceEpsilon;
    }

    bool Cubic::ConvexTo(const Cubic& next) const {
        return Convex({ Anchor0X(), Anchor0Y() }, { Anchor1X(), Anchor1Y() }, { next.Anchor1X(), next.Anchor1Y() });
    }

    std::array<float, 4> Cubic::CalculateBounds(bool approximate) const {
        if (ZeroLength()) return { Anchor0X(), Anchor0Y(), Anchor0X(), Anchor0Y() };
        float minX = std::min(Anchor0X(), Anchor1X()), minY = std::min(Anchor0Y(), Anchor1Y());
        float maxX = std::max(Anchor0X(), Anchor1X()), maxY = std::max(Anchor0Y(), Anchor1Y());
        if (approximate)
            return { std::min(minX, std::min(Control0X(), Control1X())), std::min(minY, std::min(Control0Y(), Control1Y())),
                     std::max(maxX, std::max(Control0X(), Control1X())), std::max(maxY, std::max(Control0Y(), Control1Y())) };
        auto zeroIsh = [](float v) { return std::abs(v) < DistanceEpsilon; };
        // Extrema where the derivative is zero, per axis.
        auto axis = [&](int o, float& mn, float& mx) {
            const float p0 = points[o], c0 = points[2 + o], c1 = points[4 + o], p1 = points[6 + o];
            const float a = -p0 + 3 * c0 - 3 * c1 + p1, b = 2 * p0 - 4 * c0 + 2 * c1, c = -p0 + c0;
            auto consider = [&](float t) {
                if (t < 0 || t > 1) return;
                const Point q = PointOnCurve(t);
                const float v = o == 0 ? q.x : q.y;
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            };
            if (zeroIsh(a)) {
                if (b != 0) consider(2 * c / (-2 * b));
            } else {
                const float s = b * b - 4 * a * c;
                if (s >= 0) {
                    consider((-b + std::sqrt(s)) / (2 * a));
                    consider((-b - std::sqrt(s)) / (2 * a));
                }
            }
        };
        axis(0, minX, maxX);
        axis(1, minY, maxY);
        return { minX, minY, maxX, maxY };
    }

    std::pair<Cubic, Cubic> Cubic::Split(float t) const {
        const float u = 1 - t;
        const Point p = PointOnCurve(t);
        return { Cubic(Anchor0X(), Anchor0Y(), Anchor0X() * u + Control0X() * t, Anchor0Y() * u + Control0Y() * t,
                       Anchor0X() * (u * u) + Control0X() * (2 * u * t) + Control1X() * (t * t),
                       Anchor0Y() * (u * u) + Control0Y() * (2 * u * t) + Control1Y() * (t * t), p.x, p.y),
                 Cubic(p.x, p.y, Control0X() * (u * u) + Control1X() * (2 * u * t) + Anchor1X() * (t * t),
                       Control0Y() * (u * u) + Control1Y() * (2 * u * t) + Anchor1Y() * (t * t),
                       Control1X() * u + Anchor1X() * t, Control1Y() * u + Anchor1Y() * t, Anchor1X(), Anchor1Y()) };
    }

    Cubic Cubic::Transformed(const PointTransformer& f) const {
        Cubic r;
        for (int i = 0; i < 8; i += 2) {
            const Point p = f(points[i], points[i + 1]);
            r.points[i] = p.x;
            r.points[i + 1] = p.y;
        }
        return r;
    }

    Cubic Cubic::StraightLine(float x0, float y0, float x1, float y1) {
        return { x0, y0, Interpolate(x0, x1, 1.0f / 3), Interpolate(y0, y1, 1.0f / 3), Interpolate(x0, x1, 2.0f / 3), Interpolate(y0, y1, 2.0f / 3), x1, y1 };
    }

    Cubic Cubic::CircularArc(float cx, float cy, float x0, float y0, float x1, float y1) {
        const Point p0d = DirectionVector(x0 - cx, y0 - cy), p1d = DirectionVector(x1 - cx, y1 - cy);
        const Point r0 = p0d.Rotate90(), r1 = p1d.Rotate90();
        const bool clockwise = r0.Dot(x1 - cx, y1 - cy) >= 0;
        const float cosa = p0d.Dot(p1d);
        if (cosa > 0.999f) return StraightLine(x0, y0, x1, y1);
        const float k = Distance(x0 - cx, y0 - cy) * 4.0f / 3.0f * (std::sqrt(2 * (1 - cosa)) - std::sqrt(1 - cosa * cosa)) / (1 - cosa) * (clockwise ? 1.0f : -1.0f);
        return { x0, y0, x0 + r0.x * k, y0 + r0.y * k, x1 - r1.x * k, y1 - r1.y * k, x1, y1 };
    }

}
