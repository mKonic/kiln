#pragma once

// KeyCycleOscillator (constraintlayout-core motion utils): an attribute's KeyCycle waves over
// the motion: wave points (position, period, offset, phase, amplitude) interpolated by a spline,
// driving an Oscillator.

#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/Oscillator.h"

#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;
    class CustomVariable;

    class KeyCycleOscillator {
    public:
        virtual ~KeyCycleOscillator() = default;
        static std::shared_ptr<KeyCycleOscillator> makeWidgetCycle(const std::string& attribute);
        bool variesByPath() const { return mVariesBy == 1; }
        std::string toString() const;
        void setType(const std::string& type) { mType = type; }
        float get(float t) { return float(mCycleOscillator->getValues(t)); }
        float getSlope(float position) { return float(mCycleOscillator->getSlope(position)); }
        const std::shared_ptr<CurveFit>& getCurveFit() const { return mCurveFit; }
        void setPoint(int framePosition, int shape, const std::string& waveString, int variesBy, float period, float offset, float phase, float value,
                      const CustomVariable* custom);
        void setPoint(int framePosition, int shape, const std::string& waveString, int variesBy, float period, float offset, float phase, float value);
        void setup(float pathLength);
        virtual void setProperty(MotionWidget&, float) {}
        // instanceof PathRotateSet.
        virtual class PathRotateSet* asPathRotateSet() { return nullptr; }

        int mVariesBy = 0;

    protected:
        virtual void setCustom(const CustomVariable*) {}

    private:
        struct WavePoint {
            int mPosition;
            float mValue;
            float mOffset;
            float mPeriod;
            float mPhase;
        };

        class CycleOscillator {
        public:
            CycleOscillator(int waveShape, const std::string& customShape, int variesBy, int steps);
            double getValues(float time);
            double getLastPhase() const { return mSplineValueCache[1]; }
            double getSlope(float time);
            void setPoint(int index, int framePosition, float wavePeriod, float offset, float phase, float values);
            void setup(float pathLength);

        private:
            static constexpr int mOffst = 0;
            static constexpr int mPhase = 1;
            static constexpr int mValue = 2;
            Oscillator mOscillator;
            std::vector<float> mValues;
            std::vector<double> mPosition;
            std::vector<float> mPeriod;
            std::vector<float> mOffsetArr;
            std::vector<float> mPhaseArr;
            std::shared_ptr<CurveFit> mCurveFit;
            std::vector<double> mSplineValueCache;
            std::vector<double> mSplineSlopeCache;
        };

        std::shared_ptr<CurveFit> mCurveFit;
        std::shared_ptr<CycleOscillator> mCycleOscillator;
        std::string mType;
        int mWaveShape = 0;
        std::string mWaveString;
        std::vector<WavePoint> mWavePoints;
    };

    class PathRotateSet : public KeyCycleOscillator {
    public:
        explicit PathRotateSet(const std::string& str);
        void setProperty(MotionWidget& widget, float t) override;
        void setPathRotate(MotionWidget& view, float t, double dx, double dy);
        PathRotateSet* asPathRotateSet() override { return this; }

    private:
        std::string mTypeName;
        int mTypeId;
    };

}
