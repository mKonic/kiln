#include "kiln/graphics/Path.h"

#include <algorithm>
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

    void Path::Close() {
        if (m_Verbs.empty() || m_Verbs.back() == Verb::Close) return;
        m_Verbs.push_back(Verb::Close);
        m_NeedsMove = true;
    }

    namespace {
        constexpr float kPi = std::numbers::pi_v<float>;

        // Appends an elliptical arc as cubics (each at most 90 degrees). Angles in radians.
        void ArcCubics(Path& p, float cx, float cy, float rx, float ry, float start, float sweep) {
            const int n = std::max(1, int(std::ceil(std::abs(sweep) / (kPi / 2) - 1e-4f)));
            const float step = sweep / float(n);
            const float k = 4.0f / 3.0f * std::tan(step / 4);
            float a0 = start;
            for (int i = 0; i < n; ++i) {
                const float a1 = a0 + step;
                const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                p.CubicTo(cx + rx * (c0 - k * s0), cy + ry * (s0 + k * c0),
                          cx + rx * (c1 + k * s1), cy + ry * (s1 - k * c1),
                          cx + rx * c1, cy + ry * s1);
                a0 = a1;
            }
        }
        float Radians(float deg) { return deg * kPi / 180.0f; }
    }

    void Path::ArcTo(Rect oval, float startDeg, float sweepDeg, bool forceMoveTo) {
        sweepDeg = std::clamp(sweepDeg, -360.0f, 360.0f);
        const float cx = (oval.left + oval.right) * 0.5f, cy = (oval.top + oval.bottom) * 0.5f;
        const float rx = oval.Width() * 0.5f, ry = oval.Height() * 0.5f;
        const float a = Radians(startDeg);
        const float sx = cx + rx * std::cos(a), sy = cy + ry * std::sin(a);
        if (forceMoveTo || m_NeedsMove || m_Verbs.empty()) MoveTo(sx, sy);
        else LineTo(sx, sy);
        if (sweepDeg != 0) ArcCubics(*this, cx, cy, rx, ry, a, Radians(sweepDeg));
    }

    void Path::AddArc(Rect oval, float startDeg, float sweepDeg) {
        if (std::abs(sweepDeg) >= 360.0f) {
            AddOval(oval, sweepDeg > 0 ? Direction::Clockwise : Direction::CounterClockwise);
            return;
        }
        ArcTo(oval, startDeg, sweepDeg, true);
    }

    void Path::AddRect(Rect r, Direction dir) {
        MoveTo(r.left, r.top);
        if (dir == Direction::Clockwise) {
            LineTo(r.right, r.top); LineTo(r.right, r.bottom); LineTo(r.left, r.bottom);
        } else {
            LineTo(r.left, r.bottom); LineTo(r.right, r.bottom); LineTo(r.right, r.top);
        }
        Close();
    }

    void Path::AddOval(Rect oval, Direction dir) {
        const float cx = (oval.left + oval.right) * 0.5f, cy = (oval.top + oval.bottom) * 0.5f;
        const float rx = oval.Width() * 0.5f, ry = oval.Height() * 0.5f;
        MoveTo(cx + rx, cy);
        ArcCubics(*this, cx, cy, rx, ry, 0, dir == Direction::Clockwise ? 2 * kPi : -2 * kPi);
        Close();
    }

    void Path::AddRoundRect(const RoundRect& rr, Direction dir) {
        const Rect& r = rr.rect;
        // Radii scaled down together when adjacent ones overlap (SkRRect::scaleRadii).
        float scale = 1;
        auto fit = [&](float a, float b, float len) { if (a + b > len && a + b > 0) scale = std::min(scale, len / (a + b)); };
        fit(rr.topLeft.x, rr.topRight.x, r.Width());
        fit(rr.bottomLeft.x, rr.bottomRight.x, r.Width());
        fit(rr.topLeft.y, rr.bottomLeft.y, r.Height());
        fit(rr.topRight.y, rr.bottomRight.y, r.Height());
        const Offset tl{ rr.topLeft.x * scale, rr.topLeft.y * scale }, tr{ rr.topRight.x * scale, rr.topRight.y * scale };
        const Offset br{ rr.bottomRight.x * scale, rr.bottomRight.y * scale }, bl{ rr.bottomLeft.x * scale, rr.bottomLeft.y * scale };
        auto corner = [&](Offset rad, float cx, float cy, float start, float sweep) {
            if (rad.x <= 0 || rad.y <= 0) {
                const float a = start + sweep;
                LineTo(cx + rad.x * std::cos(a), cy + rad.y * std::sin(a));
                return;
            }
            ArcCubics(*this, cx, cy, rad.x, rad.y, start, sweep);
        };
        if (dir == Direction::Clockwise) {
            MoveTo(r.left + tl.x, r.top);
            LineTo(r.right - tr.x, r.top);
            corner(tr, r.right - tr.x, r.top + tr.y, -kPi / 2, kPi / 2);
            LineTo(r.right, r.bottom - br.y);
            corner(br, r.right - br.x, r.bottom - br.y, 0, kPi / 2);
            LineTo(r.left + bl.x, r.bottom);
            corner(bl, r.left + bl.x, r.bottom - bl.y, kPi / 2, kPi / 2);
            LineTo(r.left, r.top + tl.y);
            corner(tl, r.left + tl.x, r.top + tl.y, kPi, kPi / 2);
        } else {
            MoveTo(r.left + tl.x, r.top);
            corner(tl, r.left + tl.x, r.top + tl.y, -kPi / 2, -kPi / 2);
            LineTo(r.left, r.bottom - bl.y);
            corner(bl, r.left + bl.x, r.bottom - bl.y, kPi, -kPi / 2);
            LineTo(r.right - br.x, r.bottom);
            corner(br, r.right - br.x, r.bottom - br.y, kPi / 2, -kPi / 2);
            LineTo(r.right, r.top + tr.y);
            corner(tr, r.right - tr.x, r.top + tr.y, 0, -kPi / 2);
        }
        Close();
    }

    void Path::AddPath(const Path& other, Offset offset) {
        std::size_t pi = 0;
        for (Verb v : other.m_Verbs) {
            auto pt = [&](std::size_t i) { return other.m_Points[pi + i] + offset; };
            switch (v) {
                case Verb::Move: { const Offset p = pt(0); MoveTo(p.x, p.y); pi += 1; break; }
                case Verb::Line: { const Offset p = pt(0); LineTo(p.x, p.y); pi += 1; break; }
                case Verb::Quad: { const Offset a = pt(0), b = pt(1); QuadraticTo(a.x, a.y, b.x, b.y); pi += 2; break; }
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
        std::size_t pi = 0;
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

}
