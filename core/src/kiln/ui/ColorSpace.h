#pragma once

// Oklab conversions for colour interpolation (animation ColorVectorConverter.kt: colours animate
// as (alpha, L, a, b) in Oklab and convert back, clamping L to [0,1] and a, b to [-0.5, 0.5]).
// Compose goes sRGB -> XYZ D50 -> (Bradford to D65) -> Oklab; Ottosson's direct linear-sRGB
// matrix is the same transform to float precision.

#include "kiln/ui/Geometry.h"

#include <array>

namespace kiln::ui {
    std::array<float, 4> ToOklab(Color c);            // { alpha, L, a, b }
    Color FromOklab(const std::array<float, 4>& v);
    // lerp(Color, Color, fraction): in Oklab, the fraction clamped to [0, 1] (Color.kt).
    Color Lerp(Color start, Color stop, float fraction);
    // Color.luminance: relative luminance of an sRGB colour (0 black .. 1 white).
    float Luminance(Color c);
}
