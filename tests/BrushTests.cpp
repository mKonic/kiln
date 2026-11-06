#include "Check.h"

#include "kiln/graphics/Brush.h"
#include "kiln/ui/ColorSpace.h"

using namespace kiln;
using namespace kiln::gfx;

TEST(ColorLerpRunsInOklabAndClamps) {
    const Color red(0xffff0000), blue(0xff0000ff);
    CHECK(ui::Lerp(red, blue, 0) == red);
    CHECK(ui::Lerp(red, blue, 1) == blue);
    CHECK(ui::Lerp(red, blue, 1.5f) == blue);
    // Oklab's midpoint is not the sRGB average (0x80, 0, 0x80).
    CHECK(ui::Lerp(red, blue, 0.5f) != Color(0xff800080));
}

TEST(BrushLerpBetweenSolidsAndGradients) {
    const Color red(0xffff0000), blue(0xff0000ff), green(0xff00ff00);
    const Brush solid = Brush::SolidColor(green);
    const Brush linear = Brush::LinearGradient({ red, blue }, { 0, 0 }, { 100, 0 });
    // Solid to solid.
    const Brush s = Lerp(Brush::SolidColor(red), &solid, 0.5f);
    CHECK(s.kind == Brush::Kind::Solid);
    CHECK(s.colors[0] == ui::Lerp(red, green, 0.5f));
    // A solid towards a gradient: the gradient filled with the solid colour, then lerped.
    const Brush g = Lerp(solid, &linear, 0.25f);
    CHECK(g.kind == Brush::Kind::Linear);
    CHECK(g.colors.size() == 2u);
    CHECK(g.colors[0] == ui::Lerp(green, red, 0.25f));
    CHECK(g.colors[1] == ui::Lerp(green, blue, 0.25f));
    CHECK_EQ(g.end.x, 100.0f);
    // Endpoints and colour lists of two linear gradients; the shorter list repeats its last.
    const Brush wide = Brush::LinearGradient({ red, green, blue }, { 0, 0 }, { 200, 0 });
    const Brush m = Lerp(linear, &wide, 0.5f);
    CHECK(m.colors.size() == 3u);
    CHECK(m.colors[2] == blue);
    CHECK_EQ(m.end.x, 150.0f);
    // Unspecified (infinite) ends do not interpolate: they switch at the midpoint.
    const Brush open = Brush::LinearGradient({ red, blue });
    CHECK_EQ(Lerp(linear, &open, 0.4f).end.x, 100.0f);
    CHECK(Lerp(linear, &open, 0.6f).end.x == Brush::kInf);
    // Different kinds switch at the midpoint; null is transparent.
    const Brush sweep = Brush::SweepGradient({ red, blue });
    CHECK(Lerp(linear, &sweep, 0.4f).kind == Brush::Kind::Linear);
    CHECK(Lerp(linear, &sweep, 0.6f).kind == Brush::Kind::Sweep);
    CHECK(Lerp(linear, nullptr, 1).colors[0] == Color(0x00000000));
}
