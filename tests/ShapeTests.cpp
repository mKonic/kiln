#include "Check.h"

#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/PolygonShape.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    const Density kD{ 2, 1 };
    const gfx::RoundRect& RR(const gfx::Outline& o) { return std::get<gfx::RoundRect>(o.value); }
}

TEST(CornerSizesConvert) {
    CHECK_EQ(CornerSize::Of(Dp(8)).ToPx({ 100, 50 }, kD), 16.0f);
    CHECK_EQ(CornerSize::Percent(50).ToPx({ 100, 50 }, kD), 25.0f);
    CHECK_EQ(CornerSize::Px(3).ToPx({ 100, 50 }, kD), 3.0f);
    const CornerSize mid = CornerSize::Lerp(CornerSize::Of(Dp(8)), CornerSize::Percent(50), 0.5f);
    CHECK_EQ(mid.ToPx({ 100, 50 }, kD), (16.0f + 25.0f) / 2);
}

TEST(CornersScaleDownTogether) {
    // Start corners 40 + 40 exceed the min dimension 50: both scale by 50/80.
    auto s = RoundedCorner(CornerSize::Px(40), CornerSize::Px(10), CornerSize::Px(10), CornerSize::Px(40));
    const auto o = s->CreateOutline({ 200, 50 }, LayoutDirection::Ltr, kD);
    CHECK_EQ(RR(o).topLeft.x, 25.0f);
    CHECK_EQ(RR(o).bottomLeft.x, 25.0f);
    CHECK_EQ(RR(o).topRight.x, 10.0f);
}

TEST(RoundedCornersMirrorInRtl) {
    auto s = RoundedCorner(CornerSize::Px(1), CornerSize::Px(2), CornerSize::Px(3), CornerSize::Px(4));
    const auto ltr = s->CreateOutline({ 100, 100 }, LayoutDirection::Ltr, kD);
    const auto rtl = s->CreateOutline({ 100, 100 }, LayoutDirection::Rtl, kD);
    CHECK_EQ(RR(ltr).topLeft.x, 1.0f);
    CHECK_EQ(RR(rtl).topLeft.x, 2.0f);       // topEnd is on the left in RTL
    CHECK_EQ(RR(rtl).bottomRight.x, 4.0f);
    auto abs = AbsoluteRoundedCorner(CornerSize::Px(1), CornerSize::Px(2), CornerSize::Px(3), CornerSize::Px(4));
    CHECK_EQ(RR(abs->CreateOutline({ 100, 100 }, LayoutDirection::Rtl, kD)).topLeft.x, 1.0f);
}

TEST(ZeroCornersAreARectangleAndCutCornersAPath) {
    CHECK(std::holds_alternative<Rect>(RoundedCorner(Dp(0))->CreateOutline({ 10, 10 }, LayoutDirection::Ltr, kD).value));
    const auto cut = CutCorner(CornerSize::Px(5))->CreateOutline({ 40, 20 }, LayoutDirection::Ltr, kD);
    const auto& p = std::get<gfx::Path>(cut.value);
    CHECK_EQ(int(p.Points().size()), 8);
    CHECK_EQ(p.Points()[0].y, 5.0f);         // starts below the cut top-start corner
}

TEST(ShapesLerp) {
    auto a = RoundedCorner(Dp(0)), b = RoundedCorner(Dp(20));
    auto mid = Lerp(a, b, 0.25f);
    CHECK_EQ(RR(mid->CreateOutline({ 100, 100 }, LayoutDirection::Ltr, kD)).topLeft.x, 10.0f);   // 5dp at density 2
    auto fromRect = Lerp(RectangleShape(), b, 0.5f);
    CHECK(fromRect->Equals(*RoundedCorner(Dp(10))));
}

TEST(PolygonShapesBuildTransformAndMorph) {
    using namespace kiln::foundation;
    const ui::Size size{ 100, 100 };
    const ui::Density d{ 1, 1 };
    const auto ltr = ui::LayoutDirection::Ltr;
    auto hasPointNear = [](const gfx::shapes::RoundedPolygon& p, float x, float y) {
        for (const auto& c : p.Cubics()) if (std::abs(c.Anchor0X() - x) < 1.5f && std::abs(c.Anchor0Y() - y) < 1.5f) return true;
        return false;
    };
    // A builder: a triangle filling the size; the same geometry keeps the same polygon.
    auto tri = PolygonShape::FromBuilder([](PolygonShapeScope& s) { return s.Polygon(3); });
    auto p1 = tri->ResolvePolygon(size, ltr, d);
    CHECK(p1 == tri->ResolvePolygon(size, ltr, d));
    CHECK(hasPointNear(*p1, 100, 50));            // first vertex at angle 0
    CHECK(p1 != tri->ResolvePolygon({ 200, 200 }, ltr, d));
    // Rotated a quarter turn about its centre: that vertex goes to the bottom.
    tri->Transform([](PolygonShapeTransformScope& t) { t.Rotate(90); });
    CHECK(hasPointNear(*tri->ResolvePolygon(size, ltr, d), 50, 100));
    // A copy has the transform too.
    CHECK(hasPointNear(*tri->Copy()->ResolvePolygon(size, ltr, d), 50, 100));

    // scaleToFit stretches a small star to the size's width.
    auto star = PolygonShape::Star(5, 0.4f);
    star->Transform([](PolygonShapeTransformScope& t) { t.Scale(0.5f); t.ScaleToFit(ContentScale::FillBounds()); });
    const auto bounds = star->ResolvePolygon(size, ltr, d)->CalculateBounds(true);
    CHECK(std::abs(bounds[0]) < 0.5f && std::abs(bounds[2] - 100) < 0.5f);

    // Rectangles: start corners follow the layout direction, absolute ones do not.
    auto rect = PolygonShape::Rectangle(PolygonCornerRounding::Fraction(0.5f), {}, {}, {});
    CHECK(!rect->CreateOutline(size, ltr, d).Contains({ 2, 2 }));
    CHECK(rect->CreateOutline(size, ltr, d).Contains({ 98, 2 }));
    CHECK(!rect->CreateOutline(size, ui::LayoutDirection::Rtl, d).Contains({ 98, 2 }));
    auto abs = PolygonShape::AbsoluteRectangle(PolygonCornerRounding::Of(ui::Dp(40)));
    CHECK(!abs->CreateOutline(size, ui::LayoutDirection::Rtl, d).Contains({ 3, 3 }));

    // Corner shapes convert: a cut corner cuts.
    auto cut = ToPolygonShape(CutCornerShape(CornerSize::Px(20), CornerSize::Px(20), CornerSize::Px(20), CornerSize::Px(20)));
    const auto cutOutline = cut->CreateOutline(size, ltr, d);
    CHECK(!cutOutline.Contains({ 3, 3 }));
    CHECK(cutOutline.Contains({ 50, 2 }));
    CHECK(std::abs(cutOutline.Bounds().right - 100) < 0.01f);

    // Morph: the ends are the shapes themselves, the middle is neither.
    float progress = 0;
    auto morph = MorphPolygonShape(PolygonShape::Rectangle(), PolygonShape::Circle(), [&] { return progress; });
    CHECK(morph->CreateOutline(size, ltr, d).Contains({ 2, 2 }));            // a square's corner
    progress = 1;
    CHECK(!morph->CreateOutline(size, ltr, d).Contains({ 2, 2 }));           // the circle's not
    progress = 0.5f;
    const auto mid = morph->CreateOutline(size, ltr, d);
    CHECK(mid.Contains({ 50, 50 }));
    CHECK(!mid.Contains({ 1, 1 }));
    CHECK(mid.Contains({ 8, 8 }));                                           // fuller than the circle there
}
