#include "kiln/constraintlayout/core/motion/key/MotionKeyAttributes.h"

#include "kiln/constraintlayout/core/motion/utils/SplineSet.h"

#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    using AT = TypedValues::AttributesType;

    void MotionKeyAttributes::getAttributeNames(JavaHashSet& attributes) {
        if (!std::isnan(mAlpha)) attributes.add(AT::S_ALPHA);
        if (!std::isnan(mElevation)) attributes.add(AT::S_ELEVATION);
        if (!std::isnan(mRotation)) attributes.add(AT::S_ROTATION_Z);
        if (!std::isnan(mRotationX)) attributes.add(AT::S_ROTATION_X);
        if (!std::isnan(mRotationY)) attributes.add(AT::S_ROTATION_Y);
        if (!std::isnan(mPivotX)) attributes.add(AT::S_PIVOT_X);
        if (!std::isnan(mPivotY)) attributes.add(AT::S_PIVOT_Y);
        if (!std::isnan(mTranslationX)) attributes.add(AT::S_TRANSLATION_X);
        if (!std::isnan(mTranslationY)) attributes.add(AT::S_TRANSLATION_Y);
        if (!std::isnan(mTranslationZ)) attributes.add(AT::S_TRANSLATION_Z);
        if (!std::isnan(mTransitionPathRotate)) attributes.add(AT::S_PATH_ROTATE);
        if (!std::isnan(mScaleX)) attributes.add(AT::S_SCALE_X);
        if (!std::isnan(mScaleY)) attributes.add(AT::S_SCALE_Y);
        if (!std::isnan(mProgress)) attributes.add(AT::S_PROGRESS);
        for (const std::string& s : mCustom.keys()) attributes.add(std::string(TypedValues::S_CUSTOM) + "," + s);
    }

    void MotionKeyAttributes::addValues(SplineMap& splines) {
        for (const std::string& s : splines.keys()) {
            std::shared_ptr<SplineSet>* found = splines.find(s);
            if (found == nullptr || *found == nullptr) continue;
            SplineSet& splineSet = **found;
            const std::string custom = AT::S_CUSTOM;
            if (s.rfind(custom, 0) == 0) {
                const std::string cKey = s.substr(custom.size() + 1);
                if (const CustomVariable* cValue = mCustom.find(cKey)) static_cast<CustomSpline&>(splineSet).setPoint(mFramePosition, *cValue);
                continue;
            }
            auto point = [&](float v) {
                if (!std::isnan(v)) splineSet.setPoint(mFramePosition, v);
            };
            if (s == AT::S_ALPHA) point(mAlpha);
            else if (s == AT::S_ELEVATION) point(mElevation);
            else if (s == AT::S_ROTATION_Z) point(mRotation);
            else if (s == AT::S_ROTATION_X) point(mRotationX);
            else if (s == AT::S_ROTATION_Y) point(mRotationY);
            // Upstream tests the rotation values for the pivots.
            else if (s == AT::S_PIVOT_X) {
                if (!std::isnan(mRotationX)) splineSet.setPoint(mFramePosition, mPivotX);
            } else if (s == AT::S_PIVOT_Y) {
                if (!std::isnan(mRotationY)) splineSet.setPoint(mFramePosition, mPivotY);
            } else if (s == AT::S_PATH_ROTATE) point(mTransitionPathRotate);
            else if (s == AT::S_SCALE_X) point(mScaleX);
            else if (s == AT::S_SCALE_Y) point(mScaleY);
            else if (s == AT::S_TRANSLATION_X) point(mTranslationX);
            else if (s == AT::S_TRANSLATION_Y) point(mTranslationY);
            else if (s == AT::S_TRANSLATION_Z) point(mTranslationZ);
            else if (s == AT::S_PROGRESS) point(mProgress);
            else std::fprintf(stderr, "not supported by KeyAttributes %s\n", s.c_str());
        }
    }

    bool MotionKeyAttributes::setValue(int type, int value) {
        switch (type) {
            case AT::TYPE_VISIBILITY: mVisibility = value; break;
            case AT::TYPE_CURVE_FIT: mCurveFit = value; break;
            case TYPE_FRAME_POSITION: mFramePosition = value; break;
            default:
                // Upstream calls setValue(type, value) here, recursing on itself; the float
                // overload is what it means.
                if (!setValue(type, float(value))) return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyAttributes::setValue(int type, float value) {
        switch (type) {
            case AT::TYPE_ALPHA: mAlpha = value; break;
            case AT::TYPE_TRANSLATION_X: mTranslationX = value; break;
            case AT::TYPE_TRANSLATION_Y: mTranslationY = value; break;
            case AT::TYPE_TRANSLATION_Z: mTranslationZ = value; break;
            case AT::TYPE_ELEVATION: mElevation = value; break;
            case AT::TYPE_ROTATION_X: mRotationX = value; break;
            case AT::TYPE_ROTATION_Y: mRotationY = value; break;
            case AT::TYPE_ROTATION_Z: mRotation = value; break;
            case AT::TYPE_SCALE_X: mScaleX = value; break;
            case AT::TYPE_SCALE_Y: mScaleY = value; break;
            case AT::TYPE_PIVOT_X: mPivotX = value; break;
            case AT::TYPE_PIVOT_Y: mPivotY = value; break;
            case AT::TYPE_PROGRESS: mProgress = value; break;
            case AT::TYPE_PATH_ROTATE: mTransitionPathRotate = value; break;
            case TYPE_FRAME_POSITION: mTransitionPathRotate = value; break;   // as upstream
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    void MotionKeyAttributes::setInterpolation(JavaHashMap<int>& interpolation) {
        auto put = [&](float v, const char* name) {
            if (!std::isnan(v)) interpolation.put(name, mCurveFit);
        };
        put(mAlpha, AT::S_ALPHA);
        put(mElevation, AT::S_ELEVATION);
        put(mRotation, AT::S_ROTATION_Z);
        put(mRotationX, AT::S_ROTATION_X);
        put(mRotationY, AT::S_ROTATION_Y);
        put(mPivotX, AT::S_PIVOT_X);
        put(mPivotY, AT::S_PIVOT_Y);
        put(mTranslationX, AT::S_TRANSLATION_X);
        put(mTranslationY, AT::S_TRANSLATION_Y);
        put(mTranslationZ, AT::S_TRANSLATION_Z);
        put(mTransitionPathRotate, AT::S_PATH_ROTATE);
        put(mScaleX, AT::S_SCALE_X);
        put(mScaleY, AT::S_SCALE_Y);
        put(mProgress, AT::S_PROGRESS);
        for (const std::string& s : mCustom.keys()) interpolation.put(std::string(AT::S_CUSTOM) + "," + s, mCurveFit);
    }

    bool MotionKeyAttributes::setValue(int type, const std::string& value) {
        switch (type) {
            case AT::TYPE_EASING: mTransitionEasing = value; break;
            case TYPE_TARGET: mTargetString = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    void MotionKeyAttributes::printAttributes() {
        JavaHashSet nameSet;
        getAttributeNames(nameSet);
        std::printf(" ------------- %d -------------\n", mFramePosition);
        for (const std::string& name : nameSet.keys()) std::printf("%s:%g\n", name.c_str(), double(getFloatValue(AT::getId(name))));
    }

    float MotionKeyAttributes::getFloatValue(int id) const {
        switch (id) {
            case AT::TYPE_ALPHA: return mAlpha;
            case AT::TYPE_TRANSLATION_X: return mTranslationX;
            case AT::TYPE_TRANSLATION_Y: return mTranslationY;
            case AT::TYPE_TRANSLATION_Z: return mTranslationZ;
            case AT::TYPE_ELEVATION: return mElevation;
            case AT::TYPE_ROTATION_X: return mRotationX;
            case AT::TYPE_ROTATION_Y: return mRotationY;
            case AT::TYPE_ROTATION_Z: return mRotation;
            case AT::TYPE_SCALE_X: return mScaleX;
            case AT::TYPE_SCALE_Y: return mScaleY;
            case AT::TYPE_PIVOT_X: return mPivotX;
            case AT::TYPE_PIVOT_Y: return mPivotY;
            case AT::TYPE_PROGRESS: return mProgress;
            case AT::TYPE_PATH_ROTATE: return mTransitionPathRotate;
            case TYPE_FRAME_POSITION: return float(mFramePosition);
            default: return NAN;
        }
    }

}
