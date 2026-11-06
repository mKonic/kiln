#include "kiln/graphics/PathEffect.h"

#include "kiln/graphics/PathMeasure.h"

#include <cmath>
#include <cstring>

namespace kiln::gfx {

    namespace {
        std::uint64_t Fnv(std::uint64_t h, const void* data, std::size_t n) {
            const auto* b = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
            return h;
        }

        class DashEffect final : public PathEffect {
        public:
            DashEffect(std::vector<float> intervals, float phase) : m_Intervals(std::move(intervals)) {
                for (float v : m_Intervals) m_Total += v;
                // SkDashPath::CalcDashParameters: wrap the phase into [0, total).
                if (m_Total > 0) {
                    if (phase < 0) {
                        phase = -phase;
                        if (phase > m_Total) phase = std::fmod(phase, m_Total);
                        phase = m_Total - phase;
                        if (phase == m_Total) phase = 0;
                    } else if (phase >= m_Total) {
                        phase = std::fmod(phase, m_Total);
                    }
                }
                // Index of the interval the phase falls in and how much of it remains.
                float p = phase;
                for (std::size_t i = 0; i < m_Intervals.size(); ++i) {
                    if (p > m_Intervals[i] || (p == m_Intervals[i] && m_Intervals[i] != 0)) { p -= m_Intervals[i]; continue; }
                    m_InitialIndex = i;
                    m_InitialLength = m_Intervals[i] - p;
                    break;
                }
                m_Phase = phase;
            }

            Path Apply(const Path& src) const override {
                Path dst;
                const bool valid = m_Intervals.size() >= 2 && m_Intervals.size() % 2 == 0 && m_Total > 0;
                if (!valid) return src;   // Skia rejects such a dash: the path is drawn as is
                for (const ContourMeasure& meas : MeasureContours(src, false)) {
                    bool skipFirstSegment = meas.IsClosed();
                    bool addedSegment = false;
                    const float length = meas.Length();
                    std::size_t index = m_InitialIndex;
                    float dlen = m_InitialLength;
                    float distance = 0;
                    while (distance < length) {
                        addedSegment = false;
                        if (index % 2 == 0 && !skipFirstSegment) {
                            addedSegment = true;
                            meas.GetSegment(distance, distance + dlen, dst, true);
                        }
                        distance += dlen;
                        skipFirstSegment = false;
                        index = (index + 1) % m_Intervals.size();
                        dlen = m_Intervals[index];
                    }
                    // A closed contour's skipped first dash joins the last one.
                    if (meas.IsClosed() && m_InitialIndex % 2 == 0 && m_InitialLength >= 0)
                        meas.GetSegment(0, m_InitialLength, dst, !addedSegment);
                }
                return dst;
            }

            std::uint64_t Hash() const override {
                std::uint64_t h = Fnv(1469598103934665603ull, "dash", 4);
                h = Fnv(h, m_Intervals.data(), m_Intervals.size() * sizeof(float));
                return Fnv(h, &m_Phase, sizeof(float));
            }

        private:
            std::vector<float> m_Intervals;
            float m_Total = 0, m_Phase = 0, m_InitialLength = 0;
            std::size_t m_InitialIndex = 0;
        };

        class CornerEffect final : public PathEffect {
        public:
            explicit CornerEffect(float r) : m_Radius(r) {}

            // SkCornerPathEffect::filterPath: each line-line corner becomes a quad through the
            // vertex, starting and ending min(radius, half the segment) away from it.
            Path Apply(const Path& src) const override {
                if (m_Radius <= 0) return src;
                Path dst;
                const auto& verbs = src.Verbs();
                const auto& pts = src.Points();
                std::size_t pi = 0;
                Offset moveTo{}, last{};
                bool prevIsValid = false, closed = false;
                Offset step{};
                auto stepFor = [&](Offset a, Offset b) {
                    const float len = std::hypot(b.x - a.x, b.y - a.y);
                    if (len == 0) return Offset{};
                    const float s = std::min(m_Radius, len / 2) / len;
                    return Offset{ (b.x - a.x) * s, (b.y - a.y) * s };
                };
                bool firstStep = true;
                Offset firstStepV{};
                for (std::size_t vi = 0; vi < verbs.size(); ++vi) {
                    switch (verbs[vi]) {
                        case Path::Verb::Move:
                            moveTo = last = pts[pi++];
                            prevIsValid = false;
                            firstStep = true;
                            closed = false;
                            break;
                        case Path::Verb::Line: {
                            const Offset p = pts[pi++];
                            const Offset s = stepFor(last, p);
                            if (!prevIsValid) {
                                dst.MoveTo(moveTo.x + s.x, moveTo.y + s.y);
                                prevIsValid = true;
                                if (firstStep) { firstStepV = s; firstStep = false; }
                            } else {
                                dst.QuadraticTo(last.x, last.y, last.x + s.x, last.y + s.y);
                            }
                            dst.LineTo(p.x - s.x, p.y - s.y);
                            step = s;
                            last = p;
                            break;
                        }
                        case Path::Verb::Quad: case Path::Verb::Cubic: {
                            const int n = verbs[vi] == Path::Verb::Quad ? 2 : 3;
                            if (!prevIsValid) { dst.MoveTo(last.x, last.y); prevIsValid = true; }
                            else dst.LineTo(last.x, last.y);
                            if (n == 2) dst.QuadraticTo(pts[pi].x, pts[pi].y, pts[pi + 1].x, pts[pi + 1].y);
                            else dst.CubicTo(pts[pi].x, pts[pi].y, pts[pi + 1].x, pts[pi + 1].y, pts[pi + 2].x, pts[pi + 2].y);
                            last = pts[pi + n - 1];
                            pi += std::size_t(n);
                            step = {};
                            break;
                        }
                        case Path::Verb::Close: {
                            // Round the closing corner and the corner at the start point.
                            if (prevIsValid) {
                                const Offset s = stepFor(last, moveTo);
                                if (last.x != moveTo.x || last.y != moveTo.y) {
                                    dst.QuadraticTo(last.x, last.y, last.x + s.x, last.y + s.y);
                                    dst.LineTo(moveTo.x - s.x, moveTo.y - s.y);
                                }
                                dst.QuadraticTo(moveTo.x, moveTo.y, moveTo.x + firstStepV.x, moveTo.y + firstStepV.y);
                                dst.Close();
                            }
                            closed = true;
                            last = moveTo;
                            prevIsValid = false;
                            break;
                        }
                    }
                }
                // An open contour ends at its last point.
                if (prevIsValid && !closed && (step.x != 0 || step.y != 0)) dst.LineTo(last.x, last.y);
                dst.fillType = src.fillType;
                return dst;
            }

            std::uint64_t Hash() const override {
                return Fnv(Fnv(1469598103934665603ull, "corner", 6), &m_Radius, sizeof(float));
            }

        private:
            float m_Radius;
        };

        class ChainEffect final : public PathEffect {
        public:
            ChainEffect(std::shared_ptr<const PathEffect> o, std::shared_ptr<const PathEffect> i) : m_Outer(std::move(o)), m_Inner(std::move(i)) {}
            Path Apply(const Path& src) const override { return m_Outer->Apply(m_Inner->Apply(src)); }
            std::uint64_t Hash() const override {
                const std::uint64_t a = m_Outer->Hash(), b = m_Inner->Hash();
                return Fnv(Fnv(Fnv(1469598103934665603ull, "chain", 5), &a, 8), &b, 8);
            }
        private:
            std::shared_ptr<const PathEffect> m_Outer, m_Inner;
        };
    }

    std::shared_ptr<const PathEffect> PathEffect::Dash(std::vector<float> intervals, float phase) {
        return std::make_shared<DashEffect>(std::move(intervals), phase);
    }
    std::shared_ptr<const PathEffect> PathEffect::Corner(float radius) { return std::make_shared<CornerEffect>(radius); }
    std::shared_ptr<const PathEffect> PathEffect::Chain(std::shared_ptr<const PathEffect> outer, std::shared_ptr<const PathEffect> inner) {
        return std::make_shared<ChainEffect>(std::move(outer), std::move(inner));
    }

}
