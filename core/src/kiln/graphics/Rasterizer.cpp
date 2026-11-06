#include "kiln/graphics/Rasterizer.h"

#include <algorithm>
#include <cmath>

namespace kiln::gfx {

    namespace {
        // Accumulation buffer: each row has width + 2 cells so contributions clamped to the
        // right edge never spill into the next row.
        struct Accumulator {
            int w, h, stride;
            std::vector<float> a;
            Accumulator(int w_, int h_) : w(w_), h(h_), stride(w_ + 2), a(std::size_t(stride) * h_, 0.0f) {}

            // font-rs draw_line: adds the signed area to the right of the edge in each row.
            void Line(Offset p0, Offset p1) {
                if (std::abs(p0.y - p1.y) <= 1e-7f) return;
                float dir = 1;
                if (p0.y > p1.y) { std::swap(p0, p1); dir = -1; }
                // Clamp x into [0, w]: area left of the mask folds into column 0, right of it
                // into the spare column, which keeps the row sums right.
                const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
                float x = p0.x;
                int y0 = int(std::floor(p0.y));
                if (p0.y < 0) { x -= p0.y * dxdy; y0 = 0; }
                const int y1 = std::min(h, int(std::ceil(p1.y)));
                for (int y = y0; y < y1; ++y) {
                    float* row = &a[std::size_t(y) * stride];
                    const float dy = std::min(float(y + 1), p1.y) - std::max(float(y), p0.y);
                    const float xnext = x + dxdy * dy;
                    const float d = dy * dir;
                    float xa = std::clamp(std::min(x, xnext), 0.0f, float(w));
                    float xb = std::clamp(std::max(x, xnext), 0.0f, float(w));
                    const float x0floor = std::floor(xa);
                    const int x0i = int(x0floor);
                    const float x1ceil = std::ceil(xb);
                    const int x1i = int(x1ceil);
                    if (x1i <= x0i + 1) {
                        const float xmf = 0.5f * (xa + xb) - x0floor;
                        row[x0i] += d - d * xmf;
                        row[x0i + 1] += d * xmf;
                    } else {
                        const float s = 1.0f / (xb - xa);
                        const float x0f = xa - x0floor;
                        const float a0 = 0.5f * s * (1 - x0f) * (1 - x0f);
                        const float x1f = xb - x1ceil + 1;
                        const float am = 0.5f * s * x1f * x1f;
                        row[x0i] += d * a0;
                        if (x1i == x0i + 2) {
                            row[x0i + 1] += d * (1 - a0 - am);
                        } else {
                            const float a1 = s * (1.5f - x0f);
                            row[x0i + 1] += d * (a1 - a0);
                            for (int xi = x0i + 2; xi < x1i - 1; ++xi) row[xi] += d * s;
                            const float a2 = a1 + float(x1i - x0i - 3) * s;
                            row[x1i - 1] += d * (1 - a2 - am);
                        }
                        row[x1i] += d * am;
                    }
                    x = xnext;
                }
            }
        };
    }

    Mask Rasterize(std::span<const Polyline> polygons, FillType fill, Rect clip) {
        Mask m;
        bool any = false;
        Rect b{};
        for (const Polyline& p : polygons)
            for (const Offset& q : p.points) {
                if (!any) { b = { q.x, q.y, q.x, q.y }; any = true; }
                b.left = std::min(b.left, q.x); b.top = std::min(b.top, q.y);
                b.right = std::max(b.right, q.x); b.bottom = std::max(b.bottom, q.y);
            }
        if (!any) return m;
        b = b.Intersect(clip);
        if (b.Empty()) return m;
        m.x = int(std::floor(b.left));
        m.y = int(std::floor(b.top));
        m.width = int(std::ceil(b.right)) - m.x;
        m.height = int(std::ceil(b.bottom)) - m.y;
        if (m.width <= 0 || m.height <= 0) return m;

        Accumulator acc(m.width, m.height);
        const Offset origin{ float(m.x), float(m.y) };
        for (const Polyline& p : polygons) {
            const std::size_t n = p.points.size();
            if (n < 2) continue;
            // Every polygon is filled as closed (fills close open contours implicitly).
            for (std::size_t i = 0; i < n; ++i)
                acc.Line(p.points[i] - origin, p.points[(i + 1) % n] - origin);
        }
        m.alpha.resize(std::size_t(m.width) * m.height);
        for (int y = 0; y < m.height; ++y) {
            const float* row = &acc.a[std::size_t(y) * acc.stride];
            std::uint8_t* out = &m.alpha[std::size_t(y) * m.width];
            float sum = 0;
            for (int x = 0; x < m.width; ++x) {
                sum += row[x];
                float c = std::abs(sum);
                if (fill == FillType::EvenOdd) {
                    c = std::fmod(c, 2.0f);
                    if (c > 1) c = 2 - c;
                } else {
                    c = std::min(c, 1.0f);
                }
                out[x] = std::uint8_t(c * 255.0f + 0.5f);
            }
        }
        return m;
    }

}
