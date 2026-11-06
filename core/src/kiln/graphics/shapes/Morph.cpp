#include "kiln/graphics/shapes/Morph.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace kiln::gfx::shapes {

    namespace detail {
        bool ProgressInRange(float p, float from, float to) {
            return to >= from ? (p >= from && p <= to) : (p >= from || p <= to);
        }

        float LinearMap(const std::vector<float>& xs, const std::vector<float>& ys, float x) {
            std::size_t start = 0;
            for (std::size_t i = 0; i < xs.size(); ++i)
                if (ProgressInRange(x, xs[i], xs[(i + 1) % xs.size()])) { start = i; break; }
            const std::size_t end = (start + 1) % xs.size();
            const float sizeX = PositiveModulo(xs[end] - xs[start], 1);
            const float sizeY = PositiveModulo(ys[end] - ys[start], 1);
            const float pos = sizeX < 0.001f ? 0.5f : PositiveModulo(x - xs[start], 1) / sizeX;
            return PositiveModulo(ys[start] + sizeY * pos, 1);
        }

        float ProgressDistance(float a, float b) { const float d = std::abs(a - b); return std::min(d, 1 - d); }

        namespace {
            // LengthMeasurer.closestProgressTo: (cut t, length so far).
            std::pair<float, float> ClosestProgressTo(const Cubic& c, float threshold) {
                constexpr int segments = 3;
                float total = 0, remainder = threshold;
                Point prev{ c.Anchor0X(), c.Anchor0Y() };
                for (int i = 1; i <= segments; ++i) {
                    const float progress = float(i) / segments;
                    const Point p = c.PointOnCurve(progress);
                    const float segment = (p - prev).Distance();
                    if (segment >= remainder) return { progress - (1.0f - remainder / segment) / segments, threshold };
                    remainder -= segment;
                    total += segment;
                    prev = p;
                }
                return { 1.0f, total };
            }
        }
        float MeasureCubic(const Cubic& c) { return ClosestProgressTo(c, std::numeric_limits<float>::infinity()).second; }
        float FindCubicCutPoint(const Cubic& c, float m) { return ClosestProgressTo(c, m).first; }
    }

    using namespace detail;

    namespace {
        struct ProgressableFeature { float progress; const Feature* feature; };

        struct MeasuredCubic {
            Cubic cubic;
            float start, end;
            float measuredSize;
            MeasuredCubic(const Cubic& c, float s, float e) : cubic(c), start(s), end(e), measuredSize(MeasureCubic(c)) {}

            std::pair<MeasuredCubic, MeasuredCubic> CutAtProgress(float cut) const {
                const float bounded = std::clamp(cut, start, end);
                const float size = end - start;
                const float relative = (bounded - start) / size;
                const float t = FindCubicCutPoint(cubic, relative * measuredSize);
                auto [c1, c2] = cubic.Split(t);
                return { MeasuredCubic(c1, start, bounded), MeasuredCubic(c2, bounded, end) };
            }
        };

        struct MeasuredPolygon {
            std::vector<ProgressableFeature> features;
            std::vector<MeasuredCubic> cubics;

            MeasuredPolygon(std::vector<ProgressableFeature> f, const std::vector<Cubic>& cs, const std::vector<float>& progress)
                : features(std::move(f)) {
                assert(progress.size() == cs.size() + 1);
                float startProgress = 0;
                for (std::size_t i = 0; i < cs.size(); ++i) {
                    if (progress[i + 1] - progress[i] > DistanceEpsilon) {
                        cubics.emplace_back(cs[i], startProgress, progress[i + 1]);
                        startProgress = progress[i + 1];
                    }
                }
                cubics.back().end = 1;
            }

            static MeasuredPolygon Measure(const RoundedPolygon& polygon) {
                std::vector<Cubic> cs;
                std::vector<std::pair<const Feature*, std::size_t>> featureToCubic;
                for (const Feature& f : polygon.Features())
                    for (std::size_t ci = 0; ci < f.cubics.size(); ++ci) {
                        if (!f.IsEdge() && ci == f.cubics.size() / 2) featureToCubic.push_back({ &f, cs.size() });
                        cs.push_back(f.cubics[ci]);
                    }
                std::vector<float> measures{ 0 };
                for (const Cubic& c : cs) measures.push_back(measures.back() + MeasureCubic(c));
                const float total = measures.back();
                std::vector<float> progress;
                for (float m : measures) progress.push_back(m / total);
                std::vector<ProgressableFeature> features;
                for (auto [f, ix] : featureToCubic) features.push_back({ PositiveModulo((progress[ix] + progress[ix + 1]) / 2, 1), f });
                return MeasuredPolygon(std::move(features), cs, progress);
            }

            MeasuredPolygon CutAndShift(float cut) const {
                if (cut < DistanceEpsilon) return *this;
                std::size_t target = 0;
                for (std::size_t i = 0; i < cubics.size(); ++i)
                    if (cut >= cubics[i].start && cut <= cubics[i].end) { target = i; break; }
                auto [b1, b2] = cubics[target].CutAtProgress(cut);
                std::vector<Cubic> cs{ b2.cubic };
                for (std::size_t i = 1; i < cubics.size(); ++i) cs.push_back(cubics[(i + target) % cubics.size()].cubic);
                cs.push_back(b1.cubic);
                std::vector<float> progress;
                for (std::size_t i = 0; i < cubics.size() + 2; ++i) {
                    if (i == 0) progress.push_back(0);
                    else if (i == cubics.size() + 1) progress.push_back(1);
                    else progress.push_back(PositiveModulo(cubics[(target + i - 1) % cubics.size()].end - cut, 1));
                }
                std::vector<ProgressableFeature> f;
                for (const auto& pf : features) f.push_back({ PositiveModulo(pf.progress - cut, 1), pf.feature });
                return MeasuredPolygon(std::move(f), cs, progress);
            }
        };

        struct DoubleMapper {
            std::vector<float> source, target;
            float Map(float x) const { return LinearMap(source, target, x); }
            float MapBack(float x) const { return LinearMap(target, source, x); }
        };

        Point RepresentativePoint(const Feature& f) {
            return { (f.cubics.front().Anchor0X() + f.cubics.back().Anchor1X()) / 2, (f.cubics.front().Anchor0Y() + f.cubics.back().Anchor1Y()) / 2 };
        }
        float FeatureDistSquared(const Feature& a, const Feature& b) {
            if (!a.IsEdge() && !b.IsEdge() && a.IsConvexCorner() != b.IsConvexCorner()) return std::numeric_limits<float>::max();
            return (RepresentativePoint(a) - RepresentativePoint(b)).DistanceSquared();
        }

        // FeatureMapping.doMapping + MappingHelper: closest corner pairs first, keeping the
        // mapping monotonic around the outline.
        std::vector<std::pair<float, float>> DoMapping(const std::vector<ProgressableFeature>& f1, const std::vector<ProgressableFeature>& f2) {
            struct DV { float d; std::size_t i1, i2; };
            std::vector<DV> list;
            for (std::size_t i = 0; i < f1.size(); ++i)
                for (std::size_t j = 0; j < f2.size(); ++j) {
                    const float d = FeatureDistSquared(*f1[i].feature, *f2[j].feature);
                    if (d != std::numeric_limits<float>::max()) list.push_back({ d, i, j });
                }
            std::stable_sort(list.begin(), list.end(), [](const DV& a, const DV& b) { return a.d < b.d; });
            if (list.empty()) return { { 0, 0 }, { 0.5f, 0.5f } };
            if (list.size() == 1) {
                const float a = f1[list[0].i1].progress, b = f2[list[0].i2].progress;
                return { { a, b }, { std::fmod(a + 0.5f, 1.0f), std::fmod(b + 0.5f, 1.0f) } };
            }
            std::vector<std::pair<float, float>> mapping;
            std::vector<bool> used1(f1.size()), used2(f2.size());
            for (const DV& dv : list) {
                if (used1[dv.i1] || used2[dv.i2]) continue;
                const float p1 = f1[dv.i1].progress, p2 = f2[dv.i2].progress;
                auto it = std::lower_bound(mapping.begin(), mapping.end(), p1, [](const auto& m, float v) { return m.first < v; });
                if (it != mapping.end() && it->first == p1) continue;   // Kotlin requires distinct progress
                const std::size_t ins = std::size_t(it - mapping.begin());
                const std::size_t n = mapping.size();
                if (n >= 1) {
                    const auto [before1, before2] = mapping[(ins + n - 1) % n];
                    const auto [after1, after2] = mapping[ins % n];
                    if (ProgressDistance(p1, before1) < DistanceEpsilon || ProgressDistance(p1, after1) < DistanceEpsilon ||
                        ProgressDistance(p2, before2) < DistanceEpsilon || ProgressDistance(p2, after2) < DistanceEpsilon)
                        continue;
                    if (n > 1 && !ProgressInRange(p2, before2, after2)) continue;
                }
                mapping.insert(mapping.begin() + std::ptrdiff_t(ins), { p1, p2 });
                used1[dv.i1] = true;
                used2[dv.i2] = true;
            }
            return mapping;
        }

        DoubleMapper FeatureMapper(const std::vector<ProgressableFeature>& a, const std::vector<ProgressableFeature>& b) {
            std::vector<ProgressableFeature> ca, cb;
            for (const auto& f : a) if (!f.feature->IsEdge()) ca.push_back(f);
            for (const auto& f : b) if (!f.feature->IsEdge()) cb.push_back(f);
            DoubleMapper m;
            for (auto [x, y] : DoMapping(ca, cb)) { m.source.push_back(x); m.target.push_back(y); }
            return m;
        }

        std::vector<std::pair<Cubic, Cubic>> Match(const RoundedPolygon& p1, const RoundedPolygon& p2) {
            const MeasuredPolygon m1 = MeasuredPolygon::Measure(p1);
            const MeasuredPolygon m2 = MeasuredPolygon::Measure(p2);
            const DoubleMapper mapper = FeatureMapper(m1.features, m2.features);
            const float cut = mapper.Map(0);
            const MeasuredPolygon& bs1 = m1;
            const MeasuredPolygon bs2 = m2.CutAndShift(cut);
            std::vector<std::pair<Cubic, Cubic>> ret;
            std::size_t i1 = 0, i2 = 0;
            std::optional<MeasuredCubic> b1, b2;
            if (i1 < bs1.cubics.size()) b1 = bs1.cubics[i1++];
            if (i2 < bs2.cubics.size()) b2 = bs2.cubics[i2++];
            while (b1 && b2) {
                const float b1a = i1 == bs1.cubics.size() ? 1.0f : b1->end;
                const float b2a = i2 == bs2.cubics.size() ? 1.0f : mapper.MapBack(PositiveModulo(b2->end + cut, 1));
                const float minb = std::min(b1a, b2a);
                std::optional<MeasuredCubic> seg1, nb1, seg2, nb2;
                if (b1a > minb + AngleEpsilon) {
                    auto [s, n] = b1->CutAtProgress(minb);
                    seg1 = s; nb1 = n;
                } else {
                    seg1 = b1;
                    if (i1 < bs1.cubics.size()) nb1 = bs1.cubics[i1++]; else ++i1;
                }
                if (b2a > minb + AngleEpsilon) {
                    auto [s, n] = b2->CutAtProgress(PositiveModulo(mapper.Map(minb) - cut, 1));
                    seg2 = s; nb2 = n;
                } else {
                    seg2 = b2;
                    if (i2 < bs2.cubics.size()) nb2 = bs2.cubics[i2++]; else ++i2;
                }
                ret.push_back({ seg1->cubic, seg2->cubic });
                b1 = nb1;
                b2 = nb2;
            }
            return ret;
        }
    }

    Morph::Morph(const RoundedPolygon& start, const RoundedPolygon& end) : m_Start(start), m_End(end), m_Match(Match(start, end)) {}

    std::vector<Cubic> Morph::AsCubics(float progress) const {
        std::vector<Cubic> out;
        std::optional<Cubic> first, last;
        for (const auto& [a, b] : m_Match) {
            const Cubic c = Cubic::Interpolated(a, b, progress);
            if (!first) first = c;
            if (last) out.push_back(*last);
            last = c;
        }
        if (last && first)
            out.push_back(Cubic(last->Anchor0X(), last->Anchor0Y(), last->Control0X(), last->Control0Y(), last->Control1X(), last->Control1Y(),
                                first->Anchor0X(), first->Anchor0Y()));
        return out;
    }

    Path Morph::ToPath(float progress) const {
        Path p;
        bool firstCubic = true;
        for (const Cubic& c : AsCubics(progress)) {
            if (firstCubic) { p.MoveTo(c.Anchor0X(), c.Anchor0Y()); firstCubic = false; }
            p.CubicTo(c.Control0X(), c.Control0Y(), c.Control1X(), c.Control1Y(), c.Anchor1X(), c.Anchor1Y());
        }
        p.Close();
        return p;
    }

    std::array<float, 4> Morph::CalculateBounds(bool approximate) const {
        const auto a = m_Start.CalculateBounds(approximate), b = m_End.CalculateBounds(approximate);
        return { std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3]) };
    }

    std::array<float, 4> Morph::CalculateMaxBounds() const {
        const auto a = m_Start.CalculateMaxBounds(), b = m_End.CalculateMaxBounds();
        return { std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3]) };
    }

}
