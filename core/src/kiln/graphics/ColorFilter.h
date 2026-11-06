#pragma once

// BlendMode (the 29 Compose/Skia modes, in Compose's order) and ColorFilter: tint (the filter
// colour blended onto each pixel), colour matrix (Android's 4x5 on unpremultiplied RGBA, offsets
// in 0..255) and lighting.

#include "kiln/ui/Geometry.h"

#include <array>
#include <cstdint>

namespace kiln::gfx {

    using ui::Color;

    enum class BlendMode : std::uint8_t {
        Clear, Src, Dst, SrcOver, DstOver, SrcIn, DstIn, SrcOut, DstOut, SrcAtop, DstAtop, Xor, Plus,
        Modulate, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, Hardlight, Softlight,
        Difference, Exclusion, Multiply, Hue, Saturation, Color, Luminosity,
    };

    struct ColorFilter {
        enum class Kind : std::uint8_t { Tint, Matrix };
        Kind kind = Kind::Tint;
        Color color;                          // Tint
        BlendMode mode = BlendMode::SrcIn;    // Tint
        std::array<float, 20> matrix{};       // Matrix, row-major 4x5

        static ColorFilter Tint(Color c, BlendMode m = BlendMode::SrcIn) { ColorFilter f; f.color = c; f.mode = m; return f; }
        static ColorFilter ColorMatrix(const std::array<float, 20>& m) { ColorFilter f; f.kind = Kind::Matrix; f.matrix = m; return f; }
        // ColorFilter.lighting: rgb * multiply + add, alpha untouched (LightingColorFilter).
        static ColorFilter Lighting(Color multiply, Color add) {
            auto ch = [](Color c, int s) { return float((c.argb >> s) & 0xff); };
            ColorFilter f;
            f.kind = Kind::Matrix;
            f.matrix = { ch(multiply, 16) / 255, 0, 0, 0, ch(add, 16),
                         0, ch(multiply, 8) / 255, 0, 0, ch(add, 8),
                         0, 0, ch(multiply, 0) / 255, 0, ch(add, 0),
                         0, 0, 0, 1, 0 };
            return f;
        }
        bool operator==(const ColorFilter&) const = default;
    };

}
