#pragma once

// SplineSet (constraintlayout-core motion utils): one attribute's keyframe values over the
// motion (positions 0..100) as a curve, set on a widget at a progress. Custom attributes and
// variables interpolate several values at once (colours).

#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyFrameArray.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core {
    class WidgetFrame;
}

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;

    class SplineSet {
    public:
        virtual ~SplineSet() = default;
        virtual void setProperty(TypedValues& widget, float t);
        std::string toString() const;
        void setType(const std::string& type) { mType = type; }
        float get(float t) { return float(mCurveFit->getPos(t, 0)); }
        float getSlope(float t) { return float(mCurveFit->getSlope(t, 0)); }
        const std::shared_ptr<CurveFit>& getCurveFit() const { return mCurveFit; }
        virtual void setPoint(int position, float value);
        virtual void setup(int curveType);
        static std::shared_ptr<SplineSet> makeCustomSpline(const std::string& str, std::shared_ptr<KeyFrameArray::CustomArray> attrList);
        static std::shared_ptr<SplineSet> makeCustomSplineSet(const std::string& str, std::shared_ptr<KeyFrameArray::CustomVar> attrList);
        static std::shared_ptr<SplineSet> makeSpline(const std::string& str, int64_t currentTime);
        // instanceof CustomSpline.
        virtual class CustomSpline* asCustomSpline() { return nullptr; }

    protected:
        std::shared_ptr<CurveFit> mCurveFit;
        std::vector<int> mTimePoints;
        std::vector<float> mValues;
        std::string mType;
    };

    class CustomSet : public SplineSet {
    public:
        CustomSet(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomArray> attrList);
        void setup(int curveType) override;
        void setPoint(int position, float value) override;
        void setPoint(int position, const CustomAttribute& value) { mConstraintAttributeList->append(position, value); }
        // Upstream's WidgetFrame.setCustomValue does nothing.
        void setProperty(WidgetFrame& view, float t);
        using SplineSet::setProperty;

    private:
        std::string mAttributeName;
        std::shared_ptr<KeyFrameArray::CustomArray> mConstraintAttributeList;
        std::vector<float> mTempValues;
    };

    class CustomSpline : public SplineSet {
    public:
        CustomSpline(const std::string& attribute, std::shared_ptr<KeyFrameArray::CustomVar> attrList);
        void setup(int curveType) override;
        void setPoint(int position, float value) override;
        void setProperty(TypedValues& widget, float t) override;
        void setPoint(int position, const CustomVariable& value) { mConstraintAttributeList->append(position, value); }
        void setProperty(MotionWidget& view, float t);
        CustomSpline* asCustomSpline() override { return this; }

    private:
        std::string mAttributeName;
        std::shared_ptr<KeyFrameArray::CustomVar> mConstraintAttributeList;
        std::vector<float> mTempValues;
    };

}
