#include "Check.h"

#include "kiln/graphics/PathEffect.h"
#include "kiln/graphics/PathMeasure.h"
#include "kiln/graphics/Rasterizer.h"

#include <cmath>
#include <numbers>

using namespace kiln;
using namespace kiln::gfx;

namespace {
    bool Near(float a, float b, float tol) { return std::abs(a - b) <= tol; }
    float TotalLength(const Path& p) {
        float sum = 0;
        for (const auto& c : MeasureContours(p, false)) sum += c.Length();
        return sum;
    }
    float StrokeArea(const Path& p, Stroke s) {
        auto polys = StrokeToPolygons(p, s, {});
        const Mask m = Rasterize(polys, FillType::NonZero, { -1000, -1000, 1000, 1000 });
        float a = 0;
        for (auto v : m.alpha) a += v / 255.0f;
        return a;
    }
}

TEST(MeasureLinesAndCurves) {
    Path p;
    p.MoveTo(0, 0); p.LineTo(30, 0); p.LineTo(30, 40);
    PathMeasure m;
    m.SetPath(&p, false);
    CHECK(Near(m.Length(), 70, 1e-3f));
    CHECK(Near(m.Position(45).y, 15, 1e-3f));
    CHECK(Near(m.Tangent(45).y, 1, 1e-4f));
    m.SetPath(&p, true);                                      // forceClosed adds the diagonal
    CHECK(Near(m.Length(), 120, 1e-3f));
    Path circle;
    circle.AddOval({ 0, 0, 100, 100 });
    m.SetPath(&circle, false);
    CHECK(Near(m.Length(), std::numbers::pi_v<float> * 100, 0.05f));
    // Only the first contour counts (android.graphics.PathMeasure).
    circle.AddRect({ 200, 200, 210, 210 });
    m.SetPath(&circle, false);
    CHECK(Near(m.Length(), std::numbers::pi_v<float> * 100, 0.05f));
}

TEST(SegmentsAreSubCurves) {
    Path circle;
    circle.AddOval({ 0, 0, 100, 100 });
    PathMeasure m;
    m.SetPath(&circle, false);
    Path half;
    CHECK(m.GetSegment(0, m.Length() / 2, half));
    CHECK(Near(TotalLength(half), m.Length() / 2, 0.05f));
    CHECK(half.Verbs().size() <= 4);                         // chopped cubics, not a polyline
    Path none;
    CHECK(!m.GetSegment(30, 30, none));                       // empty range
    CHECK(none.IsEmpty());
}

TEST(DashKeepsTheOnIntervals) {
    Path line;
    line.MoveTo(0, 0); line.LineTo(100, 0);
    const Path dashed = PathEffect::Dash({ 10, 10 })->Apply(line);
    CHECK(Near(TotalLength(dashed), 50, 1e-3f));              // 5 dashes of 10
    CHECK_EQ(int(MeasureContours(dashed, false).size()), 5);
    // Phase 5: starts halfway into the first dash.
    const Path shifted = PathEffect::Dash({ 10, 10 }, 5)->Apply(line);
    CHECK(Near(TotalLength(shifted), 50, 1e-3f));
    CHECK_EQ(int(MeasureContours(shifted, false).size()), 6);
}

TEST(DashOnAClosedContourJoinsTheEnds) {
    Path sq;
    sq.AddRect({ 0, 0, 30, 30 });                             // length 120
    // 25 on / 10 off: the last dash wraps through the start and continues into the first.
    const Path dashed = PathEffect::Dash({ 25, 10 })->Apply(sq);
    CHECK(Near(TotalLength(dashed), 25 * 3 + 15, 1e-2f));    // 3 dashes, then 15 joined with the first
}

TEST(CornerEffectRoundsCorners) {
    Path sq;
    sq.AddRect({ 0, 0, 40, 40 });
    const Path rounded = PathEffect::Corner(10)->Apply(sq);
    // Each corner trades 20 px of straight edges for a quad of about 15.
    const float len = TotalLength(rounded);
    CHECK(len < 160 - 4 * 3);
    CHECK(len > 160 - 4 * 7);
}

TEST(StrokeAppliesThePathEffect) {
    Path line;
    line.MoveTo(10, 10); line.LineTo(110, 10);
    Stroke s{ 2 };
    s.pathEffect = PathEffect::Dash({ 10, 10 });
    CHECK(Near(StrokeArea(line, s), 100, 0.6f));             // half of 200
}
