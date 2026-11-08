#include "Check.h"

#include "kiln/graphics/Shadow.h"
#include "kiln/graphics/PathShadow.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shadow.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Shapes.h"

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

TEST(PaintedShadowValuesFollowCompose) {
    using namespace ui;
    using shadow::Shadow;
    const Shadow color(1_dp, Color(0xff00ffff), 2_dp, { 3_dp, 4_dp }, 0.5f, BlendMode::Dst);
    const Shadow solid(1_dp, Brush::SolidColor(Color(0xff00ffff)), 2_dp, { 3_dp, 4_dp }, 0.5f, BlendMode::Dst);
    CHECK(color == solid);
    CHECK(!solid.brush);
    CHECK(Shadow(1_dp, Color::Unspecified()).color == Color(0xff000000));
    CHECK_EQ(Shadow(1_dp, Color{}, {}, {}, 2).alpha, 1.0f);
    CHECK_EQ(Shadow(1_dp, Color{}, {}, {}, -1).alpha, 0.0f);
    const Brush gradient = Brush::HorizontalGradient({ Color(0xffff0000), Color(0xff0000ff) });
    const Shadow start(2_dp, gradient, 4_dp, { 6_dp, 8_dp }, 0.4f, BlendMode::Src);
    const Shadow end(6_dp, gradient, 8_dp, { 10_dp, 12_dp }, 0.8f, BlendMode::Dst);
    const Shadow middle = shadow::Lerp(start, end, 0.5f);
    CHECK(middle.radius == 4_dp && middle.spread == 6_dp);
    CHECK((middle.offset == DpOffset{ 8_dp, 10_dp }));
    CHECK(std::fabs(middle.alpha - 0.6f) < 1e-6f);
    CHECK(middle.blendMode == BlendMode::Dst);
    CHECK(shadow::Lerp(start, end, -0.5f).radius == 0_dp);
    CHECK(!shadow::Lerp(std::optional<Shadow>{}, std::optional<Shadow>{}, 0.5f));
    const auto entering = shadow::Lerp(std::optional<Shadow>{}, std::optional<Shadow>{ color }, 0.5f);
    CHECK(entering && entering->radius == color.radius && entering->offset == color.offset);
    CHECK(entering && entering->color.Alpha() < color.color.Alpha());
}

namespace {
    double MaskArea(const shadow::ShadowMask& mask) {
        const auto& image = mask.image;
        double sum = 0;
        for (int i = 0; i < image.Width() * image.Height(); ++i) sum += double(image.Pixels()[i * 4]) * image.Pixels()[i * 4 + 1] / (255.0 * 255.0);
        return sum;
    }
}

TEST(PaintedShadowMasksKeepComposeGeometryAndShareOffsets) {
    using namespace ui;
    using shadow::Shadow;
    shadow::ShadowContext context;
    const auto shape = foundation::RectangleShape();
    const Size size{ 40, 24 };
    auto get = [&](const Shadow& shadow, bool inner = false, Density d = {}) {
        return context.Obtain(shape, size, LayoutDirection::Ltr, d, shadow, inner);
    };
    Shadow s(0_dp, Color(0xff000000), 3_dp);
    const auto drop = get(s);
    CHECK(drop && drop->image.Width() == 46 && drop->image.Height() == 30);
    CHECK(drop && std::fabs(MaskArea(*drop) - 46 * 30) < 0.01);
    s.offset = { 4_dp, 2_dp };
    s.color = Color(0xff00ffff);
    CHECK(get(s) == drop);   // geometry cache ignores drop offset and tint
    const auto inner = get(s, true);
    s.offset = {};
    const auto centred = get(s, true);
    CHECK(inner && centred && inner != centred);
    CHECK(centred && std::fabs(MaskArea(*centred) - (40 * 24 - 34 * 18)) < 0.01);
    CHECK(get(s, false, Density{ 2, 1 }) != drop);
    context.ClearCache();
    CHECK(get(s) != drop);
    for (float radius : { 0.1f, 1.0f, 2.0f, 8.0f, 240.0f }) {
        const auto blurred = get(Shadow(Dp(radius)));
        CHECK(blurred && MaskArea(*blurred) > 0 && MaskArea(*blurred) <= 40 * 24 + 1);
    }
    const auto ring = std::make_shared<GenericShape>([](Path& p, Size, LayoutDirection) {
        p.fillType = FillType::EvenOdd;
        p.AddRect({ 0, 0, 40, 24 }); p.AddRect({ 10, 6, 30, 18 });
    });
    const auto ringMask = context.Obtain(ring, size, LayoutDirection::Ltr, {}, Shadow(0_dp), false);
    CHECK(ringMask && std::fabs(MaskArea(*ringMask) - (40 * 24 - 20 * 12)) < 0.01);
    CHECK(!get(Shadow(Dp(std::numeric_limits<float>::infinity()))));
}

TEST(PaintedShadowBlocksReadLazilyWithoutRecomposition) {
    using namespace ui;
    using namespace foundation;
    Owner owner;
    rt::Dispatcher dispatcher;
    rt::Composition composition(owner.Root(), dispatcher);
    rt::State<float> offset{ 0 };
    int compositions = 0, reads = 0;
    composition.SetContent([&] {
        ++compositions;
        Spacer(DropShadow(SizeMod(Modifier{}, 40_dp, 24_dp), RectangleShape(), [&](DropShadowScope& s) {
            ++reads;
            s.radius = s.ToPx(2_dp);
            s.offset = { offset.Get(), 0 };
        }));
    });
    composition.Recompose();
    owner.MeasureAndLayout({ 100, 100 });
    CHECK_EQ(reads, 0);
    owner.Draw({});
    owner.Draw({});
    CHECK_EQ(reads, 1);
    offset.Set(8);
    rt::Observation::ApplyChanges();
    composition.Recompose();
    CHECK_EQ(reads, 1);
    owner.Draw({});
    CHECK_EQ(reads, 2);
    CHECK_EQ(compositions, 1);
    owner.SetDensity({ 2, 1 });
    owner.MeasureAndLayout({ 100, 100 });
    owner.Draw({});
    CHECK_EQ(reads, 3);
}
