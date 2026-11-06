#include "kiln/animation/Spline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiln::anim::detail {

    namespace {
        constexpr int kStartVertical = 1, kStartHorizontal = 2, kStartLinear = 3, kDownArc = 4, kUpArc = 5;
        // ArcSpline's public mode codes.
        constexpr int kArcStartLinear = 0, kArcStartVertical = 1, kArcStartHorizontal = 2, kArcStartFlip = 3,
                      kArcBelow = 4, kArcAbove = 5;
        constexpr int kLutSize = 101;
        constexpr float kEpsilon = 0.001f;
        constexpr float kHalfPi = 3.14159265358979323846f * 0.5f;

        // Arrays.binarySearch: the index, or -(insertion point) - 1.
        int BinarySearch(const float* a, int n, float key) {
            int lo = 0, hi = n - 1;
            while (lo <= hi) {
                const int mid = (lo + hi) >> 1;
                if (a[mid] < key) lo = mid + 1;
                else if (a[mid] > key) hi = mid - 1;
                else return mid;
            }
            return -(lo + 1);
        }

        float HermiteInterpolate(float h, float x, float y1, float y2, float t1, float t2) {
            const float x2 = x * x, x3 = x2 * x;
            return h * t1 * (x - 2 * x2 + x3) + h * t2 * (x3 - x2) + y1 - (3 * x2 - 2 * x3) * (y1 - y2);
        }
        float HermiteDifferential(float h, float x, float y1, float y2, float t1, float t2) {
            const float x2 = x * x;
            return h * (t1 - 2 * x * (2 * t1 + t2) + 3 * (t1 + t2) * x2) - 6 * (x - x2) * (y1 - y2);
        }
    }

    // --- ArcSpline -------------------------------------------------------------------------

    ArcSpline::Arc::Arc(int mode, float t1, float t2, float ax1, float ay1, float ax2, float ay2)
        : time1(t1), time2(t2), x1(ax1), y1(ay1), x2(ax2), y2(ay2) {
        const float dx = x2 - x1, dy = y2 - y1;
        bool isVertical = false;
        switch (mode) {
            case kStartVertical: isVertical = true; break;
            case kUpArc: isVertical = dy < 0; break;
            case kDownArc: isVertical = dy > 0; break;
            default: break;
        }
        vertical = isVertical ? -1.0f : 1.0f;
        oneOverDeltaTime = 1 / (time2 - time1);
        bool linear = mode == kStartLinear;
        if (linear || std::abs(dx) < kEpsilon || std::abs(dy) < kEpsilon) {
            linear = true;
            arcDistance = std::hypot(dy, dx);
            arcVelocity = arcDistance * oneOverDeltaTime;
            ellipseCenterX = dx * oneOverDeltaTime;   // the slope, cached in the unused centre
            ellipseCenterY = dy * oneOverDeltaTime;
            ellipseA = std::numeric_limits<float>::quiet_NaN();
            ellipseB = std::numeric_limits<float>::quiet_NaN();
        } else {
            ellipseA = dx * vertical;
            ellipseB = dy * -vertical;
            ellipseCenterX = isVertical ? x2 : x1;
            ellipseCenterY = isVertical ? y1 : y2;
            BuildTable();
            arcVelocity = arcDistance * oneOverDeltaTime;
        }
        isLinear = linear;
    }

    float ArcSpline::Arc::Lookup(float v) const {
        if (v <= 0) return 0;
        if (v >= 1) return 1;
        const float pos = v * (kLutSize - 1);
        const int iv = int(pos);
        const float off = pos - float(iv);
        return lut[std::size_t(iv)] + off * (lut[std::size_t(iv) + 1] - lut[std::size_t(iv)]);
    }

    float ArcSpline::Arc::Angle(float time) const {
        const float percent = (vertical == -1.0f ? time2 - time : time - time1) * oneOverDeltaTime;
        return kHalfPi * Lookup(percent);
    }

    void ArcSpline::Arc::Velocity(float s, float c, float& dx, float& dy) const {
        const float vx = ellipseA * c, vy = -ellipseB * s;
        const float norm = arcVelocity / std::hypot(vx, vy);
        dx = vx * vertical * norm;
        dy = vy * vertical * norm;
    }

    void ArcSpline::Arc::BuildTable() {
        // Arc length by angle, then the angle for each fraction of the length.
        std::array<float, 91> percent{};
        const float a = x2 - x1, b = y1 - y2;
        float lx = 0, ly = b, dist = 0;
        constexpr int lastIndex = 90;
        for (int i = 1; i <= lastIndex; ++i) {
            const float angle = float(90.0 * i / lastIndex * (3.14159265358979323846 / 180.0));
            const float px = a * std::sin(angle), py = b * std::cos(angle);
            dist += std::hypot(px - lx, py - ly);
            percent[std::size_t(i)] = dist;
            lx = px;
            ly = py;
        }
        arcDistance = dist;
        for (int i = 1; i <= lastIndex; ++i) percent[std::size_t(i)] /= dist;
        const float lutLast = float(kLutSize - 1);
        for (int i = 0; i < kLutSize; ++i) {
            const float pos = float(i) / lutLast;
            const int index = BinarySearch(percent.data(), lastIndex + 1, pos);
            if (index >= 0) lut[std::size_t(i)] = float(index) / float(lastIndex);
            else if (index == -1) lut[std::size_t(i)] = 0;
            else {
                const int p1 = -index - 2, p2 = -index - 1;
                lut[std::size_t(i)] = (float(p1) + (pos - percent[std::size_t(p1)]) / (percent[std::size_t(p2)] - percent[std::size_t(p1)])) / float(lastIndex);
            }
        }
    }

    ArcSpline::ArcSpline(const std::vector<int>& modes, const std::vector<float>& times, const std::vector<std::vector<float>>& values) {
        int mode = kStartVertical, last = kStartVertical;
        for (std::size_t i = 0; i + 1 < times.size(); ++i) {
            switch (modes[i]) {
                case kArcStartVertical: mode = kStartVertical; last = mode; break;
                case kArcStartHorizontal: mode = kStartHorizontal; last = mode; break;
                case kArcStartFlip: mode = last == kStartVertical ? kStartHorizontal : kStartVertical; last = mode; break;
                case kArcStartLinear: mode = kStartLinear; break;
                case kArcAbove: mode = kUpArc; break;
                case kArcBelow: mode = kDownArc; break;
                default: break;
            }
            const auto& a = values[i];
            const auto& b = values[i + 1];
            std::vector<Arc> row;
            for (std::size_t k = 0; k + 1 < a.size(); k += 2)
                row.emplace_back(mode, times[i], times[i + 1], a[k], a[k + 1], b[k], b[k + 1]);
            m_Arcs.push_back(std::move(row));
        }
    }

    void ArcSpline::Position(float t, std::vector<float>& v) const {
        const std::size_t lastIndex = m_Arcs.size() - 1;
        const float start = m_Arcs[0][0].time1, end = m_Arcs[lastIndex][0].time2;
        if (t < start || t > end) {
            // Extrapolate along the end tangent.
            const std::size_t p = t > end ? lastIndex : 0;
            const float t0 = t > end ? end : start;
            const float dt = t - t0;
            for (std::size_t i = 0, j = 0; i + 1 < v.size(); i += 2, ++j) {
                const Arc& arc = m_Arcs[p][j];
                if (arc.isLinear) {
                    v[i] = arc.LinearX(t0) + dt * arc.ellipseCenterX;
                    v[i + 1] = arc.LinearY(t0) + dt * arc.ellipseCenterY;
                } else {
                    const float angle = arc.Angle(t0), s = std::sin(angle), c = std::cos(angle);
                    float dx, dy;
                    arc.Velocity(s, c, dx, dy);
                    v[i] = arc.X(s) + dt * dx;
                    v[i + 1] = arc.Y(c) + dt * dy;
                }
            }
            return;
        }
        for (const auto& row : m_Arcs) {
            bool populated = false;
            for (std::size_t j = 0, k = 0; j + 1 < v.size(); j += 2, ++k) {
                const Arc& arc = row[k];
                if (t > arc.time2) continue;
                if (arc.isLinear) {
                    v[j] = arc.LinearX(t);
                    v[j + 1] = arc.LinearY(t);
                } else {
                    const float angle = arc.Angle(t), s = std::sin(angle), c = std::cos(angle);
                    v[j] = arc.X(s);
                    v[j + 1] = arc.Y(c);
                }
                populated = true;
            }
            if (populated) return;
        }
    }

    void ArcSpline::Slope(float time, std::vector<float>& v) const {
        const float t = std::clamp(time, m_Arcs[0][0].time1, m_Arcs.back()[0].time2);
        for (const auto& row : m_Arcs) {
            bool populated = false;
            for (std::size_t j = 0, k = 0; j + 1 < v.size(); j += 2, ++k) {
                const Arc& arc = row[k];
                if (t > arc.time2) continue;
                if (arc.isLinear) {
                    v[j] = arc.ellipseCenterX;
                    v[j + 1] = arc.ellipseCenterY;
                } else {
                    const float angle = arc.Angle(t);
                    arc.Velocity(std::sin(angle), std::cos(angle), v[j], v[j + 1]);
                }
                populated = true;
            }
            if (populated) return;
        }
    }

    // --- MonoSpline ------------------------------------------------------------------------

    MonoSpline::MonoSpline(std::vector<float> time, std::vector<std::vector<float>> y, float periodicBias) {
        const std::size_t n = time.size(), dim = y[0].size();
        std::vector<std::vector<float>> slope(n - 1, std::vector<float>(dim)), tangent(n, std::vector<float>(dim));
        for (std::size_t j = 0; j < dim; ++j) {
            for (std::size_t i = 0; i + 1 < n; ++i) {
                const float dt = time[i + 1] - time[i];
                slope[i][j] = (y[i + 1][j] - y[i][j]) / dt;
                tangent[i][j] = i == 0 ? slope[i][j] : (slope[i - 1][j] + slope[i][j]) * 0.5f;
            }
            tangent[n - 1][j] = slope[n - 2][j];
        }
        if (!std::isnan(periodicBias)) {
            for (std::size_t j = 0; j < dim; ++j) {
                const float adjusted = slope[n - 2][j] * (1 - periodicBias) + slope[0][j] * periodicBias;
                slope[0][j] = adjusted;
                slope[n - 2][j] = adjusted;
                tangent[n - 1][j] = adjusted;
                tangent[0][j] = adjusted;
            }
        }
        // Keep each segment monotone (Fritsch-Carlson).
        for (std::size_t i = 0; i + 1 < n; ++i)
            for (std::size_t j = 0; j < dim; ++j) {
                if (slope[i][j] == 0) { tangent[i][j] = 0; tangent[i + 1][j] = 0; continue; }
                const float a = tangent[i][j] / slope[i][j], b = tangent[i + 1][j] / slope[i][j];
                const float h = std::hypot(a, b);
                if (h > 9.0f) {
                    const float t = 3.0f / h;
                    tangent[i][j] = t * a * slope[i][j];
                    tangent[i + 1][j] = t * b * slope[i][j];
                }
            }
        m_Times = std::move(time);
        m_Values = std::move(y);
        m_Tangents = std::move(tangent);
    }

    float MonoSpline::SlopeAt(float time, std::size_t j) const {
        const std::size_t n = m_Times.size();
        const float t = std::clamp(time, m_Times[0], m_Times[n - 1]);
        for (std::size_t i = 0; i + 1 < n; ++i)
            if (t <= m_Times[i + 1]) {
                const float h = m_Times[i + 1] - m_Times[i], x = (t - m_Times[i]) / h;
                return HermiteDifferential(h, x, m_Values[i][j], m_Values[i + 1][j], m_Tangents[i][j], m_Tangents[i + 1][j]) / h;
            }
        return 0;
    }

    void MonoSpline::Position(float time, std::vector<float>& v, std::size_t index) const {
        const std::size_t n = m_Times.size(), dim = m_Values[0].size();
        const long k = time <= m_Times[0] ? 0 : (time >= m_Times[n - 1] ? long(n - 1) : -1);
        if (k != -1) {
            for (std::size_t j = 0; j < dim; ++j) v[j] = m_Values[std::size_t(k)][j] + (time - m_Times[std::size_t(k)]) * SlopeAt(m_Times[std::size_t(k)], j);
            return;
        }
        for (std::size_t i = index; i + 1 < n; ++i) {
            if (time == m_Times[i]) { for (std::size_t j = 0; j < dim; ++j) v[j] = m_Values[i][j]; return; }
            if (time < m_Times[i + 1]) {
                const float h = m_Times[i + 1] - m_Times[i], x = (time - m_Times[i]) / h;
                for (std::size_t j = 0; j < dim; ++j)
                    v[j] = HermiteInterpolate(h, x, m_Values[i][j], m_Values[i + 1][j], m_Tangents[i][j], m_Tangents[i + 1][j]);
                return;
            }
        }
    }

    void MonoSpline::Slope(float time, std::vector<float>& v, std::size_t index) const {
        const std::size_t n = m_Times.size(), dim = m_Values[0].size();
        const long k = time <= m_Times[0] ? 0 : (time >= m_Times[n - 1] ? long(n - 1) : -1);
        if (k != -1) {
            for (std::size_t j = 0; j < dim; ++j) v[j] = m_Tangents[std::size_t(k)][j];
            return;
        }
        for (std::size_t i = index; i + 1 < n; ++i)
            if (time <= m_Times[i + 1]) {
                const float h = m_Times[i + 1] - m_Times[i], x = (time - m_Times[i]) / h;
                for (std::size_t j = 0; j < dim; ++j)
                    v[j] = HermiteDifferential(h, x, m_Values[i][j], m_Values[i + 1][j], m_Tangents[i][j], m_Tangents[i + 1][j]) / h;
                return;
            }
    }

}
