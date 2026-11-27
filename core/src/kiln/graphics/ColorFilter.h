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

    // ColorMatrix (ColorMatrix.kt): a 4x5 matrix on RGBA (row-major; the fifth column adds,
    // in 0..255), built up for ColorFilter::ColorMatrix.
    struct ColorMatrix {
        std::array<float, 20> values{ 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0 };
        ColorMatrix() = default;
        explicit ColorMatrix(const std::array<float, 20>& v) : values(v) {}
        float Get(int row, int column) const { return values[std::size_t(row * 5 + column)]; }
        void Set(int row, int column, float v) { values[std::size_t(row * 5 + column)] = v; }
        void Set(const ColorMatrix& src) { values = src.values; }
        void Reset() { *this = ColorMatrix{}; }
        bool operator==(const ColorMatrix&) const = default;
        // this = this * m (the translation column carried through).
        ColorMatrix& operator*=(const ColorMatrix& m);
        // 0: greyscale (Rec. 709 luminance weights), 1: unchanged.
        void SetToSaturation(float saturation);
        void SetToScale(float redScale, float greenScale, float blueScale, float alphaScale);
        // A rotation of the hue about the red, green or blue axis (degrees).
        void SetToRotateRed(float degrees);
        void SetToRotateGreen(float degrees);
        void SetToRotateBlue(float degrees);
        // libjpeg's coefficients.
        void ConvertRgbToYuv();
        void ConvertYuvToRgb();
    };

    struct ColorFilter {
        enum class Kind : std::uint8_t { Tint, Matrix };
        Kind kind = Kind::Tint;
        Color color;                          // Tint
        BlendMode mode = BlendMode::SrcIn;    // Tint
        std::array<float, 20> matrix{};       // Matrix, row-major 4x5

        static ColorFilter Tint(Color c, BlendMode m = BlendMode::SrcIn) { ColorFilter f; f.color = c; f.mode = m; return f; }
        static ColorFilter ColorMatrix(const std::array<float, 20>& m) { ColorFilter f; f.kind = Kind::Matrix; f.matrix = m; return f; }
        static ColorFilter ColorMatrix(const gfx::ColorMatrix& m) { return ColorMatrix(m.values); }
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
