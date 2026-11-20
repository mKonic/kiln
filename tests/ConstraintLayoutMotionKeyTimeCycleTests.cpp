// Ported from constraintlayout-core MotionKeyTimeCycleTest.java (androidx). Expected values are upstream's.
#include "Check.h"
#include "ConstraintLayoutMotionSupport.h"

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyTimeCycle.h"
#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace kiln::constraintlayout::core::motion;

namespace {

    constexpr bool DISABLE = true;

    struct Scene {
        MotionWidget mMW1, mMW2, mRes;
        KeyCache mCache;
        Motion mMotion{ &mMW1 };
        Scene() {
            mMW1.setBounds(0, 0, 30, 40);
            mMW2.setBounds(400, 400, 430, 440);
            mMotion.setPathMotionArc(ArcCurveFit::ARC_START_VERTICAL);
        }
        void setup() {
            mMotion.setStart(mMW1);
            mMotion.setEnd(mMW2);
            mMotion.setup(1000, 1000, 1, 1000000);
        }
        void sample() {
            for (int p = 0; p <= 30; p++) mMotion.interpolate(mRes, p * 0.1f, 1000000 + int(p * 100), mCache);
        }
    };

    void cycleBuilder(Scene& s, int type) {
        const float amp[] = { 0, 50, 0 };
        const int pos[] = { 0, 50, 100 };
        const float period[] = { 0, 2, 0 };
        for (int i = 0; i < 3; i++) {
            auto cycle = std::make_shared<MotionKeyTimeCycle>();
            cycle->setValue(type, amp[i]);
            cycle->setValue(TypedValues::CycleType::TYPE_WAVE_PERIOD, period[i]);
            cycle->setFramePosition(pos[i]);
            s.mMotion.addKey(cycle);
        }
    }

    void basicRunThrough(Scene& s, int type) {
        cycleBuilder(s, type);
        s.setup();
        s.sample();
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
    }

}

TEST(MotionKeyTimeCycle_disabled) {
        if (DISABLE) {

        }
        CHECK_EQ(DISABLE, true);
    }

TEST(MotionKeyTimeCycle_keyCycleRotationX) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_ROTATION_X);
        CHECK_NEAR(0.0, s.mRes.getRotationX(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleRotationY) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_ROTATION_Y);
        CHECK_NEAR(0.0, s.mRes.getRotationY(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleRotationZ) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_ROTATION_Z);
        CHECK_NEAR(0.0, s.mRes.getRotationZ(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleTranslationX) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_TRANSLATION_X);
        CHECK_NEAR(0.0, s.mRes.getTranslationX(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleTranslationY) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_TRANSLATION_Y);
        CHECK_NEAR(0.0, s.mRes.getTranslationY(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleTranslationZ) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_TRANSLATION_Z);
        CHECK_NEAR(0.0, s.mRes.getTranslationZ(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleScaleX) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_SCALE_X);
        CHECK_NEAR(0.0, s.mRes.getScaleX(), 0.0001);
    }

TEST(MotionKeyTimeCycle_keyCycleScaleY) {
        if (DISABLE) {
            return;
        }
        Scene s; basicRunThrough(s, TypedValues::CycleType::TYPE_SCALE_Y);
        CHECK_NEAR(0.0, s.mRes.getScaleY(), 0.0001);
    }

