#include "Check.h"

#include "kiln/graphics/shapes/Morph.h"
#include "kiln/graphics/shapes/Shapes.h"

#include <cmath>

using namespace kiln::gfx::shapes;

namespace {
    bool Near(float a, float b, float eps = 0.01f) { return std::abs(a - b) <= eps; }
    bool CircularCubics(const std::vector<Cubic>& cs, float radius, Point center) {
        for (const Cubic& c : cs)
            for (float t = 0; t <= 1; t += 0.1f)
                if (!Near((c.PointOnCurve(t) - center).Distance(), radius)) return false;
        return true;
    }
    bool CubicsEqualish(const Cubic& a, const Cubic& b) {
        for (int i = 0; i < 8; ++i) if (!Near(a.points[std::size_t(i)], b.points[std::size_t(i)], 1e-4f)) return false;
        return true;
    }
}

// ShapesTest.circleTest
TEST(ShapesCircleCubicsStayOnTheCircle) {
    CHECK(CircularCubics(Circle().Cubics(), 1, {}));
    CHECK(CircularCubics(Circle(3).Cubics(), 1, {}));
    CHECK(CircularCubics(Circle(20).Cubics(), 1, {}));
    CHECK(CircularCubics(Circle(8, 3).Cubics(), 3, {}));
    CHECK(CircularCubics(Circle(8, 1, 1, 2).Cubics(), 1, { 1, 2 }));
}

// ShapesTest.starTest
TEST(ShapesStarAnchorsOnBothRadii) {
    auto onRadii = [](const RoundedPolygon& p, float r1, float r2, Point c) {
        for (const Cubic& cu : p.Cubics())
            for (Point a : { Point{ cu.Anchor0X(), cu.Anchor0Y() }, Point{ cu.Anchor1X(), cu.Anchor1Y() } }) {
                const float d = (a - c).Distance();
                if (!Near(d, r1) && !Near(d, r2)) return false;
            }
        return true;
    };
    CHECK(onRadii(Star(4, 1, 0.5f), 1, 0.5f, {}));
    CHECK(onRadii(Star(4, 1, 0.5f, {}, std::nullopt, nullptr, 1, 2), 1, 0.5f, { 1, 2 }));
    CHECK(onRadii(Star(4, 4, 2), 4, 2, {}));
    // roundedStarTest: rounded stars stay within the unit box.
    const auto b = Star(4, 1, 0.5f, CornerRounding{ 0.1f }, CornerRounding{ 0.2f }).CalculateBounds(false);
    CHECK(b[0] >= -1 - 1e-4f && b[1] >= -1 - 1e-4f && b[2] <= 1 + 1e-4f && b[3] <= 1 + 1e-4f);
}

// FloatMappingTest.simpleMappingTest / targetWrapsTest
TEST(FloatMappingIsPiecewiseLinearAroundTheCircle) {
    const std::vector<float> xs{ 0, 0.5f }, ys{ 0, 0.25f };
    for (float x = 0; x < 1; x += 0.1f) {
        const float expected = x < 0.5f ? x / 2 : (3 * x - 1) / 2;
        CHECK(Near(detail::LinearMap(xs, ys, x), expected, 1e-4f));
    }
    const std::vector<float> xs2{ 0, 0.1f }, ys2{ 0.5f, 0.6f };
    for (float x = 0; x < 1; x += 0.1f) CHECK(Near(detail::LinearMap(xs2, ys2, x), std::fmod(x + 0.5f, 1.0f), 1e-4f));
}

// MorphTest.cubicsTest
TEST(MorphToItselfKeepsTheCubics) {
    const RoundedPolygon p1 = RoundedPolygon::Regular(3, 1, 0.5f, 0.5f);
    const Morph m(p1, p1);
    const auto cubics = m.AsCubics(0);
    CHECK(!cubics.empty());
    for (const Cubic& mc : cubics) {
        bool matched = false;
        for (const Cubic& pc : p1.Cubics()) if (CubicsEqualish(mc, pc)) matched = true;
        CHECK(matched);
    }
}

TEST(MorphEndsAtBothShapes) {
    const RoundedPolygon tri = RoundedPolygon::Regular(3, 1, 0.5f, 0.5f);
    const RoundedPolygon sq = RoundedPolygon::Regular(4, 1, 0.5f, 0.5f);
    const Morph m(tri, sq);
    auto bounds = [](const std::vector<Cubic>& cs) {
        float mnx = 1e9f, mxx = -1e9f;
        for (const Cubic& c : cs) { mnx = std::min(mnx, c.Anchor0X()); mxx = std::max(mxx, c.Anchor0X()); }
        return std::pair{ mnx, mxx };
    };
    const auto [a0, a1] = bounds(m.AsCubics(1));
    const auto sqb = sq.CalculateBounds();
    CHECK(Near(a0, sqb[0], 0.02f));
    CHECK(Near(a1, sqb[2], 0.02f));
    // Contiguous at every progress.
    for (float t : { 0.0f, 0.3f, 0.7f, 1.0f }) {
        const auto cs = m.AsCubics(t);
        for (std::size_t i = 0; i < cs.size(); ++i) {
            const Cubic& a = cs[i];
            const Cubic& b = cs[(i + 1) % cs.size()];
            CHECK(Near(a.Anchor1X(), b.Anchor0X(), 1e-3f) && Near(a.Anchor1Y(), b.Anchor0Y(), 1e-3f));
        }
    }
}

TEST(RoundedPolygonRoundsWithinItsCorners) {
    // A rounded square: every point within the unrounded square, corners pulled in.
    const RoundedPolygon r = Rectangle(2, 2, CornerRounding{ 0.5f });
    const auto b = r.CalculateBounds(false);
    CHECK(Near(b[0], -1, 1e-3f) && Near(b[2], 1, 1e-3f));
    for (const Cubic& c : r.Cubics()) {
        const Point mid = c.PointOnCurve(0.5f);
        CHECK(!(std::abs(mid.x) > 0.9f && std::abs(mid.y) > 0.9f));   // nothing near a sharp corner
    }
}
