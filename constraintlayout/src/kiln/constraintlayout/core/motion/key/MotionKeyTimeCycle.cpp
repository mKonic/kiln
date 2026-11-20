#include "kiln/constraintlayout/core/motion/key/MotionKeyTimeCycle.h"

#include "kiln/constraintlayout/core/motion/utils/Oscillator.h"
#include "kiln/constraintlayout/core/motion/utils/TimeCycleSplineSet.h"

#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    using AT = TypedValues::AttributesType;
    using CT = TypedValues::CycleType;

    void MotionKeyTimeCycle::addTimeValues(TimeCycleMap& splines) {
        const std::string custom = CUSTOM;
        for (const std::string& s : splines.keys()) {
            std::shared_ptr<TimeCycleSplineSet>* found = splines.find(s);
            if (found == nullptr || *found == nullptr) continue;
            TimeCycleSplineSet& splineSet = **found;
            if (s.rfind(custom, 0) == 0) {
                const std::string cKey = s.substr(custom.size() + 1);
                if (const CustomVariable* cValue = mCustom.find(cKey))
                    static_cast<TimeCycleCustomVarSet&>(splineSet).setPoint(mFramePosition, *cValue, mWavePeriod, mWaveShape, mWaveOffset);
                continue;
            }
            auto point = [&](float test, float v) {
                if (!std::isnan(test)) splineSet.setPoint(mFramePosition, v, mWavePeriod, mWaveShape, mWaveOffset);
            };
            if (s == AT::S_ALPHA) point(mAlpha, mAlpha);
            else if (s == AT::S_ROTATION_X) point(mRotationX, mRotationX);
            else if (s == AT::S_ROTATION_Y) point(mRotationY, mRotationY);
            else if (s == AT::S_ROTATION_Z) point(mRotation, mRotation);
            else if (s == AT::S_PATH_ROTATE) point(mTransitionPathRotate, mTransitionPathRotate);
            else if (s == AT::S_SCALE_X) point(mScaleX, mScaleX);
            else if (s == AT::S_SCALE_Y) point(mScaleY, mScaleY);
            else if (s == AT::S_TRANSLATION_X) point(mTranslationX, mTranslationX);
            else if (s == AT::S_TRANSLATION_Y) point(mTranslationY, mTranslationY);
            else if (s == AT::S_TRANSLATION_Z) point(mTranslationZ, mTranslationZ);
            else if (s == AT::S_ELEVATION) point(mTranslationZ, mTranslationZ);   // upstream uses translationZ
            else if (s == AT::S_PROGRESS) point(mProgress, mProgress);
            else std::fprintf(stderr, "KeyTimeCycles: UNKNOWN addValues \"%s\"\n", s.c_str());
        }
    }

    bool MotionKeyTimeCycle::setValue(int type, int value) {
        switch (type) {
            case TYPE_FRAME_POSITION: mFramePosition = value; break;
            case CT::TYPE_WAVE_SHAPE: mWaveShape = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyTimeCycle::setValue(int type, float value) {
        switch (type) {
            case CT::TYPE_ALPHA: mAlpha = value; break;
            case CT::TYPE_CURVE_FIT: mCurveFit = int(value); break;   // toInt(Float) parses "1.0" and throws; ids arrive as int
            case CT::TYPE_ELEVATION: mElevation = value; break;
            case CT::TYPE_PROGRESS: mProgress = value; break;
            case CT::TYPE_ROTATION_Z: mRotation = value; break;
            case CT::TYPE_ROTATION_X: mRotationX = value; break;
            case CT::TYPE_ROTATION_Y: mRotationY = value; break;
            case CT::TYPE_SCALE_X: mScaleX = value; break;
            case CT::TYPE_SCALE_Y: mScaleY = value; break;
            case CT::TYPE_PATH_ROTATE: mTransitionPathRotate = value; break;
            case CT::TYPE_TRANSLATION_X: mTranslationX = value; break;
            case CT::TYPE_TRANSLATION_Y: mTranslationY = value; break;
            case CT::TYPE_TRANSLATION_Z: mTranslationZ = value; break;
            case CT::TYPE_WAVE_PERIOD: mWavePeriod = value; break;
            case CT::TYPE_WAVE_OFFSET: mWaveOffset = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyTimeCycle::setValue(int type, const std::string& value) {
        switch (type) {
            case CT::TYPE_WAVE_SHAPE:
                mWaveShape = Oscillator::CUSTOM;
                mCustomWaveShape = value;
                break;
            case CT::TYPE_EASING: mTransitionEasing = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    MotionKey& MotionKeyTimeCycle::copy(const MotionKey& src) {
        MotionKey::copy(src);
        const auto& k = static_cast<const MotionKeyTimeCycle&>(src);
        mTransitionEasing = k.mTransitionEasing;
        mCurveFit = k.mCurveFit;
        mWaveShape = k.mWaveShape;
        mWavePeriod = k.mWavePeriod;
        mWaveOffset = k.mWaveOffset;
        mProgress = k.mProgress;
        mAlpha = k.mAlpha;
        mElevation = k.mElevation;
        mRotation = k.mRotation;
        mTransitionPathRotate = k.mTransitionPathRotate;
        mRotationX = k.mRotationX;
        mRotationY = k.mRotationY;
        mScaleX = k.mScaleX;
        mScaleY = k.mScaleY;
        mTranslationX = k.mTranslationX;
        mTranslationY = k.mTranslationY;
        mTranslationZ = k.mTranslationZ;
        return *this;
    }

    void MotionKeyTimeCycle::getAttributeNames(JavaHashSet& attributes) {
        if (!std::isnan(mAlpha)) attributes.add(CT::S_ALPHA);
        if (!std::isnan(mElevation)) attributes.add(CT::S_ELEVATION);
        if (!std::isnan(mRotation)) attributes.add(CT::S_ROTATION_Z);
        if (!std::isnan(mRotationX)) attributes.add(CT::S_ROTATION_X);
        if (!std::isnan(mRotationY)) attributes.add(CT::S_ROTATION_Y);
        if (!std::isnan(mScaleX)) attributes.add(CT::S_SCALE_X);
        if (!std::isnan(mScaleY)) attributes.add(CT::S_SCALE_Y);
        if (!std::isnan(mTransitionPathRotate)) attributes.add(CT::S_PATH_ROTATE);
        if (!std::isnan(mTranslationX)) attributes.add(CT::S_TRANSLATION_X);
        if (!std::isnan(mTranslationY)) attributes.add(CT::S_TRANSLATION_Y);
        if (!std::isnan(mTranslationZ)) attributes.add(CT::S_TRANSLATION_Z);
        for (const std::string& s : mCustom.keys()) attributes.add(std::string(TypedValues::S_CUSTOM) + "," + s);
    }

}
