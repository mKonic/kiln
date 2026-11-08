#include "Check.h"

#include "kiln/graphics/Shadow.h"
#include "kiln/graphics/PathShadow.h"
#include "kiln/foundation/Layouts.h"

#include <algorithm>
#include <limits>

#include <cmath>

using namespace kiln;
using namespace kiln::gfx;

TEST(FrameworkLightFollowsThreadedRenderer) {
    // A 1080 x 2400 display at density 2.625, the window at its top-left.
    const ShadowLight l = ShadowLight::Framework(2.625f, 1080, 2400, 0, 0);
    CHECK(std::fabs(l.x - 540) < 1e-3f);
    CHECK(std::fabs(l.y - 0) < 1e-3f);
    CHECK(std::fabs(l.z - 1275.0f) < 0.01f);
    CHECK(std::fabs(l.radius - 2100.0f) < 1e-3f);
    CHECK_EQ(int(l.ambientAlpha), 9);
    CHECK_EQ(int(l.spotAlpha), 48);
    // A window lower on the display moves the light up in its coordinates.
    CHECK(std::fabs(ShadowLight::Framework(2.625f, 1080, 2400, 100, 300).y + 300) < 1e-3f);
}

TEST(ShadowMetricsMatchSkia) {
    CHECK(std::fabs(ShadowMetrics::AmbientBlurRadius(10) - 5.0f) < 1e-5f);        // 10/128*64
    CHECK(std::fabs(ShadowMetrics::AmbientBlurRadius(1000) - 150.0f) < 1e-5f);    // capped at 300/128*64
    CHECK(std::fabs(ShadowMetrics::AmbientRecipAlpha(64) - 1.5f) < 1e-5f);
    float blur, scale;
    ui::Offset t;
    ShadowMetrics::GetSpotParams(100, 540, 0, 1100, 800, &blur, &scale, &t);
    CHECK(std::fabs(blur - 80.0f) < 1e-4f);      // 800 * 100 / 1000
    CHECK(std::fabs(scale - 1.1f) < 1e-5f);      // 1100 / 1000
    CHECK(std::fabs(t.x + 54.0f) < 1e-4f && std::fabs(t.y) < 1e-6f);
}

TEST(ShadowFalloffAndMeshFollowShadowRRectOp) {
    CHECK(std::fabs(ShadowFalloffTexel(0) - 0.0f) < 1e-6f);
    CHECK(std::fabs(ShadowFalloffTexel(127) - 250.0f / 255.0f) < 1e-6f);
    std::vector<ShadowVertex> v;
    const float c[4] = { 0, 0, 0, 0.5f };
    // A filled rrect: 4 corner fans (4 triangles each), 4 edge quads and the centre quad.
    AppendShadowRRect(v, c, { 0, 0, 100, 60 }, 10, false, 8, 100);
    CHECK_EQ(int(v.size()), (16 + 8 + 2) * 3);
    v.clear();
    // A stroked one with an inset larger than the umbra: the overstroke quads are added.
    AppendShadowRRect(v, c, { 0, 0, 100, 60 }, 10, false, 8, 20);
    CHECK_EQ(int(v.size()), (8 + 16 + 8) * 3);
    v.clear();
    AppendShadowRRect(v, c, { 0, 0, 40, 40 }, 20, true, 8, 100);   // filled circle: an octagon fan
    CHECK_EQ(int(v.size()), 8 * 3);
    // Outer octagon vertices sit on the unit circle in the shadow parameters.
    CHECK(std::fabs(std::hypot(v[0].px, v[0].py) - std::hypot(0.41421356f, 1.0f)) < 1e-5f);
}


namespace {
    bool FiniteShadow(const PathShadow& s) {
        return std::all_of(s.triangles.begin(), s.triangles.end(), [](const ShadowMeshVertex& v) {
            return std::isfinite(v.position.x) && std::isfinite(v.position.y) && v.coverage >= 0 && v.coverage <= 1;
        });
    }
    bool Covers(const PathShadow& s, ui::Offset p) {
        auto cross = [](ui::Offset a, ui::Offset b, ui::Offset c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
        for (std::size_t i = 0; i + 2 < s.triangles.size(); i += 3) {
            const auto a = s.triangles[i].position, b = s.triangles[i + 1].position, c = s.triangles[i + 2].position;
            const float area = cross(a, b, c);
            if (std::fabs(area) < 1e-6f) continue;
            const float u = cross(p, b, c) / area, v = cross(a, p, c) / area, w = cross(a, b, p) / area;
            if (u >= 0 && v >= 0 && w >= 0 &&
                u * s.triangles[i].coverage + v * s.triangles[i + 1].coverage + w * s.triangles[i + 2].coverage > 0) return true;
        }
        return false;
    }
}

TEST(PathShadowTessellatesCurvesAndNonuniformCorners) {
    Path oval;
    oval.AddOval({ 0, 0, 100, 60 });
    Path quad;
    quad.MoveTo(0, 60); quad.QuadraticTo(50, -60, 100, 60); quad.Close();
    Path cubic;
    cubic.MoveTo(0, 60); cubic.CubicTo(0, -20, 100, -20, 100, 60); cubic.Close();
    Path corners;
    corners.AddRoundRect({ { 0, 0, 100, 60 }, { 30, 10 }, { 5, 20 }, { 15, 30 }, { 0, 0 } });
    Path concave;
    concave.MoveTo(0, 0); concave.LineTo(100, 0); concave.LineTo(50, 30); concave.LineTo(100, 60); concave.LineTo(0, 60); concave.Close();
    ShadowProjection projection;
    projection.zPlane = { 0, 0, 12 };
    const ShadowLight light = ShadowLight::Framework(1, 400, 800, 0, 0);
    for (const Path& path : { oval, quad, cubic, corners, concave }) {
        for (const ShadowLight* source : { static_cast<const ShadowLight*>(nullptr), &light }) {
            for (bool transparent : { false, true }) {
                const PathShadow s = MakePathShadow(path, projection, source, transparent);
                CHECK(s.blurred || !s.triangles.empty());
                CHECK(FiniteShadow(s));
                CHECK_EQ(s.triangles.size() % 3, std::size_t(0));
            }
        }
    }
    // The Ganesh subdivision must be enabled: control polygons would reach y=-60/-20,
    // whereas both curves reach y=0, plus the six-pixel ambient outset.
    for (const Path& curved : { quad, cubic }) {
        const auto mesh = MakePathShadow(curved, projection, nullptr, true);
        CHECK(!mesh.blurred && !mesh.triangles.empty());
        CHECK(std::all_of(mesh.triangles.begin(), mesh.triangles.end(), [](const ShadowMeshVertex& v) { return v.position.y >= -7; }));
    }
    Path rect;
    rect.AddRect({ 0, 0, 100, 60 });
    CHECK(!Covers(MakePathShadow(rect, projection, nullptr, false), { 50, 30 }));
    CHECK(Covers(MakePathShadow(rect, projection, nullptr, true), { 50, 30 }));
}

TEST(PathShadowFallbackRetainsContoursFillRuleAndSkiaMetrics) {
    Path path;
    path.fillType = FillType::EvenOdd;
    path.AddOval({ 0, 0, 100, 60 });
    path.AddOval({ 20, 15, 80, 45 });
    ShadowProjection projection;
    projection.zPlane = { 0, 0, 12 };
    projection.matrix = { 2, 0, 30, 0, 0.5f, 40, 0, 0, 1 };
    const auto ambient = MakePathShadow(path, projection, nullptr, false);
    CHECK(ambient.blurred);
    CHECK(ambient.blurPath.fillType == FillType::EvenOdd);
    CHECK_EQ(ambient.blurPath.Verbs().size(), path.Verbs().size());
    CHECK(std::fabs(ambient.blurPath.Bounds().left - 30) < 1e-4f);
    CHECK(std::fabs(ambient.strokeWidth - 0.5f * (6 - 0.5f * 6 * (1 + 12.f / 128))) < 1e-5f);
    CHECK(std::fabs(ambient.sigma - (0.57735f * 0.5f * 6 * (1 + 12.f / 128) + 0.5f)) < 1e-5f);
    const ShadowLight light = ShadowLight::Framework(1, 400, 800, 0, 0);
    const auto spot = MakePathShadow(path, projection, &light, false);
    CHECK(spot.blurred);
    CHECK(spot.blurPath.fillType == FillType::EvenOdd);
    CHECK_EQ(spot.strokeWidth, 0.0f);
    CHECK(std::fabs(spot.sigma - (0.57735f * ShadowMetrics::SpotBlurRadius(12, light.z, light.radius) + 0.5f)) < 1e-5f);
}

TEST(PathShadowTransformsAndInvalidInput) {
    Path path;
    path.AddOval({ 0, 0, 100, 60 });
    ShadowProjection projection;
    projection.zPlane = { 0, 0, 12 };
    const auto origin = MakePathShadow(path, projection, nullptr, true);
    projection.matrix[2] = 123;
    projection.matrix[5] = -80;
    const auto translated = MakePathShadow(path, projection, nullptr, true);
    CHECK_EQ(origin.triangles.size(), translated.triangles.size());
    CHECK(!origin.triangles.empty());
    bool moved = true;
    for (std::size_t i = 0; i < std::min(origin.triangles.size(), translated.triangles.size()); ++i)
        moved &= std::fabs(translated.triangles[i].position.x - origin.triangles[i].position.x - 123) < 1e-3f &&
                 std::fabs(translated.triangles[i].position.y - origin.triangles[i].position.y + 80) < 1e-3f;
    CHECK(moved);
    projection.matrix = { 0.95f, 0.1f, 24, -0.2f, 0.98f, 42, 0.0004f, 0.0009f, 1 };
    projection.zPlane = { -0.05f, 0.1f, 12 };
    const ShadowLight light = ShadowLight::Framework(1, 400, 800, 0, 0);
    for (const ShadowLight* source : { static_cast<const ShadowLight*>(nullptr), &light }) {
        const auto tilted = MakePathShadow(path, projection, source, true);
        CHECK(tilted.blurred || !tilted.triangles.empty());
        CHECK(FiniteShadow(tilted));
    }
    projection.matrix[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(MakePathShadow(path, projection, nullptr, false).triangles.empty());
    projection = {};
    projection.zPlane = { 0, 0, 12 };
    Path invalid;
    invalid.MoveTo(0, 0); invalid.LineTo(std::numeric_limits<float>::infinity(), 30); invalid.LineTo(50, 0); invalid.Close();
    CHECK(MakePathShadow(invalid, projection, nullptr, false).triangles.empty());
    const auto empty = MakePathShadow({}, projection, nullptr, false);
    CHECK(!empty.blurred && empty.triangles.empty());
}


TEST(PerspectiveShadowUsesNativeRoundRectWinding) {
    // The platform's Outline.setRoundRect makes a clockwise path. The counter-clockwise
    // path default leaves a visible wedge in Skia's opaque perspective umbra for this tilt.
    foundation::GraphicsLayerParams layer;
    layer.rotationX = 18;
    layer.rotationY = -24;
    ShadowProjection projection;
    foundation::GraphicsLayerMatrix(layer, { 168, 168 }, projection.matrix.data());
    auto h = projection.matrix;
    for (int i = 0; i < 3; ++i) {
        projection.matrix[i] = h[i] + 735 * h[6 + i];
        projection.matrix[3 + i] = h[3 + i] + 1496 * h[6 + i];
    }
    const float radians = 3.14159265358979323846f / 180;
    const float a = -std::cos(18 * radians) * std::sin(-24 * radians), b = std::sin(18 * radians);
    projection.zPlane = { a, b, 31.5f - 84 * a - 84 * b };
    Path path;
    path.AddRoundRect(RoundRect::Uniform({ 0, 0, 168, 168 }, 31.5f), Path::Direction::Clockwise);
    const ShadowLight light = ShadowLight::Framework(2.625f, 1080, 2400, 0, 0);
    const auto shadow = MakePathShadow(path, projection, &light, false);
    CHECK(!shadow.blurred && !shadow.triangles.empty());
    CHECK(FiniteShadow(shadow));
    CHECK(Covers(shadow, { 850, 1685 }));
}
