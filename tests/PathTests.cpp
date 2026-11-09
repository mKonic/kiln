#include "Check.h"

#include "kiln/graphics/PathEffect.h"
#include "kiln/graphics/PathIterator.h"
#include "kiln/graphics/Vertices.h"
#include "kiln/graphics/Rasterizer.h"
#include "kiln/graphics/vector/ImageVector.h"

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

TEST(MoveOnlyStrokeContoursDoNotBecomeCapsButZeroLengthSegmentsDo) {
    for (auto cap : { StrokeCap::Butt, StrokeCap::Round, StrokeCap::Square }) {
        const Stroke stroke{ 4, 4, cap };
        Path move;
        move.MoveTo(10, 10);
        CHECK(StrokeToPolygons(move, stroke, {}).empty());
        Path line;
        line.MoveTo(0, 0); line.LineTo(20, 0);
        const auto original = StrokeToPolygons(line, stroke, {});
        line.MoveTo(100, 100); // the final track move must not leave a cap-sized dot
        const auto trailing = StrokeToPolygons(line, stroke, {});
        CHECK_EQ(trailing.size(), original.size());
        Path zero;
        zero.MoveTo(10, 10); zero.LineTo(10, 10);
        CHECK(StrokeToPolygons(zero, stroke, {}).empty() == (cap == StrokeCap::Butt));
        move.Close(); // Skia treats move+close as a zero-length line for non-butt caps
        CHECK(StrokeToPolygons(move, stroke, {}).empty() == (cap == StrokeCap::Butt));
    }
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

TEST(OvalsAreConicsLikeSkia) {
    gfx::Path p;
    p.AddOval({ 0, 0, 100, 100 }, gfx::Path::Direction::Clockwise);
    // Move at the right-hand point, four quarter conics through the bounds' corners, close.
    CHECK(p.Verbs().size() == 6u);
    CHECK(p.Verbs()[1] == gfx::Path::Verb::Conic);
    CHECK_EQ(p.Points()[0].x, 100.0f);
    CHECK_EQ(p.Points()[1].x, 100.0f);   // control point: the bottom-right corner
    CHECK_EQ(p.Points()[1].y, 100.0f);
    CHECK(std::abs(p.ConicWeights()[0] - 0.70710678f) < 1e-6f);
    // Every point of the flattened circle lies on it.
    for (const auto& poly : p.Flatten(gfx::Matrix{}, 0.05f))
        for (const auto& q : poly.points) CHECK(std::abs(std::hypot(q.x - 50, q.y - 50) - 50) < 0.06f);
    // A quarter arc is one conic.
    gfx::Path arc;
    arc.ArcTo({ 0, 0, 100, 100 }, 0, 90, true);
    CHECK(arc.Verbs().size() == 2u);
    CHECK(arc.Verbs()[1] == gfx::Path::Verb::Conic);
}

TEST(PathIteratorReportsSegmentsWithTheirStartPoints) {
    gfx::Path p;
    p.MoveTo(0, 0);
    p.LineTo(10, 0);
    p.ArcTo({ 0, 0, 20, 20 }, 0, 90, false);
    p.Close();
    gfx::PathIterator asConic(p, gfx::PathIterator::ConicEvaluation::AsConic);
    std::vector<gfx::PathSegment> segs;
    while (asConic.HasNext()) segs.push_back(asConic.Next());
    CHECK(asConic.Next().type == gfx::PathSegment::Type::Done);
    CHECK(segs.size() == 5u);   // move, line, line to the arc start, conic, close
    CHECK(segs[1].points.size() == 2u);
    CHECK_EQ(segs[1].points[0].x, 0.0f);
    CHECK(segs[3].type == gfx::PathSegment::Type::Conic);
    CHECK_EQ(segs[3].points[0].x, 20.0f);
    CHECK(segs[3].weight > 0.7f);
    // By default the conic becomes quadratics.
    gfx::PathIterator quads(p);
    CHECK(quads.CalculateSize() >= 5u);
    CHECK_EQ(quads.CalculateSize(false), 5u);
    bool anyConic = false;
    while (quads.HasNext()) anyConic = anyConic || quads.Next().type == gfx::PathSegment::Type::Conic;
    CHECK(!anyConic);
}

namespace {
    float CoveredArea(const gfx::Path& p) {
        const auto mask = gfx::Rasterize(p.Flatten(gfx::Matrix{}), p.fillType, { 0, 0, 200, 200 });
        float a = 0;
        for (auto v : mask.alpha) a += float(v) / 255.0f;
        return a;
    }
}

TEST(PathOpCombinesAreas) {
    const gfx::Path a = Square(0, 0, 100, 100), b = Square(50, 50, 150, 150);
    using Op = gfx::PathOperation;
    CHECK(std::abs(CoveredArea(gfx::Path::Combine(Op::Union, a, b)) - 17500) < 20);
    CHECK(std::abs(CoveredArea(gfx::Path::Combine(Op::Intersect, a, b)) - 2500) < 20);
    CHECK(std::abs(CoveredArea(gfx::Path::Combine(Op::Difference, a, b)) - 7500) < 20);
    CHECK(std::abs(CoveredArea(gfx::Path::Combine(Op::ReverseDifference, a, b)) - 7500) < 20);
    CHECK(std::abs(CoveredArea(gfx::Path::Combine(Op::Xor, a, b)) - 15000) < 20);
    // A circle cut out of a square keeps its curve (flattened finely).
    gfx::Path circle;
    circle.AddOval({ 25, 25, 75, 75 });
    const float holed = CoveredArea(gfx::Path::Combine(Op::Difference, a, circle));
    CHECK(std::abs(holed - (10000 - 3.14159265f * 625)) < 20);
}

TEST(VerticesExpandToTriangleLists) {
    gfx::Vertices v;
    v.positions = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    v.mode = gfx::VertexMode::TriangleFan;
    CHECK(v.TriangleList() == std::vector<std::uint32_t>({ 0, 1, 2, 0, 2, 3 }));
    v.mode = gfx::VertexMode::TriangleStrip;
    CHECK(v.TriangleList() == std::vector<std::uint32_t>({ 0, 1, 2, 1, 2, 3 }));
    v.mode = gfx::VertexMode::Triangles;
    v.indices = { 3, 2, 1 };
    CHECK(v.TriangleList() == std::vector<std::uint32_t>({ 3, 2, 1 }));
}

TEST(VectorPathDataParsesLikeCompose) {
    using namespace gfx::vector;
    using K = PathNode::Kind;
    // Commas and spaces are optional between numbers; '-' and a second '.' start a new number.
    PathData n = ParsePathString("  M10,10h5l-2-.5.5.5zm1 2");
    CHECK(n.size() == 6);
    CHECK(n[0].kind == K::MoveTo && n[0].v[0] == 10 && n[0].v[1] == 10);
    CHECK(n[1].kind == K::RelativeHorizontalTo && n[1].v[0] == 5);
    CHECK(n[2].kind == K::RelativeLineTo && n[2].v[0] == -2 && n[2].v[1] == -0.5f);
    CHECK(n[3].kind == K::RelativeLineTo && n[3].v[0] == 0.5f && n[3].v[1] == 0.5f);
    CHECK(n[4].kind == K::Close && n[5].kind == K::RelativeMoveTo);
    // A moveto's extra pairs are linetos; exponents belong to numbers.
    n = ParsePathString("M1 2 3e1 4E-1");
    CHECK(n.size() == 2 && n[1].kind == K::LineTo && n[1].v[0] == 30 && std::fabs(n[1].v[1] - 0.4f) < 1e-7f);
    // An arc's flags may run into the next number.
    n = ParsePathString("M0 0a5 5 0 104 0");
    CHECK(n.size() == 2 && n[1].kind == K::RelativeArcTo && n[1].v[3] == 1 && n[1].v[4] == 0 && n[1].v[5] == 4 && n[1].v[6] == 0);
    // The half circle arc becomes cubics ending where the arc ends.
    Path p;
    ToPath(n, p);
    CHECK(std::fabs(p.Points().back().x - 4) < 1e-4f && std::fabs(p.Points().back().y) < 1e-4f);
    // Reflective curves mirror the previous control point.
    n = ParsePathString("M0 0C1 0 2 1 2 2S3 4 4 4");
    Path q;
    ToPath(n, q);
    const auto& pts = q.Points();
    CHECK(pts.size() == 7 && pts[4].x == 2 && pts[4].y == 3);
    // The builder makes the same nodes.
    CHECK(PathBuilder().MoveTo(10, 10).HorizontalLineToRelative(5).Nodes() == PathData(ParsePathString("M10 10h5")));
}
