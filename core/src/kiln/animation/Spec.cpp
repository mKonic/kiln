#include "kiln/animation/Spec.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiln::anim {

    // --- SpringSimulation.updateValuesSeconds ---------------------------------------------------

    SpringMotion SpringStep(float dampingRatio, float stiffness, float last, float lastVelocity, float finalPosition, double dt) {
        const double naturalFreq = std::sqrt(double(stiffness));
        const double adjusted = last - finalPosition;
        const double dr2 = double(dampingRatio) * dampingRatio;
        const double r = -dampingRatio * naturalFreq;
        double displacement, velocity;
        if (dampingRatio > 1) {
            const double s = naturalFreq * std::sqrt(dr2 - 1);
            const double gp = r + s, gm = r - s;
            const double cb = (gm * adjusted - lastVelocity) / (gm - gp);
            const double ca = adjusted - cb;
            displacement = ca * std::exp(gm * dt) + cb * std::exp(gp * dt);
            velocity = ca * gm * std::exp(gm * dt) + cb * gp * std::exp(gp * dt);
        } else if (dampingRatio == 1.0f) {
            const double ca = adjusted, cb = lastVelocity + naturalFreq * adjusted;
            const double n = -naturalFreq * dt;
            displacement = (ca + cb * dt) * std::exp(n);
            velocity = (ca + cb * dt) * std::exp(n) * (-naturalFreq) + cb * std::exp(n);
        } else {
            const double df = naturalFreq * std::sqrt(1 - dr2);
            const double cosC = adjusted, sinC = (1 / df) * (-r * adjusted + lastVelocity);
            const double dfdt = df * dt;
            displacement = std::exp(r * dt) * (cosC * std::cos(dfdt) + sinC * std::sin(dfdt));
            velocity = displacement * r + std::exp(r * dt) * (-df * cosC * std::sin(dfdt) + df * sinC * std::cos(dfdt));
        }
        return { float(displacement + finalPosition), float(velocity) };
    }

    // --- SpringEstimation.kt --------------------------------------------------------------------

    namespace {
        bool NotFinite(double v) { return !std::isfinite(v); }
        double Newton(double x, auto fn, auto fnPrime) { return x - fn(x) / fnPrime(x); }

        double UnderDamped(double r, double imag, double p0, double v0, double delta) {
            const double c1 = p0, c2 = (v0 - r * c1) / imag;
            return std::log(delta / std::sqrt(c1 * c1 + c2 * c2)) / r;
        }
        double CriticallyDamped(double r, double p0, double v0, double delta) {
            const double c1 = p0, c2 = v0 - r * c1;
            const double t1 = std::log(std::abs(delta / c1)) / r;
            double guess = std::log(std::abs(delta / c2)), t = guess;
            for (int i = 0; i <= 5; ++i) t = guess - std::log(std::abs(t / r));
            const double t2 = t / r;
            double tCurr = NotFinite(t1) ? t2 : NotFinite(t2) ? t1 : std::max(t1, t2);
            const double tInf = -(r * c1 + c2) / (r * c2);
            const double xInf = c1 * std::exp(r * tInf) + c2 * tInf * std::exp(r * tInf);
            double signedDelta;
            if (std::isnan(tInf) || tInf <= 0.0) signedDelta = -delta;
            else if (tInf > 0.0 && -xInf < delta) { if (c2 < 0 && c1 > 0) tCurr = 0.0; signedDelta = -delta; }
            else { tCurr = -(2.0 / r) - (c1 / c2); signedDelta = delta; }
            double tDelta = std::numeric_limits<double>::max();
            for (int it = 0; tDelta > 0.001 && it < 100; ++it) {
                const double tLast = tCurr;
                tCurr = Newton(tCurr, [&](double x) { return (c1 + c2 * x) * std::exp(r * x) + signedDelta; },
                               [&](double x) { return (c2 * (r * x + 1) + c1 * r) * std::exp(r * x); });
                tDelta = std::abs(tLast - tCurr);
            }
            return tCurr;
        }
        double OverDamped(double r1, double r2, double p0, double v0, double delta) {
            const double c2 = (r1 * p0 - v0) / (r1 - r2), c1 = p0 - c2;
            const double t1 = std::log(std::abs(delta / c1)) / r1, t2 = std::log(std::abs(delta / c2)) / r2;
            double tCurr = NotFinite(t1) ? t2 : NotFinite(t2) ? t1 : std::max(t1, t2);
            const double tInf = std::log((c1 * r1) / (-c2 * r2)) / (r2 - r1);
            const double xInf = c1 * std::exp(r1 * tInf) + c2 * std::exp(r2 * tInf);
            double signedDelta;
            if (std::isnan(tInf) || tInf <= 0.0) signedDelta = -delta;
            else if (tInf > 0.0 && -xInf < delta) { if (c2 > 0.0 && c1 < 0.0) tCurr = 0.0; signedDelta = -delta; }
            else { tCurr = std::log(-(c2 * r2 * r2) / (c1 * r1 * r1)) / (r1 - r2); signedDelta = delta; }
            if (std::abs(c1 * r1 * std::exp(r1 * tCurr) + c2 * r2 * std::exp(r2 * tCurr)) < 0.0001) return tCurr;
            double tDelta = std::numeric_limits<double>::max();
            for (int it = 0; tDelta > 0.001 && it < 100; ++it) {
                const double tLast = tCurr;
                tCurr = Newton(tCurr, [&](double x) { return c1 * std::exp(r1 * x) + c2 * std::exp(r2 * x) + signedDelta; },
                               [&](double x) { return c1 * r1 * std::exp(r1 * x) + c2 * r2 * std::exp(r2 * x); });
                tDelta = std::abs(tLast - tCurr);
            }
            return tCurr;
        }
    }

    std::int64_t EstimateSpringDurationNanos(float stiffness, float dampingRatio, float initialVelocity,
                                             float initialDisplacement, float delta) {
        constexpr std::int64_t kMaxLongMillis = std::numeric_limits<std::int64_t>::max() / 1000000;
        if (dampingRatio == 0) return kMaxLongMillis * 1000000;
        const double k = stiffness, dr = dampingRatio;
        const double damping = 2.0 * dr * std::sqrt(k);
        const double partial = damping * damping - 4.0 * k;
        const double real = partial < 0 ? 0.0 : std::sqrt(partial), imag = partial < 0 ? std::sqrt(std::abs(partial)) : 0.0;
        const double r1 = (-damping + real) * 0.5, i1 = imag * 0.5, r2 = (-damping - real) * 0.5;
        const double p = initialDisplacement, v = initialVelocity;
        double seconds;
        if (p == 0.0 && v == 0.0) seconds = 0.0;
        else {
            const double v0 = p < 0 ? -v : v, p0 = std::abs(p);
            if (dr > 1.0) seconds = OverDamped(r1, r2, p0, v0, delta);
            else if (dr < 1.0) seconds = UnderDamped(r1, i1, p0, v0, delta);
            else seconds = CriticallyDamped(r1, p0, v0, delta);
        }
        return std::int64_t(seconds * 1e9);
    }

    namespace {

        class SpringSpec final : public AnimationSpec {
        public:
            SpringSpec(float d, float k, float t) : m_D(d), m_K(k), m_T(t) {}
            Vec ValueAt(std::int64_t n, const Vec& f, const Vec& to, const Vec& v0, int dims) const override {
                Vec out{};
                for (int i = 0; i < dims; ++i) out[i] = SpringStep(m_D, m_K, f[i], v0[i], to[i], double(n) / 1e9).value;
                return out;
            }
            Vec VelocityAt(std::int64_t n, const Vec& f, const Vec& to, const Vec& v0, int dims) const override {
                Vec out{};
                for (int i = 0; i < dims; ++i) out[i] = SpringStep(m_D, m_K, f[i], v0[i], to[i], double(n) / 1e9).velocity;
                return out;
            }
            std::int64_t DurationNanos(const Vec& f, const Vec& to, const Vec& v0, int dims) const override {
                std::int64_t longest = 0;
                for (int i = 0; i < dims; ++i)
                    longest = std::max(longest, EstimateSpringDurationNanos(m_K, m_D, v0[i] / m_T, (f[i] - to[i]) / m_T, 1.0f));
                return longest;
            }
        private:
            float m_D, m_K, m_T;
        };

        class TweenSpec final : public AnimationSpec {
        public:
            TweenSpec(int d, int delay, Easing e) : m_Dur(std::int64_t(d) * 1000000), m_Delay(std::int64_t(delay) * 1000000), m_E(std::move(e)) {}
            Vec ValueAt(std::int64_t n, const Vec& f, const Vec& to, const Vec&, int dims) const override {
                const std::int64_t c = std::clamp<std::int64_t>(n - m_Delay, 0, m_Dur);
                const float raw = m_Dur == 0 ? 1.0f : float(c) / float(m_Dur);
                const float frac = m_E.Transform(raw);
                Vec out{};
                for (int i = 0; i < dims; ++i) out[i] = f[i] + (to[i] - f[i]) * frac;
                return out;
            }
            Vec VelocityAt(std::int64_t n, const Vec& f, const Vec& to, const Vec& v0, int dims) const override {
                const std::int64_t c = std::clamp<std::int64_t>(n - m_Delay, 0, m_Dur);
                if (c == 0) return v0;
                const Vec a = ValueAt(c - 1000000 + m_Delay, f, to, v0, dims), b = ValueAt(c + m_Delay, f, to, v0, dims);
                Vec out{};
                for (int i = 0; i < dims; ++i) out[i] = (b[i] - a[i]) * 1000.0f;
                return out;
            }
            std::int64_t DurationNanos(const Vec&, const Vec&, const Vec&, int) const override { return m_Delay + m_Dur; }
        private:
            std::int64_t m_Dur, m_Delay;
            Easing m_E;
        };

        class SnapSpec final : public AnimationSpec {
        public:
            explicit SnapSpec(int delay) : m_Delay(std::int64_t(delay) * 1000000) {}
            Vec ValueAt(std::int64_t n, const Vec& f, const Vec& to, const Vec&, int) const override { return n < m_Delay ? f : to; }
            Vec VelocityAt(std::int64_t, const Vec&, const Vec&, const Vec& v0, int) const override { return v0; }
            std::int64_t DurationNanos(const Vec&, const Vec&, const Vec&, int) const override { return m_Delay; }
        private:
            std::int64_t m_Delay;
        };

        class InfiniteSpec final : public AnimationSpec {
        public:
            InfiniteSpec(SpecPtr s, RepeatMode m, std::int64_t off) : m_S(std::move(s)), m_M(m), m_Off(off) {}
            bool Infinite() const override { return true; }
            // VectorizedInfiniteRepeatableSpec: play time (plus the start offset, <= 0 clamps to 0)
            // folds into one iteration; Reverse runs odd iterations backwards in time.
            std::int64_t Fold(std::int64_t n, const Vec& f, const Vec& to, const Vec& v0, int dims) const {
                const std::int64_t d = std::max<std::int64_t>(1, m_S->DurationNanos(f, to, v0, dims));
                const std::int64_t t = n + m_Off;
                if (t <= 0) return 0;
                const std::int64_t repeats = t / d;
                if (m_M == RepeatMode::Restart || repeats % 2 == 0) return t - repeats * d;
                return (repeats + 1) * d - t;
            }
            Vec ValueAt(std::int64_t n, const Vec& f, const Vec& to, const Vec& v0, int dims) const override {
                return m_S->ValueAt(Fold(n, f, to, v0, dims), f, to, v0, dims);
            }
            Vec VelocityAt(std::int64_t n, const Vec& f, const Vec& to, const Vec& v0, int dims) const override {
                return m_S->VelocityAt(Fold(n, f, to, v0, dims), f, to, v0, dims);
            }
            std::int64_t DurationNanos(const Vec&, const Vec&, const Vec&, int) const override { return std::numeric_limits<std::int64_t>::max(); }
        private:
            SpecPtr m_S;
            RepeatMode m_M;
            std::int64_t m_Off;
        };
    }

    SpecPtr Spring(float d, float k, float t) { return std::make_shared<SpringSpec>(d, k, t); }
    namespace {
        // VectorizedKeyframesSpec, linear path.
        class KeyframesSpec final : public AnimationSpec {
        public:
            KeyframesSpec(int d, std::vector<Keyframe> k, int delay) : m_DurMs(d), m_DelayMs(delay), m_Frames(std::move(k)) {
                std::stable_sort(m_Frames.begin(), m_Frames.end(), [](const Keyframe& a, const Keyframe& b) { return a.timeMillis < b.timeMillis; });
            }
            Vec ValueAt(std::int64_t n, const Vec& from, const Vec& to, const Vec&, int dims) const override {
                const std::int64_t ms = std::clamp<std::int64_t>(n / 1000000 - m_DelayMs, 0, m_DurMs);
                const float t = std::clamp(float(n - std::int64_t(m_DelayMs) * 1000000) / 1e6f, 0.0f, float(m_DurMs));
                for (const Keyframe& k : m_Frames)
                    if (k.timeMillis == ms && float(ms) == t) return k.value;   // exactly on a keyframe
                if (t >= float(m_DurMs)) return to;
                if (t <= 0) return from;
                // Segment [start, end] around t; the start keyframe's easing shapes it.
                int t0 = 0, t1 = m_DurMs;
                Vec v0 = from, v1 = to;
                const Easing* easing = nullptr;
                static const Easing linear = Linear();
                for (const Keyframe& k : m_Frames) {
                    if (float(k.timeMillis) <= t) { t0 = k.timeMillis; v0 = k.value; easing = &k.easing; }
                    else { t1 = k.timeMillis; v1 = k.value; break; }
                }
                if (!easing) easing = &linear;
                const float f = t1 > t0 ? easing->Transform((t - float(t0)) / float(t1 - t0)) : 1.0f;
                Vec out{};
                for (int i = 0; i < dims; ++i) out[i] = v0[i] + (v1[i] - v0[i]) * f;
                return out;
            }
            Vec VelocityAt(std::int64_t, const Vec&, const Vec&, const Vec&, int) const override { return {}; }
            std::int64_t DurationNanos(const Vec&, const Vec&, const Vec&, int) const override {
                return (std::int64_t(m_DelayMs) + m_DurMs) * 1000000;
            }
        private:
            int m_DurMs, m_DelayMs;
            std::vector<Keyframe> m_Frames;
        };

        // VectorizedRepeatableSpec.
        class RepeatableSpec final : public AnimationSpec {
        public:
            RepeatableSpec(int iterations, SpecPtr s, RepeatMode m, std::int64_t off)
                : m_Iterations(std::max(1, iterations)), m_Spec(std::move(s)), m_Mode(m), m_Offset(off) {}
            std::int64_t Iteration(const Vec& f, const Vec& t, const Vec& v, int d) const { return m_Spec->DurationNanos(f, t, v, d); }
            std::int64_t RepetitionPlay(std::int64_t n, std::int64_t dur) const {
                if (dur <= 0) return 0;
                const std::int64_t played = n + m_Offset;
                const std::int64_t repeats = std::min<std::int64_t>(played / dur, m_Iterations - 1);
                if (m_Mode == RepeatMode::Restart || repeats % 2 == 0) return played - repeats * dur;
                return (repeats + 1) * dur - played;
            }
            Vec ValueAt(std::int64_t n, const Vec& f, const Vec& t, const Vec& v0, int d) const override {
                return m_Spec->ValueAt(RepetitionPlay(n, Iteration(f, t, v0, d)), f, t, v0, d);
            }
            Vec VelocityAt(std::int64_t n, const Vec& f, const Vec& t, const Vec& v0, int d) const override {
                return m_Spec->VelocityAt(RepetitionPlay(n, Iteration(f, t, v0, d)), f, t, v0, d);
            }
            std::int64_t DurationNanos(const Vec& f, const Vec& t, const Vec& v0, int d) const override {
                return m_Iterations * Iteration(f, t, v0, d) - m_Offset;
            }
        private:
            int m_Iterations;
            SpecPtr m_Spec;
            RepeatMode m_Mode;
            std::int64_t m_Offset;
        };
    }

    SpecPtr Keyframes(int d, std::vector<Keyframe> k, int delay) { return std::make_shared<KeyframesSpec>(d, std::move(k), delay); }
    SpecPtr Repeatable(int it, SpecPtr s, RepeatMode m, std::int64_t off) { return std::make_shared<RepeatableSpec>(it, std::move(s), m, off); }

    SpecPtr Tween(int d, int delay, Easing e) { return std::make_shared<TweenSpec>(d, delay, std::move(e)); }
    SpecPtr Snap(int delay) { return std::make_shared<SnapSpec>(delay); }
    SpecPtr InfiniteRepeatable(SpecPtr s, RepeatMode m, std::int64_t off) { return std::make_shared<InfiniteSpec>(std::move(s), m, off); }

}
