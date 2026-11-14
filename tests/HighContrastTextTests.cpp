// High-contrast text's colours (HWUI DrawTextFunctor's adjustHighContrastInnerTextColor and
// shouldDarken, Layout's background) and the owner's switch.
#include "Check.h"

#include "kiln/graphics/ColorSpace.h"
#include "kiln/text/HighContrastText.h"
#include "kiln/ui/LayoutNode.h"

using namespace kiln;

namespace {
    float L(ui::Color c) { return gfx::colorspace::ConvertColor(c, gfx::colorspace::ColorSpaces::CieLab())[0]; }
    float A(ui::Color c) { return gfx::colorspace::ConvertColor(c, gfx::colorspace::ColorSpaces::CieLab())[1]; }
}

TEST(HighContrastTextColoursFollowHwui) {
    using namespace text;
    const ui::Color black(0xFF000000u), white(0xFFFFFFFFu), darkGray(0xFF333333u), midDark(0xFF6E6E6Eu), midLight(0xFF8A8A8Au);
    // Dark text: white outline and background; light text: black ones.
    CHECK(ShouldDarkenForHighContrast(black) && !ShouldDarkenForHighContrast(white));
    CHECK(HighContrastOutlineColor(black) == white && HighContrastOutlineColor(white) == black);
    CHECK(HighContrastBackgroundColor(black) == ui::Color(0xB3FFFFFFu) && HighContrastBackgroundColor(white) == ui::Color(0xB3000000u));
    // Grey goes to black or white, near-middle grey to L 20 or 80.
    CHECK(HighContrastInnerColor(darkGray) == black);
    CHECK(HighContrastInnerColor(ui::Color(0xFFDDDDDDu)) == white);
    CHECK(L(midDark) > 40 && L(midDark) <= 50 && std::abs(L(HighContrastInnerColor(midDark)) - 20) < 1);
    CHECK(L(midLight) > 50 && L(midLight) < 60 && std::abs(L(HighContrastInnerColor(midLight)) - 80) < 1);
    // Colour keeps its hue: red is pushed to L 90 (outside sRGB, so the channels clamp, as
    // they do on an 8-bit surface), a light blue reaches it, dark blue stays (already at most 20).
    const ui::Color red(0xFFFF0000u), sky(0xFF80C0FFu), navy(0xFF000040u);
    CHECK(!ShouldDarkenForHighContrast(red) && L(HighContrastInnerColor(red)) > L(red) + 10 && A(HighContrastInnerColor(red)) > 10);
    CHECK(!ShouldDarkenForHighContrast(sky) && std::abs(L(HighContrastInnerColor(sky)) - 90) < 1.5f);
    CHECK(ShouldDarkenForHighContrast(navy) && L(navy) <= 20 && HighContrastInnerColor(navy) == navy);
    // Translucent text comes out opaque.
    CHECK(HighContrastInnerColor(ui::Color(0x80000000u)) == black);
    // Sizes.
    CHECK(HighContrastBorderWidth(14) == 4 && HighContrastBorderWidth(40) == 8);
    CHECK(HighContrastBackgroundRadius(14, 2) == 10 && HighContrastBackgroundRadius(40, 2) == 20);
}

TEST(HighContrastTextRedrawsEverything) {
    ui::Owner owner;
    const auto before = owner.DrawGeneration();
    owner.SetHighContrastText(true, true);
    CHECK(owner.HighContrastText() && owner.DrawGeneration() == before + 1);
    owner.SetHighContrastText(true, true);   // unchanged
    CHECK(owner.DrawGeneration() == before + 1);
    owner.SetHighContrastText(false, true);
    CHECK(!owner.HighContrastText() && owner.DrawGeneration() == before + 2);
}
