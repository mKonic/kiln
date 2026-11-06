#include "kiln/ui/VelocityTracker.h"

#include <cmath>
#include <vector>

namespace kiln::ui {

    namespace {
        constexpr int kAssumeStoppedMs = 40, kHorizonMs = 100, kMinSamples = 2;

        float Dot(const std::vector<float>& a, const std::vector<float>& b) {
            float r = 0; for (std::size_t i = 0; i < a.size(); ++i) r += a[i] * b[i]; return r;
        }

        // polyFitLeastSquares (VelocityTracker.kt), weights all 1. Returns false when degenerate.
        bool PolyFit(const float* x, const float* y, int m, int degree, float* coeff) {
            const int deg = degree >= m ? m - 1 : degree;
            const int n = deg + 1;
            std::vector<std::vector<float>> a(n, std::vector<float>(m)), q(n, std::vector<float>(m)), r(n, std::vector<float>(n));
            for (int h = 0; h < m; ++h) {
                a[0][h] = 1;
                for (int i = 1; i < n; ++i) a[i][h] = a[i - 1][h] * x[h];
            }
            for (int j = 0; j < n; ++j) {
                auto& w = q[j];
                w = a[j];
                for (int i = 0; i < j; ++i) {
                    const float dot = Dot(w, q[i]);
                    for (int h = 0; h < m; ++h) w[h] -= dot * q[i][h];
                }
                const float inv = 1.0f / std::max(std::sqrt(Dot(w, w)), 1e-6f);
                for (int h = 0; h < m; ++h) w[h] *= inv;
                for (int i = 0; i < n; ++i) r[j][i] = i < j ? 0.0f : Dot(w, a[i]);
            }
            std::vector<float> wy(y, y + m);
            for (int i = n - 1; i >= 0; --i) {
                float c = Dot(q[i], wy);
                for (int j = n - 1; j > i; --j) c -= r[i][j] * coeff[j];
                coeff[i] = c / r[i][i];
            }
            for (int i = n; i <= degree; ++i) coeff[i] = 0;
            return true;
        }

        float KineticToVelocity(float e) { return (e > 0 ? 1.0f : e < 0 ? -1.0f : 0.0f) * std::sqrt(2 * std::abs(e)); }

        float Impulse(const float* data, const float* time, int count, bool differential) {
            float work = 0;
            const int start = count - 1;
            float nextTime = time[start];
            for (int i = start; i >= 1; --i) {
                const float current = nextTime;
                nextTime = time[i - 1];
                if (current == nextTime) continue;
                const float delta = differential ? -data[i - 1] : data[i] - data[i - 1];
                const float vCurr = delta / (current - nextTime);
                const float vPrev = KineticToVelocity(work);
                work += (vCurr - vPrev) * std::abs(vCurr);
                if (i == start) work *= 0.5f;
            }
            return KineticToVelocity(work);
        }
    }

    void VelocityTracker1D::AddDataPoint(std::int64_t t, float v) {
        m_Index = (m_Index + 1) % kHistory;
        m_Samples[m_Index] = Sample{ t, v };
    }

    void VelocityTracker1D::Reset() {
        m_Samples.fill(std::nullopt);
        m_Index = 0;
    }

    float VelocityTracker1D::CalculateVelocity() const {
        float data[kHistory], time[kHistory];
        int count = 0, index = m_Index;
        if (!m_Samples[index]) return 0;
        const Sample newest = *m_Samples[index];
        Sample previous = newest;
        do {
            if (!m_Samples[index]) break;
            const Sample s = *m_Samples[index];
            const float age = float(newest.time - s.time);
            const float delta = float(std::abs(s.time - previous.time));
            previous = (m_Strategy == Strategy::Lsq2 || m_Differential) ? s : newest;
            if (age > kHorizonMs || delta > kAssumeStoppedMs) break;
            data[count] = s.value;
            time[count] = -age;
            index = (index == 0 ? kHistory : index) - 1;
            ++count;
        } while (count < kHistory);
        if (count < kMinSamples) return 0;
        if (m_Strategy == Strategy::Impulse) return Impulse(data, time, count, m_Differential) * 1000;
        float coeff[3] = { 0, 0, 0 };
        if (!PolyFit(time, data, count, 2, coeff)) return 0;
        return coeff[1] * 1000;
    }

    float VelocityTracker1D::CalculateVelocity(float maximum) const {
        const float v = CalculateVelocity();
        if (v == 0 || std::isnan(v)) return 0;
        return v > 0 ? std::min(v, maximum) : std::max(v, -maximum);
    }

    void VelocityTracker::AddPointerInputChange(const PointerInputChange& c, Offset offset) {
        if (c.ChangedToDownIgnoreConsumed()) Reset();
        if (!c.ChangedToUpIgnoreConsumed()) AddPosition(c.uptimeMillis, c.position + offset);
        if (c.ChangedToUpIgnoreConsumed() && c.uptimeMillis - m_LastMove > 40) Reset();
        m_LastMove = c.uptimeMillis;
    }

}
