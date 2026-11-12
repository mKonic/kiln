#include "kiln/graphics/Brush.h"

#include "kiln/ui/ColorSpace.h"

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

    namespace {
        float LerpF(float a, float b, float t) { return (1 - t) * a + t * b; }
        std::vector<Color> LerpColors(const std::vector<Color>& l, const std::vector<Color>& r, float t) {
            std::vector<Color> out(std::max(l.size(), r.size()));
            for (std::size_t i = 0; i < out.size(); ++i) out[i] = ui::Lerp(l[std::min(i, l.size() - 1)], r[std::min(i, r.size() - 1)], t);
            return out;
        }
        std::vector<float> LerpStops(const std::vector<float>& l, const std::vector<float>& r, float t) {
            if (l.empty() || r.empty()) return {};
            std::vector<float> out(std::max(l.size(), r.size()));
            for (std::size_t i = 0; i < out.size(); ++i) out[i] = LerpF(l[std::min(i, l.size() - 1)], r[std::min(i, r.size() - 1)], t);
            return out;
        }
        Offset LerpSafe(Offset l, Offset r, float t) {
            const bool finite = std::isfinite(l.x) && std::isfinite(l.y) && std::isfinite(r.x) && std::isfinite(r.y);
            return finite ? Offset{ LerpF(l.x, r.x, t), LerpF(l.y, r.y, t) } : (t < 0.5f ? l : r);
        }
        // `a`.lerp(`b`, t) of Brush.kt; false when `a` cannot interpolate towards `b`.
        bool LerpFrom(const Brush& a, const Brush& other, float t, Brush& out) {
            if (a.kind == Brush::Kind::Solid) {
                if (other.kind != Brush::Kind::Solid) return false;
                out = Brush::SolidColor(ui::Lerp(a.colors.at(0), other.colors.at(0), t));
                return true;
            }
            Brush b = other;
            if (b.kind == Brush::Kind::Solid) {
                // The same gradient filled with the solid colour.
                const Color c = other.colors.at(0);
                b = a;
                std::fill(b.colors.begin(), b.colors.end(), c);
            }
            if (b.kind != a.kind) return false;
            out = a;
            out.colors = LerpColors(a.colors, b.colors, t);
            out.stops = LerpStops(a.stops, b.stops, t);
            switch (a.kind) {
                case Brush::Kind::Linear:
                    out.start = LerpSafe(a.start, b.start, t);
                    out.end = LerpSafe(a.end, b.end, t);
                    out.tile = t < 0.5f ? a.tile : b.tile;
                    break;
                case Brush::Kind::Radial:
                    out.center = a.center && b.center ? std::optional<Offset>(Offset{ LerpF(a.center->x, b.center->x, t), LerpF(a.center->y, b.center->y, t) })
                                                      : (t < 0.5f ? a.center : b.center);
                    out.radius = LerpF(a.radius, b.radius, t);
                    out.tile = t < 0.5f ? a.tile : b.tile;
                    break;
                case Brush::Kind::Sweep:
                    out.center = a.center && b.center ? std::optional<Offset>(Offset{ LerpF(a.center->x, b.center->x, t), LerpF(a.center->y, b.center->y, t) })
                                                      : (t < 0.5f ? a.center : b.center);
                    break;
                case Brush::Kind::Solid: case Brush::Kind::Image: case Brush::Kind::Composite: case Brush::Kind::Shader: break;
            }
            return true;
        }
    }

    Brush Lerp(const Brush& a, const Brush* b, float t) {
        const Brush other = b ? *b : Brush::SolidColor(Color(0x00000000));
        if (a == other) return t < 0.5f ? a : other;
        Brush out;
        if (LerpFrom(a, other, t, out)) return out;
        if (LerpFrom(other, a, 1 - t, out)) return out;
        return t < 0.5f ? a : other;
    }

}
