#include "kiln/graphics/Path.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// Stroker: offsets each flattened contour by half the width on both sides, with joins at
// interior vertices and caps at open ends (SkStroke's geometry; the result is filled NonZero, so
// overlaps on the inner side of a turn need no clean-up).

namespace kiln::gfx {

    namespace {
        constexpr float kPi = std::numbers::pi_v<float>;
        Offset Add(Offset a, Offset b) { return { a.x + b.x, a.y + b.y }; }
        Offset Sub(Offset a, Offset b) { return { a.x - b.x, a.y - b.y }; }
        Offset Mul(Offset a, float s) { return { a.x * s, a.y * s }; }
        float Cross(Offset a, Offset b) { return a.x * b.y - a.y * b.x; }
        float Dot(Offset a, Offset b) { return a.x * b.x + a.y * b.y; }
        Offset Unit(Offset v) { const float l = std::sqrt(Dot(v, v)); return l > 0 ? Mul(v, 1 / l) : Offset{}; }
        Offset Left(Offset d) { return { d.y, -d.x }; }   // left of travel in a y-down space

        struct Stroker {
            float hw;           // half width (path space)
            float tol;          // path-space tolerance
            const Stroke& s;

            // Points of a circular arc around c from direction a to b (both unit, radius hw),
            // turning the short way with sign `dir` (+1 clockwise in y-down).
            void Arc(std::vector<Offset>& out, Offset c, Offset a, Offset b, float dir) const {
                float angle = std::acos(std::clamp(Dot(a, b), -1.0f, 1.0f));
                if (dir < 0) angle = -angle;
                const float step = 2 * std::acos(std::clamp(1 - tol / hw, -1.0f, 1.0f));
                const int n = std::clamp(int(std::ceil(std::abs(angle) / std::max(step, 1e-3f))), 1, 64);
                const float start = std::atan2(a.y, a.x);
                for (int i = 1; i < n; ++i) {
                    const float t = start + angle * float(i) / float(n);
                    out.push_back(Add(c, { hw * std::cos(t), hw * std::sin(t) }));
                }
            }

            // Join at p between incoming direction d0 and outgoing d1, on the side given by
            // sign (+1 left, -1 right). Appends the points after p+n0*hw up to p+n1*hw.
            void Join(std::vector<Offset>& out, Offset p, Offset d0, Offset d1, float side) const {
                const Offset n0 = Mul(Left(d0), side * hw), n1 = Mul(Left(d1), side * hw);
                // In y-down a left turn has Cross < 0; the side the path turns towards is inner.
                const float turn = -Cross(d0, d1) * side;   // > 0: this side is the inner side
                if (turn >= -1e-6f) {
                    // Inner side (or straight): go through the vertex so short segments stay closed.
                    out.push_back(Add(p, n0));
                    if (turn > 1e-6f) out.push_back(p);
                    out.push_back(Add(p, n1));
                    return;
                }
                out.push_back(Add(p, n0));
                switch (s.join) {
                    case StrokeJoin::Round:
                        Arc(out, p, Unit(n0), Unit(n1), side);
                        break;
                    case StrokeJoin::Miter: {
                        // Miter length / width = 1 / sin(theta/2), theta the angle between segments.
                        const float cosTheta = Dot(Mul(d0, -1), d1);
                        const float sinHalf = std::sqrt(std::max(0.0f, (1 - cosTheta) * 0.5f));
                        if (sinHalf > 0 && 1 / sinHalf <= s.miter) {
                            const Offset bis = Unit(Add(n0, n1));
                            const float len = hw / std::max(Dot(bis, Unit(n0)), 1e-6f);
                            out.push_back(Add(p, Mul(bis, len)));
                        }
                        break;
                    }
                    case StrokeJoin::Bevel: break;
                }
                out.push_back(Add(p, n1));
            }

            void Cap(std::vector<Offset>& out, Offset p, Offset d) const {
                // From the left offset of travel direction d at p, around the end, to the right.
                const Offset n = Mul(Left(d), hw);
                switch (s.cap) {
                    case StrokeCap::Butt: break;
                    case StrokeCap::Square: {
                        const Offset e = Mul(d, hw);
                        out.push_back(Add(Add(p, n), e));
                        out.push_back(Add(Sub(p, n), e));
                        break;
                    }
                    case StrokeCap::Round:
                        out.push_back(Add(p, n));
                        Arc(out, p, Unit(n), Unit(Mul(n, -1)), 1);    // through the direction of travel
                        out.push_back(Sub(p, n));
                        break;
                }
            }

            void Contour(const Polyline& in, std::vector<Polyline>& out) const {
                std::vector<Offset> pts;
                for (const Offset& p : in.points)
                    if (pts.empty() || Dot(Sub(p, pts.back()), Sub(p, pts.back())) > 1e-12f) pts.push_back(p);
                if (in.closed && pts.size() > 2 && Dot(Sub(pts.front(), pts.back()), Sub(pts.front(), pts.back())) <= 1e-12f) pts.pop_back();
                if (pts.size() == 1) {
                    // Zero-length contour: round and square caps draw a dot (Skia), butt nothing.
                    if (s.cap == StrokeCap::Butt) return;
                    Polyline dot{ {}, true };
                    const Offset p = pts[0];
                    if (s.cap == StrokeCap::Square) {
                        dot.points = { { p.x - hw, p.y - hw }, { p.x + hw, p.y - hw }, { p.x + hw, p.y + hw }, { p.x - hw, p.y + hw } };
                    } else {
                        dot.points.push_back({ p.x + hw, p.y });
                        Arc(dot.points, p, { 1, 0 }, { -1, 0 }, 1);
                        dot.points.push_back({ p.x - hw, p.y });
                        Arc(dot.points, p, { -1, 0 }, { 1, 0 }, 1);
                    }
                    out.push_back(std::move(dot));
                    return;
                }
                if (pts.size() < 2) return;
                const std::size_t n = pts.size();
                std::vector<Offset> dirs;
                const std::size_t segs = in.closed ? n : n - 1;
                for (std::size_t i = 0; i < segs; ++i) dirs.push_back(Unit(Sub(pts[(i + 1) % n], pts[i])));

                if (in.closed) {
                    // Two loops of opposite orientation: winding 1 between them, 0 inside.
                    for (float side : { 1.0f, -1.0f }) {
                        Polyline loop{ {}, true };
                        for (std::size_t i = 0; i < n; ++i) Join(loop.points, pts[i], dirs[(i + segs - 1) % segs], dirs[i], side);
                        if (side < 0) std::reverse(loop.points.begin(), loop.points.end());
                        out.push_back(std::move(loop));
                    }
                    return;
                }
                Polyline poly{ {}, true };
                auto& o = poly.points;
                // Left side forward.
                o.push_back(Add(pts[0], Mul(Left(dirs[0]), hw)));
                for (std::size_t i = 1; i + 1 < n; ++i) Join(o, pts[i], dirs[i - 1], dirs[i], 1);
                o.push_back(Add(pts[n - 1], Mul(Left(dirs[segs - 1]), hw)));
                Cap(o, pts[n - 1], dirs[segs - 1]);
                // Right side backward (travelling the reversed polyline its left is our right).
                o.push_back(Sub(pts[n - 1], Mul(Left(dirs[segs - 1]), hw)));
                for (std::size_t i = n - 2; i >= 1; --i) {
                    Join(o, pts[i], Mul(dirs[i], -1), Mul(dirs[i - 1], -1), 1);
                    if (i == 1) break;
                }
                o.push_back(Sub(pts[0], Mul(Left(dirs[0]), hw)));
                Cap(o, pts[0], Mul(dirs[0], -1));
                out.push_back(std::move(poly));
            }
        };
    }

    std::vector<Polyline> StrokeToPolygons(const Path& path, const Stroke& stroke, const Matrix& m, float tolerance) {
        const float scale = std::max(m.MaxScale(), 1e-6f);
        const float tol = tolerance / scale;
        // Hairline: one device pixel wide whatever the transform.
        const float width = stroke.width > 0 ? stroke.width : 1.0f / scale;
        Stroker st{ width * 0.5f, tol, stroke };
        std::vector<Polyline> out;
        for (const Polyline& c : path.Flatten(Matrix{}, tol)) st.Contour(c, out);
        for (Polyline& p : out)
            for (Offset& q : p.points) q = m.Map(q);
        (void)kPi;
        return out;
    }

}
