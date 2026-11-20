#include "kiln/constraintlayout/core/motion/utils/TimeCycleSplineSet.h"

#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/utils/Oscillator.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace kiln::constraintlayout::core::motion {

    namespace {
        float signum(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : v; }

        void swap(std::vector<int>& array, std::vector<std::array<float, 3>>& value, int a, int b) {
            std::swap(array[std::size_t(a)], array[std::size_t(b)]);
            std::swap(value[std::size_t(a)], value[std::size_t(b)]);
        }
        int partition(std::vector<int>& array, std::vector<std::array<float, 3>>& value, int low, int hi) {
            const int pivot = array[std::size_t(hi)];
            int i = low;
            for (int j = low; j < hi; j++) {
                if (array[std::size_t(j)] <= pivot) {
                    swap(array, value, i, j);
                    i++;
                }
            }
            swap(array, value, i, hi);
            return i;
        }
        void doubleQuickSort(std::vector<int>& key, std::vector<std::array<float, 3>>& value, int low, int hi) {
            std::vector<int> stack(key.size() + 10);
            int count = 0;
            stack[std::size_t(count++)] = hi;
            stack[std::size_t(count++)] = low;
            while (count > 0) {
                low = stack[std::size_t(--count)];
                hi = stack[std::size_t(--count)];
                if (low < hi) {
                    const int p = partition(key, value, low, hi);
                    if (std::size_t(count) + 4 > stack.size()) stack.resize(stack.size() * 2);
                    stack[std::size_t(count++)] = p - 1;
                    stack[std::size_t(count++)] = low;
                    stack[std::size_t(count++)] = hi;
                    stack[std::size_t(count++)] = p + 1;
                }
            }
        }
        std::string secondField(const std::string& attribute) {
            const std::size_t comma = attribute.find(',');
            if (comma == std::string::npos) throw std::out_of_range("Index 1 out of bounds for length 1");
            const std::size_t next = attribute.find(',', comma + 1);
            return attribute.substr(comma + 1, next == std::string::npos ? std::string::npos : next - comma - 1);
        }

        // The shared setup of the custom sets: the custom values plus period and offset.
        template<typename List>
        std::shared_ptr<CurveFit> customSetup(int curveType, List& list, KeyFrameArray::FloatArray& waveProperties, std::vector<float>& tempValues,
                                              std::vector<float>& customCache) {
            const int size = list.size();
            const int dimensionality = list.valueAt(0).numberOfInterpolatedValues();
            std::vector<double> time(static_cast<std::size_t>(size));
            tempValues.assign(std::size_t(dimensionality) + 2, 0);
            customCache.assign(std::size_t(dimensionality), 0);
            std::vector<std::vector<double>> values(static_cast<std::size_t>(size), std::vector<double>(std::size_t(dimensionality) + 2));
            for (int i = 0; i < size; i++) {
                const int key = list.keyAt(i);
                const auto& ca = list.valueAt(i);
                const std::vector<float>& waveProp = waveProperties.valueAt(i);
                time[std::size_t(i)] = key * 1E-2;
                ca.getValuesToInterpolate(tempValues.data());
                for (std::size_t k = 0; k < tempValues.size(); k++) values[std::size_t(i)][k] = tempValues[k];
                values[std::size_t(i)][std::size_t(dimensionality)] = waveProp[0];
                values[std::size_t(i)][std::size_t(dimensionality) + 1] = waveProp[1];
            }
            return CurveFit::get(curveType, time, values);
        }
    }

    std::string TimeCycleSplineSet::toString() const {
        std::string str = mType;
        for (int i = 0; i < mCount; i++) {
            char buf[64];
            const auto& v = mValues[std::size_t(i)];
            std::snprintf(buf, sizeof buf, "[%.2f, %.2f, %.2f]", double(v[0]), double(v[1]), double(v[2]));
            str += "[" + std::to_string(mTimePoints[std::size_t(i)]) + " , " + buf + "] ";
        }
        return str;
    }

    float TimeCycleSplineSet::calcWave(float period) const {
        const float p = period;
        switch (mWaveShape) {
            default:
            case Oscillator::SIN_WAVE:
                return float(std::sin(p * sVal2PI));
            case Oscillator::SQUARE_WAVE:
                return signum(p * sVal2PI);
            case Oscillator::TRIANGLE_WAVE:
                return 1 - std::abs(p);
            case Oscillator::SAW_WAVE:
                return std::fmod(p * 2 + 1, 2.0f) - 1;
            case Oscillator::REVERSE_SAW_WAVE:
                return 1 - std::fmod(p * 2 + 1, 2.0f);
            case Oscillator::COS_WAVE:
                return float(std::cos(p * sVal2PI));
            case Oscillator::BOUNCE: {
                const float x = 1 - std::abs(std::fmod(p * 4, 4.0f) - 2);
                return 1 - x * x;
            }
        }
    }

    void TimeCycleSplineSet::setPoint(int position, float value, float period, int shape, float offset) {
        if (mCount >= int(mTimePoints.size())) {   // upstream's arrays hold ten; past that it throws
            mTimePoints.resize(mTimePoints.size() * 2);
            mValues.resize(mValues.size() * 2);
        }
        mTimePoints[std::size_t(mCount)] = position;
        mValues[std::size_t(mCount)][CURVE_VALUE] = value;
        mValues[std::size_t(mCount)][CURVE_PERIOD] = period;
        mValues[std::size_t(mCount)][CURVE_OFFSET] = offset;
        mWaveShape = std::max(mWaveShape, shape);
        mCount++;
    }

    void TimeCycleSplineSet::setup(int curveType) {
        if (mCount == 0) {
            std::fprintf(stderr, "Error no points added to %s\n", mType.c_str());
            return;
        }
        doubleQuickSort(mTimePoints, mValues, 0, mCount - 1);
        int unique = 0;
        for (std::size_t i = 1; i < mTimePoints.size(); i++)
            if (mTimePoints[i] != mTimePoints[i - 1]) unique++;
        if (unique == 0) unique = 1;
        std::vector<double> time(static_cast<std::size_t>(unique));
        std::vector<std::vector<double>> values(static_cast<std::size_t>(unique), std::vector<double>(3));
        int k = 0;
        for (int i = 0; i < mCount; i++) {
            if (i > 0 && mTimePoints[std::size_t(i)] == mTimePoints[std::size_t(i - 1)]) continue;
            time[std::size_t(k)] = mTimePoints[std::size_t(i)] * 1E-2;
            values[std::size_t(k)][0] = mValues[std::size_t(i)][0];
            values[std::size_t(k)][1] = mValues[std::size_t(i)][1];
            values[std::size_t(k)][2] = mValues[std::size_t(i)][2];
            k++;
        }
        mCurveFit = CurveFit::get(curveType, time, values);
    }

    // TimeCycleCustomSet

    TimeCycleCustomSet::TimeCycleCustomSet(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomArray> attrList)
        : mAttributeName(secondField(attribute)), mConstraintAttributeList(std::move(attrList)) {}

    void TimeCycleCustomSet::setup(int curveType) {
        mCurveFit = customSetup(curveType, *mConstraintAttributeList, mWaveProperties, mTempValues, mCustomCache);
    }

    void TimeCycleCustomSet::setPoint(int, float, float, int, float) {
        throw std::runtime_error("don't call for custom attribute call setPoint(pos, ConstraintAttribute,...)");
    }

    void TimeCycleCustomSet::setPoint(int position, const CustomAttribute& value, float period, int shape, float offset) {
        mConstraintAttributeList->append(position, value);
        mWaveProperties.append(position, { period, offset });
        mWaveShape = std::max(mWaveShape, shape);
    }

    bool TimeCycleCustomSet::setProperty(MotionWidget& view, float t, int64_t time, KeyCache& cache) {
        mCurveFit->getPos(t, mTempValues.data());
        const float period = mTempValues[mTempValues.size() - 2];
        const float offset = mTempValues[mTempValues.size() - 1];
        const int64_t delta_time = time - mLastTime;
        if (std::isnan(mLastCycle)) {
            mLastCycle = cache.getFloatValue(&view, mAttributeName, 0);
            if (std::isnan(mLastCycle)) mLastCycle = 0;
        }
        mLastCycle = float(std::fmod(mLastCycle + delta_time * 1E-9 * period, 1.0));
        mLastTime = time;
        const float wave = calcWave(mLastCycle);
        mContinue = false;
        for (std::size_t i = 0; i < mCustomCache.size(); i++) {
            mContinue |= mTempValues[i] != 0.0;
            mCustomCache[i] = mTempValues[i] * wave + offset;
        }
        view.setInterpolatedValue(mConstraintAttributeList->valueAt(0), mCustomCache.data());
        if (period != 0.0f) mContinue = true;
        return mContinue;
    }

    // TimeCycleCustomVarSet

    TimeCycleCustomVarSet::TimeCycleCustomVarSet(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomVar> attrList)
        : mAttributeName(secondField(attribute)), mConstraintAttributeList(std::move(attrList)) {}

    void TimeCycleCustomVarSet::setup(int curveType) {
        mCurveFit = customSetup(curveType, *mConstraintAttributeList, mWaveProperties, mTempValues, mCustomCache);
    }

    void TimeCycleCustomVarSet::setPoint(int, float, float, int, float) {
        throw std::runtime_error("don't call for custom attribute call setPoint(pos, ConstraintAttribute,...)");
    }

    void TimeCycleCustomVarSet::setPoint(int position, const CustomVariable& value, float period, int shape, float offset) {
        mConstraintAttributeList->append(position, value);
        mWaveProperties.append(position, { period, offset });
        mWaveShape = std::max(mWaveShape, shape);
    }

    bool TimeCycleCustomVarSet::setProperty(MotionWidget& view, float t, int64_t time, KeyCache& cache) {
        mCurveFit->getPos(t, mTempValues.data());
        const float period = mTempValues[mTempValues.size() - 2];
        const float offset = mTempValues[mTempValues.size() - 1];
        const int64_t delta_time = time - mLastTime;
        if (std::isnan(mLastCycle)) {
            mLastCycle = cache.getFloatValue(&view, mAttributeName, 0);
            if (std::isnan(mLastCycle)) mLastCycle = 0;
        }
        mLastCycle = float(std::fmod(mLastCycle + delta_time * 1E-9 * period, 1.0));
        mLastTime = time;
        const float wave = calcWave(mLastCycle);
        mContinue = false;
        for (std::size_t i = 0; i < mCustomCache.size(); i++) {
            mContinue |= mTempValues[i] != 0.0;
            mCustomCache[i] = mTempValues[i] * wave + offset;
        }
        mConstraintAttributeList->valueAt(0).setInterpolatedValue(view, mCustomCache.data());
        if (period != 0.0f) mContinue = true;
        return mContinue;
    }

}
