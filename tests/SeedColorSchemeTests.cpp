#include "Check.h"

#include "kiln/material3/SeedColorScheme.h"
#include "kiln/material3/internal/colorUtil/ColorUtil.h"

#include <cmath>

using namespace kiln::material3;
using kiln::ui::Color;

namespace {
    // A role's tone is its L*.
    float Tone(Color c) { return colorutil::CamUtils::LstarFromInt(std::int32_t(c.argb)); }
    bool ToneNear(Color c, float tone) { return std::fabs(Tone(c) - tone) <= 1.0f; }
}

// material-color-utilities scheme_monochrome_test.cc, through ColorSchemeFromSeed.
TEST(MonochromeSeedSchemesHaveTheSpecTones) {
    const Color blue(0xFF0000FFu);
    const ColorScheme d = ColorSchemeFromSeed(blue, true, SchemeVariant::Monochrome);
    CHECK(ToneNear(d.primary, 100) && ToneNear(d.onPrimary, 10) && ToneNear(d.primaryContainer, 85) && ToneNear(d.onPrimaryContainer, 0));
    CHECK(ToneNear(d.secondary, 80) && ToneNear(d.onSecondary, 10) && ToneNear(d.secondaryContainer, 30) && ToneNear(d.onSecondaryContainer, 90));
    CHECK(ToneNear(d.tertiary, 90) && ToneNear(d.onTertiary, 10) && ToneNear(d.tertiaryContainer, 60) && ToneNear(d.onTertiaryContainer, 0));
    const ColorScheme l = ColorSchemeFromSeed(blue, false, SchemeVariant::Monochrome);
    CHECK(ToneNear(l.primary, 0) && ToneNear(l.onPrimary, 90) && ToneNear(l.primaryContainer, 25) && ToneNear(l.onPrimaryContainer, 100));
    CHECK(ToneNear(l.secondary, 40) && ToneNear(l.onSecondary, 100) && ToneNear(l.secondaryContainer, 85) && ToneNear(l.onSecondaryContainer, 10));
    CHECK(ToneNear(l.tertiary, 25) && ToneNear(l.onTertiary, 90) && ToneNear(l.tertiaryContainer, 49) && ToneNear(l.onTertiaryContainer, 100));
}

TEST(TonalSpotSeedSchemesFollowTheSeedHue) {
    const Color seed(0xFF3366CCu);
    const ColorScheme l = ColorSchemeFromSeed(seed, false);
    const ColorScheme d = ColorSchemeFromSeed(seed, true);
    CHECK(ToneNear(l.primary, 40) && ToneNear(d.primary, 80));
    CHECK(ToneNear(l.surface, 98) && ToneNear(d.surface, 6));
    CHECK(l.surfaceTint == l.primary);
    const float seedHue = colorutil::Cam::FromInt(std::int32_t(seed.argb)).hue;
    const float primaryHue = colorutil::Cam::FromInt(std::int32_t(l.primary.argb)).hue;
    CHECK(std::fabs(seedHue - primaryHue) < 5.0f);
    // Higher contrast pulls the primary further from the surface.
    const ColorScheme high = ColorSchemeFromSeed(seed, false, SchemeVariant::TonalSpot, 1.0);
    CHECK(Tone(high.primary) < Tone(l.primary));
}
