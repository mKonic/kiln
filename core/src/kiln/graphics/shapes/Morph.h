#pragma once

// androidx.graphics.shapes port, part 4: Morph between two RoundedPolygons (Morph.kt with
// PolygonMeasure.kt, FloatMapping.kt and FeatureMapping.kt). Corners are matched by position,
// outlines are cut so both shapes have the same number of cubics, and the cubics interpolate.

#include "kiln/graphics/shapes/RoundedPolygon.h"

#include <utility>
#include <vector>

namespace kiln::gfx::shapes {

    class Morph {
    public:
        Morph(const RoundedPolygon& start, const RoundedPolygon& end);

        std::vector<Cubic> AsCubics(float progress) const;
        Path ToPath(float progress) const;
        std::array<float, 4> CalculateBounds(bool approximate = true) const;
        std::array<float, 4> CalculateMaxBounds() const;
        const std::vector<std::pair<Cubic, Cubic>>& MorphMatch() const { return m_Match; }

    private:
        RoundedPolygon m_Start, m_End;
        std::vector<std::pair<Cubic, Cubic>> m_Match;
    };

    // Exposed for tests.
    namespace detail {
        bool ProgressInRange(float progress, float from, float to);
        float LinearMap(const std::vector<float>& xs, const std::vector<float>& ys, float x);
        float ProgressDistance(float a, float b);
        // Cubic length by three chords (LengthMeasurer).
        float MeasureCubic(const Cubic& c);
    }

}
