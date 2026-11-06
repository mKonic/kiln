#pragma once

// Coverage rasteriser for flattened polygons: exact signed-area accumulation per pixel
// (font-rs's method), folded by fill type. Output is an 8-bit alpha mask.

#include "kiln/graphics/Path.h"

#include <cstdint>
#include <span>
#include <vector>

namespace kiln::gfx {

    struct Mask {
        int x = 0, y = 0;            // device position of the mask's top-left pixel
        int width = 0, height = 0;
        std::vector<std::uint8_t> alpha;   // width * height
    };

    // Rasterises device-space polygons clipped to `clip` (device px). The mask covers the
    // polygons' bounds intersected with the clip, rounded out to whole pixels.
    Mask Rasterize(std::span<const Polyline> polygons, FillType fill, Rect clip);

}
