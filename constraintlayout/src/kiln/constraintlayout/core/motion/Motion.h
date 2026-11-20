#pragma once

// Motion (constraintlayout-core motion): animates one widget between its start and end
// frames: the path (keyframe positions, arcs, easing), attribute splines, cycles, time cycles
// and custom attributes, evaluated at a progress by interpolate().

#include "kiln/constraintlayout/core/motion/MotionConstrainedPoint.h"
#include "kiln/constraintlayout/core/motion/MotionPaths.h"
#include "kiln/constraintlayout/core/motion/key/MotionKey.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyCycle.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyPosition.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyTimeCycle.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyTrigger.h"
#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"
#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;

    class Motion : public TypedValues {
    public:
        static constexpr int PATH_PERCENT = 0;
        static constexpr int PATH_PERPENDICULAR = 1;
        static constexpr int HORIZONTAL_PATH_X = 2;
        static constexpr int HORIZONTAL_PATH_Y = 3;
        static constexpr int VERTICAL_PATH_X = 4;
        static constexpr int VERTICAL_PATH_Y = 5;
        static constexpr int DRAW_PATH_NONE = 0;
        static constexpr int DRAW_PATH_BASIC = 1;
        static constexpr int DRAW_PATH_RELATIVE = 2;
        static constexpr int DRAW_PATH_CARTESIAN = 3;
        static constexpr int DRAW_PATH_AS_CONFIGURED = 4;
        static constexpr int DRAW_PATH_RECTANGLE = 5;
        static constexpr int DRAW_PATH_SCREEN = 6;
        static constexpr int ROTATION_RIGHT = 1;
        static constexpr int ROTATION_LEFT = 2;

        explicit Motion(MotionWidget* view) { setView(view); }

        int getTransformPivotTarget() const { return mTransformPivotTarget; }
        void setTransformPivotTarget(int transformPivotTarget) {
            mTransformPivotTarget = transformPivotTarget;
            mTransformPivotView = nullptr;
        }
        MotionPaths& getKeyFrame(int i) { return mMotionPaths[std::size_t(i)]; }
        float getStartX() const { return mStartMotionPath.mX; }
        float getStartY() const { return mStartMotionPath.mY; }
        float getFinalX() const { return mEndMotionPath.mX; }
        float getFinalY() const { return mEndMotionPath.mY; }
        float getStartWidth() const { return mStartMotionPath.mWidth; }
        float getStartHeight() const { return mStartMotionPath.mHeight; }
        float getFinalWidth() const { return mEndMotionPath.mWidth; }
        float getFinalHeight() const { return mEndMotionPath.mHeight; }
        const std::string& getAnimateRelativeTo() const { return mStartMotionPath.mAnimateRelativeTo; }
        void setupRelative(Motion* motionController) { mRelativeMotion = motionController; }
        float getCenterX() const { return mCurrentCenterX; }
        float getCenterY() const { return mCurrentCenterY; }
        void getCenter(double p, float* pos, float* vel);
        void buildPath(float* points, int pointCount);
        const std::vector<double>& getPos(double position);
        void buildBounds(float* bounds, int pointCount);
        MotionKeyPosition* getPositionKeyframe(int layoutWidth, int layoutHeight, float x, float y);
        int buildKeyFrames(float* keyFrames, int* mode, int* pos);
        int buildKeyBounds(float* keyBounds, int* mode);
        int getAttributeValues(const std::string& attributeType, float* points, int length);
        void buildRect(float p, float* path, int offset);
        void buildRectangles(float* path, int pointCount);
        float getKeyFrameParameter(int type, float x, float y) const;
        void addKeys(const std::vector<std::shared_ptr<MotionKey>>& list) { mKeyList.insert(mKeyList.end(), list.begin(), list.end()); }
        void addKey(std::shared_ptr<MotionKey> key) { mKeyList.push_back(std::move(key)); }
        void setPathMotionArc(int arc) { mPathMotionArc = arc; }
        void setup(int parentWidth, int parentHeight, float transitionDuration, int64_t currentTime);
        std::string toString() const;
        void setView(MotionWidget* view) { mView = view; }
        MotionWidget* getView() const { return mView; }
        void setStart(MotionWidget& mw);
        void setEnd(MotionWidget& mw);
        void setStartState(const ViewState& rect, MotionWidget& v, int rotation, int preWidth, int preHeight);
        void rotate(const Rect& rect, Rect& out, int rotation, int preHeight, int preWidth) const;
        void setBothStates(MotionWidget& v);
        void endTrigger(bool) {}
        bool interpolate(MotionWidget& child, float globalPosition, int64_t time, KeyCache* keyCache);
        bool interpolate(MotionWidget& child, float globalPosition, int64_t time, KeyCache& keyCache) {
            return interpolate(child, globalPosition, time, &keyCache);
        }
        void getDpDt(float position, float locationX, float locationY, float* anchorDpDt);
        void getPostLayoutDvDp(float position, int width, int height, float locationX, float locationY, float* anchorDpDt);
        int getDrawPath() const;
        void setDrawPath(int debugMode) { mStartMotionPath.mDrawPath = debugMode; }
        std::string name() const;
        void positionKeyframe(MotionWidget& view, MotionKeyPosition& key, float x, float y, std::string* attribute, float* value);
        int getKeyFramePositions(int* type, float* pos);
        int getKeyFrameInfo(int type, int* info);

        bool setValue(int id, int value) override;
        bool setValue(int id, float value) override;
        bool setValue(int id, const std::string& value) override;
        bool setValue(int, bool) override { return false; }
        int getId(const std::string&) override { return 0; }
        void setStaggerScale(float staggerScale) { mStaggerScale = staggerScale; }
        void setStaggerOffset(float staggerOffset) { mStaggerOffset = staggerOffset; }
        float getMotionStagger() const { return mMotionStagger; }
        void setIdString(const std::string& stringId) {
            mId = stringId;
            mStartMotionPath.mId = mId;
        }

        std::string mId;
        float mMotionStagger = NAN;
        float mStaggerOffset = 0;
        float mStaggerScale = 1.0f;
        float mCurrentCenterX = 0, mCurrentCenterY = 0;

    private:
        void setupRelative();
        float getPreCycleDistance();
        void insertKey(MotionPaths point);
        void readView(MotionPaths& motionPaths);
        float getAdjustedPosition(float position, float* velocity);
        // The easing of the keyframe span containing position, as buildPath and friends apply it.
        double easedPosition(float position) const;

        Rect mTempRect;
        MotionWidget* mView = nullptr;
        std::string mConstraintTag;
        int mCurveFitType = CurveFit::SPLINE;
        MotionPaths mStartMotionPath;
        MotionPaths mEndMotionPath;
        MotionConstrainedPoint mStartPoint;
        MotionConstrainedPoint mEndPoint;
        std::vector<std::shared_ptr<CurveFit>> mSpline;
        std::shared_ptr<CurveFit> mArcSpline;
        std::vector<int> mInterpolateVariables;
        std::vector<double> mInterpolateData;
        std::vector<double> mInterpolateVelocity;
        std::vector<std::string> mAttributeNames;
        std::vector<int> mAttributeInterpolatorCount;
        std::vector<float> mValuesBuff = std::vector<float>(4);
        std::vector<MotionPaths> mMotionPaths;
        float mVelocity[1] = { 0 };
        std::vector<std::shared_ptr<MotionKey>> mKeyList;
        std::unique_ptr<TimeCycleMap> mTimeCycleAttributesMap;
        std::unique_ptr<SplineMap> mAttributesMap;
        std::unique_ptr<KeyCycleOscillatorMap> mCycleMap;
        std::vector<MotionKeyTrigger*> mKeyTriggers;
        int mPathMotionArc = -1;
        int mTransformPivotTarget = -1;
        MotionWidget* mTransformPivotView = nullptr;
        int mQuantizeMotionSteps = -1;
        float mQuantizeMotionPhase = NAN;
        std::shared_ptr<DifferentialInterpolator> mQuantizeMotionInterpolator;
        bool mNoMovement = false;
        Motion* mRelativeMotion = nullptr;
        std::vector<std::string> mAttributeTable;
    };

}
