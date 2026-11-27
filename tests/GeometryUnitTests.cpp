// ui-geometry / ui-unit operations, ported from RoundRectTest, RectTest, OffsetTest, IntOffsetTest,
// IntRectTest and TextUnitTest.
#include "Check.h"

#include "kiln/graphics/Path.h"
#include "kiln/text/TextStyle.h"
#include "kiln/ui/Geometry.h"

using namespace kiln;
using ui::Offset;
using ui::Rect;

TEST(RoundRectContainsScalesItsRadii) {
    auto check = [](float s) {
        gfx::RoundRect r{ Rect{ 1, 1, 2, 2 }, { 0.5f * s, 0.5f * s }, { 0.25f * s, 0.25f * s }, { 0.25f * s, 0.75f * s }, { 0, 0 } };
        CHECK(!r.Contains({ 1.0f, 1.0f }));
        CHECK(!r.Contains({ 1.1f, 1.1f }));
        CHECK(r.Contains({ 1.15f, 1.15f }));
        CHECK(!r.Contains({ 2.0f, 1.0f }));
        CHECK(!r.Contains({ 1.93f, 1.07f }));
        CHECK(!r.Contains({ 1.97f, 1.7f }));
        CHECK(r.Contains({ 1.7f, 1.97f }));
        CHECK(r.Contains({ 1.0f, 1.99f }));
    };
    check(1);
    check(10000);   // testRoundRectContainsLargeRadii
}

TEST(RectInflateOverlapsAndDimensions) {
    const Rect r{ 0, 0, 10, 20 };
    CHECK_EQ(r.Inflate(5), (Rect{ -5, -5, 15, 25 }));
    CHECK_EQ(r.Deflate(2), (Rect{ 2, 2, 8, 18 }));
    CHECK(r.Overlaps({ 5, 5, 15, 15 }));
    CHECK(!r.Overlaps({ 10, 0, 20, 20 }));   // touching edges do not overlap
    CHECK_EQ(r.MaxDimension(), 20.0f);
    CHECK_EQ((Rect{ 10, 0, 0, 5 }).MinDimension(), 5.0f);   // magnitudes
    CHECK(r.IsFinite() && !r.IsInfinite());
    CHECK((Rect{ 0, 0, INFINITY, 1 }).IsInfinite());
    CHECK(!(Rect{ 0, NAN, 1, 1 }).IsFinite());
    const ui::IntRect ir{ 0, 0, 10, 10 };
    CHECK_EQ(ir.Inflate(1), (ui::IntRect{ -1, -1, 11, 11 }));
    CHECK(ir.Overlaps({ 9, 9, 12, 12 }) && !ir.Overlaps({ 10, 0, 12, 10 }));
    CHECK_EQ((ui::IntRect{ 0, 0, -30, 4 }).MaxDimension(), 30);
}

TEST(OffsetAndIntOffsetArithmetic) {
    const Offset o{ 3, 4 };
    CHECK_EQ(o.GetDistance(), 5.0f);
    CHECK_EQ(o.GetDistanceSquared(), 25.0f);
    CHECK_EQ(-o, (Offset{ -3, -4 }));
    CHECK_EQ(o * 2, (Offset{ 6, 8 }));
    CHECK_EQ(o / 2, (Offset{ 1.5f, 2 }));
    CHECK_EQ((Offset{ 5, -7 } % 3), (Offset{ 2, -1 }));
    CHECK(o.IsValid() && (Offset{ INFINITY, 0 }).IsValid() && !(Offset{ NAN, 0 }).IsValid());
    const ui::IntOffset i{ 10, -10 };
    CHECK_EQ(i * 0.25f, (ui::IntOffset{ 3, -2 }));   // 2.5 -> 3, -2.5 -> -2 (round half up)
    CHECK_EQ(i / 4.0f, (ui::IntOffset{ 3, -2 }));
    CHECK_EQ(i % 3, (ui::IntOffset{ 1, -1 }));
    CHECK_EQ(-i, (ui::IntOffset{ -10, 10 }));
    CHECK_EQ((ui::IntSize{ 3, 5 } * 2), (ui::IntSize{ 6, 10 }));
    CHECK_EQ((ui::DpSize{ ui::Dp(4), ui::Dp(8) } / 2), (ui::DpSize{ ui::Dp(2), ui::Dp(4) }));
    const ui::Density d{ 2, 1 };
    CHECK_EQ(d.ToDpSize({ 10, 20 }), (ui::DpSize{ ui::Dp(5), ui::Dp(10) }));
    CHECK(!d.ToDpSize(ui::Size::Unspecified()).Specified());
    CHECK_EQ(text::ToSp(d, ui::Dp(16)), text::Sp(16));
    CHECK_EQ(text::ToSp(d, 32.0f), text::Sp(16));
}

TEST(TextUnitArithmeticKeepsTheUnit) {
    CHECK_EQ(text::Sp(3) * 2, text::Sp(6));
    CHECK_EQ(text::Em(3) / 2, text::Em(1.5f));
    CHECK_EQ(-text::Sp(3), text::Sp(-3));
    CHECK(text::Sp(1) < text::Sp(2));
    CHECK_THROWS(text::Sp(1) < text::Em(2));
    CHECK_THROWS(text::TextUnit::Unspecified() * 2);
}
