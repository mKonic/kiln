#pragma once

// The value types of the generated token tables (tools/tokengen). Key tokens are ids into the
// theme's tables, like material3's ColorToken / ShapeToken / TypographyToken / MotionSchemeToken
// value classes; the theme resolves them (MaterialTheme: colour scheme, shapes, typography,
// motion scheme).

#include "kiln/animation/Easing.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/text/TextStyle.h"
#include "kiln/ui/Geometry.h"

namespace kiln::material3::tokens {

    struct ColorToken {
        int id = 0;
        constexpr bool operator==(const ColorToken&) const = default;
    };
    struct ShapeToken {
        int id = 0;
        constexpr bool operator==(const ShapeToken&) const = default;
    };
    struct TypographyToken {
        int id = 0;
        constexpr bool operator==(const TypographyToken&) const = default;
    };
    struct MotionSchemeToken {
        int id = 0;
        constexpr bool operator==(const MotionSchemeToken&) const = default;
    };

    // A generic font family (FontFamily.SansSerif and the like) as a constant.
    struct FontFamilyToken {
        text::FontFamily::Kind kind = text::FontFamily::Kind::Default;
        text::FontFamily Resolve() const { return text::FontFamily{ kind }; }
        constexpr bool operator==(const FontFamilyToken&) const = default;
    };

    // CubicBezierEasing(a, b, c, d) as a constant; ToEasing builds the curve.
    struct CubicBezierEasing {
        float a = 0, b = 0, c = 0, d = 0;
        anim::Easing ToEasing() const { return anim::Easing::CubicBezier(a, b, c, d); }
        constexpr bool operator==(const CubicBezierEasing&) const = default;
    };

}
