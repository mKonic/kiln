#pragma once

// PathMeasure (android.graphics.PathMeasure / SkContourMeasure): length, position, tangent and
// sub-segments along a contour. Segments are emitted as chopped curves, not polylines.

#include "kiln/graphics/Path.h"

namespace kiln::gfx {

    class ContourMeasure {
    public:
        float Length() const { return m_Length; }
        bool IsClosed() const { return m_Closed; }
        // Appends the part between the distances to dst; false when empty.
        bool GetSegment(float start, float stop, Path& dst, bool startWithMoveTo) const;
        Offset Position(float distance) const;
        Offset Tangent(float distance) const;   // unit

    private:
        friend class PathMeasure;
        friend std::vector<ContourMeasure> MeasureContours(const Path&, bool, float);
        enum class Kind : std::uint8_t { Line, Quad, Conic, Cubic };
        struct Segment { Kind kind; Offset p[4]; float w = 1; };   // w: conic weight
        struct Sample { float distance; std::uint32_t segment; float t; };   // cumulative
        std::vector<Segment> m_Segments;
        std::vector<Sample> m_Samples;
        float m_Length = 0;
        bool m_Closed = false;

        void Locate(float d, std::uint32_t& seg, float& t) const;
        static Offset Eval(const Segment& s, float t);
        static Offset Deriv(const Segment& s, float t);
        static Segment Chop(const Segment& s, float t0, float t1);
    };

    // Every contour of the path (SkContourMeasureIter). Zero-length contours are skipped.
    std::vector<ContourMeasure> MeasureContours(const Path& path, bool forceClosed, float resScale = 1);

    // Compose's PathMeasure: the path's first contour.
    class PathMeasure {
    public:
        void SetPath(const Path* path, bool forceClosed);
        float Length() const { return m_Contour.Length(); }
        bool GetSegment(float start, float stop, Path& dst, bool startWithMoveTo = true) const {
            return m_Contour.GetSegment(start, stop, dst, startWithMoveTo);
        }
        Offset Position(float d) const { return m_Contour.Position(d); }
        Offset Tangent(float d) const { return m_Contour.Tangent(d); }
    private:
        ContourMeasure m_Contour;
    };

}
