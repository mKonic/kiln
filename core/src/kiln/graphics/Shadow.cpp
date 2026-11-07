#include "kiln/graphics/Shadow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace kiln::gfx {

    ShadowLight ShadowLight::Framework(float density, float displayWidth, float displayHeight, float windowLeft, float windowTop) {
        // ThreadedRenderer(): lightY / lightZ / lightRadius are theme dimensions (dimens.xml:
        // light_y 0dp, light_z 500dp, light_radius 800dp) in pixels; setLightCenter places the
        // light over the display's horizontal centre and scales its height with the display.
        const float mLightY = 0.0f * density;
        const float mLightZ = 500.0f * density;
        const float mLightRadius = 800.0f * density;
        ShadowLight l;
        l.x = displayWidth / 2.0f - windowLeft;
        l.y = mLightY - windowTop;
        const float zRatio = std::min(displayWidth, displayHeight) / (450.0f * density);
        const float zWeightedAdjustment = (zRatio + 2) / 3.0f;
        l.z = mLightZ * zWeightedAdjustment;
        l.radius = mLightRadius;
        // setLightSourceAlpha(ambientShadowAlpha 0.039, spotShadowAlpha 0.19)
        l.ambientAlpha = std::uint8_t(255 * 0.039f);
        l.spotAlpha = std::uint8_t(255 * 0.19f);
        return l;
    }

    // --- SkDrawShadowMetrics -----------------------------------------------------------------------

    namespace ShadowMetrics {
        namespace {
            constexpr float kAmbientHeightFactor = 1.0f / 128.0f;
            constexpr float kAmbientGeomFactor = 64.0f;
            constexpr float kMaxAmbientRadius = 300 * kAmbientHeightFactor * kAmbientGeomFactor;
            float DivideAndPin(float numer, float denom, float min, float max) {
                const float r = numer / denom;   // sk_ieee_float_divide
                return std::isnan(r) ? min : std::clamp(r, min, max);   // SkTPin
            }
        }

        float AmbientBlurRadius(float height) { return std::min(height * kAmbientHeightFactor * kAmbientGeomFactor, kMaxAmbientRadius); }
        float AmbientRecipAlpha(float height) { return 1.0f + std::max(height * kAmbientHeightFactor, 0.0f); }
        float SpotBlurRadius(float occluderZ, float lightZ, float lightRadius) {
            return lightRadius * DivideAndPin(occluderZ, lightZ - occluderZ, 0.0f, 0.95f);
        }
        void GetSpotParams(float occluderZ, float lightX, float lightY, float lightZ, float lightRadius, float* blurRadius, float* scale,
                           ui::Offset* translate) {
            const float zRatio = DivideAndPin(occluderZ, lightZ - occluderZ, 0.0f, 0.95f);
            *blurRadius = lightRadius * zRatio;
            *scale = DivideAndPin(lightZ, lightZ - occluderZ, 1.0f, 1.95f);
            *translate = { -zRatio * lightX, -zRatio * lightY };
        }
    }

    // --- ShadowRRectOp geometry --------------------------------------------------------------------

    namespace {
        constexpr std::uint16_t kFillCircleIndices[] = {
            0, 1, 8, 1, 2, 8, 2, 3, 8, 3, 4, 8, 4, 5, 8, 5, 6, 8, 6, 7, 8, 7, 0, 8,
        };
        constexpr std::uint16_t kStrokeCircleIndices[] = {
            0, 1, 9, 0, 9, 8, 1, 2, 10, 1, 10, 9, 2, 3, 11, 2, 11, 10, 3, 4, 12, 3, 12, 11,
            4, 5, 13, 4, 13, 12, 5, 6, 14, 5, 14, 13, 6, 7, 15, 6, 15, 14, 7, 0, 8, 7, 8, 15,
        };
        constexpr std::uint16_t kRRectIndices[] = {
            // overstroke quads (skipped unless overstroked)
            0, 6, 25, 0, 25, 24, 6, 18, 27, 6, 27, 25, 18, 12, 26, 18, 26, 27, 12, 0, 24, 12, 24, 26,
            // corners
            0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5,
            6, 11, 10, 6, 10, 9, 6, 9, 8, 6, 8, 7,
            12, 17, 16, 12, 16, 15, 12, 15, 14, 12, 14, 13,
            18, 19, 20, 18, 20, 21, 18, 21, 22, 18, 22, 23,
            // edges
            0, 5, 11, 0, 11, 6, 6, 7, 19, 6, 19, 18, 18, 23, 17, 18, 17, 12, 12, 13, 1, 12, 1, 0,
            // fill quad (only when filled)
            0, 6, 18, 0, 18, 12,
        };
        constexpr int kIndicesPerOverstrokeRRect = int(std::size(kRRectIndices)) - 6;
        constexpr int kIndicesPerStrokeRRect = kIndicesPerOverstrokeRRect - 6 * 4;
        constexpr int kIndicesPerFillRRect = kIndicesPerStrokeRRect + 6;

        enum class RRectType { Fill, Stroke, Overstroke };

        struct CircleVertex { ui::Offset pos; ui::Offset offset; float distanceCorrection; };

        void FillInCircleVerts(std::vector<CircleVertex>& v, ui::Rect bounds, float outerRadius, float innerRadius, float blurRadius,
                               bool isStroked) {
            const float distanceCorrection = outerRadius / blurRadius;
            const float rIn = innerRadius;
            innerRadius = innerRadius / outerRadius;   // normalized
            const ui::Offset center{ (bounds.left + bounds.right) * 0.5f, (bounds.top + bounds.bottom) * 0.5f };
            const float halfWidth = 0.5f * bounds.Width();
            const float octOffset = 0.41421356237f;   // sqrt(2) - 1
            auto add = [&](float px, float py, float ox, float oy) { v.push_back({ { center.x + px, center.y + py }, { ox, oy }, distanceCorrection }); };
            add(-octOffset * halfWidth, -halfWidth, -octOffset, -1);
            add(octOffset * halfWidth, -halfWidth, octOffset, -1);
            add(halfWidth, -octOffset * halfWidth, 1, -octOffset);
            add(halfWidth, octOffset * halfWidth, 1, octOffset);
            add(octOffset * halfWidth, halfWidth, octOffset, 1);
            add(-octOffset * halfWidth, halfWidth, -octOffset, 1);
            add(-halfWidth, octOffset * halfWidth, -1, octOffset);
            add(-halfWidth, -octOffset * halfWidth, -1, -octOffset);
            if (isStroked) {
                // the inner ring (cosine and sine of pi/8)
                const float c = 0.923579533f, s = 0.382683432f, r = rIn;
                add(-s * r, -c * r, -s * innerRadius, -c * innerRadius);
                add(s * r, -c * r, s * innerRadius, -c * innerRadius);
                add(c * r, -s * r, c * innerRadius, -s * innerRadius);
                add(c * r, s * r, c * innerRadius, s * innerRadius);
                add(s * r, c * r, s * innerRadius, c * innerRadius);
                add(-s * r, c * r, -s * innerRadius, c * innerRadius);
                add(-c * r, s * r, -c * innerRadius, s * innerRadius);
                add(-c * r, -s * r, -c * innerRadius, -s * innerRadius);
            } else {
                add(0, 0, 0, 0);
            }
        }

        void FillInRRectVerts(std::vector<CircleVertex>& v, ui::Rect bounds, float outerRadius, float umbraInset, float innerRadius,
                              float blurRadius, RRectType type) {
            const float minDim = 0.5f * std::min(bounds.Width(), bounds.Height());
            if (umbraInset > minDim) umbraInset = minDim;
            const std::array<float, 4> xInner = { bounds.left + umbraInset, bounds.right - umbraInset, bounds.left + umbraInset, bounds.right - umbraInset };
            const std::array<float, 4> xMid = { bounds.left + outerRadius, bounds.right - outerRadius, bounds.left + outerRadius, bounds.right - outerRadius };
            const std::array<float, 4> xOuter = { bounds.left, bounds.right, bounds.left, bounds.right };
            const std::array<float, 4> yInner = { bounds.top + umbraInset, bounds.top + umbraInset, bounds.bottom - umbraInset, bounds.bottom - umbraInset };
            const std::array<float, 4> yMid = { bounds.top + outerRadius, bounds.top + outerRadius, bounds.bottom - outerRadius, bounds.bottom - outerRadius };
            const std::array<float, 4> yOuter = { bounds.top, bounds.top, bounds.bottom, bounds.bottom };
            // The skewed corner vectors that keep the corner a quarter circle (see ShadowRRectOp).
            ui::Offset outerVec{ outerRadius - umbraInset, -outerRadius - umbraInset };
            const float len = std::sqrt(outerVec.x * outerVec.x + outerVec.y * outerVec.y);
            if (len > 0) outerVec = { outerVec.x / len, outerVec.y / len };   // SkVector::normalize
            const float diagVal = umbraInset / (1.41421356f * (outerRadius - umbraInset) - outerRadius);
            const ui::Offset diagVec{ diagVal, diagVal };
            const float distanceCorrection = umbraInset / blurRadius;
            for (int i = 0; i < 4; ++i) {
                v.push_back({ { xInner[i], yInner[i] }, { 0, 0 }, distanceCorrection });
                v.push_back({ { xOuter[i], yInner[i] }, { 0, -1 }, distanceCorrection });
                v.push_back({ { xOuter[i], yMid[i] }, outerVec, distanceCorrection });
                v.push_back({ { xOuter[i], yOuter[i] }, diagVec, distanceCorrection });
                v.push_back({ { xMid[i], yOuter[i] }, outerVec, distanceCorrection });
                v.push_back({ { xInner[i], yOuter[i] }, { 0, -1 }, distanceCorrection });
            }
            if (type == RRectType::Overstroke) {
                const float inset = umbraInset + innerRadius;
                v.push_back({ { bounds.left + inset, bounds.top + inset }, { 0, 0 }, distanceCorrection });
                v.push_back({ { bounds.right - inset, bounds.top + inset }, { 0, 0 }, distanceCorrection });
                v.push_back({ { bounds.left + inset, bounds.bottom - inset }, { 0, 0 }, distanceCorrection });
                v.push_back({ { bounds.right - inset, bounds.bottom - inset }, { 0, 0 }, distanceCorrection });
            }
        }
    }

    void AppendShadowRRect(std::vector<ShadowVertex>& out, const float c[4], ui::Rect devRect, float devRadius, bool isCircle,
                           float blurRadius, float insetWidth) {
        // ShadowCircularRRectOp's constructor
        const ui::Rect bounds = devRect;
        float innerRadius = 0.0f;
        const float outerRadius = devRadius;
        const float umbraInset = isCircle ? 0.0f : std::max(outerRadius, blurRadius);
        RRectType type = RRectType::Fill;
        if (isCircle) {
            innerRadius = devRadius - insetWidth;
            type = innerRadius > 0 ? RRectType::Stroke : RRectType::Fill;
        } else if (insetWidth <= 0.5f * std::min(devRect.Width(), devRect.Height())) {
            innerRadius = std::max(insetWidth - umbraInset, 0.0f);
            type = innerRadius > 0 ? RRectType::Overstroke : RRectType::Stroke;
        }

        std::vector<CircleVertex> verts;
        const std::uint16_t* indices;
        int indexCount;
        if (isCircle) {
            const bool stroked = type == RRectType::Stroke;
            FillInCircleVerts(verts, bounds, outerRadius, innerRadius, blurRadius, stroked);
            indices = stroked ? kStrokeCircleIndices : kFillCircleIndices;
            indexCount = stroked ? int(std::size(kStrokeCircleIndices)) : int(std::size(kFillCircleIndices));
        } else {
            FillInRRectVerts(verts, bounds, outerRadius, umbraInset, innerRadius, blurRadius, type);
            indices = type == RRectType::Overstroke ? kRRectIndices : kRRectIndices + 6 * 4;
            indexCount = type == RRectType::Fill ? kIndicesPerFillRRect : type == RRectType::Stroke ? kIndicesPerStrokeRRect : kIndicesPerOverstrokeRRect;
        }
        out.reserve(out.size() + std::size_t(indexCount));
        for (int i = 0; i < indexCount; ++i) {
            const CircleVertex& v = verts[indices[i]];
            out.push_back({ v.pos.x, v.pos.y, c[0], c[1], c[2], c[3], v.offset.x, v.offset.y, v.distanceCorrection });
        }
    }

    float ShadowFalloffTexel(int i) {
        const float d = 1.0f - float(i) / 127.0f;
        return float(int(std::floor((std::exp(-4 * d * d) - 0.018f) * 255 + 0.5f))) / 255.0f;   // SkScalarRoundToInt
    }

}
