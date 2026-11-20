#pragma once

// TimeCycleSplineSet (constraintlayout-core motion utils): an attribute's KeyTimeCycle: a wave
// that runs in time (not progress), its amplitude, period and offset interpolated over the
// motion.

#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"
#include "kiln/constraintlayout/core/motion/utils/KeyFrameArray.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;

    class TimeCycleSplineSet {
    public:
        virtual ~TimeCycleSplineSet() = default;
        std::string toString() const;
        void setType(const std::string& type) { mType = type; }
        const std::shared_ptr<CurveFit>& getCurveFit() const { return mCurveFit; }
        virtual void setPoint(int position, float value, float period, int shape, float offset);
        virtual void setup(int curveType);

    protected:
        float calcWave(float period) const;
        void setStartTime(int64_t currentTime) { mLastTime = currentTime; }

        static constexpr int CURVE_VALUE = 0;
        static constexpr int CURVE_PERIOD = 1;
        static constexpr int CURVE_OFFSET = 2;
        static constexpr float sVal2PI = float(2 * 3.141592653589793);

        std::shared_ptr<CurveFit> mCurveFit;
        int mWaveShape = 0;
        // Ten slots, as upstream allocates (setup scans all of them).
        std::vector<int> mTimePoints = std::vector<int>(10);
        std::vector<std::array<float, 3>> mValues = std::vector<std::array<float, 3>>(10);
        int mCount = 0;
        std::string mType;
        std::array<float, 3> mCache{};
        bool mContinue = false;
        int64_t mLastTime = 0;
        float mLastCycle = NAN;
    };

    class TimeCycleCustomSet : public TimeCycleSplineSet {
    public:
        TimeCycleCustomSet(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomArray> attrList);
        void setup(int curveType) override;
        void setPoint(int position, float value, float period, int shape, float offset) override;
        void setPoint(int position, const CustomAttribute& value, float period, int shape, float offset);
        bool setProperty(MotionWidget& view, float t, int64_t time, KeyCache& cache);

    private:
        std::string mAttributeName;
        std::shared_ptr<KeyFrameArray::CustomArray> mConstraintAttributeList;
        KeyFrameArray::FloatArray mWaveProperties;
        std::vector<float> mTempValues;
        std::vector<float> mCustomCache;
    };

    class TimeCycleCustomVarSet : public TimeCycleSplineSet {
    public:
        TimeCycleCustomVarSet(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomVar> attrList);
        void setup(int curveType) override;
        void setPoint(int position, float value, float period, int shape, float offset) override;
        void setPoint(int position, const CustomVariable& value, float period, int shape, float offset);
        bool setProperty(MotionWidget& view, float t, int64_t time, KeyCache& cache);

    private:
        std::string mAttributeName;
        std::shared_ptr<KeyFrameArray::CustomVar> mConstraintAttributeList;
        KeyFrameArray::FloatArray mWaveProperties;
        std::vector<float> mTempValues;
        std::vector<float> mCustomCache;
    };

}
