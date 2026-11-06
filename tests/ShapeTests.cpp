#include "Check.h"

#include "kiln/foundation/Shapes.h"

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
