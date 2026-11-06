#include "kiln/graphics/PathMeasure.h"

#include <algorithm>
#include <cmath>

namespace kiln::gfx {

    namespace {
        Offset L(Offset a, Offset b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
        float Dist(Offset a, Offset b) { return std::hypot(b.x - a.x, b.y - a.y); }
    }

    Offset ContourMeasure::Eval(const Segment& s, float t) {
        switch (s.kind) {
            case Kind::Line: return L(s.p[0], s.p[1], t);
            case Kind::Quad: return L(L(s.p[0], s.p[1], t), L(s.p[1], s.p[2], t), t);
            case Kind::Cubic: {
                const Offset a = L(s.p[0], s.p[1], t), b = L(s.p[1], s.p[2], t), c = L(s.p[2], s.p[3], t);
                return L(L(a, b, t), L(b, c, t), t);
            }
        }
        return {};
    }

    Offset ContourMeasure::Deriv(const Segment& s, float t) {
        switch (s.kind) {
            case Kind::Line: return s.p[1] - s.p[0];
            case Kind::Quad: { const Offset a = L(s.p[0], s.p[1], t), b = L(s.p[1], s.p[2], t); return b - a; }
            case Kind::Cubic: {
                const Offset a = L(s.p[0], s.p[1], t), b = L(s.p[1], s.p[2], t), c = L(s.p[2], s.p[3], t);
                Offset d = L(b, c, t) - L(a, b, t);
                if (std::abs(d.x) + std::abs(d.y) < 1e-9f) d = (t < 0.5f ? s.p[2] - s.p[0] : s.p[3] - s.p[1]);   // cusp at an end
                return d;
            }
        }
        return {};
    }

    // The sub-curve between t0 and t1 (de Casteljau twice).
    ContourMeasure::Segment ContourMeasure::Chop(const Segment& s, float t0, float t1) {
        Segment out{ s.kind, {} };
        switch (s.kind) {
            case Kind::Line:
                out.p[0] = Eval(s, t0); out.p[1] = Eval(s, t1);
                break;
            case Kind::Quad: {
                // Split at t1, keep the head, then split the head at t0/t1.
                Offset a = s.p[0], b = L(s.p[0], s.p[1], t1), c = Eval(s, t1);
                const float u = t1 > 0 ? t0 / t1 : 0;
                const Offset ab = L(a, b, u), bc = L(b, c, u);
                out.p[0] = L(ab, bc, u); out.p[1] = bc; out.p[2] = c;
                break;
            }
            case Kind::Cubic: {
                const Offset p01 = L(s.p[0], s.p[1], t1), p12 = L(s.p[1], s.p[2], t1), p23 = L(s.p[2], s.p[3], t1);
                const Offset p012 = L(p01, p12, t1), p123 = L(p12, p23, t1);
                const Offset h[4] = { s.p[0], p01, p012, L(p012, p123, t1) };
                const float u = t1 > 0 ? t0 / t1 : 0;
                const Offset q01 = L(h[0], h[1], u), q12 = L(h[1], h[2], u), q23 = L(h[2], h[3], u);
                const Offset q012 = L(q01, q12, u), q123 = L(q12, q23, u);
                out.p[0] = L(q012, q123, u); out.p[1] = q123; out.p[2] = q23; out.p[3] = h[3];
                break;
            }
        }
        return out;
    }

    std::vector<ContourMeasure> MeasureContours(const Path& path, bool forceClosed, float resScale) {
        std::vector<ContourMeasure> out;
        const float tol = 0.05f / std::max(resScale, 1e-3f);
        ContourMeasure cur;
        Offset start{}, last{};
        auto addSegment = [&](ContourMeasure::Segment s) {
            // Sample densely enough that chords stay within tol of the curve.
            int n = 1;
            if (s.kind != ContourMeasure::Kind::Line) {
                const float dd = s.kind == ContourMeasure::Kind::Quad
                    ? std::hypot(s.p[0].x - 2 * s.p[1].x + s.p[2].x, s.p[0].y - 2 * s.p[1].y + s.p[2].y) * 0.25f
                    : std::max(std::hypot(s.p[0].x - 2 * s.p[1].x + s.p[2].x, s.p[0].y - 2 * s.p[1].y + s.p[2].y),
                               std::hypot(s.p[1].x - 2 * s.p[2].x + s.p[3].x, s.p[1].y - 2 * s.p[2].y + s.p[3].y)) * 0.75f;
                n = std::clamp(int(std::ceil(std::sqrt(dd / tol))), 1, 1000);
            }
            const auto index = std::uint32_t(cur.m_Segments.size());
            Offset prev = s.p[0];
            float len = cur.m_Length;
            bool any = false;
            for (int i = 1; i <= n; ++i) {
                const float t = float(i) / float(n);
                const Offset q = ContourMeasure::Eval(s, t);
                const float d = Dist(prev, q);
                if (d > 0) { len += d; cur.m_Samples.push_back({ len, index, t }); any = true; }
                prev = q;
            }
            if (any) { cur.m_Segments.push_back(s); cur.m_Length = len; }
        };
        auto finish = [&](bool closed) {
            if (closed && Dist(last, start) > 0) addSegment({ ContourMeasure::Kind::Line, { last, start } });
            cur.m_Closed = closed;
            if (cur.m_Length > 0) out.push_back(std::move(cur));
            cur = ContourMeasure{};
        };
        const auto& verbs = path.Verbs();
        const auto& pts = path.Points();
        std::size_t pi = 0;
        bool open = false;
        for (Path::Verb v : verbs) {
            switch (v) {
                case Path::Verb::Move:
                    if (open) finish(forceClosed);
                    start = last = pts[pi++];
                    open = true;
                    break;
                case Path::Verb::Line:
                    addSegment({ ContourMeasure::Kind::Line, { last, pts[pi] } });
                    last = pts[pi++];
                    break;
                case Path::Verb::Quad:
                    addSegment({ ContourMeasure::Kind::Quad, { last, pts[pi], pts[pi + 1] } });
                    last = pts[pi + 1];
                    pi += 2;
                    break;
                case Path::Verb::Cubic:
                    addSegment({ ContourMeasure::Kind::Cubic, { last, pts[pi], pts[pi + 1], pts[pi + 2] } });
                    last = pts[pi + 2];
                    pi += 3;
                    break;
                case Path::Verb::Close:
                    if (open) finish(true);
                    open = false;
                    last = start;
                    break;
            }
        }
        if (open) finish(forceClosed);
        return out;
    }

    void ContourMeasure::Locate(float d, std::uint32_t& seg, float& t) const {
        // First sample at or past d; interpolate t within its segment.
        auto it = std::lower_bound(m_Samples.begin(), m_Samples.end(), d, [](const Sample& s, float v) { return s.distance < v; });
        if (it == m_Samples.end()) it = m_Samples.end() - 1;
        seg = it->segment;
        float prevD = 0, prevT = 0;
        if (it != m_Samples.begin()) {
            const Sample& p = *(it - 1);
            prevD = p.distance;
            if (p.segment == seg) prevT = p.t;
        }
        const float span = it->distance - prevD;
        t = prevT + (span > 0 ? (d - prevD) / span : 0) * (it->t - prevT);
    }

    Offset ContourMeasure::Position(float d) const {
        if (m_Segments.empty()) return {};
        std::uint32_t s; float t;
        Locate(std::clamp(d, 0.0f, m_Length), s, t);
        return Eval(m_Segments[s], t);
    }

    Offset ContourMeasure::Tangent(float d) const {
        if (m_Segments.empty()) return {};
        std::uint32_t s; float t;
        Locate(std::clamp(d, 0.0f, m_Length), s, t);
        const Offset v = Deriv(m_Segments[s], t);
        const float l = std::hypot(v.x, v.y);
        return l > 0 ? Offset{ v.x / l, v.y / l } : Offset{};
    }

    bool ContourMeasure::GetSegment(float start, float stop, Path& dst, bool startWithMoveTo) const {
        start = std::max(start, 0.0f);
        stop = std::min(stop, m_Length);
        if (!(start < stop) || m_Segments.empty()) return false;   // SkContourMeasure: empty or inverted
        std::uint32_t s0, s1; float t0, t1;
        Locate(start, s0, t0);
        Locate(stop, s1, t1);
        auto emit = [&](const Segment& seg, float a, float b) {
            const Segment c = Chop(seg, a, b);
            switch (c.kind) {
                case Kind::Line: dst.LineTo(c.p[1].x, c.p[1].y); break;
                case Kind::Quad: dst.QuadraticTo(c.p[1].x, c.p[1].y, c.p[2].x, c.p[2].y); break;
                case Kind::Cubic: dst.CubicTo(c.p[1].x, c.p[1].y, c.p[2].x, c.p[2].y, c.p[3].x, c.p[3].y); break;
            }
        };
        const Offset p = Eval(m_Segments[s0], t0);
        if (startWithMoveTo) dst.MoveTo(p.x, p.y);
        else if (dst.IsEmpty()) dst.MoveTo(p.x, p.y);
        else dst.LineTo(p.x, p.y);   // continue the current contour
        if (s0 == s1) {
            emit(m_Segments[s0], t0, t1);
        } else {
            emit(m_Segments[s0], t0, 1);
            for (std::uint32_t i = s0 + 1; i < s1; ++i) emit(m_Segments[i], 0, 1);
            emit(m_Segments[s1], 0, t1);
        }
        return true;
    }

    void PathMeasure::SetPath(const Path* path, bool forceClosed) {
        m_Contour = ContourMeasure{};
        if (!path) return;
        auto contours = MeasureContours(*path, forceClosed);
        if (!contours.empty()) m_Contour = std::move(contours.front());
    }

}
