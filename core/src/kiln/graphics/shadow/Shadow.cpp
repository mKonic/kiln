#include "kiln/graphics/shadow/Shadow.h"
#include "kiln/ui/ColorSpace.h"

namespace kiln::gfx::shadow {
    Shadow Lerp(const Shadow& a, const Shadow& b, float t) {
        auto mix = [t](float x, float y) { return x + (y - x) * t; };
        Shadow s(ui::Dp(mix(a.radius.value, b.radius.value)), ui::Lerp(a.color, b.color, t),
                 ui::Dp(mix(a.spread.value, b.spread.value)),
                 { ui::Dp(mix(a.offset.x.value, b.offset.x.value)), ui::Dp(mix(a.offset.y.value, b.offset.y.value)) },
                 mix(a.alpha, b.alpha), t < 0.5f ? a.blendMode : b.blendMode);
        if (a.brush) s.brush = gfx::Lerp(*a.brush, b.brush ? &*b.brush : nullptr, t);
        else if (b.brush) s.brush = gfx::Lerp(*b.brush, nullptr, 1 - t);
        if (s.brush && s.brush->kind == Brush::Kind::Solid) {
            s.color = s.brush->colors.empty() ? ui::Color::Transparent() : s.brush->colors[0];
            s.brush.reset();
        }
        return s;
    }

    std::optional<Shadow> Lerp(const std::optional<Shadow>& a, const std::optional<Shadow>& b, float t) {
        if (!a && !b) return std::nullopt;
        if (!a) return Lerp(b->TransparentCopy(), *b, t);
        if (!b) return Lerp(*a, a->TransparentCopy(), t);
        return Lerp(*a, *b, t);
    }
}
