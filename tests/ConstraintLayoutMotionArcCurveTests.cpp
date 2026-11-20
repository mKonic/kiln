// Ported from constraintlayout-core MotionArcCurveTest.java (androidx). Expected values are upstream's.
#include "Check.h"
#include "ConstraintLayoutMotionSupport.h"

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyFrameArray.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

using namespace kiln::constraintlayout::core::motion;

TEST(MotionArcCurve_arcTest1) {
        std::vector<std::vector<double>> points = {
                {0, 0}, {1, 1}, {2, 0}
        };
        std::vector<double> time = {
                0, 5, 10
        };
        std::vector<int> mode = {
                ArcCurveFit::ARC_START_VERTICAL,
                ArcCurveFit::ARC_START_HORIZONTAL,
        };
        auto spline = CurveFit::getArc(mode, time, points);

        for (int i = 0; i < int(time.size()); i++) {
            CHECK_NEAR(points[i][0], spline->getPos(time[i], 0), 0.001);
            CHECK_NEAR(points[i][1], spline->getPos(time[i], 1), 0.001);
        }
        CHECK_NEAR(0, spline->getSlope(time[0] + 0.01, 0), 0.001);
        CHECK_NEAR(0, spline->getSlope(time[1] - 0.01, 1), 0.001);
        CHECK_NEAR(0, spline->getSlope(time[1] + 0.01, 1), 0.001);
        double dx = spline->getSlope((time[0] + time[1]) / 2, 0);
        double dy = spline->getSlope((time[0] + time[1]) / 2, 1);
        CHECK_NEAR(1, dx / dy, 0.001);
        double x = spline->getPos((time[0] + time[1]) / 2, 0);
        double y = spline->getPos((time[0] + time[1]) / 2, 1);
        CHECK_NEAR(1 - std::sqrt(0.5), x, 0.001);
        CHECK_NEAR(std::sqrt(0.5), y, 0.001);
    }

TEST(MotionArcCurve_arcTest2) {
        std::vector<std::vector<double>> points = {
                {0, 0}, {1, 1}, {2, 0}
        };
        std::vector<double> time = {
                0, 5, 10
        };
        std::vector<int> mode = {
                ArcCurveFit::ARC_BELOW,
                ArcCurveFit::ARC_BELOW,
        };
        auto spline = CurveFit::getArc(mode, time, points);

        for (int i = 0; i < int(time.size()); i++) {
            CHECK_NEAR(points[i][0], spline->getPos(time[i], 0), 0.001);
            CHECK_NEAR(points[i][1], spline->getPos(time[i], 1), 0.001);
        }
        CHECK_NEAR(0, spline->getSlope(time[0] + 0.01, 0), 0.001);
        CHECK_NEAR(0, spline->getSlope(time[1] - 0.01, 1), 0.001);
        CHECK_NEAR(0, spline->getSlope(time[1] + 0.01, 1), 0.001);
        double dx = spline->getSlope((time[0] + time[1]) / 2, 0);
        double dy = spline->getSlope((time[0] + time[1]) / 2, 1);
        CHECK_NEAR(1, dx / dy, 0.001);
        double x = spline->getPos((time[0] + time[1]) / 2, 0);
        double y = spline->getPos((time[0] + time[1]) / 2, 1);
        CHECK_NEAR(1 - std::sqrt(0.5), x, 0.001);
        CHECK_NEAR(std::sqrt(0.5), y, 0.001);
    }

TEST(MotionArcCurve_arcTest3) {
        std::vector<std::vector<double>> points = {
                {0, 0}, {1, 1}, {2, 0}
        };
        std::vector<double> time = {
                0, 5, 10
        };
        std::vector<int> mode = {
                ArcCurveFit::ARC_ABOVE,
                ArcCurveFit::ARC_ABOVE,
        };
        auto spline = CurveFit::getArc(mode, time, points);

        for (int i = 0; i < int(time.size()); i++) {
            CHECK_NEAR(points[i][0], spline->getPos(time[i], 0), 0.001);
            CHECK_NEAR(points[i][1], spline->getPos(time[i], 1), 0.001);
        }
        int count = 50;
        float dt = (float) (time[int(time.size()) - 1] - time[0]) / count - 0.0001f;
        std::vector<float> xp(count);
        std::vector<float> yp(count);
        for (int i = 0; i < int(xp.size()); i++) {
            double p = time[0] + i * dt;
            xp[i] = (float) spline->getPos(p, 0);
            yp[i] = (float) spline->getPos(p, 1);
        }
        std::string expect =
                "|*****                    *****| 0.0\n"
                "|     **                **     |\n"
                "|       **            **       |\n"
                "|        *            *        |\n"
                "|         *          *         |\n"
                "|          *        *          | 0.263\n"
                "|           *      **          |\n"
                "|            *    *            |\n"
                "|            *    *            |\n"
                "|             *  *             |\n"
                "|             *  *             | 0.526\n"
                "|                              |\n"
                "|             *  *             |\n"
                "|              **              |\n"
                "|              **              |\n"
                "|              **              | 0.789\n"
                "|              **              |\n"
                "|              **              |\n"
                "|                              |\n"
                "|              *               | 0.999\n"
                "0.0                        1.936\n";
        CHECK_EQ(expect, cltest::TextDraw(30, 20, xp, yp, false));
        CHECK_NEAR(0, spline->getSlope(time[0] + 0.0001, 1), 0.001);
        CHECK_NEAR(0, spline->getSlope(time[1] - 0.01, 0), 0.001);
        CHECK_NEAR(0, spline->getSlope(time[1] + 0.01, 0), 0.001);
        double dx = spline->getSlope((time[0] + time[1]) / 2, 0);
        double dy = spline->getSlope((time[0] + time[1]) / 2, 1);
        CHECK_NEAR(1, dx / dy, 0.001);
        double x = spline->getPos((time[0] + time[1]) / 2, 1);
        double y = spline->getPos((time[0] + time[1]) / 2, 0);
        CHECK_NEAR(1 - std::sqrt(0.5), x, 0.001);
        CHECK_NEAR(std::sqrt(0.5), y, 0.001);
    }


// From constraintlayout-core MotionKeyFrameArray.java: null values become empty optionals.
TEST(MotionKeyFrameArray_arcTest1) {
    KeyFrameSlots<std::optional<CustomAttribute>> array;
    for (int i = 0; i < 32; i++) {
        CHECK_EQ(i, array.size());
        array.append(i, std::nullopt);
    }
    for (int i = 0; i < array.size(); i++) CHECK(!array.valueAt(i).has_value());
    array.clear();
    uint32_t seed = 1;
    for (int i = 0; i < 32; i++) {
        seed = seed * 1103515245u + 12345u;
        const int k = int((seed >> 16) % 100);
        array.append(k, CustomAttribute("foo", CustomAttribute::AttributeType::INT_TYPE));
    }
    CHECK(array.size() <= 32);
}
