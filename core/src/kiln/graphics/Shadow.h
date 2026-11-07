#pragma once

// Elevation shadows as Android draws them: HWUI's light (LightingInfo, set per window by
// ThreadedRenderer.setLightCenter) and Skia's shadow geometry (SkDrawShadowMetrics, and the
// analytic round-rect / circle shadow ShadowRRectOp draws through GrRRectShadowGeoProc).

#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <vector>

namespace kiln::gfx {

    // LightingInfo: the light's centre in window pixels, its radius, and the theme's shadow
    // alphas as bytes ((uint8_t)(255 * alpha), android_graphics_HardwareRenderer.cpp).
    struct ShadowLight {
        float x = 0, y = 0, z = 0, radius = 0;
        std::uint8_t ambientAlpha = 0, spotAlpha = 0;
        bool operator==(const ShadowLight&) const = default;
        // ThreadedRenderer: the framework's light (light_y 0 dp, light_z 500 dp, light_radius
        // 800 dp; ambientShadowAlpha 0.039, spotShadowAlpha 0.19 in the Material themes) for a
        // window at `windowLeft/Top` on a display of `displayWidth x displayHeight` pixels.
        static ShadowLight Framework(float density, float displayWidth, float displayHeight, float windowLeft, float windowTop);
    };

    namespace ShadowMetrics {
        float AmbientBlurRadius(float height);
        float AmbientRecipAlpha(float height);
        float SpotBlurRadius(float occluderZ, float lightZ, float lightRadius);
        void GetSpotParams(float occluderZ, float lightX, float lightY, float lightZ, float lightRadius,
                           float* blurRadius, float* scale, ui::Offset* translate);
    }

    // A vertex of the analytic shadow mesh: device position, premultiplied colour, and the
    // shadow parameters (offset in the unit corner circle, distance correction) the falloff
    // lookup reads.
    struct ShadowVertex {
        float x, y;
        float r, g, b, a;
        float px, py, pz;
    };

    // ShadowCircularRRectOp: the triangles (a list) of one shadow rrect in device space.
    // `insetWidth` past half the smaller side fills it.
    void AppendShadowRRect(std::vector<ShadowVertex>& out, const float rgbaPremul[4], ui::Rect devRect, float devRadius, bool isCircle,
                           float blurRadius, float insetWidth);

    // The falloff texture (create_falloff_texture): 128 A8 texels, exp(-4 d^2) - 0.018.
    float ShadowFalloffTexel(int i);

}
