#pragma once

// MotionPaths (constraintlayout-core motion): one point of a widget's path: position (0..1),
// bounds, easing and arc mode, from the start, the end or a KeyPosition. Points sort by
// position.

#include "kiln/constraintlayout/core/motion/CustomVariable.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyPosition.h"
#include "kiln/constraintlayout/core/motion/utils/Easing.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class Motion;
    class MotionWidget;

    class MotionPaths {
    public:
        static constexpr bool DEBUG = false;
        static constexpr bool OLD_WAY = false;
        static constexpr int OFF_POSITION = 0;
        static constexpr int OFF_X = 1;
        static constexpr int OFF_Y = 2;
        static constexpr int OFF_WIDTH = 3;
        static constexpr int OFF_HEIGHT = 4;
        static constexpr int OFF_PATH_ROTATE = 5;
        static constexpr int PERPENDICULAR = MotionKeyPosition::TYPE_PATH;
        static constexpr int CARTESIAN = MotionKeyPosition::TYPE_CARTESIAN;
        static constexpr int SCREEN = MotionKeyPosition::TYPE_SCREEN;

        MotionPaths() = default;
        MotionPaths(int parentWidth, int parentHeight, const MotionKeyPosition& c, const MotionPaths& startTimePoint, const MotionPaths& endTimePoint);

        void initCartesian(const MotionKeyPosition& c, const MotionPaths& startTimePoint, const MotionPaths& endTimePoint);
        void initPolar(int parentWidth, int parentHeight, const MotionKeyPosition& c, const MotionPaths& s, const MotionPaths& e);
        void setupRelative(Motion* mc, const MotionPaths& relative);
        void initScreen(int parentWidth, int parentHeight, const MotionKeyPosition& c, const MotionPaths& startTimePoint, const MotionPaths& endTimePoint);
        void initPath(const MotionKeyPosition& c, const MotionPaths& startTimePoint, const MotionPaths& endTimePoint);
        void different(const MotionPaths& points, bool* mask, const std::vector<std::string>& custom, bool arcMode) const;
        void getCenter(double p, const std::vector<int>& toUse, const double* data, float* point, int offset) const;
        void getCenter(double p, const std::vector<int>& toUse, const double* data, float* point, const double* vdata, float* velocity) const;
        void getCenterVelocity(double p, const std::vector<int>& toUse, const double* data, float* point, int offset) const;
        void getBounds(const std::vector<int>& toUse, const double* data, float* point, int offset) const;
        void setView(float position, MotionWidget& view, const std::vector<int>& toUse, const double* data, std::vector<double>& slope,
                     const double* cycle);
        void getRect(const std::vector<int>& toUse, const double* data, float* path, int offset) const;
        void setDpDt(float locationX, float locationY, float* anchorDpDt, const std::vector<int>& toUse, const double* deltaData, const double* data) const;
        void fillStandard(double* data, const std::vector<int>& toUse) const;
        bool hasCustomData(const std::string& name) const { return mCustomAttributes.contains(name); }
        int getCustomDataCount(const std::string& name) const;
        int getCustomData(const std::string& name, double* value, int offset) const;
        void setBounds(float x, float y, float w, float h) {
            mX = x;
            mY = y;
            mWidth = w;
            mHeight = h;
        }
        int compareTo(const MotionPaths& o) const { return mPosition < o.mPosition ? -1 : mPosition > o.mPosition ? 1 : 0; }
        void applyParameters(MotionWidget& c);
        void configureRelativeTo(Motion& toOrbit);

        std::string mId;
        std::shared_ptr<Easing> mKeyFrameEasing;
        int mDrawPath = 0;
        float mTime = 0;
        float mPosition = 0;
        float mX = 0;
        float mY = 0;
        float mWidth = 0;
        float mHeight = 0;
        float mPathRotate = NAN;
        float mProgress = NAN;
        int mPathMotionArc = -1;
        std::string mAnimateRelativeTo;   // empty: Java's null
        float mRelativeAngle = NAN;
        Motion* mRelativeToController = nullptr;
        JavaHashMap<CustomVariable> mCustomAttributes;
        int mMode = 0;
        int mAnimateCircleAngleTo = 0;

    private:
        std::vector<double> mTempValue = std::vector<double>(18);
        std::vector<double> mTempDelta = std::vector<double>(18);
    };

}
