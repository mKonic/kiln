#include "kiln/constraintlayout/core/motion/key/MotionKeyCycle.h"

#include "kiln/constraintlayout/core/motion/utils/KeyCycleOscillator.h"

#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    using CT = TypedValues::CycleType;

    void MotionKeyCycle::getAttributeNames(JavaHashSet& attributes) {
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

    bool MotionKeyCycle::setValue(int type, int value) {
        switch (type) {
            case CT::TYPE_CURVE_FIT: mCurveFit = value; return true;
            case CT::TYPE_WAVE_SHAPE: mWaveShape = value; return true;
            default:
                if (setValue(type, float(value))) return true;
                return MotionKey::setValue(type, value);
        }
    }

    bool MotionKeyCycle::setValue(int type, const std::string& value) {
        switch (type) {
            case CT::TYPE_EASING: mTransitionEasing = value; return true;
            case CT::TYPE_CUSTOM_WAVE_SHAPE: mCustomWaveShape = value; return true;
            default: return MotionKey::setValue(type, value);
        }
    }

    bool MotionKeyCycle::setValue(int type, float value) {
        switch (type) {
            case CT::TYPE_ALPHA: mAlpha = value; break;
            case CT::TYPE_TRANSLATION_X: mTranslationX = value; break;
            case CT::TYPE_TRANSLATION_Y: mTranslationY = value; break;
            case CT::TYPE_TRANSLATION_Z: mTranslationZ = value; break;
            case CT::TYPE_ELEVATION: mElevation = value; break;
            case CT::TYPE_ROTATION_X: mRotationX = value; break;
            case CT::TYPE_ROTATION_Y: mRotationY = value; break;
            case CT::TYPE_ROTATION_Z: mRotation = value; break;
            case CT::TYPE_SCALE_X: mScaleX = value; break;
            case CT::TYPE_SCALE_Y: mScaleY = value; break;
            case CT::TYPE_PROGRESS: mProgress = value; break;
            case CT::TYPE_PATH_ROTATE: mTransitionPathRotate = value; break;
            case CT::TYPE_WAVE_PERIOD: mWavePeriod = value; break;
            case CT::TYPE_WAVE_OFFSET: mWaveOffset = value; break;
            case CT::TYPE_WAVE_PHASE: mWavePhase = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    float MotionKeyCycle::getValue(const std::string& key) const {
        if (key == CT::S_ALPHA) return mAlpha;
        if (key == CT::S_ELEVATION) return mElevation;
        if (key == CT::S_ROTATION_Z) return mRotation;
        if (key == CT::S_ROTATION_X) return mRotationX;
        if (key == CT::S_ROTATION_Y) return mRotationY;
        if (key == CT::S_PATH_ROTATE) return mTransitionPathRotate;
        if (key == CT::S_SCALE_X) return mScaleX;
        if (key == CT::S_SCALE_Y) return mScaleY;
        if (key == CT::S_TRANSLATION_X) return mTranslationX;
        if (key == CT::S_TRANSLATION_Y) return mTranslationY;
        if (key == CT::S_TRANSLATION_Z) return mTranslationZ;
        if (key == CT::S_WAVE_OFFSET) return mWaveOffset;
        if (key == CT::S_WAVE_PHASE) return mWavePhase;
        if (key == CT::S_PROGRESS) return mProgress;
        return NAN;
    }

    int MotionKeyCycle::getId(const std::string& name) {
        if (name == CT::S_CURVE_FIT) return CT::TYPE_CURVE_FIT;
        if (name == CT::S_VISIBILITY) return CT::TYPE_VISIBILITY;
        if (name == CT::S_ALPHA) return CT::TYPE_ALPHA;
        if (name == CT::S_TRANSLATION_X) return CT::TYPE_TRANSLATION_X;
        if (name == CT::S_TRANSLATION_Y) return CT::TYPE_TRANSLATION_Y;
        if (name == CT::S_TRANSLATION_Z) return CT::TYPE_TRANSLATION_Z;
        if (name == CT::S_ROTATION_X) return CT::TYPE_ROTATION_X;
        if (name == CT::S_ROTATION_Y) return CT::TYPE_ROTATION_Y;
        if (name == CT::S_ROTATION_Z) return CT::TYPE_ROTATION_Z;
        if (name == CT::S_SCALE_X) return CT::TYPE_SCALE_X;
        if (name == CT::S_SCALE_Y) return CT::TYPE_SCALE_Y;
        if (name == CT::S_PIVOT_X) return CT::TYPE_PIVOT_X;
        if (name == CT::S_PIVOT_Y) return CT::TYPE_PIVOT_Y;
        if (name == CT::S_PROGRESS) return CT::TYPE_PROGRESS;
        if (name == CT::S_PATH_ROTATE) return CT::TYPE_PATH_ROTATE;
        if (name == CT::S_EASING) return CT::TYPE_EASING;
        if (name == CT::S_WAVE_PERIOD) return CT::TYPE_WAVE_PERIOD;
        if (name == CT::S_WAVE_SHAPE) return CT::TYPE_WAVE_SHAPE;
        if (name == CT::S_WAVE_PHASE) return CT::TYPE_WAVE_PHASE;
        if (name == CT::S_WAVE_OFFSET) return CT::TYPE_WAVE_OFFSET;
        if (name == CT::S_CUSTOM_WAVE_SHAPE) return CT::TYPE_CUSTOM_WAVE_SHAPE;
        return -1;
    }

    void MotionKeyCycle::addCycleValues(KeyCycleOscillatorMap& oscSet) {
        const std::string custom = TypedValues::S_CUSTOM;
        for (const std::string& key : oscSet.keys()) {
            if (key.rfind(custom, 0) == 0) {
                const std::string customKey = key.substr(custom.size() + 1);
                const CustomVariable* cValue = mCustom.find(customKey);
                if (cValue == nullptr || cValue->getType() != Custom::TYPE_FLOAT) continue;
                std::shared_ptr<KeyCycleOscillator>* osc = oscSet.find(key);
                if (osc == nullptr || *osc == nullptr) continue;
                (*osc)->setPoint(mFramePosition, mWaveShape, mCustomWaveShape, -1, mWavePeriod, mWaveOffset, mWavePhase / 360,
                                 cValue->getValueToInterpolate(), cValue);
                continue;
            }
            const float value = getValue(key);
            if (std::isnan(value)) continue;
            std::shared_ptr<KeyCycleOscillator>* osc = oscSet.find(key);
            if (osc == nullptr || *osc == nullptr) continue;
            (*osc)->setPoint(mFramePosition, mWaveShape, mCustomWaveShape, -1, mWavePeriod, mWaveOffset, mWavePhase / 360, value);
        }
    }

    void MotionKeyCycle::dump() const {
        std::printf("MotionKeyCycle{mWaveShape=%d, mWavePeriod=%g, mWaveOffset=%g, mWavePhase=%g, mRotation=%g}\n", mWaveShape, double(mWavePeriod),
                    double(mWaveOffset), double(mWavePhase), double(mRotation));
    }

    void MotionKeyCycle::printAttributes() {
        JavaHashSet nameSet;
        getAttributeNames(nameSet);
        std::printf(" ------------- %d -------------\n", mFramePosition);
        std::printf("MotionKeyCycle{Shape=%d, Period=%g, Offset=%g, Phase=%g}\n", mWaveShape, double(mWavePeriod), double(mWaveOffset), double(mWavePhase));
        for (const std::string& name : nameSet.keys()) std::printf("%s:%g\n", name.c_str(), double(getValue(name)));
    }

}
