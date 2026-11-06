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
        Clipper2Lib::Paths64 ToClipper(const Path& p, float tolerance) {
            Clipper2Lib::Paths64 out;
            for (const Polyline& poly : p.Flatten(Matrix{}, tolerance)) {
                if (poly.points.size() < 3) continue;
                Clipper2Lib::Path64 c;
                c.reserve(poly.points.size());
                for (const Offset& q : poly.points)
                    c.emplace_back(std::int64_t(std::llround(double(q.x) * kClipScale)), std::int64_t(std::llround(double(q.y) * kClipScale)));
                out.push_back(std::move(c));
            }
            return out;
        }
        Clipper2Lib::FillRule RuleOf(const Path& p) {
            return p.fillType == FillType::EvenOdd ? Clipper2Lib::FillRule::EvenOdd : Clipper2Lib::FillRule::NonZero;
        }
    }

    bool Path::Op(const Path& a, const Path& b, PathOperation op, float tolerance) {
        using namespace Clipper2Lib;
        // Each operand resolved by its own fill type first, then combined (Skia keeps per-path fill
        // types too); both are then simple, so one rule serves the combination.
        const Paths64 sa = Union(ToClipper(a, tolerance), RuleOf(a));
        const Paths64 sb = Union(ToClipper(b, tolerance), RuleOf(b));
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
        for (const Path64& c : r) {
            if (c.empty()) continue;
            MoveTo(float(double(c[0].x) / kClipScale), float(double(c[0].y) / kClipScale));
            for (std::size_t i = 1; i < c.size(); ++i) LineTo(float(double(c[i].x) / kClipScale), float(double(c[i].y) / kClipScale));
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
