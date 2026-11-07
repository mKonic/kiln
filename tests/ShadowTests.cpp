#include "Check.h"

#include "kiln/graphics/Shadow.h"

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
