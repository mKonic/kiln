#include "Check.h"
#include "kiln/graphics/ColorSpace.h"
#include <cmath>

#include "kiln/material3/internal/colorUtil/ColorUtil.h"
#include "kiln/material3/tokens/ColorLightTokens.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace kiln::material3;
using namespace kiln::material3::colorutil;

// Ported from material3's androidDeviceTest ColorUtilTest.kt.

namespace {
    constexpr std::int32_t BLACK = std::int32_t(0xFF000000u), WHITE = -1, MIDGRAY = std::int32_t(0xFF777777u);
    constexpr std::int32_t RED = std::int32_t(0xFFFF0000u), GREEN = std::int32_t(0xFF00FF00u), BLUE = std::int32_t(0xFF0000FFu);
    bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }
}

TEST(SetLuminanceAtTheEndsGivesBlackAndWhite) {
    for (std::uint32_t c : { 0xFFFF0000u, 0xFF0000FFu, 0xFF00FF00u, 0xFFFF00FFu, 0xFF00FFFFu, 0xFFFFFFFFu, 0xFF000000u }) {
        CHECK_EQ(SetLuminance(c, 0.0f), 0xFF000000u);
        CHECK_EQ(SetLuminance(c, 100.0f), 0xFFFFFFFFu);
    }
}

// ColorUtilTest's per-tone tables are left out: they predate the HCT solver (Red at L* 0 there is
// 5B0000 while setLuminance0_returnsBlack wants black) and pass only because their tolerance,
// 1.0 on 0..1 components, accepts any colour. The tone's L* is checked instead.
TEST(SetLuminanceGivesTheRequestedLstar) {
    for (std::uint32_t base : { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFF000000u, 0xFF6750A4u, 0xFF7D5260u }) {
        for (float l = 10.0f; l <= 90.0f; l += 10.0f) {
            const float got = CamUtils::LstarFromInt(std::int32_t(SetLuminance(base, l)));
            CHECK(Near(got, l, 0.5));
        }
    }
}

TEST(CamAndYMatchCompose) {
    CHECK(Near(CamUtils::YFromLstar(50.0), 18.418, 0.001));
    CHECK(Near(CamUtils::YFromLstar(0.0), 0.0, 0.001));
    CHECK(Near(CamUtils::YFromLstar(100.0), 100.0, 0.001));
    auto cam = [](std::int32_t argb, float j, float chroma, float hue, float m, float s) {
        const Cam c = Cam::FromInt(argb);
        return Near(c.j, j, 0.001) && Near(c.chroma, chroma, 0.001) && Near(c.hue, hue, 0.001) && Near(c.m, m, 0.001) && Near(c.s, s, 0.001);
    };
    CHECK(cam(RED, 46.445f, 113.357f, 27.408f, 89.494f, 91.889f));
    CHECK(cam(GREEN, 79.331f, 108.410f, 142.139f, 85.587f, 78.604f));
    CHECK(cam(BLUE, 25.465f, 87.230f, 282.788f, 68.867f, 93.674f));
    CHECK(cam(BLACK, 0, 0, 0, 0, 0));
    CHECK(cam(WHITE, 100.0f, 2.869f, 209.492f, 2.265f, 12.068f));
    const Cam red = Cam::FromInt(RED);
    CHECK(Near(red.jstar, 59.584f, 0.001) && Near(red.astar, 43.297f, 0.001) && Near(red.bstar, 22.451f, 0.001));
    CHECK(Near(Cam::FromInt(WHITE).Distance(Cam::FromInt(BLACK)), 25.661f, 0.001));
    CHECK(Near(Cam::FromInt(RED).Distance(Cam::FromInt(BLUE)), 21.415f, 0.001));
    const Frame& vc = Frame::Default();
    CHECK(Near(vc.n, 0.184f, 0.001) && Near(vc.aw, 29.981f, 0.001) && Near(vc.nbb, 1.016f, 0.001));
    CHECK(Near(vc.rgbD[0], 1.021f, 0.001) && Near(vc.rgbD[1], 0.986f, 0.001) && Near(vc.rgbD[2], 0.933f, 0.001));
    CHECK(Near(vc.flRoot, 0.789f, 0.001));
}

TEST(GamutMapReturnsTheColourItCameFrom) {
    namespace L = kiln::material3::tokens::ColorLightTokens;
    std::vector<std::int32_t> colors = { RED, GREEN, BLUE, WHITE, BLACK, MIDGRAY, std::int32_t(0xFF009200u) /* -0xff6e00 */ };
    for (kiln::ui::Color c : { L::Surface, L::SurfaceContainer, L::OnSurface, L::Primary, L::OnPrimary, L::Secondary, L::OnSecondary,
                               L::Tertiary, L::OnTertiary, L::PrimaryContainer, L::OnPrimaryContainer, L::SecondaryContainer,
                               L::OnSecondaryContainer, L::TertiaryContainer, L::OnTertiaryContainer, L::Error, L::OnError,
                               L::ErrorContainer, L::OnErrorContainer })
        colors.push_back(std::int32_t(c.argb));
    for (std::int32_t color : colors) {
        const Cam cam = Cam::FromInt(color);
        CHECK_EQ(Cam::GetInt(cam.hue, cam.chroma, CamUtils::LstarFromInt(color)), color);
    }
    const std::int32_t mapped = Cam::GetInt(309.0f, 40.0f, 70.0f);
    const Cam cam = Cam::FromInt(mapped);
    CHECK(Near(cam.hue, 308.759f, 0.001) && Near(cam.chroma, 40.148f, 0.001) && Near(CamUtils::LstarFromInt(mapped), 70.029f, 0.001));
}

TEST(ColorSpacesConvertLikeCompose) {
    using namespace kiln::gfx::colorspace;
    auto near = [](float a, float b, float eps = 2e-3f) { return std::abs(a - b) < eps; };
    // sRGB's transfer: mid grey is 0.214 linear.
    const Vec3 lin = Connect(ColorSpaces::Srgb(), ColorSpaces::LinearSrgb()).Transform(0.5f, 0.5f, 0.5f);
    CHECK(near(lin[0], 0.2140411f));
    // sRGB red inside Display P3.
    const Vec3 p3 = Connect(ColorSpaces::Srgb(), ColorSpaces::DisplayP3()).Transform(1, 0, 0);
    CHECK(near(p3[0], 0.9175f) && near(p3[1], 0.2003f) && near(p3[2], 0.1386f));
    // White is L 100 in L*a*b* (D50, adapted from D65) and L 1 in Oklab.
    const Vec3 lab = Connect(ColorSpaces::Srgb(), ColorSpaces::CieLab()).Transform(1, 1, 1);
    CHECK(near(lab[0], 100.0f, 0.05f) && near(lab[1], 0.0f, 0.05f) && near(lab[2], 0.0f, 0.05f));
    const auto ok = ConvertColor(kiln::ui::Color(0xFFFFFFFFu), ColorSpaces::Oklab());
    CHECK(near(ok[0], 1.0f) && near(ok[1], 0.0f) && near(ok[2], 0.0f) && ok[3] == 1.0f);
    // A Display P3 colour inside sRGB's gamut comes back to the same sRGB colour.
    const auto back = ColorFromSpace(p3[0], p3[1], p3[2], 1, ColorSpaces::DisplayP3());
    CHECK(back == kiln::ui::Color(0xFFFF0000u));
    // Named spaces' flags, and match finds Display P3 from its D50 matrix.
    CHECK(ColorSpaces::Srgb()->IsSrgb() && !ColorSpaces::DisplayP3()->IsSrgb() && ColorSpaces::DisplayP3()->IsWideGamut());
    const auto d50 = std::static_pointer_cast<const Rgb>(Adapt(ColorSpaces::DisplayP3(), Illuminant::D50));
    CHECK(ColorSpaces::Match(d50->Transform(), *std::static_pointer_cast<const Rgb>(ColorSpaces::DisplayP3())->GetTransferParameters()) == ColorSpaces::DisplayP3());
}
