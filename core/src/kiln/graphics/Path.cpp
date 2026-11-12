#include "kiln/graphics/Path.h"

#include "kiln/graphics/Conic.h"
#include "kiln/graphics/Shape.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

namespace kiln::gfx {

    float Matrix::MaxScale() const {
        const float s = a * a + b * b + c * c + d * d;
        const float det = a * d - b * c;
        const float disc = std::sqrt(std::max(0.0f, s * s - 4 * det * det));
        return std::sqrt((s + disc) * 0.5f);
    }

    void Path::EnsureMove() {
        if (!m_NeedsMove) return;
        // Skia: a segment without a preceding moveTo starts at the last contour's start (or 0,0).
        const Offset p = m_Verbs.empty() ? Offset{} : m_Points[m_ContourStart];
        MoveTo(p.x, p.y);
    }

    void Path::MoveTo(float x, float y) {
        if (!m_Verbs.empty() && m_Verbs.back() == Verb::Move) {   // consecutive moves collapse
            m_Points.back() = { x, y };
        } else {
            m_Verbs.push_back(Verb::Move);
            m_Points.push_back({ x, y });
        }
        m_ContourStart = m_Points.size() - 1;
        m_NeedsMove = false;
    }

    void Path::LineTo(float x, float y) {
        EnsureMove();
        m_Verbs.push_back(Verb::Line);
        m_Points.push_back({ x, y });
    }

    void Path::QuadraticTo(float x1, float y1, float x2, float y2) {
        EnsureMove();
        m_Verbs.push_back(Verb::Quad);
        m_Points.push_back({ x1, y1 });
        m_Points.push_back({ x2, y2 });
    }

    void Path::RelativeQuadraticTo(float dx1, float dy1, float dx2, float dy2) {
        const Offset p = Last();
        QuadraticTo(p.x + dx1, p.y + dy1, p.x + dx2, p.y + dy2);
    }

    void Path::CubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        EnsureMove();
        m_Verbs.push_back(Verb::Cubic);
        m_Points.push_back({ x1, y1 });
        m_Points.push_back({ x2, y2 });
        m_Points.push_back({ x3, y3 });
    }

    void Path::RelativeCubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        const Offset p = Last();
        CubicTo(p.x + dx1, p.y + dy1, p.x + dx2, p.y + dy2, p.x + dx3, p.y + dy3);
    }

    void Path::ConicTo(float x1, float y1, float x2, float y2, float w) {
        if (!(w > 0)) { LineTo(x2, y2); return; }
        if (!std::isfinite(w)) { LineTo(x1, y1); LineTo(x2, y2); return; }
        if (w == 1) { QuadraticTo(x1, y1, x2, y2); return; }
        EnsureMove();
        m_Verbs.push_back(Verb::Conic);
        m_Points.push_back({ x1, y1 });
        m_Points.push_back({ x2, y2 });
        m_Weights.push_back(w);
    }

    void Path::Close() {
        if (m_Verbs.empty() || m_Verbs.back() == Verb::Close) return;
        m_Verbs.push_back(Verb::Close);
        m_NeedsMove = true;
    }

    namespace {
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kRoot2Over2 = 0.707106781f;
        float Radians(float deg) { return deg * kPi / 180.0f; }
        // SkScalarSinSnapToZero / CosSnapToZero.
        float SnapToZero(float v) { return std::abs(v) <= 1.0f / (1 << 12) ? 0.0f : v; }
        bool NearlyEqual(Offset a, Offset b) {
            constexpr float tol = 1.0f / (1 << 12);
            return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
        }
        // SkPath_*PointIterator: the corner points of a rect / oval / round rect, walked from
        // `start` clockwise or counter-clockwise.
        template <unsigned N> struct PointIter {
            std::array<Offset, N> pts{};
            unsigned cur, advance;
            PointIter(bool cw, unsigned start) : cur(start % N), advance(cw ? 1 : N - 1) {}
            Offset Current() const { return pts[cur]; }
            Offset Next() { cur = (cur + advance) % N; return pts[cur]; }
        };
        PointIter<4> RectIter(Rect r, bool cw, unsigned start) {
            PointIter<4> it(cw, start);
            it.pts = { Offset{ r.left, r.top }, Offset{ r.right, r.top }, Offset{ r.right, r.bottom }, Offset{ r.left, r.bottom } };
            return it;
        }
        PointIter<4> OvalIter(Rect r, bool cw, unsigned start) {
            PointIter<4> it(cw, start);
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            it.pts = { Offset{ cx, r.top }, Offset{ r.right, cy }, Offset{ cx, r.bottom }, Offset{ r.left, cy } };
            return it;
        }
    }

    // SkPathBuilder::arcTo(oval, startAngle, sweepAngle, forceMoveTo).
    void Path::ArcTo(Rect oval, float startDeg, float sweepDeg, bool forceMoveTo) {
        if (oval.Width() < 0 || oval.Height() < 0) return;
        startDeg = std::fmod(startDeg, 360.0f);
        if (m_Verbs.empty()) forceMoveTo = true;
        auto addPt = [&](Offset p) {
            if (forceMoveTo || m_NeedsMove) MoveTo(p.x, p.y);
            else if (!NearlyEqual(Last(), p)) LineTo(p.x, p.y);
        };
        // A lone point (arc_is_lone_point).
        if (sweepDeg == 0 && (startDeg == 0 || startDeg == 360)) {
            const Offset p{ oval.right, (oval.top + oval.bottom) / 2 };
            if (forceMoveTo) MoveTo(p.x, p.y); else LineTo(p.x, p.y);
            return;
        }
        if (oval.Width() == 0 && oval.Height() == 0) {
            if (forceMoveTo) MoveTo(oval.right, oval.top); else LineTo(oval.right, oval.top);
            return;
        }
        // angles_to_unit_vectors.
        const float startRad = Radians(startDeg);
        float stopRad = Radians(startDeg + sweepDeg);
        const Offset startV{ SnapToZero(std::cos(startRad)), SnapToZero(std::sin(startRad)) };
        Offset stopV{ SnapToZero(std::cos(stopRad)), SnapToZero(std::sin(stopRad)) };
        if (startV.x == stopV.x && startV.y == stopV.y) {
            // Nearly a full turn lost to precision: nudge the stop back until they differ.
            const float sw = std::abs(sweepDeg);
            if (sw < 360 && sw > 359) {
                const float delta = std::copysign(1.0f / 512, sweepDeg);
                do {
                    stopRad -= delta;
                    stopV = { SnapToZero(std::cos(stopRad)), SnapToZero(std::sin(stopRad)) };
                } while (startV.x == stopV.x && startV.y == stopV.y);
            }
        }
        const bool cw = sweepDeg > 0;
        const float cx = (oval.left + oval.right) / 2, cy = (oval.top + oval.bottom) / 2;
        const float rx = oval.Width() / 2, ry = oval.Height() / 2;
        if (startV.x == stopV.x && startV.y == stopV.y) {
            // Too small a sweep for the unit vectors: a line to the end point.
            const float end = Radians(startDeg + sweepDeg);
            addPt({ cx + rx * std::cos(end), cy + ry * std::sin(end) });
            return;
        }
        const std::vector<Conic> conics = BuildUnitArc(startV, stopV, cw, { rx, ry }, { cx, cy });
        if (conics.empty()) {
            addPt({ stopV.x * rx + cx, stopV.y * ry + cy });
            return;
        }
        addPt(conics[0].points[0]);
        for (const Conic& c : conics) ConicTo(c.points[1].x, c.points[1].y, c.points[2].x, c.points[2].y, c.weight);
    }

    // SkPathBuilder::addArc: a full turn starting on a quadrant is an oval.
    void Path::AddArc(Rect oval, float startDeg, float sweepDeg) {
        if (oval.Empty() || sweepDeg == 0) return;
        if (sweepDeg >= 360 || sweepDeg <= -360) {
            const float over90 = startDeg / 90, rounded = std::round(over90);
            if (std::abs(over90 - rounded) <= 1.0f / (1 << 12)) {
                float index = std::fmod(rounded + 1, 4.0f);
                if (index < 0) index += 4;
                AddOvalAt(oval, sweepDeg > 0 ? Direction::Clockwise : Direction::CounterClockwise, unsigned(index));
                return;
            }
        }
        ArcTo(oval, startDeg, sweepDeg, true);
    }

    void Path::AddRect(Rect r, Direction dir) {
        // SkPathRawShapes set_as_rect, start index 0.
        auto it = RectIter(r, dir == Direction::Clockwise, 0);
        MoveTo(it.Current().x, it.Current().y);
        for (int i = 0; i < 3; ++i) { const Offset p = it.Next(); LineTo(p.x, p.y); }
        Close();
    }

    void Path::AddOval(Rect oval, Direction dir) { AddOvalAt(oval, dir, 1); }

    // set_as_oval: four quarter conics from oval point `index` (1: the right-hand point).
    void Path::AddOvalAt(Rect oval, Direction dir, unsigned index) {
        const bool cw = dir == Direction::Clockwise;
        auto ovalIt = OvalIter(oval, cw, index);
        auto rectIt = RectIter(oval, cw, index + (cw ? 0 : 1));
        MoveTo(ovalIt.Current().x, ovalIt.Current().y);
        for (int i = 0; i < 4; ++i) {
            const Offset c = rectIt.Next(), e = ovalIt.Next();
            ConicTo(c.x, c.y, e.x, e.y, kRoot2Over2);
        }
        Close();
    }

    // SkPathBuilder::addRRect with the legacy start index (6 clockwise, 7 counter-clockwise).
    void Path::AddRoundRect(const RoundRect& rr, Direction dir) {
        const Rect& r = rr.rect;
        // Radii scaled down together when adjacent ones overlap (SkRRect::scaleRadii).
        float scale = 1;
        auto fit = [&](float a, float b, float len) { if (a + b > len && a + b > 0) scale = std::min(scale, len / (a + b)); };
        fit(rr.topLeft.x, rr.topRight.x, r.Width());
        fit(rr.bottomLeft.x, rr.bottomRight.x, r.Width());
        fit(rr.topLeft.y, rr.bottomLeft.y, r.Height());
        fit(rr.topRight.y, rr.bottomRight.y, r.Height());
        auto clampR = [&](Offset v) { return (v.x <= 0 || v.y <= 0) ? Offset{} : Offset{ v.x * scale, v.y * scale }; };
        const Offset tl = clampR(rr.topLeft), tr = clampR(rr.topRight), br = clampR(rr.bottomRight), bl = clampR(rr.bottomLeft);
        const bool cw = dir == Direction::Clockwise;
        const unsigned index = cw ? 6 : 7;
        // SimplifyRRect: no radii is a rect, radii of half the size everywhere an oval.
        const bool isRect = tl.x == 0 && tr.x == 0 && br.x == 0 && bl.x == 0;
        const float hw = r.Width() / 2, hh = r.Height() / 2;
        auto half = [&](Offset v) { return std::abs(v.x - hw) <= 1e-6f * std::max(1.0f, hw) && std::abs(v.y - hh) <= 1e-6f * std::max(1.0f, hh); };
        const bool isOval = half(tl) && half(tr) && half(br) && half(bl);
        if (r.Empty() || isRect) {
            auto it = RectIter(r, cw, (index + 1) / 2);
            MoveTo(it.Current().x, it.Current().y);
            for (int i = 0; i < 3; ++i) { const Offset p = it.Next(); LineTo(p.x, p.y); }
            Close();
            return;
        }
        if (isOval) { AddOvalAt(r, dir, index / 2); return; }
        PointIter<8> rrIt(cw, index);
        rrIt.pts = { Offset{ r.left + tl.x, r.top }, Offset{ r.right - tr.x, r.top }, Offset{ r.right, r.top + tr.y },
                     Offset{ r.right, r.bottom - br.y }, Offset{ r.right - br.x, r.bottom }, Offset{ r.left + bl.x, r.bottom },
                     Offset{ r.left, r.bottom - bl.y }, Offset{ r.left, r.top + tl.y } };
        auto rectIt = RectIter(r, cw, index / 2 + (cw ? 0 : 1));
        // A conic first on odd indices clockwise / even ones counter-clockwise.
        const bool startsWithConic = (index & 1) == (cw ? 1u : 0u);
        MoveTo(rrIt.Current().x, rrIt.Current().y);
        if (startsWithConic) {
            for (int i = 0; i < 3; ++i) {
                const Offset c = rectIt.Next(), e = rrIt.Next();
                ConicTo(c.x, c.y, e.x, e.y, kRoot2Over2);
                const Offset l = rrIt.Next();
                LineTo(l.x, l.y);
            }
            const Offset c = rectIt.Next(), e = rrIt.Next();
            ConicTo(c.x, c.y, e.x, e.y, kRoot2Over2);   // the closing line comes from Close
        } else {
            for (int i = 0; i < 4; ++i) {
                const Offset l = rrIt.Next();
                LineTo(l.x, l.y);
                const Offset c = rectIt.Next(), e = rrIt.Next();
                ConicTo(c.x, c.y, e.x, e.y, kRoot2Over2);
            }
        }
        Close();
    }

    void Path::AddPath(const Path& other, Offset offset) {
        std::size_t pi = 0, wi = 0;
        for (Verb v : other.m_Verbs) {
            auto pt = [&](std::size_t i) { return other.m_Points[pi + i] + offset; };
            switch (v) {
                case Verb::Move: { const Offset p = pt(0); MoveTo(p.x, p.y); pi += 1; break; }
                case Verb::Line: { const Offset p = pt(0); LineTo(p.x, p.y); pi += 1; break; }
                case Verb::Quad: { const Offset a = pt(0), b = pt(1); QuadraticTo(a.x, a.y, b.x, b.y); pi += 2; break; }
                case Verb::Conic: { const Offset a = pt(0), b = pt(1); ConicTo(a.x, a.y, b.x, b.y, other.m_Weights[wi++]); pi += 2; break; }
                case Verb::Cubic: { const Offset a = pt(0), b = pt(1), c = pt(2); CubicTo(a.x, a.y, b.x, b.y, c.x, c.y); pi += 3; break; }
                case Verb::Close: Close(); break;
            }
        }
    }

    Rect Path::Bounds() const {
        if (m_Points.empty()) return {};
        Rect r{ m_Points[0].x, m_Points[0].y, m_Points[0].x, m_Points[0].y };
        for (const Offset& p : m_Points) {
            r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y);
            r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y);
        }
        return r;
    }

    std::uint64_t Path::Hash() const {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&](const void* data, std::size_t n) {
            const auto* b = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
        };
        mix(m_Verbs.data(), m_Verbs.size());
        mix(m_Points.data(), m_Points.size() * sizeof(Offset));
        mix(m_Weights.data(), m_Weights.size() * sizeof(float));
        mix(&fillType, 1);
        return h;
    }

    namespace {
        float Len(Offset v) { return std::sqrt(v.x * v.x + v.y * v.y); }
        Offset Sub(Offset a, Offset b) { return { a.x - b.x, a.y - b.y }; }
        Offset Lerp(Offset a, Offset b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
    }

    std::vector<Polyline> Path::Flatten(const Matrix& m, float tol) const {
        std::vector<Polyline> out;
        Polyline* cur = nullptr;
        std::size_t pi = 0, wi = 0;
        Offset last{};
        auto add = [&](Offset p) { if (cur) cur->points.push_back(p); last = p; };
        for (Verb v : m_Verbs) {
            switch (v) {
                case Verb::Move:
                    out.push_back({});
                    cur = &out.back();
                    add(m.Map(m_Points[pi++]));
                    break;
                case Verb::Line:
                    add(m.Map(m_Points[pi++]));
                    break;
                case Verb::Quad: {
                    const Offset p0 = last, p1 = m.Map(m_Points[pi]), p2 = m.Map(m_Points[pi + 1]);
                    pi += 2;
                    // Wang's formula: segments so the chord error stays under tol.
                    const float dd = Len({ p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y });
                    const int n = std::clamp(int(std::ceil(std::sqrt(0.25f * dd / tol))), 1, 500);
                    for (int i = 1; i <= n; ++i) {
                        const float t = float(i) / float(n);
                        add(Lerp(Lerp(p0, p1, t), Lerp(p1, p2, t), t));
                    }
                    break;
                }
                case Verb::Conic: {
                    // Quadratics within half the tolerance, each flattened like a quadratic.
                    const Offset cp[3] = { last, m.Map(m_Points[pi]), m.Map(m_Points[pi + 1]) };
                    pi += 2;
                    std::vector<Offset> q;
                    const int quads = ConicToQuadratics(cp, m_Weights[wi++], tol * 0.5f, q);
                    for (int k = 0; k < quads; ++k) {
                        const Offset p0 = last, p1 = q[std::size_t(2 * k + 1)], p2 = q[std::size_t(2 * k + 2)];
                        const float dd = Len({ p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y });
                        const int n = std::clamp(int(std::ceil(std::sqrt(0.25f * dd / (tol * 0.5f)))), 1, 500);
                        for (int i = 1; i <= n; ++i) {
                            const float t = float(i) / float(n);
                            add(Lerp(Lerp(p0, p1, t), Lerp(p1, p2, t), t));
                        }
                    }
                    break;
                }
                case Verb::Cubic: {
                    const Offset p0 = last, p1 = m.Map(m_Points[pi]), p2 = m.Map(m_Points[pi + 1]), p3 = m.Map(m_Points[pi + 2]);
                    pi += 3;
                    const float dd = std::max(Len({ p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y }),
                                              Len({ p1.x - 2 * p2.x + p3.x, p1.y - 2 * p2.y + p3.y }));
                    const int n = std::clamp(int(std::ceil(std::sqrt(0.75f * dd / tol))), 1, 500);
                    for (int i = 1; i <= n; ++i) {
                        const float t = float(i) / float(n), u = 1 - t;
                        add({ u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                              u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y });
                    }
                    break;
                }
                case Verb::Close:
                    if (cur) {
                        cur->closed = true;
                        last = cur->points.front();
                    }
                    cur = nullptr;
                    break;
            }
        }
        (void)Sub;
        return out;
    }

    namespace {
        // Fixed point for the clipper: 1/1024 px.
        constexpr double kClipScale = 1024.0;
        Clipper2Lib::Point64 ClipPoint(Offset q, std::int64_t z) {
            return Clipper2Lib::Point64(std::int64_t(std::llround(double(q.x) * kClipScale)), std::int64_t(std::llround(double(q.y) * kClipScale)), z);
        }
        Clipper2Lib::FillRule RuleOf(const Path& p) {
            return p.fillType == FillType::EvenOdd ? Clipper2Lib::FillRule::EvenOdd : Clipper2Lib::FillRule::NonZero;
        }
        // A curve the operands were flattened from: a quadratic (3 points) or cubic (4), sampled
        // at t = i / n. A sample's clipper z is (segment + 1) << 32 | i; lines and intersections
        // carry 0.
        struct CurveSegment { int degree; Offset p[4]; int n; float weight = 1; bool conic = false; };
        Offset Lerp2(Offset a, Offset b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
        // The curve's polar form (blossom) at the given parameters, one per degree; a conic's in
        // homogeneous coordinates (x w, y w, w), its weight in `w`.
        Offset Blossom(const CurveSegment& c, const float* t, float* w = nullptr) {
            if (c.conic) {
                struct H { float x, y, w; };
                auto lerp = [](H a, H b, float u) { return H{ a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.w + (b.w - a.w) * u }; };
                const H h0{ c.p[0].x, c.p[0].y, 1 }, h1{ c.p[1].x * c.weight, c.p[1].y * c.weight, c.weight }, h2{ c.p[2].x, c.p[2].y, 1 };
                const H r = lerp(lerp(h0, h1, t[0]), lerp(h1, h2, t[0]), t[1]);
                if (w) *w = r.w;
                return { r.x / r.w, r.y / r.w };
            }
            Offset q[4] = { c.p[0], c.p[1], c.p[2], c.p[3] };
            for (int level = 0; level < c.degree; ++level)
                for (int k = 0; k < c.degree - level; ++k) q[k] = Lerp2(q[k], q[k + 1], t[level]);
            if (w) *w = 1;
            return q[0];
        }
    }

    bool Path::Op(const Path& a, const Path& b, PathOperation op, float tolerance) {
        using namespace Clipper2Lib;
        std::vector<CurveSegment> curves;
        // Flattens like Flatten, tagging curve samples (Skia's PathOps keeps curves; the clipper
        // works on polygons, so runs of samples from one curve become that curve's piece again).
        auto toClipper = [&](const Path& p) {
            Paths64 out;
            Path64 cur;
            Offset last{};
            std::size_t pi = 0, wi = 0;
            auto flush = [&] { if (cur.size() >= 3) out.push_back(std::move(cur)); cur.clear(); };
            auto curve = [&](int degree, const Offset* cp, float tol) {
                CurveSegment c{ degree, { cp[0], cp[1], cp[2], degree == 3 ? cp[3] : cp[2] }, 1 };
                float dd = Len({ cp[0].x - 2 * cp[1].x + cp[2].x, cp[0].y - 2 * cp[1].y + cp[2].y });
                if (degree == 3) dd = std::max(dd, Len({ cp[1].x - 2 * cp[2].x + cp[3].x, cp[1].y - 2 * cp[2].y + cp[3].y }));
                c.n = std::clamp(int(std::ceil(std::sqrt((degree == 3 ? 0.75f : 0.25f) * dd / tol))), 1, 500);
                curves.push_back(c);
                const std::int64_t id = std::int64_t(curves.size()) << 32;
                for (int i = 1; i <= c.n; ++i) {
                    const float t[3] = { float(i) / float(c.n), float(i) / float(c.n), float(i) / float(c.n) };
                    cur.push_back(ClipPoint(Blossom(c, t), id | i));
                }
                last = c.p[degree];
            };
            for (Verb v : p.m_Verbs) {
                switch (v) {
                    case Verb::Move: flush(); last = p.m_Points[pi++]; cur.push_back(ClipPoint(last, 0)); break;
                    case Verb::Line: last = p.m_Points[pi++]; cur.push_back(ClipPoint(last, 0)); break;
                    case Verb::Quad: { const Offset cp[3] = { last, p.m_Points[pi], p.m_Points[pi + 1] }; pi += 2; curve(2, cp, tolerance); break; }
                    case Verb::Cubic: { const Offset cp[4] = { last, p.m_Points[pi], p.m_Points[pi + 1], p.m_Points[pi + 2] }; pi += 3; curve(3, cp, tolerance); break; }
                    case Verb::Conic: {
                        const Offset cp[3] = { last, p.m_Points[pi], p.m_Points[pi + 1] };
                        pi += 2;
                        // Sampled as the conic itself, as finely as Flatten's quadratics would be.
                        const float weight = p.m_Weights[wi++];
                        std::vector<Offset> q;
                        const int quads = ConicToQuadratics(cp, weight, tolerance * 0.5f, q);
                        int samples = 0;
                        Offset from = cp[0];
                        for (int k = 0; k < quads; ++k) {
                            const Offset q1 = q[std::size_t(2 * k + 1)], q2 = q[std::size_t(2 * k + 2)];
                            const float dd = Len({ from.x - 2 * q1.x + q2.x, from.y - 2 * q1.y + q2.y });
                            samples += std::clamp(int(std::ceil(std::sqrt(0.25f * dd / (tolerance * 0.5f)))), 1, 500);
                            from = q2;
                        }
                        CurveSegment c{ 2, { cp[0], cp[1], cp[2], cp[2] }, std::clamp(samples, 1, 2000), weight, true };
                        curves.push_back(c);
                        const std::int64_t id = std::int64_t(curves.size()) << 32;
                        for (int i = 1; i <= c.n; ++i) {
                            const float t[2] = { float(i) / float(c.n), float(i) / float(c.n) };
                            cur.push_back(ClipPoint(Blossom(c, t), id | i));
                        }
                        last = cp[2];
                        break;
                    }
                    case Verb::Close: flush(); break;
                }
            }
            flush();
            return out;
        };
        // Each operand resolved by its own fill type first, then combined (Skia keeps per-path fill
        // types too); both are then simple, so one rule serves the combination.
        const Paths64 sa = Union(toClipper(a), RuleOf(a));
        const Paths64 sb = Union(toClipper(b), RuleOf(b));
        Paths64 r;
        switch (op) {
            case PathOperation::Difference: r = BooleanOp(ClipType::Difference, FillRule::NonZero, sa, sb); break;
            case PathOperation::Intersect: r = BooleanOp(ClipType::Intersection, FillRule::NonZero, sa, sb); break;
            case PathOperation::Union: r = BooleanOp(ClipType::Union, FillRule::NonZero, sa, sb); break;
            case PathOperation::Xor: r = BooleanOp(ClipType::Xor, FillRule::NonZero, sa, sb); break;
            case PathOperation::ReverseDifference: r = BooleanOp(ClipType::Difference, FillRule::NonZero, sb, sa); break;
        }
        Reset();
        fillType = FillType::EvenOdd;
        auto at = [](const Point64& q) { return Offset{ float(double(q.x) / kClipScale), float(double(q.y) / kClipScale) }; };
        auto segOf = [](const Point64& q) { return q.z >> 32; };
        auto sampleOf = [](const Point64& q) { return int(q.z & 0xffffffff); };
        for (const Path64& c : r) {
            const std::size_t n = c.size();
            if (n == 0) continue;
            // Start where a run of one curve's samples cannot wrap around: at a vertex whose
            // predecessor belongs elsewhere.
            std::size_t start = 0;
            for (std::size_t k = 0; k < n; ++k) {
                const Point64& prev = c[(k + n - 1) % n];
                if (segOf(c[k]) == 0 || segOf(prev) != segOf(c[k])) { start = k; break; }
            }
            auto pt = [&](std::size_t k) -> const Point64& { return c[(start + k) % n]; };
            // Whether vertex k sits on curve cs at sample i (an endpoint shared with a neighbour
            // carries the neighbour's tag).
            auto onSample = [&](std::size_t k, const CurveSegment& cs, int i) {
                if (i < 0 || i > cs.n) return false;
                const float t[3] = { float(i) / float(cs.n), float(i) / float(cs.n), float(i) / float(cs.n) };
                const Point64 q = ClipPoint(Blossom(cs, t), 0);
                return q.x == pt(k).x && q.y == pt(k).y;
            };
            MoveTo(at(pt(0)).x, at(pt(0)).y);
            // Vertices k..n (n: back at the start) are walked; each step emits a line or a curve.
            auto vertex = [&](std::size_t k) -> const Point64& { return pt(k % n); };
            std::size_t k = 0;
            while (k < n) {
                const Point64& next = vertex(k + 1);
                const std::int64_t seg = k + 1 < n ? segOf(next) : 0;
                if (k + 1 == n) break;   // Close draws the last edge
                if (seg == 0) {
                    LineTo(at(next).x, at(next).y);
                    ++k;
                    continue;
                }
                const CurveSegment& cs = curves[std::size_t(seg - 1)];
                // The run of this curve's samples from k + 1, monotone in t.
                std::size_t end = k + 1;
                int dir = 0;
                while (end + 1 < n && segOf(pt(end + 1)) == seg) {
                    const int step = sampleOf(pt(end + 1)) - sampleOf(pt(end));
                    const int d = step > 0 ? 1 : step < 0 ? -1 : 0;
                    if (d == 0 || (dir != 0 && d != dir)) break;
                    dir = d;
                    ++end;
                }
                const int first = sampleOf(pt(k + 1));
                if (dir == 0) dir = onSample(k, cs, first - 1) ? 1 : onSample(k, cs, first + 1) ? -1 : 0;
                // An end of the run: the neighbouring sample itself, or an intersection on the chord
                // to it (its parameter from where it falls along the chord).
                auto extend = [&](std::size_t v, int sample, int step, float& t) {
                    if (step == 0) return false;
                    const int beyond = sample + step;
                    if (onSample(v, cs, beyond)) { t = float(beyond) / float(cs.n); return true; }
                    if (pt(v).z != 0 || beyond < 0 || beyond > cs.n) return false;
                    const float ta[3] = { float(sample) / float(cs.n), float(sample) / float(cs.n), float(sample) / float(cs.n) };
                    const float tb[3] = { float(beyond) / float(cs.n), float(beyond) / float(cs.n), float(beyond) / float(cs.n) };
                    const Offset a0 = Blossom(cs, ta), b0 = Blossom(cs, tb), q = at(pt(v));
                    const Offset d{ b0.x - a0.x, b0.y - a0.y };
                    const float len2 = d.x * d.x + d.y * d.y;
                    if (len2 <= 0) return false;
                    const float f = ((q.x - a0.x) * d.x + (q.y - a0.y) * d.y) / len2;
                    const float off = std::abs((q.x - a0.x) * d.y - (q.y - a0.y) * d.x) / std::sqrt(len2);
                    if (f < 0 || f > 1 || off > 0.01f) return false;   // on the chord (fixed-point rounding aside)
                    t = (float(sample) + float(step) * f) / float(cs.n);
                    return true;
                };
                float t0 = float(first) / float(cs.n);
                if (!extend(k, first, -dir, t0)) LineTo(at(pt(k + 1)).x, at(pt(k + 1)).y);   // entered the curve elsewhere
                float t1 = float(sampleOf(pt(end))) / float(cs.n);
                if (extend((end + 1) % n, sampleOf(pt(end)), dir, t1)) ++end;
                if (t1 != t0) {
                    const Offset e = at(vertex(end));
                    const float u0 = t0, u1 = t1;
                    if (cs.conic) {
                        // The sub-conic: its homogeneous control point, the weight renormalised
                        // so both ends weigh 1.
                        float wa = 1, wm = 1, wc = 1;
                        const float ta[2] = { u0, u0 }, tm[2] = { u0, u1 }, tc[2] = { u1, u1 };
                        Blossom(cs, ta, &wa);
                        const Offset c1 = Blossom(cs, tm, &wm);
                        Blossom(cs, tc, &wc);
                        ConicTo(c1.x, c1.y, e.x, e.y, wm / std::sqrt(wa * wc));
                    } else if (cs.degree == 2) {
                        const float tc[2] = { u0, u1 };
                        const Offset c1 = Blossom(cs, tc);
                        QuadraticTo(c1.x, c1.y, e.x, e.y);
                    } else {
                        const float ta[3] = { u0, u0, u1 }, tb[3] = { u0, u1, u1 };
                        const Offset c1 = Blossom(cs, ta), c2 = Blossom(cs, tb);
                        CubicTo(c1.x, c1.y, c2.x, c2.y, e.x, e.y);
                    }
                }
                k = end;
            }
            Close();
        }
        return true;
    }

    bool Outline::Contains(ui::Offset p) const {
        if (auto* r = std::get_if<ui::Rect>(&value)) return r->Contains(p);
        if (auto* rr = std::get_if<RoundRect>(&value)) {
            const ui::Rect& r = rr->rect;
            if (!r.Contains(p)) return false;
            // Inside a corner's box, the point must be inside that corner's ellipse.
            auto corner = [&](Offset rad, float cx, float cy, bool left, bool top) {
                if (rad.x <= 0 || rad.y <= 0) return true;
                const bool inBox = (left ? p.x < cx : p.x > cx) && (top ? p.y < cy : p.y > cy);
                if (!inBox) return true;
                const float dx = (p.x - cx) / rad.x, dy = (p.y - cy) / rad.y;
                return dx * dx + dy * dy <= 1;
            };
            return corner(rr->topLeft, r.left + rr->topLeft.x, r.top + rr->topLeft.y, true, true) &&
                   corner(rr->topRight, r.right - rr->topRight.x, r.top + rr->topRight.y, false, true) &&
                   corner(rr->bottomRight, r.right - rr->bottomRight.x, r.bottom - rr->bottomRight.y, false, false) &&
                   corner(rr->bottomLeft, r.left + rr->bottomLeft.x, r.bottom - rr->bottomLeft.y, true, false);
        }
        // Winding number over the flattened path.
        const Path& path = std::get<Path>(value);
        int winding = 0;
        for (const Polyline& poly : path.Flatten(Matrix{}, 0.25f)) {
            const auto& q = poly.points;
            for (std::size_t i = 0; i < q.size(); ++i) {
                const Offset a = q[i], b = q[(i + 1) % q.size()];
                if ((a.y <= p.y) != (b.y <= p.y)) {
                    const float x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
                    if (x > p.x) winding += b.y > a.y ? 1 : -1;
                }
            }
        }
        return path.fillType == FillType::EvenOdd ? (winding & 1) != 0 : winding != 0;
    }

}
