#pragma once

// Path geometry (androidx.compose.ui.graphics.Path with Skia semantics): contours of lines,
// quadratic and cubic Béziers; arcs and ovals become cubics. Flattened to polylines at a device
// tolerance for filling and stroking (see Rasterizer.h, Stroke.h).

#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <vector>

namespace kiln::gfx {

    using ui::Offset;
    using ui::Rect;

    enum class FillType : std::uint8_t { NonZero, EvenOdd };
    enum class StrokeCap : std::uint8_t { Butt, Round, Square };
    enum class StrokeJoin : std::uint8_t { Miter, Round, Bevel };

    // DrawScope Stroke: width 0 is a hairline (one device pixel).
    struct Stroke {
        float width = 0;
        float miter = 4;
        StrokeCap cap = StrokeCap::Butt;
        StrokeJoin join = StrokeJoin::Miter;
        bool operator==(const Stroke&) const = default;
    };

    // Per-corner elliptical radii (RoundRect).
    struct RoundRect {
        Rect rect;
        Offset topLeft, topRight, bottomRight, bottomLeft;   // x/y radii
        static RoundRect Uniform(Rect r, float radius) {
            return { r, { radius, radius }, { radius, radius }, { radius, radius }, { radius, radius } };
        }
    };

    // 2D affine transform: x' = a*x + c*y + e, y' = b*x + d*y + f.
    struct Matrix {
        float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
        Offset Map(Offset p) const { return { a * p.x + c * p.y + e, b * p.x + d * p.y + f }; }
        static Matrix Translate(float x, float y) { return { 1, 0, 0, 1, x, y }; }
        static Matrix Scale(float sx, float sy) { return { sx, 0, 0, sy, 0, 0 }; }
        // Largest stretch of a unit vector (for flattening tolerance).
        float MaxScale() const;
    };

    struct Polyline {
        std::vector<Offset> points;
        bool closed = false;
    };

    class Path {
    public:
        enum class Verb : std::uint8_t { Move, Line, Quad, Cubic, Close };
        enum class Direction : std::uint8_t { Clockwise, CounterClockwise };

        FillType fillType = FillType::NonZero;

        void MoveTo(float x, float y);
        void RelativeMoveTo(float dx, float dy) { const Offset p = Last(); MoveTo(p.x + dx, p.y + dy); }
        void LineTo(float x, float y);
        void RelativeLineTo(float dx, float dy) { const Offset p = Last(); LineTo(p.x + dx, p.y + dy); }
        void QuadraticTo(float x1, float y1, float x2, float y2);
        void RelativeQuadraticTo(float dx1, float dy1, float dx2, float dy2);
        void CubicTo(float x1, float y1, float x2, float y2, float x3, float y3);
        void RelativeCubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3);
        // arcTo: the arc of `oval` from startAngle sweeping sweepAngle (degrees, clockwise in a
        // y-down space). Without forceMoveTo a line joins the current point to the arc start.
        void ArcTo(Rect oval, float startAngleDegrees, float sweepAngleDegrees, bool forceMoveTo);
        void AddRect(Rect r, Direction dir = Direction::CounterClockwise);
        void AddOval(Rect oval, Direction dir = Direction::CounterClockwise);
        void AddRoundRect(const RoundRect& rr, Direction dir = Direction::CounterClockwise);
        void AddArc(Rect oval, float startAngleDegrees, float sweepAngleDegrees);
        void AddPath(const Path& other, Offset offset = {});
        void Close();
        void Reset() { m_Verbs.clear(); m_Points.clear(); m_ContourStart = 0; m_NeedsMove = true; }
        void Translate(Offset o) { Transform(Matrix::Translate(o.x, o.y)); }
        void Transform(const Matrix& m) { for (Offset& p : m_Points) p = m.Map(p); }

        bool IsEmpty() const { return m_Verbs.empty(); }
        Rect Bounds() const;   // of all points, control points included (like getBounds' conservative form)
        const std::vector<Verb>& Verbs() const { return m_Verbs; }
        const std::vector<Offset>& Points() const { return m_Points; }
        std::uint64_t Hash() const;

        // Polylines through `m` with at most `tolerance` device pixels of error.
        std::vector<Polyline> Flatten(const Matrix& m, float tolerance = 0.05f) const;

    private:
        Offset Last() const { return m_Points.empty() ? Offset{} : m_Points.back(); }
        void EnsureMove();
        std::vector<Verb> m_Verbs;
        std::vector<Offset> m_Points;
        std::size_t m_ContourStart = 0;   // index into m_Points of the current contour's start
        bool m_NeedsMove = true;
    };

    // Stroke outline as fill polygons (fill them NonZero). `m` maps to device space; the stroke
    // is built in path space so non-uniform scale stretches it like Skia does.
    std::vector<Polyline> StrokeToPolygons(const Path& path, const Stroke& stroke, const Matrix& m, float tolerance = 0.05f);

}
