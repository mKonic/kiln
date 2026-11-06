#pragma once

// The splines behind keyframes with arcs and keyframesWithSpline (animation-core ArcSpline.kt,
// MonoSpline.kt). Times are in seconds; values are rows of components.

#include <array>
#include <vector>

namespace kiln::anim::detail {

    // ArcSpline: each pair of components (x, y) moves along a quarter ellipse between
    // keyframes (or a line). Modes per segment: 0 linear, 1 start vertical, 2 start horizontal,
    // 3 flip, 4 below, 5 above.
    class ArcSpline {
    public:
        ArcSpline(const std::vector<int>& modes, const std::vector<float>& times, const std::vector<std::vector<float>>& values);
        void Position(float time, std::vector<float>& out) const;
        void Slope(float time, std::vector<float>& out) const;

    private:
        struct Arc {
            Arc(int mode, float t1, float t2, float x1, float y1, float x2, float y2);
            float time1, time2, x1, y1, x2, y2;
            float arcDistance = 0, oneOverDeltaTime, arcVelocity, vertical;
            float ellipseA, ellipseB, ellipseCenterX, ellipseCenterY;
            bool isLinear;
            std::array<float, 101> lut{};
            float Angle(float time) const;
            float Lookup(float v) const;
            void BuildTable();
            float LinearX(float t) const { return x1 + (t - time1) * oneOverDeltaTime * (x2 - x1); }
            float LinearY(float t) const { return y1 + (t - time1) * oneOverDeltaTime * (y2 - y1); }
            // At an angle: position and velocity.
            float X(float s) const { return ellipseCenterX + ellipseA * s; }
            float Y(float c) const { return ellipseCenterY + ellipseB * c; }
            void Velocity(float s, float c, float& dx, float& dy) const;
        };
        std::vector<std::vector<Arc>> m_Arcs;
    };

    // MonoSpline: monotone cubic Hermite interpolation; extrapolates linearly outside. A
    // periodic bias (not NaN) blends the end slopes so the curve can loop.
    class MonoSpline {
    public:
        MonoSpline(std::vector<float> times, std::vector<std::vector<float>> values, float periodicBias);
        void Position(float time, std::vector<float>& out, std::size_t index = 0) const;
        void Slope(float time, std::vector<float>& out, std::size_t index = 0) const;

    private:
        float SlopeAt(float time, std::size_t j) const;
        std::vector<float> m_Times;
        std::vector<std::vector<float>> m_Values, m_Tangents;
    };

}
