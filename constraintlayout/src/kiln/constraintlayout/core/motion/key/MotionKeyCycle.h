#pragma once

// MotionKeyCycle (constraintlayout-core motion key): an oscillation (wave shape, period,
// offset, phase) of attributes around their interpolated values, from a frame position.

#include "kiln/constraintlayout/core/motion/key/MotionKey.h"
#include "kiln/constraintlayout/core/motion/utils/Oscillator.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class KeyCycleOscillator;
    using KeyCycleOscillatorMap = JavaHashMap<std::shared_ptr<KeyCycleOscillator>>;

    class MotionKeyCycle : public MotionKey {
    public:
        static constexpr const char* NAME = "KeyCycle";
        static constexpr const char* WAVE_PERIOD = "wavePeriod";
        static constexpr const char* WAVE_OFFSET = "waveOffset";
        static constexpr const char* WAVE_PHASE = "wavePhase";
        static constexpr const char* WAVE_SHAPE = "waveShape";
        static constexpr int SHAPE_SIN_WAVE = Oscillator::SIN_WAVE;
        static constexpr int SHAPE_SQUARE_WAVE = Oscillator::SQUARE_WAVE;
        static constexpr int SHAPE_TRIANGLE_WAVE = Oscillator::TRIANGLE_WAVE;
        static constexpr int SHAPE_SAW_WAVE = Oscillator::SAW_WAVE;
        static constexpr int SHAPE_REVERSE_SAW_WAVE = Oscillator::REVERSE_SAW_WAVE;
        static constexpr int SHAPE_COS_WAVE = Oscillator::COS_WAVE;
        static constexpr int SHAPE_BOUNCE = Oscillator::BOUNCE;
        static constexpr int KEY_TYPE = 4;

        MotionKeyCycle() { mType = KEY_TYPE; }
        void getAttributeNames(JavaHashSet& attributes) override;
        void addValues(SplineMap&) override {}
        bool setValue(int type, int value) override;
        bool setValue(int type, const std::string& value) override;
        bool setValue(int type, float value) override;
        bool setValue(int type, bool value) override { return MotionKey::setValue(type, value); }
        float getValue(const std::string& key) const;
        std::shared_ptr<MotionKey> clone() const override { return nullptr; }
        int getId(const std::string& name) override;
        void addCycleValues(KeyCycleOscillatorMap& oscSet);
        void dump() const;
        void printAttributes();

    private:
        std::string mTransitionEasing;
        int mCurveFit = 0;
        int mWaveShape = -1;
        std::string mCustomWaveShape;   // empty: Java's null
        float mWavePeriod = NAN;
        float mWaveOffset = 0;
        float mWavePhase = 0;
        float mProgress = NAN;
        float mAlpha = NAN;
        float mElevation = NAN;
        float mRotation = NAN;
        float mTransitionPathRotate = NAN;
        float mRotationX = NAN;
        float mRotationY = NAN;
        float mScaleX = NAN;
        float mScaleY = NAN;
        float mTranslationX = NAN;
        float mTranslationY = NAN;
        float mTranslationZ = NAN;
    };

}
