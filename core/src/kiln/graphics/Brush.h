#pragma once

// Brush (androidx.compose.ui.graphics.Brush): a solid colour or a gradient. Gradient geometry is
// in DrawScope coordinates; infinite ends and unspecified centres resolve against the scope size
// when drawing (LinearGradient/RadialGradient/SweepGradient.createShader).

#include "kiln/ui/Geometry.h"

#include <limits>
#include <optional>
#include <vector>

namespace kiln::gfx {

    using ui::Color;
    using ui::Offset;
    using ui::Size;

    enum class TileMode : std::uint8_t { Clamp, Repeated, Mirror, Decal };

    struct Brush {
        enum class Kind : std::uint8_t { Solid, Linear, Radial, Sweep };
        static constexpr float kInf = std::numeric_limits<float>::infinity();

        Kind kind = Kind::Solid;
        std::vector<Color> colors;          // Solid: colors[0]
        std::vector<float> stops;           // empty: evenly spaced
        Offset start, end{ kInf, kInf };    // Linear
        std::optional<Offset> center;       // Radial / Sweep; nullopt = scope centre
        float radius = kInf;                // Radial; infinite = minDimension / 2
        TileMode tile = TileMode::Clamp;

        static Brush SolidColor(Color c) { Brush b; b.colors = { c }; return b; }
        static Brush LinearGradient(std::vector<Color> colors, Offset start = {}, Offset end = { kInf, kInf },
                                    TileMode tile = TileMode::Clamp, std::vector<float> stops = {}) {
            Brush b; b.kind = Kind::Linear; b.colors = std::move(colors); b.stops = std::move(stops);
            b.start = start; b.end = end; b.tile = tile; return b;
        }
        static Brush HorizontalGradient(std::vector<Color> colors, float startX = 0, float endX = kInf, TileMode tile = TileMode::Clamp) {
            return LinearGradient(std::move(colors), { startX, 0 }, { endX, 0 }, tile);
        }
        static Brush VerticalGradient(std::vector<Color> colors, float startY = 0, float endY = kInf, TileMode tile = TileMode::Clamp) {
            return LinearGradient(std::move(colors), { 0, startY }, { 0, endY }, tile);
        }
        static Brush RadialGradient(std::vector<Color> colors, std::optional<Offset> center = std::nullopt, float radius = kInf,
                                    TileMode tile = TileMode::Clamp, std::vector<float> stops = {}) {
            Brush b; b.kind = Kind::Radial; b.colors = std::move(colors); b.stops = std::move(stops);
            b.center = center; b.radius = radius; b.tile = tile; return b;
        }
        static Brush SweepGradient(std::vector<Color> colors, std::optional<Offset> center = std::nullopt, std::vector<float> stops = {}) {
            Brush b; b.kind = Kind::Sweep; b.colors = std::move(colors); b.stops = std::move(stops); b.center = center; return b;
        }

        // Geometry for a scope of `size`: Linear (x0, y0, x1, y1); Radial (cx, cy, r, 0);
        // Sweep (cx, cy, 0, 0).
        void Resolve(Size size, float out[4]) const {
            auto fix = [](float v, float inf) { return v == kInf ? inf : v; };
            switch (kind) {
                case Kind::Linear:
                    out[0] = fix(start.x, size.width); out[1] = fix(start.y, size.height);
                    out[2] = fix(end.x, size.width); out[3] = fix(end.y, size.height);
                    break;
                case Kind::Radial: case Kind::Sweep: {
                    const Offset c = center.value_or(Offset{ size.width / 2, size.height / 2 });
                    out[0] = c.x; out[1] = c.y;
                    out[2] = kind == Kind::Radial ? (radius == kInf ? size.MinDimension() / 2 : radius) : 0;
                    out[3] = 0;
                    break;
                }
                case Kind::Solid: out[0] = out[1] = out[2] = out[3] = 0; break;
            }
        }

        bool operator==(const Brush&) const = default;
    };

    // The 256-texel colour ramp of a gradient: colours interpolated unpremultiplied between
    // stops (Skia's default), stored premultiplied RGBA8.
    void BuildRamp(const Brush& b, std::uint8_t out[256 * 4]);

}
