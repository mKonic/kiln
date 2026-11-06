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

        // Even entries are "on" lengths, odd ones "off"; phase shifts into the pattern (SkDashPath).
        static std::shared_ptr<const PathEffect> Dash(std::vector<float> intervals, float phase = 0);
        // Rounds the corners between line segments with radius (SkCornerPathEffect).
        static std::shared_ptr<const PathEffect> Corner(float radius);
        // outer(inner(path)).
        static std::shared_ptr<const PathEffect> Chain(std::shared_ptr<const PathEffect> outer, std::shared_ptr<const PathEffect> inner);
    };

}
