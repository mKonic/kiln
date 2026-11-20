// Ported from constraintlayout-core MotionControlTest.java (androidx). Expected values are upstream's.
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

TEST(MotionControl_simpleLinear) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        mw1.setBounds(0, 0, 30, 40);
        mw2.setBounds(500, 600, 530, 640);
        Motion motion(&mw1);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.setup(1000, 1000, 1, 1000000);

        for (float p = 0; p <= 1; p += 0.1) {
            motion.interpolate(res, p, 1000000 + (int) (p * 100), cache);

        }
        motion.interpolate(res, 0.5f, 1000000 + 1000, cache);
        CHECK_EQ((int) (0.5 + (mw1.getLeft() + mw2.getLeft()) / 2), res.getLeft());
        CHECK_EQ((int) (0.5 + (mw1.getRight() + mw2.getRight()) / 2), res.getRight());
        CHECK_EQ((int) (0.5 + (mw1.getTop() + mw2.getTop()) / 2), res.getTop());
        CHECK_EQ((int) (0.5 + (mw1.getBottom() + mw2.getBottom()) / 2), res.getBottom());
    }

TEST(MotionControl_archMode1) {
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

TEST(MotionControl_archMode2) {
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

TEST(MotionControl_archMode3) {
        MotionWidget mw1;
        MotionWidget mw2;
        MotionWidget res;
        KeyCache cache;
        auto keyPosition = std::make_shared<MotionKeyPosition>();
        mw1.setBounds(0, 0, 3, 4);
        mw2.setBounds(400, 400, 460, 480);
        keyPosition->setFramePosition(50);
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

        motion.interpolate(res, 0.7f, 1000000 + 1000, cache);
        std::string str = res.toString();

        CHECK_EQ("400, 288, 460, 328", str);
    }


// From constraintlayout-core MotionBenchmarkTest.java: setting up and sampling a motion with two
// key positions, a thousand times.
namespace {
    int setUpMotionController(bool arc) {
        MotionWidget mw1, mw2, res;
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
        if (arc) motion.setPathMotionArc(ArcCurveFit::ARC_START_HORIZONTAL);
        motion.setStart(mw1);
        motion.setEnd(mw2);
        motion.addKey(keyPosition);
        motion.addKey(keyPosition2);
        motion.setup(1000, 1000, 2, 1000000);
        motion.interpolate(res, 0.1f, 1000000 + int(0.5 * 100), cache);
        return res.getLeft();
    }
}

TEST(MotionBenchmark_motionController1000xSetup) {
    for (int i = 0; i < 1000; i++) CHECK_EQ(40, setUpMotionController(false));
}

TEST(MotionBenchmark_motionControllerArc1000xSetup) {
    for (int i = 0; i < 1000; i++) CHECK_EQ(60, setUpMotionController(true));
}
