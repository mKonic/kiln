#include "kiln/foundation/style/StyleProperty.h"

namespace kiln::foundation {

    namespace {
        ui::Dp Clamp(ui::Dp d) { return !d.Specified() || d.value < 0 ? ui::Dp(0) : d; }
        float Mix(float a, float b, float t) { return a + (b - a) * t; }
    }

    ui::Dp StyleLerp(ui::Dp a, ui::Dp b, float t) { return ui::Dp(Mix(Clamp(a).value, Clamp(b).value, t)); }

    text::TextUnit StyleLerp(text::TextUnit a, text::TextUnit b, float t) {
        if (a.IsSp() && b.IsSp()) return text::Sp(Mix(a.value, b.value, t));
        if (a.IsEm() && b.IsEm()) return text::Em(Mix(a.value, b.value, t));
        return t < 0.5f ? a : b;
    }

    std::optional<Breadth> LerpBreadth(const Breadth& a, const Breadth& b, float t) {
        // None takes the zero of the other side's kind.
        auto zeroLike = [](const Breadth& other) {
            if (other.IsDistance()) return Breadth(ui::Dp(0));
            if (other.IsFraction()) return Breadth(0.0f);
            return Breadth::None();
        };
        const Breadth ea = a.kind == Breadth::Kind::None ? zeroLike(b) : a;
        const Breadth eb = b.kind == Breadth::Kind::None ? zeroLike(a) : b;
        if (ea.IsDistance() && eb.IsDistance()) return Breadth(ui::Dp(Mix(Clamp(ea.distance).value, Clamp(eb.distance).value, t)));
        if (ea.IsFraction() && eb.IsFraction()) return Breadth(Mix(ea.fraction, eb.fraction, t));
        return std::nullopt;
    }

    ui::Color Fill::AsColor() const {
        switch (kind) {
            case Kind::Color: return color;
            case Kind::Brush: return brush.kind == gfx::Brush::Kind::Solid && !brush.colors.empty() ? brush.colors[0] : ui::Color::Unspecified();
            case Kind::None: break;
        }
        return ui::Color::Unspecified();
    }

    std::optional<gfx::Brush> Fill::AsBrush() const {
        switch (kind) {
            case Kind::Color: return gfx::Brush::SolidColor(color);
            case Kind::Brush: return brush;
            case Kind::None: break;
        }
        return std::nullopt;
    }

    Fill LerpFill(const Fill& a, const Fill& b, float t) {
        // None and colours are colours; otherwise each side as a brush (lerpTarget: a colour is
        // a solid brush, None a transparent one).
        const bool aColor = a.kind != Fill::Kind::Brush, bColor = b.kind != Fill::Kind::Brush;
        if (aColor && bColor) return Fill(ui::Lerp(a.AsColor(), b.AsColor(), t));
        auto target = [](const Fill& f) {
            switch (f.kind) {
                case Fill::Kind::Color: return gfx::Brush::SolidColor(f.color);
                case Fill::Kind::Brush: return f.brush;
                case Fill::Kind::None: break;
            }
            return gfx::Brush::SolidColor(ui::Color::Transparent());
        };
        const gfx::Brush ta = target(a), tb = target(b);
        return Fill(gfx::Lerp(ta, &tb, t));
    }

    Shadows LerpShadows(const Shadows& a, const Shadows& b, float t) {
        // lerpTarget: a simple shadow's shadow; nothing for None and compound shadows.
        auto target = [](const Shadows& s) -> std::optional<gfx::shadow::Shadow> {
            if (s.kind == Shadows::Kind::Simple) return s.shadows.front();
            return std::nullopt;
        };
        const auto ta = target(a), tb = target(b);
        if (!ta && !tb) return t < 0.5f ? a : b;
        if (ta && tb && *ta == *tb) return Shadows(t < 0.5f ? *ta : *tb);
        if (auto r = gfx::shadow::Lerp(ta, tb, t)) return Shadows(*r);
        return t < 0.5f ? a : b;
    }

}
