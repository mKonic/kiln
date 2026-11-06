#include "kiln/graphics/Conic.h"

#include <cmath>

namespace kiln::gfx {

    namespace {
        bool Finite(const Offset* p, int count) {
            for (int i = 0; i < count; ++i) if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y)) return false;
            return true;
        }
        // Not normalisable: the same point within float precision (graphics-path `equals`).
        bool SamePoint(Offset a, Offset b) {
            const float dx = a.x - b.x, dy = a.y - b.y;
            return !(std::isfinite(dx) && std::isfinite(dy) && (dx != 0 || dy != 0));
        }
        bool Between(float a, float b, float c) { return (a - b) * (c - b) <= 0; }

        // Recursive halving; the y fix-ups keep y-monotonic conics monotonic (Skia subdivide).
        void Subdivide(const Conic& src, std::vector<Offset>& out, int level) {
            if (level == 0) {
                out.push_back(src.points[1]);
                out.push_back(src.points[2]);
                return;
            }
            Conic dst[2];
            src.Split(dst);
            const float startY = src.points[0].y, endY = src.points[2].y;
            if (Between(startY, src.points[1].y, endY)) {
                const float midY = dst[0].points[2].y;
                if (!Between(startY, midY, endY)) {
                    const float closerY = std::abs(midY - startY) < std::abs(midY - endY) ? startY : endY;
                    dst[0].points[2].y = dst[1].points[0].y = closerY;
                }
                if (!Between(startY, dst[0].points[1].y, dst[0].points[2].y)) dst[0].points[1].y = startY;
                if (!Between(dst[1].points[0].y, dst[1].points[1].y, endY)) dst[1].points[1].y = endY;
            }
            --level;
            Subdivide(dst[0], out, level);
            Subdivide(dst[1], out, level);
        }
    }

    int Conic::QuadraticPow2(float tolerance) const {
        if (!(tolerance > 0) || !std::isfinite(tolerance) || !Finite(points.data(), 3)) return 0;
        const float a = weight - 1;
        const float k = a / (4 * (2 + a));
        const float x = k * (points[0].x - 2 * points[1].x + points[2].x);
        const float y = k * (points[0].y - 2 * points[1].y + points[2].y);
        float error = std::sqrt(x * x + y * y);
        int count = 0;
        for (; count < kMaxQuadraticPow2; ++count) {
            if (error <= tolerance) break;
            error *= 0.25f;
        }
        return count;
    }

    void Conic::Split(Conic out[2]) const {
        const float scale = 1 / (1 + weight);
        const float newW = std::sqrt(0.5f + weight * 0.5f);
        const Offset p0 = points[0], p1 = points[1], p2 = points[2];
        const Offset wp1{ weight * p1.x, weight * p1.y };
        Offset m{ (p0.x + 2 * wp1.x + p2.x) * scale * 0.5f, (p0.y + 2 * wp1.y + p2.y) * scale * 0.5f };
        if (!std::isfinite(m.x) || !std::isfinite(m.y)) {
            const double w = weight, half = 1.0 / (1.0 + w) * 0.5;
            m = { float((p0.x + 2 * w * p1.x + p2.x) * half), float((p0.y + 2 * w * p1.y + p2.y) * half) };
        }
        out[0].points = { p0, Offset{ (p0.x + wp1.x) * scale, (p0.y + wp1.y) * scale }, m };
        out[1].points = { m, Offset{ (wp1.x + p2.x) * scale, (wp1.y + p2.y) * scale }, p2 };
        out[0].weight = out[1].weight = newW;
    }

    int Conic::ToQuadratics(std::vector<Offset>& out, int pow2) const {
        const std::size_t first = out.size();
        out.push_back(points[0]);
        if (pow2 >= kMaxQuadraticPow2) {
            // A conic so pointy that its halves are lines: two degenerate quadratics.
            Conic dst[2];
            Split(dst);
            if (SamePoint(dst[0].points[1], dst[0].points[2]) && SamePoint(dst[1].points[0], dst[1].points[1])) {
                out.push_back(dst[0].points[1]);
                out.push_back(dst[0].points[1]);
                out.push_back(dst[0].points[1]);
                out.push_back(dst[1].points[2]);
                pow2 = 1;
            } else {
                Subdivide(*this, out, pow2);
            }
        } else {
            Subdivide(*this, out, pow2);
        }
        const int quads = 1 << pow2;
        const int count = 2 * quads + 1;
        if (!Finite(out.data() + first, count))
            for (int i = 1; i < count - 1; ++i) out[first + std::size_t(i)] = points[1];
        return quads;
    }

    Offset Conic::Eval(float t) const {
        const float u = 1 - t;
        const float w0 = u * u, w1 = 2 * weight * u * t, w2 = t * t;
        const float d = w0 + w1 + w2;
        return { (w0 * points[0].x + w1 * points[1].x + w2 * points[2].x) / d,
                 (w0 * points[0].y + w1 * points[1].y + w2 * points[2].y) / d };
    }

    int ConicToQuadratics(const Offset points[3], float weight, float tolerance, std::vector<Offset>& out) {
        Conic c{ { points[0], points[1], points[2] }, weight };
        return c.ToQuadratics(out, c.QuadraticPow2(tolerance));
    }

    std::vector<Conic> BuildUnitArc(Offset uStart, Offset uStop, bool clockwise, Offset scale, Offset translate) {
        constexpr float kNearlyZero = 1.0f / (1 << 12);
        constexpr float kRoot2Over2 = 0.707106781f;
        // Rotate so uStart is (1, 0).
        const float x = uStart.x * uStop.x + uStart.y * uStop.y;
        float y = uStart.x * uStop.y - uStart.y * uStop.x;
        const float absY = std::abs(y);
        // (Nearly) coincident vectors: nothing, unless they are 180 degrees apart.
        if (absY <= kNearlyZero && x > 0 && ((y >= 0 && clockwise) || (y <= 0 && !clockwise))) return {};
        if (!clockwise) y = -y;
        // One conic per quadrant: which quadrant does (x, y) lie in?
        int quadrant = 0;
        if (y == 0) {
            quadrant = 2;   // 180
        } else if (x == 0) {
            quadrant = y > 0 ? 1 : 3;
        } else {
            if (y < 0) quadrant += 2;
            if ((x < 0) != (y < 0)) quadrant += 1;
        }
        static constexpr Offset kQuadrantPts[] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
        std::vector<Conic> dst;
        for (int i = 0; i < quadrant; ++i)
            dst.push_back({ { kQuadrantPts[i * 2], kQuadrantPts[i * 2 + 1], kQuadrantPts[i * 2 + 2] }, kRoot2Over2 });
        // The remaining (under 90 degree) arc.
        const Offset finalP{ x, y };
        const Offset lastQ = kQuadrantPts[quadrant * 2];
        const float dot = lastQ.x * finalP.x + lastQ.y * finalP.y;
        if (std::isnan(dot)) return {};
        if (dot < 1) {
            Offset off{ lastQ.x + x, lastQ.y + y };
            // The bisector, scaled to the off-curve point: length 1 / cos(theta / 2), which is
            // also the weight.
            const float cosThetaOver2 = std::sqrt((1 + dot) / 2);
            const float len = std::hypot(off.x, off.y);
            if (len > 0) off = { off.x / len / cosThetaOver2, off.y / len / cosThetaOver2 };
            if (!(std::abs(lastQ.x - off.x) <= kNearlyZero && std::abs(lastQ.y - off.y) <= kNearlyZero))
                dst.push_back({ { lastQ, off, finalP }, cosThetaOver2 });
        }
        // Undo the rotation (and mirror for counter-clockwise), then map onto the oval.
        for (Conic& c : dst)
            for (Offset& p : c.points) {
                const float py = clockwise ? p.y : -p.y;
                const Offset r{ uStart.x * p.x - uStart.y * py, uStart.y * p.x + uStart.x * py };
                p = { r.x * scale.x + translate.x, r.y * scale.y + translate.y };
            }
        return dst;
    }

}
