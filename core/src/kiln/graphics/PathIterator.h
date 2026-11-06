#pragma once

// PathIterator / PathSegment (ui-graphics PathIterator.kt over androidx graphics-path): a path's
// segments, each with the points it spans (a line or curve starts at the previous end point).
// Conics come as conics, or by default as quadratics within `tolerance`.

#include "kiln/graphics/Path.h"

#include <vector>

namespace kiln::gfx {

    struct PathSegment {
        enum class Type : std::uint8_t { Move, Line, Quadratic, Conic, Cubic, Close, Done };
        Type type = Type::Done;
        std::vector<Offset> points;   // Move 1, Line 2, Quadratic 3, Conic 3, Cubic 4, else none
        float weight = 0;             // Conic only
    };

    class PathIterator {
    public:
        enum class ConicEvaluation : std::uint8_t { AsConic, AsQuadratics };
        explicit PathIterator(const Path& path, ConicEvaluation conics = ConicEvaluation::AsQuadratics, float tolerance = 0.25f);
        bool HasNext() const { return m_Index < m_Segments.size(); }
        // The next segment; Done once past the end.
        PathSegment Next() { return HasNext() ? m_Segments[m_Index++] : PathSegment{}; }
        // The number of segments (Done excluded); conics count as their quadratics when converted.
        std::size_t CalculateSize(bool includeConvertedConics = true) const;

    private:
        std::vector<PathSegment> m_Segments;
        std::size_t m_Index = 0, m_ConicCount = 0, m_QuadsFromConics = 0;
    };

}
