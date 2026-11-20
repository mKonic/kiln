// Ported from constraintlayout-core MotionKeyPositionTest.java (androidx). Expected values are upstream's.
#include "Check.h"
#include "ConstraintLayoutMotionSupport.h"

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyPosition.h"
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
        float mProgress = 0;
        Scene() {
            mMW1.setBounds(0, 0, 30, 40);
            mMW2.setBounds(400, 400, 430, 440);
        }
        void setup() {
            mMotion.setStart(mMW1);
            mMotion.setEnd(mMW2);
            mMotion.setup(1000, 1000, 1, 1000000);
        }
    };

}

TEST(MotionKeyPosition_keyPosition1) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        mw1.setBounds(0, 0, 30, 40);
        mw2.setBounds(400, 400, 430, 440);
        Motion motion(&mw1);
        motion.setPathMotionArc(ArcCurveFit::ARC_START_VERTICAL);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.setup(1000, 1000, 1, 1000000);
        
        motion.interpolate(res, 0.5f, 1000000 + 1000, cache);
        int left = (int) (0.5 + 400 * (1 - std::sqrt(0.5)));
        int top = (int) (0.5 + 400 * (std::sqrt(0.5)));
        CHECK_EQ(left, res.getLeft());
        CHECK_EQ(147, res.getRight());
        CHECK_NEAR(top, res.getTop(), 0.01);
        CHECK_EQ(top + 40, res.getBottom());
    }

TEST(MotionKeyPosition_keyPosition2) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        mw1.setBounds(0, 0, 30, 40);
        mw2.setBounds(400, 400, 430, 440);
        Motion motion(&mw1);
        motion.setPathMotionArc(ArcCurveFit::ARC_START_HORIZONTAL);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.setup(1000, 1000, 2, 1000000);
        motion.interpolate(res, 0.5f, 1000000 + (int) (0.5 * 100), cache);

        motion.interpolate(res, 0.5f, 1000000 + 1000, cache);
        CHECK_EQ(283, res.getLeft());
        CHECK_EQ(313, res.getRight());
        CHECK_EQ(117, res.getTop());
        CHECK_EQ(157, res.getBottom());
    }

TEST(MotionKeyPosition_keyPosition3) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        mw1.setBounds(0, 0, 30, 40);
        mw2.setBounds(400, 400, 460, 480);
        auto keyPosition = std::make_shared<MotionKeyPosition>();
        keyPosition->setFramePosition(30);
        keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_X, 0.3f);
        keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_Y, 0.3f);
        auto keyPosition2 = std::make_shared<MotionKeyPosition>();
        keyPosition2->setFramePosition(88);
        keyPosition2->setValue(TypedValues::PositionType::TYPE_PERCENT_X, .9f);
        keyPosition2->setValue(TypedValues::PositionType::TYPE_PERCENT_Y, 0.5f);
        Motion motion(&mw1);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.addKey(keyPosition);
        motion.addKey(keyPosition2);
        motion.setup(1000, 1000, 2, 1000000);
        motion.interpolate(res, 0.5f, 1000000 + (int) (0.5 * 100), cache);

        motion.interpolate(res, 0.0f, 1000000 + 1000, cache);
        CHECK_EQ("0, 0, 30, 40", res.toString());
        motion.interpolate(res, 0.2f, 1000000 + 1000, cache);
        CHECK_EQ("80, 86, 116, 134", res.toString());
        motion.interpolate(res, 0.3f, 1000000 + 1000, cache);
        CHECK_EQ("120, 120, 159, 172", res.toString());
        motion.interpolate(res, 0.5f, 1000000 + 1000, cache);
        CHECK_EQ("204, 120, 249, 180", res.toString());
        motion.interpolate(res, 0.7f, 1000000 + 1000, cache);
        CHECK_EQ("289, 106, 339, 174", res.toString());
        motion.interpolate(res, 0.9f, 1000000 + 1000, cache);
        CHECK_EQ("367, 215, 424, 291", res.toString());
        motion.interpolate(res, 1.0f, 1000000 + 1000, cache);
        CHECK_EQ("400, 400, 460, 480", res.toString());
    }

TEST(MotionKeyPosition_keyPosition4) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        auto keyPosition = std::make_shared<MotionKeyPosition>();
        mw1.setBounds(0, 0, 30, 40);
        mw2.setBounds(400, 400, 460, 480);
        keyPosition->setFramePosition(20);
        keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_X, 1.0f);
        keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_Y, 0.5f);
        keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_HEIGHT, 0.2f);
        keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_WIDTH, 1.0f);
        Motion motion(&mw1);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.addKey(keyPosition);
        motion.setup(1000, 1000, 2, 1000000);
        motion.interpolate(res, 0.5f, 1000000 + (int) (0.5 * 100), cache);

        motion.interpolate(res, 0.5f, 1000000 + 1000, cache);
        CHECK_EQ("400, 325, 460, 385", res.toString());
    }

TEST(MotionKeyPosition_keyPosition3x) {
        Scene s;
        KeyCache cache;
        std::vector<int> frames = {25, 50, 75};
        std::vector<float> percentX = {0.1f, 0.8f, 0.1f};
        std::vector<float> percentY = {0.4f, 0.8f, 0.0f};
        for (int i = 0; i < int(frames.size()); i++) {
            auto keyPosition = std::make_shared<MotionKeyPosition>();
            keyPosition->setFramePosition(frames[i]);
            keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_X, percentX[i]);
            keyPosition->setValue(TypedValues::PositionType::TYPE_PERCENT_Y, percentY[i]);
            s.mMotion.addKey(keyPosition);
        }
        s.setup();
        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + (int) (0.5 * 100), cache);

        s.mMotion.interpolate(s.mRes, 0.5f, 1000000 + (int) (0.5 * 100), cache);

        CHECK_EQ("320, 320, 350, 360", s.mRes.toString());
    }

