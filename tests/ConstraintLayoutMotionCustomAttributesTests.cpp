// Ported from constraintlayout-core MotionCustomAttributesTest.java (androidx). Expected values are upstream's.
#include "Check.h"
#include "ConstraintLayoutMotionSupport.h"

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
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
        void setup() {
            mMotion.setStart(mMW1);
            mMotion.setEnd(mMW2);
            mMotion.setup(0, 0, 1, 1000000);
        }
    };

}

TEST(MotionCustomAttributes_customFloat) {
        Scene s;
        s.mMW1.setCustomAttribute("bob", TypedValues::Custom::TYPE_FLOAT, 0.0f);
        s.mMW2.setCustomAttribute("bob", TypedValues::Custom::TYPE_FLOAT, 1.0f);
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_NEAR(0.5, s.mRes.getCustomAttribute("bob")->getFloatValue(), 0.001);
    }

TEST(MotionCustomAttributes_customColor1) {
        Scene s;
        s.mMW1.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFF00FF00u));
        s.mMW2.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFFFF00FFu));
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_EQ(int(0xffbababau), s.mRes.getCustomAttribute("fish")->getColorValue());
    }

TEST(MotionCustomAttributes_customColor2) {
        Scene s;
        s.mMW1.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0xFF000000u));
        s.mMW2.setCustomAttribute("fish", TypedValues::Custom::TYPE_COLOR, int(0x00880088u));
        s.setup();
        
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + 1000, s.mCache);
        CHECK_EQ(int(0x7f630063u), s.mRes.getCustomAttribute("fish")->getColorValue());
    }

