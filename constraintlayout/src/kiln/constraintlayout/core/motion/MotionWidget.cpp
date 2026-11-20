#include "kiln/constraintlayout/core/motion/MotionWidget.h"

#include "kiln/constraintlayout/core/motion/CustomVariable.h"

namespace kiln::constraintlayout::core::motion {

    using AT = TypedValues::AttributesType;
    using MT = TypedValues::MotionType;

    std::string MotionWidget::toString() const {
        return std::to_string(mWidgetFrame.left) + ", " + std::to_string(mWidgetFrame.top) + ", " + std::to_string(mWidgetFrame.right) + ", " +
               std::to_string(mWidgetFrame.bottom);
    }

    bool MotionWidget::setValue(int id, int value) {
        if (setValueAttributes(id, float(value))) return true;
        return setValueMotion(id, value);
    }

    bool MotionWidget::setValue(int id, float value) {
        if (setValueAttributes(id, value)) return true;
        return setValueMotion(id, value);
    }

    bool MotionWidget::setValue(int id, const std::string& value) {
        if (id == MT::TYPE_ANIMATE_RELATIVE_TO) {
            mMotion.mAnimateRelativeTo = value;
            return true;
        }
        return setValueMotion(id, value);
    }

    bool MotionWidget::setValueMotion(int id, int value) {
        switch (id) {
            case MT::TYPE_ANIMATE_CIRCLEANGLE_TO: mMotion.mAnimateCircleAngleTo = value; break;
            case MT::TYPE_PATHMOTION_ARC: mMotion.mPathMotionArc = value; break;
            case MT::TYPE_DRAW_PATH: mMotion.mDrawPath = value; break;
            case MT::TYPE_POLAR_RELATIVETO: mMotion.mPolarRelativeTo = value; break;
            case MT::TYPE_QUANTIZE_MOTIONSTEPS: mMotion.mQuantizeMotionSteps = value; break;
            case MT::TYPE_QUANTIZE_INTERPOLATOR_TYPE: mMotion.mQuantizeInterpolatorType = value; break;
            case MT::TYPE_QUANTIZE_INTERPOLATOR_ID: mMotion.mQuantizeInterpolatorID = value; break;
            default: return false;
        }
        return true;
    }

    bool MotionWidget::setValueMotion(int id, const std::string& value) {
        switch (id) {
            case MT::TYPE_EASING: mMotion.mTransitionEasing = value; break;
            case MT::TYPE_QUANTIZE_INTERPOLATOR: mMotion.mQuantizeInterpolatorString = value; break;
            default: return false;
        }
        return true;
    }

    bool MotionWidget::setValueMotion(int id, float value) {
        switch (id) {
            case MT::TYPE_STAGGER: mMotion.mMotionStagger = value; break;
            case MT::TYPE_PATH_ROTATE: mMotion.mPathRotate = value; break;
            case MT::TYPE_QUANTIZE_MOTION_PHASE: mMotion.mQuantizeMotionPhase = value; break;
            default: return false;
        }
        return true;
    }

    bool MotionWidget::setValueAttributes(int id, float value) {
        switch (id) {
            case AT::TYPE_ALPHA: mWidgetFrame.alpha = value; break;
            case AT::TYPE_TRANSLATION_X: mWidgetFrame.translationX = value; break;
            case AT::TYPE_TRANSLATION_Y: mWidgetFrame.translationY = value; break;
            case AT::TYPE_TRANSLATION_Z: mWidgetFrame.translationZ = value; break;
            case AT::TYPE_ROTATION_X: mWidgetFrame.rotationX = value; break;
            case AT::TYPE_ROTATION_Y: mWidgetFrame.rotationY = value; break;
            case AT::TYPE_ROTATION_Z: mWidgetFrame.rotationZ = value; break;
            case AT::TYPE_SCALE_X: mWidgetFrame.scaleX = value; break;
            case AT::TYPE_SCALE_Y: mWidgetFrame.scaleY = value; break;
            case AT::TYPE_PIVOT_X: mWidgetFrame.pivotX = value; break;
            case AT::TYPE_PIVOT_Y: mWidgetFrame.pivotY = value; break;
            case AT::TYPE_PROGRESS: mProgress = value; break;
            case AT::TYPE_PATH_ROTATE: mTransitionPathRotate = value; break;
            default: return false;
        }
        return true;
    }

    float MotionWidget::getValueAttributes(int id) const {
        switch (id) {
            case AT::TYPE_ALPHA: return mWidgetFrame.alpha;
            case AT::TYPE_TRANSLATION_X: return mWidgetFrame.translationX;
            case AT::TYPE_TRANSLATION_Y: return mWidgetFrame.translationY;
            case AT::TYPE_TRANSLATION_Z: return mWidgetFrame.translationZ;
            case AT::TYPE_ROTATION_X: return mWidgetFrame.rotationX;
            case AT::TYPE_ROTATION_Y: return mWidgetFrame.rotationY;
            case AT::TYPE_ROTATION_Z: return mWidgetFrame.rotationZ;
            case AT::TYPE_SCALE_X: return mWidgetFrame.scaleX;
            case AT::TYPE_SCALE_Y: return mWidgetFrame.scaleY;
            case AT::TYPE_PIVOT_X: return mWidgetFrame.pivotX;
            case AT::TYPE_PIVOT_Y: return mWidgetFrame.pivotY;
            case AT::TYPE_PROGRESS: return mProgress;
            case AT::TYPE_PATH_ROTATE: return mTransitionPathRotate;
            default: return NAN;
        }
    }

    int MotionWidget::getId(const std::string& name) {
        const int ret = AT::getId(name);
        if (ret != -1) return ret;
        return MT::getId(name);
    }

    void MotionWidget::setInterpolatedValue(const CustomAttribute& attribute, const float* cache) {
        mWidgetFrame.setCustomAttribute(attribute.mName, TypedValues::Custom::TYPE_FLOAT, cache[0]);
    }

}
