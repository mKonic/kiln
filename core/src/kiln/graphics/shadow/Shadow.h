#pragma once

// ui-graphics/shadow/Shadow.kt. These are position-independent painted shadows, not elevation
// shadows or text shadows. Value geometry is in dp; modifier blocks use pixels instead.
#include "kiln/graphics/Brush.h"
#include "kiln/graphics/ColorFilter.h"

namespace kiln::gfx::shadow {
    struct Shadow {
        ui::Dp radius, spread;
        ui::DpOffset offset;
        ui::Color color;
        std::optional<Brush> brush;
        float alpha = 1;
        BlendMode blendMode = BlendMode::SrcOver;

        explicit Shadow(ui::Dp radius = {}, ui::Color color = ui::Color(0xff000000), ui::Dp spread = {},
                        ui::DpOffset offset = {}, float alpha = 1, BlendMode blend = BlendMode::SrcOver)
            : radius(radius), spread(spread), offset(offset), color(color.Specified() ? color : ui::Color(0xff000000)),
              alpha(std::clamp(alpha, 0.0f, 1.0f)), blendMode(blend) {}
        Shadow(ui::Dp radius, Brush brush, ui::Dp spread = {}, ui::DpOffset offset = {}, float alpha = 1,
               BlendMode blend = BlendMode::SrcOver)
            : Shadow(radius, ui::Color(0xff000000), spread, offset, alpha, blend) {
            if (brush.kind == Brush::Kind::Solid) color = brush.colors.empty() ? ui::Color::Transparent() : brush.colors[0];
            else this->brush = std::move(brush);
        }
        Shadow WithoutOffset() const { Shadow s = *this; s.offset = {}; return s; }
        Shadow TransparentCopy() const { return Shadow(radius, ui::Color::Transparent(), spread, offset, alpha, blendMode); }
        bool operator==(const Shadow&) const = default;
    };

    Shadow Lerp(const Shadow& a, const Shadow& b, float t);
    std::optional<Shadow> Lerp(const std::optional<Shadow>& a, const std::optional<Shadow>& b, float t);
}
