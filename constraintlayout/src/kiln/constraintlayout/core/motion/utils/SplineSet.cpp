#include "kiln/constraintlayout/core/motion/utils/SplineSet.h"

#include "kiln/constraintlayout/core/motion/MotionWidget.h"

#include <cstdio>
#include <stdexcept>

namespace kiln::constraintlayout::core::motion {

    namespace {
        // Sort.doubleQuickSort: upstream's iterative quicksort, keeping the values with the keys.
        void swap(std::vector<int>& array, std::vector<float>& value, int a, int b) {
            std::swap(array[std::size_t(a)], array[std::size_t(b)]);
            std::swap(value[std::size_t(a)], value[std::size_t(b)]);
        }
        int partition(std::vector<int>& array, std::vector<float>& value, int low, int hi) {
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
        void doubleQuickSort(std::vector<int>& key, std::vector<float>& value, int low, int hi) {
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
        // attribute.split(",")[1]
        std::string secondField(const std::string& attribute) {
            const std::size_t comma = attribute.find(',');
            if (comma == std::string::npos) throw std::out_of_range("Index 1 out of bounds for length 1");
            const std::size_t next = attribute.find(',', comma + 1);
            return attribute.substr(comma + 1, next == std::string::npos ? std::string::npos : next - comma - 1);
        }

        class CoreSpline final : public SplineSet {
        public:
            CoreSpline(const std::string& str, int64_t) { mType = str; }
            void setProperty(TypedValues& widget, float t) override {
                const int id = widget.getId(mType);
                widget.setValue(id, get(t));
            }
        };
    }

    void SplineSet::setProperty(TypedValues& widget, float t) { widget.setValue(TypedValues::AttributesType::getId(mType), get(t)); }

    std::string SplineSet::toString() const {
        std::string str = mType;
        for (std::size_t i = 0; i < mTimePoints.size(); i++) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.2f", double(mValues[i]));
            str += "[" + std::to_string(mTimePoints[i]) + " , " + buf + "] ";
        }
        return str;
    }

    void SplineSet::setPoint(int position, float value) {
        mTimePoints.push_back(position);
        mValues.push_back(value);
    }

    void SplineSet::setup(int curveType) {
        const int count = int(mTimePoints.size());
        if (count == 0) return;
        doubleQuickSort(mTimePoints, mValues, 0, count - 1);
        int unique = 1;
        for (int i = 1; i < count; i++)
            if (mTimePoints[std::size_t(i - 1)] != mTimePoints[std::size_t(i)]) unique++;
        std::vector<double> time(static_cast<std::size_t>(unique));
        std::vector<std::vector<double>> values(static_cast<std::size_t>(unique), std::vector<double>(1));
        int k = 0;
        for (int i = 0; i < count; i++) {
            if (i > 0 && mTimePoints[std::size_t(i)] == mTimePoints[std::size_t(i - 1)]) continue;
            time[std::size_t(k)] = mTimePoints[std::size_t(i)] * 1E-2;
            values[std::size_t(k)][0] = mValues[std::size_t(i)];
            k++;
        }
        mCurveFit = CurveFit::get(curveType, time, values);
    }

    std::shared_ptr<SplineSet> SplineSet::makeCustomSpline(const std::string& str, std::shared_ptr<KeyFrameArray::CustomArray> attrList) {
        return std::make_shared<CustomSet>(str, std::move(attrList));
    }
    std::shared_ptr<SplineSet> SplineSet::makeCustomSplineSet(const std::string& str, std::shared_ptr<KeyFrameArray::CustomVar> attrList) {
        return std::make_shared<CustomSpline>(str, std::move(attrList));
    }
    std::shared_ptr<SplineSet> SplineSet::makeSpline(const std::string& str, int64_t currentTime) {
        return std::make_shared<CoreSpline>(str, currentTime);
    }

    // CustomSet

    CustomSet::CustomSet(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomArray> attrList)
        : mAttributeName(secondField(attribute)), mConstraintAttributeList(std::move(attrList)) {}

    void CustomSet::setup(int curveType) {
        const int size = mConstraintAttributeList->size();
        const int dimensionality = mConstraintAttributeList->valueAt(0).numberOfInterpolatedValues();
        std::vector<double> time(static_cast<std::size_t>(size));
        mTempValues.assign(std::size_t(dimensionality), 0);
        std::vector<std::vector<double>> values(static_cast<std::size_t>(size), std::vector<double>(static_cast<std::size_t>(dimensionality)));
        for (int i = 0; i < size; i++) {
            const int key = mConstraintAttributeList->keyAt(i);
            const CustomAttribute& ca = mConstraintAttributeList->valueAt(i);
            time[std::size_t(i)] = key * 1E-2;
            ca.getValuesToInterpolate(mTempValues.data());
            for (std::size_t k = 0; k < mTempValues.size(); k++) values[std::size_t(i)][k] = mTempValues[k];
        }
        mCurveFit = CurveFit::get(curveType, time, values);
    }

    void CustomSet::setPoint(int, float) { throw std::runtime_error("don't call for custom attribute call setPoint(pos, ConstraintAttribute)"); }

    void CustomSet::setProperty(WidgetFrame&, float t) { mCurveFit->getPos(t, mTempValues.data()); }

    // CustomSpline

    CustomSpline::CustomSpline(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomVar> attrList)
        : mAttributeName(secondField(attribute)), mConstraintAttributeList(std::move(attrList)) {}

    void CustomSpline::setup(int curveType) {
        const int size = mConstraintAttributeList->size();
        const int dimensionality = mConstraintAttributeList->valueAt(0).numberOfInterpolatedValues();
        std::vector<double> time(static_cast<std::size_t>(size));
        mTempValues.assign(std::size_t(dimensionality), 0);
        std::vector<std::vector<double>> values(static_cast<std::size_t>(size), std::vector<double>(static_cast<std::size_t>(dimensionality)));
        for (int i = 0; i < size; i++) {
            const int key = mConstraintAttributeList->keyAt(i);
            const CustomVariable& ca = mConstraintAttributeList->valueAt(i);
            time[std::size_t(i)] = key * 1E-2;
            ca.getValuesToInterpolate(mTempValues.data());
            for (std::size_t k = 0; k < mTempValues.size(); k++) values[std::size_t(i)][k] = mTempValues[k];
        }
        mCurveFit = CurveFit::get(curveType, time, values);
    }

    void CustomSpline::setPoint(int, float) { throw std::runtime_error("don't call for custom attribute call setPoint(pos, ConstraintAttribute)"); }

    void CustomSpline::setProperty(TypedValues& widget, float t) { setProperty(static_cast<MotionWidget&>(widget), t); }

    void CustomSpline::setProperty(MotionWidget& view, float t) {
        mCurveFit->getPos(t, mTempValues.data());
        mConstraintAttributeList->valueAt(0).setInterpolatedValue(view, mTempValues.data());
    }

}
