// Ported from constraintlayout-core MotionCustomKeyAttributesTest.java (androidx). Expected values are upstream's.
#include "Check.h"
#include "ConstraintLayoutMotionSupport.h"

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyAttributes.h"
#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace kiln::constraintlayout::core::motion;

namespace {

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
    };

}

TEST(MotionCustomKeyAttributes_customFloat) {
        Scene s;
        s.mMW1.setCustomAttribute("bob", TypedValues::Custom::TYPE_FLOAT, 0.0f);
        s.mMW2.setCustomAttribute("bob", TypedValues::Custom::TYPE_FLOAT, 1.0f);
        auto mka = std::make_shared<MotionKeyAttributes>();
        mka->setFramePosition(50);
        mka->setCustomAttribute("bob", TypedValues::Custom::TYPE_FLOAT, 2.0f);
        s.mMotion.addKey(mka);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(2, s.mRes.getCustomAttribute("bob")->getFloatValue(), 0.001);
    }

TEST(MotionCustomKeyAttributes_customColor1) {
        Scene s;
        s.mMW1.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFF000000u));
        s.mMW2.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFFFFFFFFu));
        auto mka = std::make_shared<MotionKeyAttributes>();
        mka->setFramePosition(50);
        mka->setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFF000000u));
        s.mMotion.addKey(mka);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_EQ(int(0xFF000000u), s.mRes.getCustomAttribute("fish")->getColorValue());
    }

TEST(MotionCustomKeyAttributes_customColor2) {
        Scene s;
        s.mMW1.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFFFF0000u));
        s.mMW2.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFF0000FFu));
        auto mka = std::make_shared<MotionKeyAttributes>();
        mka->setFramePosition(50);
        mka->setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFF00FF00u));
        s.mMotion.addKey(mka);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_EQ(int(0xFF00FF00u), s.mRes.getCustomAttribute("fish")->getColorValue());
    }

