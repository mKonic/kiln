#pragma once

// Path effects for strokes (PathEffect.kt over Skia): dash, corner and chains of them.

#include "kiln/graphics/Path.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kiln::gfx {

    class PathEffect {
    public:
        virtual ~PathEffect() = default;
        virtual Path Apply(const Path& src) const = 0;
        virtual std::uint64_t Hash() const = 0;
        // The result is filled rather than stroked (a stamped effect sets the fill style).
        virtual bool FillsResult() const { return false; }
        // How far the result can reach beyond the source path, besides the stroke itself.
        virtual float Outset() const { return 0; }

        // Even entries are "on" lengths, odd ones "off"; phase shifts into the pattern (SkDashPath).
        static std::shared_ptr<const PathEffect> Dash(std::vector<float> intervals, float phase = 0);
        // Rounds the corners between line segments with radius (SkCornerPathEffect).
        static std::shared_ptr<const PathEffect> Corner(float radius);
        // Stamps `shape` every `advance` along the path, from `phase` (SkPath1DPathEffect): moved
        // to each point (Translate), also turned along the tangent (Rotate), or bent along the
        // path (Morph). The stamps are filled.
        enum class StampStyle : std::uint8_t { Translate, Rotate, Morph };
        static std::shared_ptr<const PathEffect> Stamped(Path shape, float advance, float phase, StampStyle style);
        // outer(inner(path)).
        static std::shared_ptr<const PathEffect> Chain(std::shared_ptr<const PathEffect> outer, std::shared_ptr<const PathEffect> inner);
    };

}
