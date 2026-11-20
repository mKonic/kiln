#pragma once

// Oscillator (constraintlayout-core motion utils): the wave of a KeyCycle, with a period that
// can vary over the motion (its area is the phase).

#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class MonotonicCurveFit;

    class Oscillator {
    public:
        static constexpr int SIN_WAVE = 0;
        static constexpr int SQUARE_WAVE = 1;
        static constexpr int TRIANGLE_WAVE = 2;
        static constexpr int SAW_WAVE = 3;
        static constexpr int REVERSE_SAW_WAVE = 4;
        static constexpr int COS_WAVE = 5;
        static constexpr int BOUNCE = 6;
        static constexpr int CUSTOM = 7;

        std::string toString() const;
        // An empty customType is Java's null.
        void setType(int type, const std::string& customType);
        void addPoint(double position, float period);
        void normalize();
        double getValue(double time, double phase);
        double getSlope(double time, double phase, double dphase);

    private:
        double getP(double time);
        double getDP(double time);

        std::vector<float> mPeriod;
        std::vector<double> mPosition;
        std::vector<double> mArea;
        std::string mCustomType;
        std::shared_ptr<MonotonicCurveFit> mCustomCurve;
        int mType = 0;
        double mPI2 = 3.141592653589793 * 2;
        bool mNormalized = false;
    };

}
