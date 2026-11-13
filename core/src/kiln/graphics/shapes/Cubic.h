#pragma once

// androidx.graphics.shapes port, part 1: Point helpers, utilities and Cubic (Point.kt, Utils.kt,
// Cubic.kt). Shapes are lists of cubic Béziers in a nominal space; RoundedPolygon builds them,
// Morph interpolates between two of them.

#include <array>
#include <kiln/Function.h>
#include <cmath>
#include <functional>
#include <utility>

namespace kiln::gfx::shapes {

    struct Point {
        float x = 0, y = 0;
        Point operator+(Point o) const { return { x + o.x, y + o.y }; }
        Point operator-(Point o) const { return { x - o.x, y - o.y }; }
        Point operator-() const { return { -x, -y }; }
        Point operator*(float s) const { return { x * s, y * s }; }
        Point operator/(float s) const { return { x / s, y / s }; }
        bool operator==(const Point&) const = default;
        float Distance() const { return std::sqrt(x * x + y * y); }
        float DistanceSquared() const { return x * x + y * y; }
        float Dot(Point o) const { return x * o.x + y * o.y; }
        float Dot(float ox, float oy) const { return x * ox + y * oy; }
        bool Clockwise(Point o) const { return x * o.y - y * o.x > 0; }
        Point Direction() const { const float d = Distance(); return { x / d, y / d }; }   // requires d > 0
        Point Rotate90() const { return { -y, x }; }
    };

    inline constexpr float DistanceEpsilon = 1e-4f;
    inline constexpr float AngleEpsilon = 1e-6f;
    inline constexpr float RelaxedDistanceEpsilon = 5e-3f;
    inline constexpr float FloatPi = 3.14159265358979323846f;
    inline constexpr float TwoPi = 2 * FloatPi;

    inline float Distance(float x, float y) { return std::sqrt(x * x + y * y); }
    inline float DistanceSquared(float x, float y) { return x * x + y * y; }
    inline Point DirectionVector(float x, float y) { const float d = Distance(x, y); return { x / d, y / d }; }
    inline Point DirectionVector(float angle) { return { std::cos(angle), std::sin(angle) }; }
    inline Point RadialToCartesian(float radius, float angle, Point center = {}) { return DirectionVector(angle) * radius + center; }
    inline float Square(float x) { return x * x; }
    inline float Interpolate(float a, float b, float f) { return (1 - f) * a + f * b; }
    inline Point Interpolate(Point a, Point b, float f) { return { Interpolate(a.x, b.x, f), Interpolate(a.y, b.y, f) }; }
    inline float PositiveModulo(float num, float mod) { return std::fmod(std::fmod(num, mod) + mod, mod); }
    bool CollinearIsh(float ax, float ay, float bx, float by, float cx, float cy, float tolerance = DistanceEpsilon);
    inline bool Convex(Point prev, Point cur, Point next) { return (cur - prev).Clockwise(next - cur); }
    // Ternary search for the minimum of f in [v0, v1].
    float FindMinimum(float v0, float v1, float tolerance, const kiln::Function<float(float)>& f);

    using PointTransformer = kiln::Function<Point(float x, float y)>;

    class Cubic {
    public:
        std::array<float, 8> points{};

        Cubic() = default;
        explicit Cubic(const std::array<float, 8>& p) : points(p) {}
        Cubic(float a0x, float a0y, float c0x, float c0y, float c1x, float c1y, float a1x, float a1y)
            : points{ a0x, a0y, c0x, c0y, c1x, c1y, a1x, a1y } {}
        Cubic(Point a0, Point c0, Point c1, Point a1) : Cubic(a0.x, a0.y, c0.x, c0.y, c1.x, c1.y, a1.x, a1.y) {}

        float Anchor0X() const { return points[0]; } float Anchor0Y() const { return points[1]; }
        float Control0X() const { return points[2]; } float Control0Y() const { return points[3]; }
        float Control1X() const { return points[4]; } float Control1Y() const { return points[5]; }
        float Anchor1X() const { return points[6]; } float Anchor1Y() const { return points[7]; }

        Point PointOnCurve(float t) const;
        bool ZeroLength() const;
        bool ConvexTo(const Cubic& next) const;
        // [minX, minY, maxX, maxY]; approximate uses the control points.
        std::array<float, 4> CalculateBounds(bool approximate = false) const;
        std::pair<Cubic, Cubic> Split(float t) const;
        Cubic Reverse() const { return { Anchor1X(), Anchor1Y(), Control1X(), Control1Y(), Control0X(), Control0Y(), Anchor0X(), Anchor0Y() }; }
        Cubic operator+(const Cubic& o) const { Cubic r; for (int i = 0; i < 8; ++i) r.points[i] = points[i] + o.points[i]; return r; }
        Cubic operator*(float s) const { Cubic r; for (int i = 0; i < 8; ++i) r.points[i] = points[i] * s; return r; }
        Cubic operator/(float s) const { return *this * (1.0f / s); }
        bool operator==(const Cubic& o) const { return points == o.points; }
        Cubic Transformed(const PointTransformer& f) const;

        static Cubic StraightLine(float x0, float y0, float x1, float y1);
        static Cubic CircularArc(float cx, float cy, float x0, float y0, float x1, float y1);
        static Cubic Empty(float x0, float y0) { return { x0, y0, x0, y0, x0, y0, x0, y0 }; }
        // MutableCubic.interpolate
        static Cubic Interpolated(const Cubic& a, const Cubic& b, float f) {
            Cubic r; for (int i = 0; i < 8; ++i) r.points[i] = Interpolate(a.points[i], b.points[i], f); return r;
        }
    };

}
