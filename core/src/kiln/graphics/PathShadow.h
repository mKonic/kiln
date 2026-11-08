#pragma once

#include "kiln/graphics/Path.h"
#include "kiln/graphics/Shadow.h"

#include <array>
#include <vector>

namespace kiln::gfx {

    // HWUI's canvas transform and the caster's z = ax + by + c plane, in local coordinates.
    // The matrix is row-major, local -> device; the light is already in device coordinates.
    struct ShadowProjection {
        std::array<float, 9> matrix{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        std::array<float, 3> zPlane{};
    };

    struct ShadowMeshVertex {
        ui::Offset position;
        float coverage = 0;   // interpolate first, then apply SkGaussianColorFilter
    };

    struct PathShadow {
        std::vector<ShadowMeshVertex> triangles;
        // SkDevice::drawShadow's fallback when a mesh cannot be made (multiple contours,
        // self-intersections, or a concave offset that cannot be triangulated).
        bool blurred = false;
        Path blurPath;       // device coordinates
        float sigma = 0;
        float strokeWidth = 0;   // ambient: stroke-and-fill; spot: fill only
    };

    // A null light requests the ambient shadow; otherwise the point-light spot shadow.
    PathShadow MakePathShadow(const Path& path, const ShadowProjection& projection, const ShadowLight* light, bool transparent);

}
