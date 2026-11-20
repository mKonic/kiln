#pragma once

// MotionKeyTimeCycle (constraintlayout-core motion key): an oscillation of attributes in
// time (not progress), from a frame position.

#include "kiln/constraintlayout/core/motion/key/MotionKey.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class TimeCycleSplineSet;
    using TimeCycleMap = JavaHashMap<std::shared_ptr<TimeCycleSplineSet>>;

    class MotionKeyTimeCycle : public MotionKey {
    public:
        static constexpr const char* NAME = "KeyTimeCycle";
        static constexpr int KEY_TYPE = 3;

        MotionKeyTimeCycle() { mType = KEY_TYPE; }
        void addTimeValues(TimeCycleMap& splines);
        void addValues(SplineMap&) override {}
        bool setValue(int type, int value) override;
        bool setValue(int type, float value) override;
        bool setValue(int type, const std::string& value) override;
        bool setValue(int type, bool value) override { return MotionKey::setValue(type, value); }
        MotionKey& copy(const MotionKey& src) override;
        void getAttributeNames(JavaHashSet& attributes) override;
        std::shared_ptr<MotionKey> clone() const override {
            auto k = std::make_shared<MotionKeyTimeCycle>();
            k->copy(*this);
            return k;
        }
        int getId(const std::string& name) override { return CycleType::getId(name); }

    private:
        std::string mTransitionEasing;
        int mCurveFit = -1;
        float mAlpha = NAN;
        float mElevation = NAN;
        float mRotation = NAN;
        float mRotationX = NAN;
        float mRotationY = NAN;
        float mTransitionPathRotate = NAN;
        float mScaleX = NAN;
        float mScaleY = NAN;
        float mTranslationX = NAN;
        float mTranslationY = NAN;
        float mTranslationZ = NAN;
        float mProgress = NAN;
        int mWaveShape = 0;
        std::string mCustomWaveShape;
        float mWavePeriod = NAN;
        float mWaveOffset = 0;
    };

}
