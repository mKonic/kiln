// Ported from constraintlayout-core MotionBasicTest.java and StopLogicTest.java (androidx).
// Expected values are upstream's.
#include "Check.h"
#include "ConstraintLayoutMotionSupport.h"

#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/Easing.h"
#include "kiln/constraintlayout/core/motion/utils/HyperSpline.h"
#include "kiln/constraintlayout/core/motion/utils/Oscillator.h"
#include "kiln/constraintlayout/core/motion/utils/SpringStopEngine.h"
#include "kiln/constraintlayout/core/motion/utils/StopLogicEngine.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace kiln::constraintlayout::core::motion;

namespace {

    int countZeroCrossings(Oscillator& o, int type) {
        const int n = 1000;
        double last = o.getValue(0, 0);
        int count = 0;
        o.setType(type, "");
        for (int i = 0; i < n; i++) {
            const double v = o.getValue(0.0001 + i / double(n), 0);
            if (v * last < 0) count++;
            last = v;
        }
        return count;
    }

    std::vector<float> stopGraph(float vel) {
        StopLogicEngine breakLogic;
        breakLogic.config(.4f, 1.0f, vel, 1, 2.0f, 0.9f);
        std::vector<float> ret(10);
        for (std::size_t i = 0; i < ret.size(); i++) {
            const float time = 2 * float(i) / float(ret.size() - 1);
            ret[i] = breakLogic.getInterpolation(time);
        }
        CHECK(breakLogic.isStopped());
        return ret;
    }

    std::string verify(StopEngine& stop, float position, float maxTime, bool expectStopped) {
        float p = stop.getInterpolation(0);
        CHECK_NEAR(p, position, 0.0001);
        const int count = 60;
        const float step = maxTime / (count - 1);
        std::vector<float> x(count), y(count);
        int c = 0;
        for (float t = 0; t < maxTime; t += step) {
            p = stop.getInterpolation(t);
            x[std::size_t(c)] = t;
            y[std::size_t(c)] = p;
            c++;
        }
        const std::string ret = cltest::TextDraw(count, count / 4, x, y, false);
        CHECK_EQ(expectStopped, stop.isStopped());
        return ret;
    }

}

TEST(MotionBasic_testHyperSpline01) {
    HyperSpline spline({ { 0, 0 }, { 1, 1 }, { 2, 2 } });
    CHECK_NEAR(1, spline.getPos(0.5, 1), 0.001);
}

TEST(MotionBasic_testCurveFit01) {
    auto spline = CurveFit::get(CurveFit::SPLINE, { 0, 5, 10 }, { { 0, 0 }, { 1, 1 }, { 2, 0 } });
    CHECK_NEAR(1, spline->getPos(5, 0), 0.001);
    CHECK_NEAR(1.4, spline->getPos(7, 0), 0.001);
    CHECK_NEAR(0.744, spline->getPos(7, 1), 0.001);
}

TEST(MotionBasic_testCurveFit02) {
    auto spline = CurveFit::get(CurveFit::LINEAR, { 0, 5, 10 }, { { 0, 0 }, { 1, 1 }, { 2, 0 } });
    CHECK_NEAR(1, spline->getPos(5, 0), 0.001);
    CHECK_NEAR(1.4, spline->getPos(7, 0), 0.001);
    CHECK_NEAR(0.6, spline->getPos(7, 1), 0.001);
}

TEST(MotionBasic_testEasing01) {
    auto easing = Easing::getInterpolator("cubic=(1,1,0,0)");
    CHECK_NEAR(0.5, easing->get(0.5), 0.001);
    CHECK_NEAR(1, easing->getDiff(0.5), 0.001);
    CHECK_NEAR(1, easing->getDiff(0.1), 0.001);
    CHECK_NEAR(1, easing->getDiff(0.9), 0.001);
    easing = Easing::getInterpolator("cubic=(1,0,0,1)");
    CHECK_NEAR(0.5, easing->get(0.5), 0.001);
    CHECK_NEAR(0, easing->getDiff(0.001), 0.001);
    CHECK_NEAR(0, easing->getDiff(0.9999), 0.001);
    easing = Easing::getInterpolator("cubic=(0.5,1,0.5,0)");
    CHECK_NEAR(0.5, easing->get(0.5), 0.001);
    CHECK_NEAR(0, easing->getDiff(0.5), 0.001);
    CHECK_NEAR(2, easing->getDiff(0.00001), 0.001);
    CHECK_NEAR(2, easing->getDiff(0.99999), 0.001);
}

TEST(MotionBasic_testLinearCurveFit01) {
    LinearCurveFit lcurve({ 0, 5, 10 }, { { 0, 0 }, { 1, 1 }, { 2, 0 } });
    CHECK_NEAR(1, lcurve.getPos(5, 0), 0.001);
    CHECK_NEAR(1.4, lcurve.getPos(7, 0), 0.001);
    CHECK_NEAR(0.6, lcurve.getPos(7, 1), 0.001);
}

TEST(MotionBasic_testOscillator01) {
    Oscillator o;
    o.setType(Oscillator::SQUARE_WAVE, "");
    o.addPoint(0, 0);
    o.addPoint(0.5, 10);
    o.addPoint(1, 0);
    o.normalize();
    CHECK_EQ(19, countZeroCrossings(o, Oscillator::SIN_WAVE));
    CHECK_EQ(19, countZeroCrossings(o, Oscillator::SQUARE_WAVE));
    CHECK_EQ(19, countZeroCrossings(o, Oscillator::TRIANGLE_WAVE));
    CHECK_EQ(19, countZeroCrossings(o, Oscillator::SAW_WAVE));
    CHECK_EQ(19, countZeroCrossings(o, Oscillator::REVERSE_SAW_WAVE));
    CHECK_EQ(20, countZeroCrossings(o, Oscillator::COS_WAVE));
}

TEST(MotionBasic_testOscillatorAtKeyframe) {
    Oscillator o;
    o.setType(Oscillator::SIN_WAVE, "");
    o.addPoint(0, 0);
    o.addPoint(0.2, 2);
    o.addPoint(0.5, 0);
    o.addPoint(0.7, 3);
    o.addPoint(1, 1);
    o.normalize();
    const double expectedAt20 = -0.7818315892114756;
    CHECK_NEAR(expectedAt20, o.getValue(0.2, 0), 0.00001);
    CHECK(std::abs(expectedAt20 - o.getValue(0.19999, 0)) < 0.001);
    CHECK(std::abs(expectedAt20 - o.getValue(0.20001, 0)) < 0.001);
}

TEST(MotionBasic_testStopLogic01) {
    const char* results[] = { "[0.4, 0.36, 0.42, 0.578, 0.778, 0.938, 0.999, 1, 1, 1]", "[0.4, 0.383, 0.464, 0.64, 0.838, 0.967, 1, 1, 1, 1]",
                              "[0.4, 0.405, 0.509, 0.697, 0.885, 0.986, 1, 1, 1, 1]", "[0.4, 0.427, 0.553, 0.75, 0.921, 0.997, 1, 1, 1, 1]",
                              "[0.4, 0.449, 0.598, 0.798, 0.948, 1, 1, 1, 1, 1]",     "[0.4, 0.472, 0.64, 0.838, 0.967, 1, 1, 1, 1, 1]",
                              "[0.4, 0.494, 0.678, 0.87, 0.981, 1, 1, 1, 1, 1]",      "[0.4, 0.516, 0.71, 0.894, 0.989, 1, 1, 1, 1, 1]",
                              "[0.4, 0.538, 0.737, 0.913, 0.995, 1, 1, 1, 1, 1]",     "[0.4, 0.56, 0.76, 0.927, 0.998, 1, 1, 1, 1, 1]" };
    for (int i = 0; i < 10; i++) CHECK_EQ(std::string(results[i]), cltest::ArrayToString(stopGraph(float(i - 4) * .1f)));
}

TEST(StopLogic_basicSpring) {
    SpringStopEngine stop;
    stop.springConfig(0.3f, 1.0f, 0.2f, 1.0f, 100.0f, 50.0f, 0.0f, 0);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|**                                                          |\n"
                               "|  ****                                                      | 0.316\n"
                               "|      ***                                                   |\n"
                               "|         ****                                               |\n"
                               "|             ****                                           |\n"
                               "|                 *****                                      |\n"
                               "|                      ******                                | 0.632\n"
                               "|                            *******                         |\n"
                               "|                                   **********               |\n"
                               "|                                             *************  |\n"
                               "|                                                           *| 0.885\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.3f, 0.9f, false));
}

TEST(StopLogic_cruseDecelerate) {
    StopLogicEngine stop;
    stop.config(0.9f, 1, 0.2f, 0.9f, 3.2f, 3.2f);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.357\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.714\n"
                               "|                                                            |\n"
                               "|**********                                                  |\n"
                               "|          *************************                         |\n"
                               "|                                   *********************** *| 1.0\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.9f, 0.9f, true));
}

TEST(StopLogic_backwardAccelerateDecelerate) {
    StopLogicEngine stop;
    stop.config(0.9f, 1, -0.2f, 0.9f, 3.2f, 3.2f);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.357\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.714\n"
                               "|                                                            |\n"
                               "|**************                                              |\n"
                               "|              **************                                |\n"
                               "|                            ****************************** *| 1.0\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.9f, 0.9f, true));
}

TEST(StopLogic_hardStop) {
    StopLogicEngine stop;
    stop.config(0.9f, 1, 1.8f, 0.9f, 3.2f, 3.2f);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.357\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.714\n"
                               "|                                                            |\n"
                               "|**                                                          |\n"
                               "|  ******                                                    |\n"
                               "|        ************************************************** *| 1.0\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.9f, 0.9f, true));
}

TEST(StopLogic_accelerateCruseDecelerate) {
    StopLogicEngine stop;
    stop.config(0.3f, 1, 0.1f, 0.9f, 3.2f, 1.2f);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|***********                                                 |\n"
                               "|           ******                                           | 0.356\n"
                               "|                 *****                                      |\n"
                               "|                      ****                                  |\n"
                               "|                          ***                               |\n"
                               "|                             ****                           |\n"
                               "|                                 ****                       | 0.712\n"
                               "|                                     *****                  |\n"
                               "|                                          *****             |\n"
                               "|                                               ***********  |\n"
                               "|                                                           *| 0.997\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.3f, 0.9f, false));
}

TEST(StopLogic_accelerateDecelerate) {
    StopLogicEngine stop;
    stop.config(0.3f, 1, 0.2f, 0.9f, 3.2f, 3.2f);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|*********                                                   |\n"
                               "|         ******                                             | 0.357\n"
                               "|               *****                                        |\n"
                               "|                    ****                                    |\n"
                               "|                        ***                                 |\n"
                               "|                           ***                              |\n"
                               "|                              ****                          | 0.714\n"
                               "|                                  ****                      |\n"
                               "|                                      ******                |\n"
                               "|                                            **************  |\n"
                               "|                                                           *| 1.0\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.3f, 0.9f, true));
}

TEST(StopLogic_backwardAccelerateCruseDecelerate) {
    StopLogicEngine stop;
    stop.config(0.5f, 1, -0.6f, 0.9f, 5.2f, 1.2f);
    const std::string expect = "|*                                                           | 0.0\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            |\n"
                               "|                                                            | 0.357\n"
                               "| ***************                                            |\n"
                               "|*               *****                                       |\n"
                               "|                     ****                                   |\n"
                               "|                         ****                               |\n"
                               "|                             ****                           | 0.714\n"
                               "|                                 ****                       |\n"
                               "|                                     *****                  |\n"
                               "|                                          **********        |\n"
                               "|                                                    ****** *| 1.0\n"
                               "0.0                                                      0.885\n";
    CHECK_EQ(expect, verify(stop, 0.5f, 0.9f, true));
}
