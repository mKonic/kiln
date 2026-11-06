#include "Check.h"

#include "kiln/graphics/PathEffect.h"
#include "kiln/graphics/Rasterizer.h"

#include <cmath>
#include <numbers>

using namespace kiln;
using namespace kiln::gfx;

namespace {
    // Total covered area in pixels.
    float Area(const Mask& m) {
        float a = 0;
        for (std::uint8_t v : m.alpha) a += v / 255.0f;
        return a;
    }
    float At(const Mask& m, int x, int y) {
        x -= m.x; y -= m.y;
        if (x < 0 || y < 0 || x >= m.width || y >= m.height) return 0;
        return m.alpha[std::size_t(y) * m.width + x] / 255.0f;
    }
    Mask Fill(const Path& p, Matrix m = {}) {
        auto polys = p.Flatten(m);
        return Rasterize(polys, p.fillType, { -1000, -1000, 1000, 1000 });
    }
    Mask StrokeMask(const Path& p, Stroke s, Matrix m = {}) {
        auto polys = StrokeToPolygons(p, s, m);
        return Rasterize(polys, FillType::NonZero, { -1000, -1000, 1000, 1000 });
    }
    bool Near(float a, float b, float tol) { return std::abs(a - b) <= tol; }
}

TEST(RectFillCoversExactArea) {
    Path p;
    p.AddRect({ 2.5f, 3, 12.5f, 9 });       // 10 x 6, half pixels on both x edges
    const Mask m = Fill(p);
    CHECK(Near(Area(m), 60, 0.05f));
    CHECK(Near(At(m, 2, 5), 0.5f, 0.01f));
    CHECK(Near(At(m, 7, 5), 1.0f, 0.01f));
    CHECK(Near(At(m, 12, 5), 0.5f, 0.01f));
    CHECK_EQ(At(m, 7, 2), 0.0f);
}

TEST(DirectionDoesNotChangeASingleFill) {
    Path cw, ccw;
    cw.AddRect({ 0, 0, 10, 10 }, Path::Direction::Clockwise);
    ccw.AddRect({ 0, 0, 10, 10 }, Path::Direction::CounterClockwise);
    CHECK(Near(Area(Fill(cw)), 100, 0.05f));
    CHECK(Near(Area(Fill(ccw)), 100, 0.05f));
}

TEST(FillTypesOnOverlappingContours) {
    Path p;
    p.AddRect({ 0, 0, 20, 20 });
    p.AddRect({ 5, 5, 15, 15 });            // same direction: winding 2 inside
    CHECK(Near(Area(Fill(p)), 400, 0.1f));  // NonZero fills the hole
    p.fillType = FillType::EvenOdd;
    CHECK(Near(Area(Fill(p)), 300, 0.1f));  // EvenOdd leaves it empty
}

TEST(CircleAreaWithinFlatteningError) {
    Path p;
    p.AddOval({ 0, 0, 40, 40 });
    const float expected = std::numbers::pi_v<float> * 400;
    CHECK(Near(Area(Fill(p)), expected, expected * 0.003f));
    // A transform scales the area.
    CHECK(Near(Area(Fill(p, Matrix::Scale(2, 1))), 2 * expected, 2 * expected * 0.003f));
}

TEST(RoundRectRadiiAreClampedToFit) {
    Path p;
    p.AddRoundRect(RoundRect::Uniform({ 0, 0, 20, 10 }, 50));   // radii shrink to a pill
    const float pill = 10 * 10 + std::numbers::pi_v<float> * 25;
    CHECK(Near(Area(Fill(p)), pill, pill * 0.006f));
}

TEST(StrokeAreaMatchesWidthTimesLength) {
    Path line;
    line.MoveTo(10, 10);
    line.LineTo(50, 10);
    CHECK(Near(Area(StrokeMask(line, { 4 })), 160, 0.2f));                                 // butt
    CHECK(Near(Area(StrokeMask(line, { 4, 4, StrokeCap::Square })), 176, 0.2f));            // + 2x2 per end
    const float round = 160 + std::numbers::pi_v<float> * 4;
    CHECK(Near(Area(StrokeMask(line, { 4, 4, StrokeCap::Round })), round, 0.5f));
    // Hairline: one device pixel wide even under scale.
    CHECK(Near(Area(StrokeMask(line, {}, Matrix::Scale(2, 2))), 80, 0.5f));   // alpha quantisation adds ~0.3
}

TEST(ClosedStrokeLeavesTheInsideEmpty) {
    Path sq;
    sq.AddRect({ 10, 10, 30, 30 });
    const Mask m = StrokeMask(sq, { 2 });
    // Miter joins: outer 22x22 minus inner 18x18.
    CHECK(Near(Area(m), 22 * 22 - 18 * 18, 0.3f));
    CHECK_EQ(At(m, 20, 20), 0.0f);
    // Bevel cuts each outer corner by a 1x1 triangle of area 0.5.
    CHECK(Near(Area(StrokeMask(sq, { 2, 4, StrokeCap::Butt, StrokeJoin::Bevel })), 22 * 22 - 18 * 18 - 2, 0.3f));
}

TEST(MiterLimitFallsBackToBevel) {
    Path v;                                 // a sharp spike: the miter would be very long
    v.MoveTo(0, 0); v.LineTo(40, 2); v.LineTo(0, 4);
    const float limited = Area(StrokeMask(v, { 2, 4 }));
    const float unlimited = Area(StrokeMask(v, { 2, 100 }));
    CHECK(unlimited > limited + 5);
}

TEST(ClipLimitsTheMask) {
    Path p;
    p.AddRect({ -50, -50, 50, 50 });
    auto polys = p.Flatten({});
    const Mask m = Rasterize(polys, FillType::NonZero, { 0, 0, 10, 10 });
    CHECK_EQ(m.width, 10);
    CHECK(Near(Area(m), 100, 0.05f));
}

namespace {
    int CountMoves(const gfx::Path& p) {
        int n = 0;
        for (auto v : p.Verbs()) if (v == gfx::Path::Verb::Move) ++n;
        return n;
    }
    gfx::Path Square(float x0, float y0, float x1, float y1) {
        gfx::Path p;
        p.MoveTo(x0, y0); p.LineTo(x1, y0); p.LineTo(x1, y1); p.LineTo(x0, y1); p.Close();
        return p;
    }
}

TEST(StampedEffectPlacesShapesAlongThePath) {
    gfx::Path line;
    line.MoveTo(0, 0);
    line.LineTo(50, 0);
    // Phase 0: from the start, every 10 while the distance stays short of the length.
    auto e = gfx::PathEffect::Stamped(Square(-1, -1, 1, 1), 10, 0, gfx::PathEffect::StampStyle::Translate);
    gfx::Path out = e->Apply(line);
    CHECK_EQ(CountMoves(out), 5);
    CHECK_EQ(out.Points()[4 * 4].x, 39.0f);   // the fifth stamp's first point
    CHECK(e->FillsResult());
    // A positive phase shifts back into the pattern: the first stamp lands at advance - phase.
    out = gfx::PathEffect::Stamped(Square(-1, -1, 1, 1), 10, 3, gfx::PathEffect::StampStyle::Translate)->Apply(line);
    CHECK_EQ(out.Points()[0].x, 6.0f);
    // Rotate turns the stamp with the tangent: along a downward line, +x points down.
    gfx::Path down;
    down.MoveTo(0, 0);
    down.LineTo(0, 30);
    out = gfx::PathEffect::Stamped(Square(4, -1, 6, 1), 100, 0, gfx::PathEffect::StampStyle::Rotate)->Apply(down);
    CHECK(std::abs(out.Points()[0].x - 1.0f) < 1e-4f);
    CHECK(std::abs(out.Points()[0].y - 4.0f) < 1e-4f);
    // Morph bends the stamp: x runs along the path, y along its normal; lines become quads.
    out = gfx::PathEffect::Stamped(Square(0, -1, 4, 1), 100, 0, gfx::PathEffect::StampStyle::Morph)->Apply(down);
    CHECK(out.Verbs()[1] == gfx::Path::Verb::Quad);
    CHECK(std::abs(out.Points()[0].x - 1.0f) < 1e-4f);
    CHECK(std::abs(out.Points()[0].y - 0.0f) < 1e-4f);
}
