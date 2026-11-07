#include "Check.h"

#include "kiln/material3/DynamicColorScheme.h"
#include "kiln/material3/internal/colorUtil/ColorUtil.h"

using namespace kiln;
using namespace kiln::material3;
using kiln::ui::Color;

namespace {
    int IndexOf(Color TonalPalette::* field) {
        for (int i = 0; i < kSystemPaletteCount; ++i) if (kSystemPaletteFields[i] == field) return i;
        return -1;
    }
}

TEST(DynamicColourNeedsThePlatformColours) {
    ui::Configuration c;
    CHECK(!DynamicLightColorScheme(c).has_value());
    CHECK(!DynamicDarkColorScheme(c).has_value());
    CHECK(!DynamicTonalPalette(c).has_value());
}

TEST(Api31SchemesComeFromThePaletteAndSynthesisedTones) {
    ui::Configuration c;
    // A palette of distinct opaque colours.
    for (int i = 0; i < kSystemPaletteCount; ++i) c.systemPalette.push_back(0xFF000000u | std::uint32_t(0x030201 * (i + 1)));
    const auto palette = DynamicTonalPalette(c);
    CHECK(palette.has_value());
    const std::uint32_t nv40 = c.systemPalette[std::size_t(IndexOf(&TonalPalette::neutralVariant40))];
    CHECK(palette->neutralVariant98 == Color(colorutil::SetLuminance(nv40, 98.0f)));
    const auto light = DynamicLightColorScheme(c);
    const auto dark = DynamicDarkColorScheme(c);
    CHECK(light && light->primary == Color(c.systemPalette[std::size_t(IndexOf(&TonalPalette::primary40))]));
    CHECK(light && light->surface == palette->neutralVariant98 && light->surfaceTint == light->primary);
    CHECK(dark && dark->primary == Color(c.systemPalette[std::size_t(IndexOf(&TonalPalette::primary80))]));
    CHECK(dark && dark->surfaceContainerLowest == palette->neutralVariant4);
    CHECK(light && light->error == LightColorScheme().error);   // not in the palette
}

TEST(Api34SchemesTakeThePlatformRoles) {
    ui::Configuration c;
    for (int i = 0; i < 2 * kSystemRoleCount; ++i) c.systemRoles.push_back(0xFF000000u | std::uint32_t(i + 1));
    const auto light = DynamicLightColorScheme(c);
    const auto dark = DynamicDarkColorScheme(c);
    CHECK(light && light->primary == Color(0xFF000001u));                              // first light role
    CHECK(dark && dark->primary == Color(0xFF000000u | std::uint32_t(kSystemRoleCount + 1)));
    CHECK(light && light->scrim == LightColorScheme().scrim && light->onError == LightColorScheme().onError);
}
