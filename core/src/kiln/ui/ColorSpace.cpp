#include "kiln/ui/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace kiln::ui {

    namespace {
        float ToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
        float ToGamma(float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f; }
    }

    std::array<float, 4> ToOklab(Color c) {
        const float r = ToLinear(((c.argb >> 16) & 0xff) / 255.0f);
        const float g = ToLinear(((c.argb >> 8) & 0xff) / 255.0f);
        const float b = ToLinear((c.argb & 0xff) / 255.0f);
        const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
        const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
        const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
        return { c.Alpha(),
                 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
    }

    Color FromOklab(const std::array<float, 4>& v) {
        const float L = std::clamp(v[1], 0.0f, 1.0f), A = std::clamp(v[2], -0.5f, 0.5f), B = std::clamp(v[3], -0.5f, 0.5f);
        float l = L + 0.3963377774f * A + 0.2158037573f * B;
        float m = L - 0.1055613458f * A - 0.0638541728f * B;
        float s = L - 0.0894841775f * A - 1.2914855480f * B;
        l = l * l * l; m = m * m * m; s = s * s * s;
        const float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
        const float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
        const float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
        auto to8 = [](float lin) { return std::uint32_t(std::lround(std::clamp(ToGamma(std::clamp(lin, 0.0f, 1.0f)), 0.0f, 1.0f) * 255)); };
        const std::uint32_t a8 = std::uint32_t(std::lround(std::clamp(v[0], 0.0f, 1.0f) * 255));
        return Color((a8 << 24) | (to8(r) << 16) | (to8(g) << 8) | to8(b));
    }

    Color Lerp(Color start, Color stop, float fraction) {
        const float t = std::clamp(fraction, 0.0f, 1.0f);
        const auto a = ToOklab(start), b = ToOklab(stop);
        return FromOklab({ a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t });
    }


    float Luminance(Color c) {
        // The sRGB transfer function, then the Rec. 709 weights.
        const auto eotf = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        const double r = eotf(((c.argb >> 16) & 0xff) / 255.0), g = eotf(((c.argb >> 8) & 0xff) / 255.0), b = eotf((c.argb & 0xff) / 255.0);
        return std::clamp(float(0.2126 * r + 0.7152 * g + 0.0722 * b), 0.0f, 1.0f);
    }

}
