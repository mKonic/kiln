#include "kiln/constraintlayout/core/motion/utils/KeyCycleOscillator.h"

#include "kiln/constraintlayout/core/motion/MotionWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    namespace {
        class CoreCycle final : public KeyCycleOscillator {
        public:
            explicit CoreCycle(const std::string& str) : mTypeName(str), mTypeId(TypedValues::CycleType::getId(str)) {}
            void setProperty(MotionWidget& widget, float t) override { widget.setValue(mTypeId, get(t)); }

        private:
            std::string mTypeName;
            int mTypeId;
        };
        constexpr double kRadiansToDegrees = 57.29577951308232;   // Math.toDegrees
    }

    std::shared_ptr<KeyCycleOscillator> KeyCycleOscillator::makeWidgetCycle(const std::string& attribute) {
        if (attribute == TypedValues::AttributesType::S_PATH_ROTATE) return std::make_shared<PathRotateSet>(attribute);
        return std::make_shared<CoreCycle>(attribute);
    }

    PathRotateSet::PathRotateSet(const std::string& str) : mTypeName(str), mTypeId(TypedValues::CycleType::getId(str)) {}
    void PathRotateSet::setProperty(MotionWidget& widget, float t) { widget.setValue(mTypeId, get(t)); }
    void PathRotateSet::setPathRotate(MotionWidget& view, float t, double dx, double dy) {
        view.setRotationZ(get(t) + float(std::atan2(dy, dx) * kRadiansToDegrees));
    }

    std::string KeyCycleOscillator::toString() const {
        std::string str = mType;
        for (const WavePoint& wp : mWavePoints) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.2f", double(wp.mValue));
            str += "[" + std::to_string(wp.mPosition) + " , " + buf + "] ";
        }
        return str;
    }

    void KeyCycleOscillator::setPoint(int framePosition, int shape, const std::string& waveString, int variesBy, float period, float offset, float phase,
                                      float value, const CustomVariable* custom) {
        mWavePoints.push_back({ framePosition, value, offset, period, phase });
        if (variesBy != -1) mVariesBy = variesBy;
        mWaveShape = shape;
        setCustom(custom);
        mWaveString = waveString;
    }

    void KeyCycleOscillator::setPoint(int framePosition, int shape, const std::string& waveString, int variesBy, float period, float offset, float phase,
                                      float value) {
        mWavePoints.push_back({ framePosition, value, offset, period, phase });
        if (variesBy != -1) mVariesBy = variesBy;
        mWaveShape = shape;
        mWaveString = waveString;
    }

    void KeyCycleOscillator::setup(float pathLength) {
        const int count = int(mWavePoints.size());
        if (count == 0) return;
        std::stable_sort(mWavePoints.begin(), mWavePoints.end(), [](const WavePoint& l, const WavePoint& r) { return l.mPosition < r.mPosition; });
        std::vector<double> time(static_cast<std::size_t>(count));
        std::vector<std::vector<double>> values(static_cast<std::size_t>(count), std::vector<double>(3));
        mCycleOscillator = std::make_shared<CycleOscillator>(mWaveShape, mWaveString, mVariesBy, count);
        int i = 0;
        for (const WavePoint& wp : mWavePoints) {
            time[std::size_t(i)] = wp.mPeriod * 1E-2;
            values[std::size_t(i)][0] = wp.mValue;
            values[std::size_t(i)][1] = wp.mOffset;
            values[std::size_t(i)][2] = wp.mPhase;
            mCycleOscillator->setPoint(i, wp.mPosition, wp.mPeriod, wp.mOffset, wp.mPhase, wp.mValue);
            i++;
        }
        mCycleOscillator->setup(pathLength);
        mCurveFit = CurveFit::get(CurveFit::SPLINE, time, values);
    }

    // CycleOscillator

    KeyCycleOscillator::CycleOscillator::CycleOscillator(int waveShape, const std::string& customShape, int, int steps) {
        mOscillator.setType(waveShape, customShape);
        const std::size_t n = std::size_t(steps);
        mValues.assign(n, 0);
        mPosition.assign(n, 0);
        mPeriod.assign(n, 0);
        mOffsetArr.assign(n, 0);
        mPhaseArr.assign(n, 0);
    }

    double KeyCycleOscillator::CycleOscillator::getValues(float time) {
        if (mCurveFit != nullptr) {
            mCurveFit->getPos(time, mSplineValueCache.data());
        } else {
            mSplineValueCache[mOffst] = mOffsetArr[0];
            mSplineValueCache[mPhase] = mPhaseArr[0];
            mSplineValueCache[mValue] = mValues[0];
        }
        const double offset = mSplineValueCache[mOffst];
        const double phase = mSplineValueCache[mPhase];
        const double waveValue = mOscillator.getValue(time, phase);
        return offset + waveValue * mSplineValueCache[mValue];
    }

    double KeyCycleOscillator::CycleOscillator::getSlope(float time) {
        if (mCurveFit != nullptr) {
            mCurveFit->getSlope(time, mSplineSlopeCache.data());
            mCurveFit->getPos(time, mSplineValueCache.data());
        } else {
            mSplineSlopeCache[mOffst] = 0;
            mSplineSlopeCache[mPhase] = 0;
            mSplineSlopeCache[mValue] = 0;
        }
        const double waveValue = mOscillator.getValue(time, mSplineValueCache[mPhase]);
        const double waveSlope = mOscillator.getSlope(time, mSplineValueCache[mPhase], mSplineSlopeCache[mPhase]);
        return mSplineSlopeCache[mOffst] + waveValue * mSplineSlopeCache[mValue] + waveSlope * mSplineValueCache[mValue];
    }

    void KeyCycleOscillator::CycleOscillator::setPoint(int index, int framePosition, float wavePeriod, float offset, float phase, float values) {
        const std::size_t i = std::size_t(index);
        mPosition[i] = framePosition / 100.0;
        mPeriod[i] = wavePeriod;
        mOffsetArr[i] = offset;
        mPhaseArr[i] = phase;
        mValues[i] = values;
    }

    void KeyCycleOscillator::CycleOscillator::setup(float pathLength) {
        (void)pathLength;
        std::vector<std::vector<double>> splineValues(mPosition.size(), std::vector<double>(3));
        mSplineValueCache.assign(2 + mValues.size(), 0);
        mSplineSlopeCache.assign(2 + mValues.size(), 0);
        if (mPosition[0] > 0) mOscillator.addPoint(0, mPeriod[0]);
        const std::size_t last = mPosition.size() - 1;
        if (mPosition[last] < 1.0f) mOscillator.addPoint(1, mPeriod[last]);
        for (std::size_t i = 0; i < splineValues.size(); i++) {
            splineValues[i][mOffst] = mOffsetArr[i];
            splineValues[i][mPhase] = mPhaseArr[i];
            splineValues[i][mValue] = mValues[i];
            mOscillator.addPoint(mPosition[i], mPeriod[i]);
        }
        mOscillator.normalize();
        if (mPosition.size() > 1) mCurveFit = CurveFit::get(CurveFit::SPLINE, mPosition, splineValues);
        else mCurveFit = nullptr;
    }

}
