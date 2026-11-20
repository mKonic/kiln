#pragma once

// MotionKeyAttributes (constraintlayout-core motion key): attribute values (alpha, rotation,
// scale, translation, ...) a widget passes through at a frame position.

#include "kiln/constraintlayout/core/motion/key/MotionKey.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class MotionKeyAttributes : public MotionKey {
    public:
        static constexpr const char* NAME = "KeyAttribute";
        static constexpr int KEY_TYPE = 1;

        MotionKeyAttributes() { mType = KEY_TYPE; }
        void getAttributeNames(JavaHashSet& attributes) override;
        void addValues(SplineMap& splines) override;
        std::shared_ptr<MotionKey> clone() const override { return nullptr; }
        bool setValue(int type, int value) override;
        bool setValue(int type, float value) override;
        bool setValue(int type, const std::string& value) override;
        bool setValue(int type, bool value) override { return MotionKey::setValue(type, value); }
        void setInterpolation(JavaHashMap<int>& interpolation) override;
        int getId(const std::string& name) override { return AttributesType::getId(name); }
        int getCurveFit() const { return mCurveFit; }
        void printAttributes();

    private:
        float getFloatValue(int id) const;

        std::string mTransitionEasing;
        int mCurveFit = -1;
        int mVisibility = 0;
        float mAlpha = NAN;
        float mElevation = NAN;
        float mRotation = NAN;
        float mRotationX = NAN;
        float mRotationY = NAN;
        float mPivotX = NAN;
        float mPivotY = NAN;
        float mTransitionPathRotate = NAN;
        float mScaleX = NAN;
        float mScaleY = NAN;
        float mTranslationX = NAN;
        float mTranslationY = NAN;
        float mTranslationZ = NAN;
        float mProgress = NAN;
    };

}
