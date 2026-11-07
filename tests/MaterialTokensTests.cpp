#include "Check.h"

#include "kiln/material3/tokens/ColorLightTokens.h"
#include "kiln/material3/tokens/FilledButtonTokens.h"
#include "kiln/material3/tokens/MotionTokens.h"
#include "kiln/material3/tokens/ShapeTokens.h"
#include "kiln/material3/tokens/TypographyTokens.h"

using namespace kiln;
using namespace kiln::material3::tokens;

// Values as the Kotlin sources have them (tools/tokengen output).
static_assert(FilledButtonTokens::ContainerColor == ColorSchemeKeyTokens::Primary);
static_assert(ColorSchemeKeyTokens::Primary.id == 25);
static_assert(FilledButtonTokens::DisabledContainerOpacity == 0.1f);
static_assert(ColorLightTokens::Primary.argb == 0xFF6750A4u);   // PaletteTokens.Primary40
static_assert(ShapeTokens::CornerExtraLargeTop.topStart.value == 28.0f && ShapeTokens::CornerExtraLargeTop.bottomEnd.value == 0.0f);
static_assert(ShapeTokens::CornerFull.percent == 50.0f);          // CircleShape
static_assert(MotionTokens::DurationMedium2 == 300.0);
static_assert(MotionTokens::EasingEmphasizedCubicBezier == CubicBezierEasing{ 0.2f, 0.0f, 0.0f, 1.0f });

TEST(TypographyTokensBuildTheirTextStyles) {
    const text::TextStyle body = TypographyTokens().BodyLarge();
    CHECK(body.span.fontSize == text::Sp(16));
    CHECK(body.paragraph.lineHeight == text::Sp(24));
    CHECK(body.span.letterSpacing == text::Sp(0.5f));
    CHECK(body.span.fontWeight == text::FontWeight::Normal());
    CHECK(body.span.fontFamily == text::FontFamily::SansSerif());
    CHECK(body.paragraph.includeFontPadding == false);
    CHECK(body.paragraph.lineHeightStyle.has_value() && body.paragraph.lineHeightStyle->alignment == text::LineHeightStyle::Center &&
          body.paragraph.lineHeightStyle->trim == text::LineHeightStyle::Trim::None);
    // A family passed in replaces the token's.
    const text::TextStyle serif = TypographyTokens(text::FontFamily::Serif()).TitleLarge();
    CHECK(serif.span.fontFamily == text::FontFamily::Serif());
    CHECK(serif.span.fontSize == text::Sp(22));
}
