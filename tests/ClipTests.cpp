#include "Check.h"

#include "kiln/ui/Draw.h"

using namespace kiln;
using namespace kiln::ui;

namespace {
    bool Near(const ClipRadii& r, ClipRadii e) {
        for (int i = 0; i < 4; ++i) if (std::abs(r[i] - e[i]) > 1e-3f) return false;
        return true;
    }
}

TEST(NestedClipKeepsTheOuterRoundingUnderASquareChild) {
    // A rounded card holding a full-size list clipped to its bounds: the card's corners stay.
    Rect out;
    ClipRadii radii;
    CHECK(IntersectRoundedRects({ 0, 0, 100, 100 }, { 12, 12, 12, 12 }, { -10, 0, 110, 200 }, {}, out, radii));
    CHECK_EQ(out.right, 100.0f);
    CHECK(Near(radii, { 12, 12, 12, 12 }));
}

TEST(NestedClipTakesEachCornerFromItsOwner) {
    Rect out;
    ClipRadii radii;
    // Inner rect inside the straight part of the outer: its own corners.
    CHECK(IntersectRoundedRects({ 0, 0, 100, 100 }, { 20, 20, 20, 20 }, { 20, 20, 80, 80 }, { 4, 4, 4, 4 }, out, radii));
    CHECK(Near(radii, { 4, 4, 4, 4 }));
    // A strip across the top: outer corners at the top, square where the edges cross below.
    CHECK(IntersectRoundedRects({ 0, 0, 100, 100 }, { 10, 10, 10, 10 }, { -5, -5, 105, 50 }, {}, out, radii));
    CHECK(Near(radii, { 10, 10, 0, 0 }));
    // Shared corners take the larger rounding.
    CHECK(IntersectRoundedRects({ 0, 0, 100, 100 }, { 10, 10, 10, 10 }, { 0, 0, 100, 100 }, { 16, 0, 0, 0 }, out, radii));
    CHECK(Near(radii, { 16, 10, 10, 10 }));
}

TEST(NestedClipRefusesARectCuttingIntoARoundedCorner) {
    Rect out;
    ClipRadii radii;
    // The inner rect's corner sits inside the outer's rounded corner: not one rounded rect.
    CHECK(!IntersectRoundedRects({ 0, 0, 100, 100 }, { 30, 30, 30, 30 }, { 5, 5, 95, 95 }, {}, out, radii));
    // A strip whose bottom edge crosses the outer's corner arcs.
    CHECK(!IntersectRoundedRects({ 0, 0, 100, 100 }, { 30, 30, 30, 30 }, { -5, -5, 105, 20 }, {}, out, radii));
}
