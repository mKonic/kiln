#include "kiln/graphics/PathShadow.h"

#include "SkDrawShadowInfo.h"
#include "SkShadowTessellator.h"

#include <algorithm>
#include <cmath>

namespace kiln::gfx {
    namespace {
        SkPath ToSkPath(const Path& path) {
            SkPath result;
            result.fVerbs.reserve(path.Verbs().size());
            for (Path::Verb v : path.Verbs()) {
                switch (v) {
                    case Path::Verb::Move: result.fVerbs.push_back(SkPathVerb::kMove); break;
                    case Path::Verb::Line: result.fVerbs.push_back(SkPathVerb::kLine); break;
                    case Path::Verb::Quad: result.fVerbs.push_back(SkPathVerb::kQuad); break;
                    case Path::Verb::Conic: result.fVerbs.push_back(SkPathVerb::kConic); break;
                    case Path::Verb::Cubic: result.fVerbs.push_back(SkPathVerb::kCubic); break;
                    case Path::Verb::Close: result.fVerbs.push_back(SkPathVerb::kClose); break;
                }
            }
            result.fPoints.reserve(path.Points().size());
            for (ui::Offset p : path.Points()) result.fPoints.push_back({ p.x, p.y });
            result.fConicWeights = path.ConicWeights();
            const ui::Rect b = path.Bounds();
            result.fBounds = SkRect::MakeLTRB(b.left, b.top, b.right, b.bottom);
            return result;
        }

        bool FinitePath(const Path& path) {
            return std::all_of(path.Points().begin(), path.Points().end(), [](ui::Offset p) { return std::isfinite(p.x) && std::isfinite(p.y); }) &&
                   std::all_of(path.ConicWeights().begin(), path.ConicWeights().end(), [](float w) { return std::isfinite(w); });
        }

        // A finite path must remain on one side of the camera plane. Besides avoiding a
        // projection through infinity, this bounds the derivative used by the blur fallback.
        bool ProjectionBounds(const Path& path, const SkMatrix& m, float& minW, float& maxX, float& maxY) {
            minW = std::numeric_limits<float>::infinity();
            maxX = maxY = 0;
            int sign = 0;
            const auto b = path.Bounds();
            for (SkPoint p : SkRect::MakeLTRB(b.left, b.top, b.right, b.bottom).toQuad()) {
                const float w = p.fX * m[6] + p.fY * m[7] + m[8];
                if (!std::isfinite(w) || w == 0 || (sign && sign != (w > 0 ? 1 : -1))) return false;
                sign = w > 0 ? 1 : -1;
                minW = std::min(minW, std::fabs(w));
                maxX = std::max(maxX, std::fabs(p.fX * m[0] + p.fY * m[1] + m[2]));
                maxY = std::max(maxY, std::fabs(p.fX * m[3] + p.fY * m[4] + m[5]));
            }
            return std::isfinite(maxX) && std::isfinite(maxY);
        }

        Path ProjectPath(const Path& path, const SkMatrix& m) {
            if (!m.hasPerspective()) {
                Path p = path;
                p.Transform({ m[0], m[3], m[1], m[4], m[2], m[5] });
                return p;
            }
            float minW, maxX, maxY;
            if (!ProjectionBounds(path, m, minW, maxX, maxY)) return {};
            // Bound the homography's Jacobian on the control-point bounds. Flattening at this
            // local tolerance keeps projected curves within the normal 0.05 device-pixel error.
            const double w = minW;
            const double dx = (std::fabs(m[0]) + maxX * std::fabs(m[6]) / w) / w;
            const double dy = (std::fabs(m[1]) + maxX * std::fabs(m[7]) / w) / w;
            const double ex = (std::fabs(m[3]) + maxY * std::fabs(m[6]) / w) / w;
            const double ey = (std::fabs(m[4]) + maxY * std::fabs(m[7]) / w) / w;
            const double scale = std::sqrt(dx * dx + dy * dy + ex * ex + ey * ey);
            Path p;
            p.fillType = path.fillType;
            for (const Polyline& c : path.Flatten({}, float(0.05 / std::max(scale, 1e-6)))) {
                bool first = true;
                for (ui::Offset v : c.points) {
                    const SkPoint q = m.mapPoint({ v.x, v.y });
                    if (!q.isFinite()) return {};
                    if (first) { p.MoveTo(q.fX, q.fY); first = false; }
                    else p.LineTo(q.fX, q.fY);
                }
                if (c.closed) p.Close();
            }
            return p;
        }

        float BlurSigma(float radius) {
            // SkBlurMask::ConvertRadiusToSigma (kBLUR_SIGMA_SCALE = 0.57735f).
            return radius > 0 ? 0.57735f * radius + 0.5f : 0;
        }
    }

    PathShadow MakePathShadow(const Path& path, const ShadowProjection& projection, const ShadowLight* light, bool transparent) {
        PathShadow result;
        if (path.IsEmpty() || !FinitePath(path) ||
            !std::all_of(projection.matrix.begin(), projection.matrix.end(), [](float x) { return std::isfinite(x); }) ||
            !std::all_of(projection.zPlane.begin(), projection.zPlane.end(), [](float x) { return std::isfinite(x); })) return result;
        SkMatrix m;
        m.fMat = projection.matrix;
        float minW, maxX, maxY;
        if (!ProjectionBounds(path, m, minW, maxX, maxY)) return result;
        const SkPath p = ToSkPath(path);
        const auto& z = projection.zPlane;
        const SkPoint3 zPlane{ z[0], z[1], z[2] };
        sk_sp<SkVertices> mesh;
        SkPoint3 lightPos;
        if (light) {
            lightPos = { light->x, light->y, light->z };
            if (!lightPos.isFinite() || !std::isfinite(light->radius) || light->radius < 0) return result;
            mesh = SkShadowTessellator::MakeSpot(p, m, zPlane, lightPos, light->radius, transparent, false);
        } else {
            // AmbientVerticesFactory generates affine meshes without translation, then moves
            // the result. Besides matching the cached path, this avoids translation-dependent
            // rounding in curve subdivision and polygon offsets.
            SkMatrix canonical = m;
            if (!m.hasPerspective()) {
                canonical.fMat[2] = 0;
                canonical.fMat[5] = 0;
            }
            mesh = SkShadowTessellator::MakeAmbient(p, canonical, zPlane, transparent);
        }
        if (mesh) {
            result.triangles.reserve(mesh->indices.size());
            for (std::uint16_t i : mesh->indices) {
                SkPoint q = mesh->positions[i];
                if (!light && !m.hasPerspective()) q.offset(m[2], m[5]);
                if (!q.isFinite()) return {};
                result.triangles.push_back({ { q.fX, q.fY }, SkColorGetA(mesh->colors[i]) / 255.0f });
            }
            return result;
        }

        // SkDevice::drawShadow: the tessellator failed. Blur a filled outline instead, in
        // device pixels so the penumbra is not stretched by the caster's transform.
        float radius;
        if (light) {
            SkMatrix shadow;
            if (!SkDrawShadowMetrics::GetSpotShadowTransform(lightPos, light->radius, m, zPlane, p.getBounds(), false, &shadow, &radius)) return result;
            result.blurPath = ProjectPath(path, shadow);
        } else {
            const float outset = SkDrawShadowMetrics::AmbientBlurRadius(zPlane.fZ);
            radius = 0.5f * outset * SkDrawShadowMetrics::AmbientRecipAlpha(zPlane.fZ);
            result.strokeWidth = std::max(0.5f * (outset - radius), 0.0f);
            result.blurPath = ProjectPath(path, m);
        }
        if (result.blurPath.IsEmpty() || !FinitePath(result.blurPath) || !std::isfinite(radius)) return {};
        result.sigma = BlurSigma(radius);
        result.blurred = true;
        return result;
    }
}
