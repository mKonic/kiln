#include "kiln/graphics/Brush.h"

#include <algorithm>
#include <cmath>

namespace kiln::gfx {

    void BuildRamp(const Brush& b, std::uint8_t out[256 * 4]) {
        const std::size_t n = b.colors.size();
        std::vector<float> stops = b.stops;
        if (stops.size() != n) {
            stops.resize(n);
            for (std::size_t i = 0; i < n; ++i) stops[i] = n > 1 ? float(i) / float(n - 1) : 0.0f;
        }
        auto channel = [](Color c, int shift) { return float((c.argb >> shift) & 0xff) / 255.0f; };
        for (int i = 0; i < 256; ++i) {
            const float t = float(i) / 255.0f;
            float a = 0, r = 0, g = 0, bl = 0;
            if (n == 1) {
                a = channel(b.colors[0], 24); r = channel(b.colors[0], 16); g = channel(b.colors[0], 8); bl = channel(b.colors[0], 0);
            } else if (n > 1) {
                std::size_t k = 0;
                while (k + 1 < n && t > stops[k + 1]) ++k;
                std::size_t k1 = std::min(k + 1, n - 1);
                float f = 0;
                if (t <= stops[0]) { k = k1 = 0; }
                else if (t >= stops[n - 1]) { k = k1 = n - 1; }
                else if (stops[k1] > stops[k]) f = (t - stops[k]) / (stops[k1] - stops[k]);
                const Color c0 = b.colors[k], c1 = b.colors[k1];
                auto mix = [&](int s) { return channel(c0, s) + (channel(c1, s) - channel(c0, s)) * f; };
                a = mix(24); r = mix(16); g = mix(8); bl = mix(0);
            }
            out[i * 4 + 0] = std::uint8_t(std::lround(r * a * 255));
            out[i * 4 + 1] = std::uint8_t(std::lround(g * a * 255));
            out[i * 4 + 2] = std::uint8_t(std::lround(bl * a * 255));
            out[i * 4 + 3] = std::uint8_t(std::lround(a * 255));
        }
    }

}
