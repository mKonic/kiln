#include "kiln/graphics/shapes/RoundedPolygon.h"

#include <algorithm>
#include <cassert>

namespace kiln::gfx::shapes {

    Feature Feature::Transformed(const PointTransformer& f) const {
        Feature r{ kind, {} };
        for (const Cubic& c : cubics) r.cubics.push_back(c.Transformed(f));
        return r;
    }

    Feature Feature::Reversed() const {
        Feature r{ kind, {} };
        for (auto it = cubics.rbegin(); it != cubics.rend(); ++it) r.cubics.push_back(it->Reverse());
        if (kind == Kind::ConvexCorner) r.kind = Kind::ConcaveCorner;
        else if (kind == Kind::ConcaveCorner) r.kind = Kind::ConvexCorner;
        return r;
    }

    RoundedPolygon::RoundedPolygon(std::vector<Feature> features, Point center) : m_Features(std::move(features)), m_Center(center) {
        // The cubics, starting halfway through the first feature when it is a three-cubic corner
        // (so the shape starts at a corner's midpoint); zero-length cubics fold into the previous.
        std::optional<Cubic> firstCubic, lastCubic;
        std::optional<std::vector<Cubic>> splitStart, splitEnd;
        if (!m_Features.empty() && m_Features[0].cubics.size() == 3) {
            auto [s, e] = m_Features[0].cubics[1].Split(0.5f);
            splitStart = std::vector<Cubic>{ m_Features[0].cubics[0], s };
            splitEnd = std::vector<Cubic>{ e, m_Features[0].cubics[2] };
        }
        for (std::size_t i = 0; i <= m_Features.size(); ++i) {
            const std::vector<Cubic>* fc;
            if (i == 0 && splitEnd) fc = &*splitEnd;
            else if (i == m_Features.size()) { if (splitStart) fc = &*splitStart; else break; }
            else fc = &m_Features[i].cubics;
            for (const Cubic& cubic : *fc) {
                if (!cubic.ZeroLength()) {
                    if (lastCubic) m_Cubics.push_back(*lastCubic);
                    lastCubic = cubic;
                    if (!firstCubic) firstCubic = cubic;
                } else if (lastCubic) {
                    lastCubic->points[6] = cubic.Anchor1X();
                    lastCubic->points[7] = cubic.Anchor1Y();
                }
            }
        }
        if (lastCubic && firstCubic) {
            m_Cubics.push_back(Cubic(lastCubic->Anchor0X(), lastCubic->Anchor0Y(), lastCubic->Control0X(), lastCubic->Control0Y(),
                                     lastCubic->Control1X(), lastCubic->Control1Y(), firstCubic->Anchor0X(), firstCubic->Anchor0Y()));
        } else {
            m_Cubics.push_back(Cubic::Empty(m_Center.x, m_Center.y));
        }
    }

    RoundedPolygon RoundedPolygon::Transformed(const PointTransformer& f) const {
        std::vector<Feature> fs;
        for (const Feature& feat : m_Features) fs.push_back(feat.Transformed(f));
        return RoundedPolygon(std::move(fs), f(m_Center.x, m_Center.y));
    }

    RoundedPolygon RoundedPolygon::Normalized() const {
        const auto b = CalculateBounds();
        const float w = b[2] - b[0], h = b[3] - b[1], side = std::max(w, h);
        const float ox = (side - w) / 2 - b[0], oy = (side - h) / 2 - b[1];
        return Transformed([=](float x, float y) { return Point{ (x + ox) / side, (y + oy) / side }; });
    }

    std::array<float, 4> RoundedPolygon::CalculateMaxBounds() const {
        float maxD2 = 0;
        for (const Cubic& c : m_Cubics) {
            const float a = DistanceSquared(c.Anchor0X() - m_Center.x, c.Anchor0Y() - m_Center.y);
            const Point m = c.PointOnCurve(0.5f);
            maxD2 = std::max(maxD2, std::max(a, DistanceSquared(m.x - m_Center.x, m.y - m_Center.y)));
        }
        const float d = std::sqrt(maxD2);
        return { m_Center.x - d, m_Center.y - d, m_Center.x + d, m_Center.y + d };
    }

    std::array<float, 4> RoundedPolygon::CalculateBounds(bool approximate) const {
        // Kotlin starts max at Float.MIN_VALUE (the smallest positive float), kept as is.
        float minX = std::numeric_limits<float>::max(), minY = minX;
        float maxX = std::numeric_limits<float>::denorm_min(), maxY = maxX;
        for (const Cubic& c : m_Cubics) {
            const auto b = c.CalculateBounds(approximate);
            minX = std::min(minX, b[0]); minY = std::min(minY, b[1]);
            maxX = std::max(maxX, b[2]); maxY = std::max(maxY, b[3]);
        }
        return { minX, minY, maxX, maxY };
    }

    Path RoundedPolygon::ToPath() const {
        Path p;
        bool first = true;
        for (const Cubic& c : m_Cubics) {
            if (first) { p.MoveTo(c.Anchor0X(), c.Anchor0Y()); first = false; }
            p.CubicTo(c.Control0X(), c.Control0Y(), c.Control1X(), c.Control1Y(), c.Anchor1X(), c.Anchor1Y());
        }
        p.Close();
        return p;
    }

    Point CalculateCenter(const std::vector<float>& v) {
        float cx = 0, cy = 0;
        for (std::size_t i = 0; i + 1 < v.size(); i += 2) { cx += v[i]; cy += v[i + 1]; }
        const float n = float(v.size() / 2);
        return { cx / n, cy / n };
    }

    namespace {
        struct RoundedCorner {
            Point p0, p1, p2;
            CornerRounding rounding;
            Point d1, d2;
            float cornerRadius = 0, smoothing = 0, cosAngle = 0, sinAngle = 0, expectedRoundCut = 0;
            Point center;

            RoundedCorner(Point a, Point b, Point c, CornerRounding r) : p0(a), p1(b), p2(c), rounding(r) {
                const Point v01 = p0 - p1, v21 = p2 - p1;
                const float d01 = v01.Distance(), d21 = v21.Distance();
                if (d01 > 0 && d21 > 0) {
                    d1 = v01 / d01;
                    d2 = v21 / d21;
                    cornerRadius = r.radius;
                    smoothing = r.smoothing;
                    cosAngle = d1.Dot(d2);
                    sinAngle = std::sqrt(1 - Square(cosAngle));
                    expectedRoundCut = sinAngle > 1e-3f ? cornerRadius * (cosAngle + 1) / sinAngle : 0;
                }
            }
            float ExpectedCut() const { return (1 + smoothing) * expectedRoundCut; }

            float ActualSmoothing(float allowedCut) const {
                if (allowedCut > ExpectedCut()) return smoothing;
                if (allowedCut > expectedRoundCut) return smoothing * (allowedCut - expectedRoundCut) / (ExpectedCut() - expectedRoundCut);
                return 0;
            }

            static std::optional<Point> LineIntersection(Point a, Point da, Point b, Point db) {
                const Point rd = db.Rotate90();
                const float den = da.Dot(rd);
                if (std::abs(den) < DistanceEpsilon) return std::nullopt;
                const float num = (b - a).Dot(rd);
                if (std::abs(den) < DistanceEpsilon * std::abs(num)) return std::nullopt;
                return a + da * (num / den);
            }

            Cubic FlankingCurve(float roundCut, float smooth, Point corner, Point sideStart, Point circleHit,
                                Point otherCircleHit, Point circleCenter, float r) const {
                const Point side = (sideStart - corner).Direction();
                const Point curveStart = corner + side * roundCut * (1 + smooth);
                const Point p = Interpolate(circleHit, (circleHit + otherCircleHit) / 2.0f, smooth);
                const Point curveEnd = circleCenter + DirectionVector(p.x - circleCenter.x, p.y - circleCenter.y) * r;
                const Point tangent = (curveEnd - circleCenter).Rotate90();
                const Point anchorEnd = LineIntersection(sideStart, side, curveEnd, tangent).value_or(circleHit);
                const Point anchorStart = (curveStart + anchorEnd * 2.0f) / 3.0f;
                return Cubic(curveStart, anchorStart, anchorEnd, curveEnd);
            }

            std::vector<Cubic> GetCubics(float allowedCut0, float allowedCut1) {
                const float allowedCut = std::min(allowedCut0, allowedCut1);
                if (expectedRoundCut < DistanceEpsilon || allowedCut < DistanceEpsilon || cornerRadius < DistanceEpsilon) {
                    center = p1;
                    return { Cubic::StraightLine(p1.x, p1.y, p1.x, p1.y) };
                }
                const float roundCut = std::min(allowedCut, expectedRoundCut);
                const float s0 = ActualSmoothing(allowedCut0), s1 = ActualSmoothing(allowedCut1);
                const float r = cornerRadius * roundCut / expectedRoundCut;
                const float centerDistance = std::sqrt(Square(r) + Square(roundCut));
                center = p1 + ((d1 + d2) / 2.0f).Direction() * centerDistance;
                const Point hit0 = p1 + d1 * roundCut, hit2 = p1 + d2 * roundCut;
                const Cubic f0 = FlankingCurve(roundCut, s0, p1, p0, hit0, hit2, center, r);
                const Cubic f2 = FlankingCurve(roundCut, s1, p1, p2, hit2, hit0, center, r).Reverse();
                return { f0, Cubic::CircularArc(center.x, center.y, f0.Anchor1X(), f0.Anchor1Y(), f2.Anchor0X(), f2.Anchor0Y()), f2 };
            }
        };

        std::vector<float> VerticesFromNumVerts(int n, float radius, float cx, float cy) {
            std::vector<float> out;
            for (int i = 0; i < n; ++i) {
                const Point v = RadialToCartesian(radius, FloatPi / float(n) * 2 * float(i)) + Point{ cx, cy };
                out.push_back(v.x);
                out.push_back(v.y);
            }
            return out;
        }
    }

    RoundedPolygon RoundedPolygon::Regular(int n, float radius, float cx, float cy, CornerRounding rounding, const std::vector<CornerRounding>* perVertex) {
        return FromVertices(VerticesFromNumVerts(n, radius, cx, cy), rounding, perVertex, cx, cy);
    }

    RoundedPolygon RoundedPolygon::FromVertices(const std::vector<float>& v, CornerRounding rounding, const std::vector<CornerRounding>* perVertex,
                                                float centerX, float centerY) {
        assert(v.size() >= 6 && v.size() % 2 == 0);
        const std::size_t n = v.size() / 2;
        std::vector<RoundedCorner> rc;
        for (std::size_t i = 0; i < n; ++i) {
            const CornerRounding r = perVertex ? (*perVertex)[i] : rounding;
            const std::size_t prev = ((i + n - 1) % n) * 2, next = ((i + 1) % n) * 2;
            rc.emplace_back(Point{ v[prev], v[prev + 1] }, Point{ v[i * 2], v[i * 2 + 1] }, Point{ v[next], v[next + 1] }, r);
        }
        // How much of each side the two corners' cuts may take (round cut first, then smoothing).
        std::vector<std::pair<float, float>> cutAdjusts;
        for (std::size_t ix = 0; ix < n; ++ix) {
            const float expectedRoundCut = rc[ix].expectedRoundCut + rc[(ix + 1) % n].expectedRoundCut;
            const float expectedCut = rc[ix].ExpectedCut() + rc[(ix + 1) % n].ExpectedCut();
            const float side = Distance(v[ix * 2] - v[((ix + 1) % n) * 2], v[ix * 2 + 1] - v[((ix + 1) % n) * 2 + 1]);
            if (expectedRoundCut > side) cutAdjusts.push_back({ side / expectedRoundCut, 0.0f });
            else if (expectedCut > side) cutAdjusts.push_back({ 1.0f, (side - expectedRoundCut) / (expectedCut - expectedRoundCut) });
            else cutAdjusts.push_back({ 1.0f, 1.0f });
        }
        std::vector<std::vector<Cubic>> corners;
        for (std::size_t i = 0; i < n; ++i) {
            float allowed[2];
            for (int delta = 0; delta <= 1; ++delta) {
                const auto [roundRatio, cutRatio] = cutAdjusts[(i + n - 1 + std::size_t(delta)) % n];
                allowed[delta] = rc[i].expectedRoundCut * roundRatio + (rc[i].ExpectedCut() - rc[i].expectedRoundCut) * cutRatio;
            }
            corners.push_back(rc[i].GetCubics(allowed[0], allowed[1]));
        }
        std::vector<Feature> features;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t prev = (i + n - 1) % n, next = (i + 1) % n;
            const bool convex = Convex({ v[prev * 2], v[prev * 2 + 1] }, { v[i * 2], v[i * 2 + 1] }, { v[next * 2], v[next * 2 + 1] });
            features.push_back(Feature::Corner(corners[i], convex));
            features.push_back(Feature::Edge({ Cubic::StraightLine(corners[i].back().Anchor1X(), corners[i].back().Anchor1Y(),
                                                                   corners[next].front().Anchor0X(), corners[next].front().Anchor0Y()) }));
        }
        const Point c = (centerX == kAutoCenter || centerY == kAutoCenter) ? CalculateCenter(v) : Point{ centerX, centerY };
        return RoundedPolygon(std::move(features), c);
    }

    RoundedPolygon RoundedPolygon::FromFeatures(std::vector<Feature> features, float centerX, float centerY) {
        assert(features.size() >= 2);
        std::vector<float> v;
        for (const Feature& f : features)
            for (const Cubic& c : f.cubics) { v.push_back(c.Anchor0X()); v.push_back(c.Anchor0Y()); }
        const Point c = CalculateCenter(v);
        return RoundedPolygon(std::move(features), { std::isnan(centerX) ? c.x : centerX, std::isnan(centerY) ? c.y : centerY });
    }

}
