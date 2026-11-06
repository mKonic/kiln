#pragma once

// VelocityTracker (ui/input/pointer/util/VelocityTracker.kt): 1D trackers with the Lsq2
// (quadratic least squares) and Impulse strategies; 2D = two Lsq2 trackers. On Android Compose
// defaults to the framework tracker (isFrameworkVelocityTrackerEnabled); kiln uses this port on
// every platform until the JNI bridge exists (PLAN M3).

#include "kiln/ui/Pointer.h"

#include <array>
#include <cstdint>
#include <optional>

namespace kiln::ui {

    class VelocityTracker1D {
    public:
        enum class Strategy { Lsq2, Impulse };
        explicit VelocityTracker1D(bool differential = false, Strategy s = Strategy::Lsq2)
            : m_Differential(differential), m_Strategy(differential ? Strategy::Impulse : s) {}
        void AddDataPoint(std::int64_t timeMillis, float value);
        float CalculateVelocity() const;                    // units per second
        float CalculateVelocity(float maximum) const;      // clamped to ±maximum, NaN -> 0
        void Reset();

    private:
        struct Sample { std::int64_t time; float value; };
        static constexpr int kHistory = 20;
        std::array<std::optional<Sample>, kHistory> m_Samples{};
        int m_Index = 0;
        bool m_Differential;
        Strategy m_Strategy;
    };

    class VelocityTracker {
    public:
        void AddPosition(std::int64_t timeMillis, Offset p) { m_X.AddDataPoint(timeMillis, p.x); m_Y.AddDataPoint(timeMillis, p.y); }
        // Lsq2VelocityTracker.addPointerInputChange: reset on down; an up more than 40 ms after
        // the last move resets (the finger stopped before lifting).
        void AddPointerInputChange(const PointerInputChange& c, Offset offset = {});
        Offset CalculateVelocity(Offset maximum = { 3.4e38f, 3.4e38f }) const {
            return { m_X.CalculateVelocity(maximum.x), m_Y.CalculateVelocity(maximum.y) };
        }
        void Reset() { m_X.Reset(); m_Y.Reset(); m_LastMove = 0; }

    private:
        VelocityTracker1D m_X, m_Y;
        std::int64_t m_LastMove = 0;
    };

}
