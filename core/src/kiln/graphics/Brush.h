#pragma once

// Brush (androidx.compose.ui.graphics.Brush): a solid colour or a gradient. Gradient geometry is
// in DrawScope coordinates; infinite ends and unspecified centres resolve against the scope size
// when drawing (LinearGradient/RadialGradient/SweepGradient.createShader).

#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Image.h"
#include "kiln/ui/Geometry.h"

#include <functional>
#include <memory>

#include <limits>
#include <optional>
#include <vector>

namespace kiln::gfx {

    using ui::Color;
    using ui::Offset;
    using ui::Size;

    enum class TileMode : std::uint8_t { Clamp, Repeated, Mirror, Decal };

    struct Brush {
        // Image: the image repeated from the origin by its tile modes (ImageShader); Composite:
        // two brushes blended (CompositeShader, src over dst by `blend`); Shader: a brush made for
        // the size drawn in (ShaderBrush.createShader).
        enum class Kind : std::uint8_t { Solid, Linear, Radial, Sweep, Image, Composite, Shader };
        static constexpr float kInf = std::numeric_limits<float>::infinity();

        Kind kind = Kind::Solid;
        std::vector<Color> colors;          // Solid: colors[0]
        std::vector<float> stops;           // empty: evenly spaced
        Offset start, end{ kInf, kInf };    // Linear
        std::optional<Offset> center;       // Radial / Sweep; nullopt = scope centre
        float radius = kInf;                // Radial; infinite = minDimension / 2
        TileMode tile = TileMode::Clamp;
        ImageBitmap image;                                       // Image
        TileMode tileY = TileMode::Clamp;                        // Image: vertical (tile is horizontal)
        std::shared_ptr<const Brush> dst, src;                   // Composite
        BlendMode blend = BlendMode::SrcOver;                    // Composite
        std::shared_ptr<const std::function<Brush(Size)>> shader;   // Shader

        // Whether drawing it needs layers (image, composite and shader brushes) rather than a
        // single shaded quad.
        bool IsLayered() const { return kind == Kind::Image || kind == Kind::Composite || kind == Kind::Shader; }
        static Brush ImageShader(ImageBitmap image, TileMode tileModeX = TileMode::Clamp, TileMode tileModeY = TileMode::Clamp) {
            Brush b; b.kind = Kind::Image; b.image = std::move(image); b.tile = tileModeX; b.tileY = tileModeY; return b;
        }
        static Brush CompositeShader(Brush dst, Brush src, BlendMode blendMode) {
            Brush b; b.kind = Kind::Composite; b.dst = std::make_shared<Brush>(std::move(dst)); b.src = std::make_shared<Brush>(std::move(src));
            b.blend = blendMode; return b;
        }
        static Brush ShaderBrush(std::function<Brush(Size)> createShader) {
            Brush b; b.kind = Kind::Shader; b.shader = std::make_shared<std::function<Brush(Size)>>(std::move(createShader)); return b;
        }

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

    // Interpolatable.lerp between brushes (Brush.kt): colours in Oklab, lists element-wise (the
    // shorter repeating its last), stops only when both have them. A solid colour lerps with a
    // gradient as that gradient filled with the colour; different gradient kinds switch at
    // t = 0.5. `b` null: transparent.
    Brush Lerp(const Brush& a, const Brush* b, float t);

    // The 256-texel colour ramp of a gradient: colours interpolated unpremultiplied between
    // stops (Skia's default), stored premultiplied RGBA8.
    void BuildRamp(const Brush& b, std::uint8_t out[256 * 4]);

}
