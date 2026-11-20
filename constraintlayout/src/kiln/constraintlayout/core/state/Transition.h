#pragma once

// Transition (constraintlayout-core state): a MotionLayout transition between two solved
// constraint sets: per widget its start, end and interpolated frames and a Motion; the
// transition's easing, stagger, keyframes and OnSwipe handling (drag to progress, touch-up
// animation by stop logic or spring).

#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/utils/Easing.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCache.h"
#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"
#include "kiln/constraintlayout/core/motion/utils/TypedBundle.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"
#include "kiln/constraintlayout/core/state/WidgetFrame.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core {
    class ConstraintWidget;
    class ConstraintWidgetContainer;
}

namespace kiln::constraintlayout::core::state {

    // CorePixelDp: dp to pixels. Interpolator: progress to eased progress.
    using CorePixelDp = std::function<float(float)>;
    using Interpolator = std::function<float(float)>;

    class Transition : public motion::TypedValues {
    public:
        static constexpr int START = 0;
        static constexpr int END = 1;
        static constexpr int INTERPOLATED = 2;
        static constexpr int EASE_IN_OUT = 0;
        static constexpr int EASE_IN = 1;
        static constexpr int EASE_OUT = 2;
        static constexpr int LINEAR = 3;
        static constexpr int BOUNCE = 4;
        static constexpr int OVERSHOOT = 5;
        static constexpr int ANTICIPATE = 6;

        class OnSwipe {
        public:
            static constexpr int ANCHOR_SIDE_TOP = 0;
            static constexpr int ANCHOR_SIDE_LEFT = 1;
            static constexpr int ANCHOR_SIDE_RIGHT = 2;
            static constexpr int ANCHOR_SIDE_BOTTOM = 3;
            static constexpr int ANCHOR_SIDE_MIDDLE = 4;
            static constexpr int ANCHOR_SIDE_START = 5;
            static constexpr int ANCHOR_SIDE_END = 6;
            static inline const std::vector<std::string> SIDES = { "top", "left", "right", "bottom", "middle", "start", "end" };
            static constexpr int DRAG_UP = 0;
            static constexpr int DRAG_DOWN = 1;
            static constexpr int DRAG_LEFT = 2;
            static constexpr int DRAG_RIGHT = 3;
            static constexpr int DRAG_START = 4;
            static constexpr int DRAG_END = 5;
            static constexpr int DRAG_CLOCKWISE = 6;
            static constexpr int DRAG_ANTICLOCKWISE = 7;
            static inline const std::vector<std::string> DIRECTIONS = { "up", "down", "left", "right", "start", "end", "clockwise", "anticlockwise" };
            static constexpr int MODE_CONTINUOUS_VELOCITY = 0;
            static constexpr int MODE_SPRING = 1;
            static inline const std::vector<std::string> MODE = { "velocity", "spring" };
            static constexpr int ON_UP_AUTOCOMPLETE = 0;
            static constexpr int ON_UP_AUTOCOMPLETE_TO_START = 1;
            static constexpr int ON_UP_AUTOCOMPLETE_TO_END = 2;
            static constexpr int ON_UP_STOP = 3;
            static constexpr int ON_UP_DECELERATE = 4;
            static constexpr int ON_UP_DECELERATE_AND_COMPLETE = 5;
            static constexpr int ON_UP_NEVER_COMPLETE_TO_START = 6;
            static constexpr int ON_UP_NEVER_COMPLETE_TO_END = 7;
            static inline const std::vector<std::string> TOUCH_UP = { "autocomplete", "toStart", "toEnd", "stop", "decelerate", "decelerateComplete",
                                                                      "neverCompleteStart", "neverCompleteEnd" };
            static constexpr int BOUNDARY_OVERSHOOT = 0;
            static constexpr int BOUNDARY_BOUNCE_START = 1;
            static constexpr int BOUNDARY_BOUNCE_END = 2;
            static constexpr int BOUNDARY_BOUNCE_BOTH = 3;
            static inline const std::vector<std::string> BOUNDARY = { "overshoot", "bounceStart", "bounceEnd", "bounceBoth" };

            float getScale() const { return mDragScale; }
            const float* getDirection() const;
            const float* getSide() const;
            void setAnchorId(std::optional<std::string> anchorId) { mAnchorId = std::move(anchorId); }
            void setAnchorSide(int anchorSide) { mAnchorSide = anchorSide; }
            void setRotationCenterId(std::optional<std::string> rotationCenterId) { mRotationCenterId = std::move(rotationCenterId); }
            void setLimitBoundsTo(std::optional<std::string> limitBoundsTo) { mLimitBoundsTo = std::move(limitBoundsTo); }
            void setDragDirection(int dragDirection) {
                mDragDirection = dragDirection;
                mDragVertical = mDragDirection < 2;
            }
            void setDragScale(float dragScale) {
                if (!std::isnan(dragScale)) mDragScale = dragScale;
            }
            void setDragThreshold(float dragThreshold) {
                if (!std::isnan(dragThreshold)) mDragThreshold = dragThreshold;
            }
            void setAutoCompleteMode(int autoCompleteMode) { mAutoCompleteMode = autoCompleteMode; }
            void setMaxVelocity(float maxVelocity) {
                if (!std::isnan(maxVelocity)) mMaxVelocity = maxVelocity;
            }
            void setMaxAcceleration(float maxAcceleration) {
                if (!std::isnan(maxAcceleration)) mMaxAcceleration = maxAcceleration;
            }
            void setOnTouchUp(int onTouchUp) { mOnTouchUp = onTouchUp; }
            void setSpringMass(float v) {
                if (!std::isnan(v)) mSpringMass = v;
            }
            void setSpringStiffness(float v) {
                if (!std::isnan(v)) mSpringStiffness = v;
            }
            void setSpringDamping(float v) {
                if (!std::isnan(v)) mSpringDamping = v;
            }
            void setSpringStopThreshold(float v) {
                if (!std::isnan(v)) mSpringStopThreshold = v;
            }
            void setSpringBoundary(int springBoundary) { mSpringBoundary = springBoundary; }
            float getDestinationPosition(float currentPosition, float velocity, float duration) const;
            void config(float position, float velocity, int64_t start, float duration);
            float getTouchUpProgress(int64_t currentTime);
            void printInfo() const;
            bool isNotDone(float progress) const;

            std::optional<std::string> mAnchorId;
            std::optional<std::string> mLimitBoundsTo;

        private:
            enum class EngineKind { None, Decelerate, StopLogic, Spring };

            int mAnchorSide = 0;
            std::unique_ptr<motion::StopEngine> mEngine;
            EngineKind mEngineKind = EngineKind::None;
            std::optional<std::string> mRotationCenterId;
            bool mDragVertical = true;
            int mDragDirection = 0;
            float mDragScale = 1;
            float mDragThreshold = 10;
            int mAutoCompleteMode = 0;
            float mMaxVelocity = 4.f;
            float mMaxAcceleration = 1.2f;
            int mOnTouchUp = 0;
            float mSpringMass = 1;
            float mSpringStiffness = 400;
            float mSpringDamping = 10;
            float mSpringStopThreshold = 0.01f;
            float mDestination = 0.0f;
            int mSpringBoundary = 0;
            int64_t mStart = 0;
        };

        class WidgetState {
        public:
            WidgetState();
            WidgetState(const WidgetState&) = delete;
            WidgetState& operator=(const WidgetState&) = delete;
            void setKeyPosition(const motion::TypedBundle& prop);
            void setKeyAttribute(const motion::TypedBundle& prop);
            void setKeyAttribute(const motion::TypedBundle& prop, const std::vector<std::optional<motion::CustomVariable>>* custom);
            void setKeyCycle(const motion::TypedBundle& prop);
            void update(ConstraintWidget* child, int state);
            std::string getPathRelativeId() const { return mMotionControl.getAnimateRelativeTo(); }
            WidgetFrame& getFrame(int type);
            void interpolate(int parentWidth, int parentHeight, float progress, Transition& transition);
            void setPathRelative(WidgetState& widgetState) { mMotionControl.setupRelative(&widgetState.mMotionControl); }

            WidgetFrame mStart;
            WidgetFrame mEnd;
            WidgetFrame mInterpolated;
            motion::MotionWidget mMotionWidgetStart{ mStart };
            motion::MotionWidget mMotionWidgetEnd{ mEnd };
            motion::MotionWidget mMotionWidgetInterpolated{ mInterpolated };
            motion::Motion mMotionControl{ &mMotionWidgetStart };
            bool mNeedSetup = true;
            motion::KeyCache mKeyCache;
            int mParentHeight = -1;
            int mParentWidth = -1;
        };

        struct KeyPosition {
            int mFrame;
            std::string mTarget;
            int mType;
            float mX;
            float mY;
        };

        explicit Transition(CorePixelDp dpToPixel) : mToPixel(std::move(dpToPixel)) {}

        bool hasOnSwipe() const { return mOnSwipe != nullptr; }
        bool isFirstDownAccepted(float posX, float posY);
        float dragToProgress(float currentProgress, int baseW, int baseH, float dx, float dy);
        void setTouchUp(float currentProgress, int64_t currentTime, float velocityX, float velocityY);
        float getTouchUpProgress(int64_t currentTime);
        bool isTouchNotDone(float currentProgress) const { return mOnSwipe->isNotDone(currentProgress); }
        static Interpolator getInterpolator(int interpolator, const std::string& interpolatorString);
        const KeyPosition* findPreviousPosition(const std::string& target, int frameNumber) const;
        const KeyPosition* findNextPosition(const std::string& target, int frameNumber) const;
        int getNumberKeyPositions(const WidgetFrame& frame) const;
        motion::Motion& getMotion(const std::string& id) { return getWidgetState(id, nullptr, 0).mMotionControl; }
        void fillKeyPositions(const WidgetFrame& frame, float* x, float* y, float* pos) const;
        bool hasPositionKeyframes() const { return !mKeyPositions.empty(); }
        void setTransitionProperties(const motion::TypedBundle& bundle);

        bool setValue(int, int) override { return false; }
        bool setValue(int id, float value) override;
        bool setValue(int id, const std::string& value) override;
        bool setValue(int, bool) override { return false; }
        int getId(const std::string&) override { return 0; }

        bool isEmpty() const { return mState.empty(); }
        void clear() { mState.clear(); }
        bool contains(const std::string& key) const { return mState.contains(key); }
        void addKeyPosition(const std::string& target, const motion::TypedBundle& bundle) { getWidgetState(target, nullptr, 0).setKeyPosition(bundle); }
        void addKeyAttribute(const std::string& target, const motion::TypedBundle& bundle) { getWidgetState(target, nullptr, 0).setKeyAttribute(bundle); }
        void addKeyAttribute(const std::string& target, const motion::TypedBundle& bundle, const std::vector<std::optional<motion::CustomVariable>>* custom) {
            getWidgetState(target, nullptr, 0).setKeyAttribute(bundle, custom);
        }
        void addKeyCycle(const std::string& target, const motion::TypedBundle& bundle) { getWidgetState(target, nullptr, 0).setKeyCycle(bundle); }
        void addKeyPosition(const std::string& target, int frame, int type, float x, float y);
        void addCustomFloat(int state, const std::string& widgetId, const std::string& property, float value);
        void addCustomColor(int state, const std::string& widgetId, const std::string& property, int color);
        int getInterpolatedWidth() const { return mParentInterpolatedWidth; }
        int getInterpolatedHeight() const { return mParentInterpolateHeight; }
        void updateFrom(ConstraintWidgetContainer& container, int state);
        void interpolate(int parentWidth, int parentHeight, float progress);
        WidgetFrame* getStart(const std::string& id);
        WidgetFrame* getEnd(const std::string& id);
        WidgetFrame* getInterpolated(const std::string& id);
        std::vector<float> getPath(const std::string& id);
        int getKeyFrames(const std::string& id, float* rectangles, int* pathMode, int* position);
        WidgetState& getWidgetState(const std::string& widgetId, ConstraintWidget* child, int transitionState);
        WidgetFrame& getStart(ConstraintWidget* child);
        WidgetFrame& getEnd(ConstraintWidget* child);
        WidgetFrame& getInterpolated(ConstraintWidget* child);
        Interpolator getInterpolator() const { return getInterpolator(mDefaultInterpolator, mDefaultInterpolatorString); }
        int getAutoTransition() const { return mAutoTransition; }
        void calcStagger();
        int getDuration() const { return mDuration; }

        // TransitionParser's access.
        OnSwipe& createOnSwipe() {
            mOnSwipe = std::make_unique<OnSwipe>();
            return *mOnSwipe;
        }
        void resetProperties() {
            mOnSwipe = nullptr;
            mBundle.clear();
        }

        const CorePixelDp mToPixel;
        int mParentStartWidth = 0, mParentStartHeight = 0;
        int mParentEndWidth = 0, mParentEndHeight = 0;
        int mParentInterpolatedWidth = 0, mParentInterpolateHeight = 0;
        bool mWrap = false;

    private:
        void calculateParentDimensions(float progress);

        std::map<int, JavaHashMap<KeyPosition>> mKeyPositions;
        JavaHashMap<std::unique_ptr<WidgetState>> mState;
        motion::TypedBundle mBundle;
        int mDefaultInterpolator = 0;
        std::string mDefaultInterpolatorString;   // empty: Java's null
        std::shared_ptr<motion::Easing> mEasing;
        int mAutoTransition = 0;
        int mDuration = 400;
        float mStagger = 0.0f;
        std::unique_ptr<OnSwipe> mOnSwipe;
    };

}
