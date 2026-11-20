#include "kiln/constraintlayout/core/state/Transition.h"

#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyAttributes.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyCycle.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyPosition.h"
#include "kiln/constraintlayout/core/motion/utils/SpringStopEngine.h"
#include "kiln/constraintlayout/core/motion/utils/StopLogicEngine.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>

namespace kiln::constraintlayout::core::state {

    using namespace motion;

    namespace {
        constexpr int SPLINE_STRING = -1;

        int64_t nanoTime() {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        constexpr float TOUCH_SIDES[][2] = { { 0.5f, 0.0f }, { 0.0f, 0.5f }, { 1.0f, 0.5f }, { 0.5f, 1.0f }, { 0.5f, 0.5f }, { 0.0f, 0.5f }, { 1.0f, 0.5f } };
        constexpr float TOUCH_DIRECTION[][2] = { { 0.0f, -1.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f }, { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 1.0f, 0.0f } };

        float signum(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : v; }
    }

    // OnSwipe

    const float* Transition::OnSwipe::getDirection() const { return TOUCH_DIRECTION[mDragDirection]; }
    const float* Transition::OnSwipe::getSide() const { return TOUCH_SIDES[mAnchorSide]; }

    float Transition::OnSwipe::getDestinationPosition(float currentPosition, float velocity, float) const {
        const float rest = currentPosition + 0.5f * std::abs(velocity) * velocity / mMaxAcceleration;
        switch (mOnTouchUp) {
            case ON_UP_AUTOCOMPLETE_TO_START: return currentPosition >= 1.0f ? 1 : 0;
            case ON_UP_NEVER_COMPLETE_TO_END: return 0;
            case ON_UP_AUTOCOMPLETE_TO_END: return currentPosition <= 0.0f ? 0 : 1;
            case ON_UP_NEVER_COMPLETE_TO_START: return 1;
            case ON_UP_STOP: return NAN;
            case ON_UP_DECELERATE: return std::max(0.0f, std::min(1.0f, rest));
            case ON_UP_DECELERATE_AND_COMPLETE:
                if (rest > 0.2f && rest < 0.8f) return rest;
                return rest > .5f ? 1 : 0;
            case ON_UP_AUTOCOMPLETE:
            default: break;
        }
        return rest > .5 ? 1 : 0;
    }

    void Transition::OnSwipe::config(float position, float velocity, int64_t start, float duration) {
        mStart = start;
        if (std::abs(velocity) > mMaxVelocity) velocity = mMaxVelocity * signum(velocity);
        mDestination = getDestinationPosition(position, velocity, duration);
        if (mDestination == position) {
            mEngine = nullptr;
            mEngineKind = EngineKind::None;
            return;
        }
        if (mOnTouchUp == ON_UP_DECELERATE && mAutoCompleteMode == MODE_CONTINUOUS_VELOCITY) {
            if (mEngineKind != EngineKind::Decelerate) {
                mEngine = std::make_unique<StopLogicEngine::Decelerate>();
                mEngineKind = EngineKind::Decelerate;
            }
            static_cast<StopLogicEngine::Decelerate&>(*mEngine).config(position, mDestination, velocity);
            return;
        }
        if (mAutoCompleteMode == MODE_CONTINUOUS_VELOCITY) {
            if (mEngineKind != EngineKind::StopLogic) {
                mEngine = std::make_unique<StopLogicEngine>();
                mEngineKind = EngineKind::StopLogic;
            }
            static_cast<StopLogicEngine&>(*mEngine).config(position, mDestination, velocity, duration, mMaxAcceleration, mMaxVelocity);
            return;
        }
        if (mEngineKind != EngineKind::Spring) {
            mEngine = std::make_unique<SpringStopEngine>();
            mEngineKind = EngineKind::Spring;
        }
        static_cast<SpringStopEngine&>(*mEngine)
            .springConfig(position, mDestination, velocity, mSpringMass, mSpringStiffness, mSpringDamping, mSpringStopThreshold, mSpringBoundary);
    }

    float Transition::OnSwipe::getTouchUpProgress(int64_t currentTime) {
        const float time = float(currentTime - mStart) * 1E-9f;
        float pos = mEngine->getInterpolation(time);
        if (mEngine->isStopped()) pos = mDestination;
        return pos;
    }

    void Transition::OnSwipe::printInfo() const {
        if (mAutoCompleteMode == MODE_CONTINUOUS_VELOCITY) {
            std::printf("velocity = %g\n", double(mEngine->getVelocity()));
            std::printf("mMaxAcceleration = %g\n", double(mMaxAcceleration));
            std::printf("mMaxVelocity = %g\n", double(mMaxVelocity));
        } else {
            std::printf("mSpringMass          = %g\n", double(mSpringMass));
            std::printf("mSpringStiffness     = %g\n", double(mSpringStiffness));
            std::printf("mSpringDamping       = %g\n", double(mSpringDamping));
            std::printf("mSpringStopThreshold = %g\n", double(mSpringStopThreshold));
            std::printf("mSpringBoundary      = %d\n", mSpringBoundary);
        }
    }

    bool Transition::OnSwipe::isNotDone(float) const {
        if (mOnTouchUp == ON_UP_STOP) return false;
        return mEngine != nullptr && !mEngine->isStopped();
    }

    // Transition

    bool Transition::isFirstDownAccepted(float posX, float posY) {
        if (mOnSwipe == nullptr) return false;
        if (mOnSwipe->mLimitBoundsTo) {
            std::unique_ptr<WidgetState>* targetWidget = mState.find(*mOnSwipe->mLimitBoundsTo);
            if (targetWidget == nullptr) {
                std::fprintf(stderr, "mLimitBoundsTo target is null\n");
                return false;
            }
            const WidgetFrame& frame = (*targetWidget)->getFrame(2);
            return posX >= frame.left && posX < frame.right && posY >= frame.top && posY < frame.bottom;
        }
        return true;
    }

    float Transition::dragToProgress(float currentProgress, int baseW, int baseH, float dx, float dy) {
        WidgetState* childWidget = nullptr;
        const std::vector<std::string> keys = mState.keys();
        if (!keys.empty()) childWidget = mState.find(keys[0])->get();
        if (mOnSwipe == nullptr || childWidget == nullptr) {
            if (childWidget != nullptr) return -dy / childWidget->mParentHeight;
            return 1.0f;
        }
        if (!mOnSwipe->mAnchorId) {
            const float* dir = mOnSwipe->getDirection();
            const float motionDpDtX = float(childWidget->mParentHeight);
            const float motionDpDtY = float(childWidget->mParentHeight);
            const float drag = dir[0] != 0 ? dx * std::abs(dir[0]) / motionDpDtX : dy * std::abs(dir[1]) / motionDpDtY;
            return drag * mOnSwipe->getScale();
        }
        WidgetState& base = **mState.find(*mOnSwipe->mAnchorId);
        const float* dir = mOnSwipe->getDirection();
        const float* side = mOnSwipe->getSide();
        float motionDpDt[2] = {};
        base.interpolate(baseW, baseH, currentProgress, *this);
        base.mMotionControl.getDpDt(currentProgress, side[0], side[1], motionDpDt);
        const float drag = dir[0] != 0 ? dx * std::abs(dir[0]) / motionDpDt[0] : dy * std::abs(dir[1]) / motionDpDt[1];
        return drag * mOnSwipe->getScale();
    }

    void Transition::setTouchUp(float currentProgress, int64_t currentTime, float velocityX, float velocityY) {
        if (mOnSwipe == nullptr) return;
        WidgetState& base = **mState.find(mOnSwipe->mAnchorId.value_or(std::string()));
        float motionDpDt[2] = {};
        const float* dir = mOnSwipe->getDirection();
        const float* side = mOnSwipe->getSide();
        base.mMotionControl.getDpDt(currentProgress, side[0], side[1], motionDpDt);
        const float movementInDir = dir[0] * motionDpDt[0] + dir[1] * motionDpDt[1];
        if (std::abs(movementInDir) < 0.01) {
            motionDpDt[0] = .01f;
            motionDpDt[1] = .01f;
        }
        float drag = dir[0] != 0 ? velocityX / motionDpDt[0] : velocityY / motionDpDt[1];
        drag *= mOnSwipe->getScale();
        mOnSwipe->config(currentProgress, drag, currentTime, mDuration * 1E-3f);
    }

    float Transition::getTouchUpProgress(int64_t currentTime) {
        if (mOnSwipe != nullptr) return mOnSwipe->getTouchUpProgress(currentTime);
        return 0;
    }

    Interpolator Transition::getInterpolator(int interpolator, const std::string& interpolatorString) {
        auto easing = [](const std::string& s) -> Interpolator {
            std::shared_ptr<Easing> e = Easing::getInterpolator(s);
            return [e](float v) { return float(e->get(v)); };
        };
        switch (interpolator) {
            case SPLINE_STRING: return easing(interpolatorString);
            case EASE_IN_OUT: return easing("standard");
            case EASE_IN: return easing("accelerate");
            case EASE_OUT: return easing("decelerate");
            case LINEAR: return easing("linear");
            case ANTICIPATE: return easing("anticipate");
            case OVERSHOOT: return easing("overshoot");
            case BOUNCE: return easing("spline(0.0, 0.2, 0.4, 0.6, 0.8 ,1.0, 0.8, 1.0, 0.9, 1.0)");
        }
        return nullptr;
    }

    const Transition::KeyPosition* Transition::findPreviousPosition(const std::string& target, int frameNumber) const {
        for (; frameNumber >= 0; frameNumber--) {
            const auto it = mKeyPositions.find(frameNumber);
            if (it == mKeyPositions.end()) continue;
            if (const KeyPosition* keyPosition = it->second.find(target)) return keyPosition;
        }
        return nullptr;
    }

    const Transition::KeyPosition* Transition::findNextPosition(const std::string& target, int frameNumber) const {
        for (; frameNumber <= 100; frameNumber++) {
            const auto it = mKeyPositions.find(frameNumber);
            if (it == mKeyPositions.end()) continue;
            if (const KeyPosition* keyPosition = it->second.find(target)) return keyPosition;
        }
        return nullptr;
    }

    int Transition::getNumberKeyPositions(const WidgetFrame& frame) const {
        int numKeyPositions = 0;
        for (int frameNumber = 0; frameNumber <= 100; frameNumber++) {
            const auto it = mKeyPositions.find(frameNumber);
            if (it != mKeyPositions.end() && it->second.find(frame.widget->stringId) != nullptr) numKeyPositions++;
        }
        return numKeyPositions;
    }

    void Transition::fillKeyPositions(const WidgetFrame& frame, float* x, float* y, float* pos) const {
        int numKeyPositions = 0;
        for (int frameNumber = 0; frameNumber <= 100; frameNumber++) {
            const auto it = mKeyPositions.find(frameNumber);
            if (it == mKeyPositions.end()) continue;
            if (const KeyPosition* keyPosition = it->second.find(frame.widget->stringId)) {
                x[numKeyPositions] = keyPosition->mX;
                y[numKeyPositions] = keyPosition->mY;
                pos[numKeyPositions] = float(keyPosition->mFrame);
                numKeyPositions++;
            }
        }
    }

    void Transition::setTransitionProperties(const TypedBundle& bundle) {
        bundle.applyDelta(mBundle);
        bundle.applyDelta(*this);
    }

    bool Transition::setValue(int id, float value) {
        if (id == TransitionType::TYPE_STAGGERED) mStagger = value;
        return false;
    }

    bool Transition::setValue(int id, const std::string& value) {
        if (id == TransitionType::TYPE_INTERPOLATOR) {
            mDefaultInterpolatorString = value;
            mEasing = Easing::getInterpolator(value);
        }
        return false;
    }

    void Transition::addKeyPosition(const std::string& target, int frame, int type, float x, float y) {
        TypedBundle bundle;
        bundle.add(PositionType::TYPE_POSITION_TYPE, 2);
        bundle.add(TYPE_FRAME_POSITION, frame);
        bundle.add(PositionType::TYPE_PERCENT_X, x);
        bundle.add(PositionType::TYPE_PERCENT_Y, y);
        getWidgetState(target, nullptr, 0).setKeyPosition(bundle);
        mKeyPositions[frame].put(target, KeyPosition{ frame, target, type, x, y });
    }

    void Transition::addCustomFloat(int state, const std::string& widgetId, const std::string& property, float value) {
        getWidgetState(widgetId, nullptr, state).getFrame(state).addCustomFloat(property, value);
    }

    void Transition::addCustomColor(int state, const std::string& widgetId, const std::string& property, int color) {
        getWidgetState(widgetId, nullptr, state).getFrame(state).addCustomColor(property, color);
    }

    void Transition::calculateParentDimensions(float progress) {
        mParentInterpolatedWidth = JavaInt(0.5f + mParentStartWidth + (mParentEndWidth - mParentStartWidth) * progress);
        mParentInterpolateHeight = JavaInt(0.5f + mParentStartHeight + (mParentEndHeight - mParentStartHeight) * progress);
    }

    void Transition::updateFrom(ConstraintWidgetContainer& container, int state) {
        mWrap = container.mListDimensionBehaviors[0] == ConstraintWidget::DimensionBehaviour::WRAP_CONTENT;
        mWrap |= container.mListDimensionBehaviors[1] == ConstraintWidget::DimensionBehaviour::WRAP_CONTENT;
        if (state == START) {
            mParentInterpolatedWidth = mParentStartWidth = container.getWidth();
            mParentInterpolateHeight = mParentStartHeight = container.getHeight();
        } else {
            mParentEndWidth = container.getWidth();
            mParentEndHeight = container.getHeight();
        }
        const std::vector<ConstraintWidget*>& children = container.getChildren();
        const std::size_t count = children.size();
        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            WidgetState& widgetState = getWidgetState(child->stringId, nullptr, state);
            widgetState.update(child, state);
            const std::string id = widgetState.getPathRelativeId();
            if (!id.empty()) widgetState.setPathRelative(getWidgetState(id, nullptr, state));
        }
        calcStagger();
    }

    void Transition::interpolate(int parentWidth, int parentHeight, float progress) {
        if (mWrap) calculateParentDimensions(progress);
        if (mEasing != nullptr) progress = float(mEasing->get(progress));
        for (const std::string& key : mState.keys()) (*mState.find(key))->interpolate(parentWidth, parentHeight, progress, *this);
    }

    WidgetFrame* Transition::getStart(const std::string& id) {
        std::unique_ptr<WidgetState>* widgetState = mState.find(id);
        return widgetState == nullptr ? nullptr : &(*widgetState)->mStart;
    }

    WidgetFrame* Transition::getEnd(const std::string& id) {
        std::unique_ptr<WidgetState>* widgetState = mState.find(id);
        return widgetState == nullptr ? nullptr : &(*widgetState)->mEnd;
    }

    WidgetFrame* Transition::getInterpolated(const std::string& id) {
        std::unique_ptr<WidgetState>* widgetState = mState.find(id);
        return widgetState == nullptr ? nullptr : &(*widgetState)->mInterpolated;
    }

    std::vector<float> Transition::getPath(const std::string& id) {
        WidgetState& widgetState = **mState.find(id);
        const int duration = 1000;
        const int frames = duration / 16;
        std::vector<float> points(std::size_t(frames) * 2);
        widgetState.mMotionControl.buildPath(points.data(), frames);
        return points;
    }

    int Transition::getKeyFrames(const std::string& id, float* rectangles, int* pathMode, int* position) {
        return (*mState.find(id))->mMotionControl.buildKeyFrames(rectangles, pathMode, position);
    }

    Transition::WidgetState& Transition::getWidgetState(const std::string& widgetId, ConstraintWidget* child, int transitionState) {
        if (std::unique_ptr<WidgetState>* existing = mState.find(widgetId)) return **existing;
        auto widgetState = std::make_unique<WidgetState>();
        mBundle.applyDelta(widgetState->mMotionControl);
        widgetState->mMotionWidgetStart.updateMotion(widgetState->mMotionControl);
        WidgetState& ref = *mState.put(widgetId, std::move(widgetState));
        if (child != nullptr) ref.update(child, transitionState);
        return ref;
    }

    WidgetFrame& Transition::getStart(ConstraintWidget* child) { return getWidgetState(child->stringId, nullptr, START).mStart; }
    WidgetFrame& Transition::getEnd(ConstraintWidget* child) { return getWidgetState(child->stringId, nullptr, END).mEnd; }
    WidgetFrame& Transition::getInterpolated(ConstraintWidget* child) { return getWidgetState(child->stringId, nullptr, INTERPOLATED).mInterpolated; }

    void Transition::calcStagger() {
        if (mStagger == 0.0f) return;
        const bool flip = mStagger < 0.0;
        const float stagger = std::abs(mStagger);
        float min = FLT_MAX, max = -FLT_MAX;
        bool useMotionStagger = false;
        const std::vector<std::string> keys = mState.keys();
        for (const std::string& widgetId : keys) {
            if (!std::isnan((*mState.find(widgetId))->mMotionControl.getMotionStagger())) {
                useMotionStagger = true;
                break;
            }
        }
        if (useMotionStagger) {
            for (const std::string& widgetId : keys) {
                const float widgetStagger = (*mState.find(widgetId))->mMotionControl.getMotionStagger();
                if (!std::isnan(widgetStagger)) {
                    min = std::min(min, widgetStagger);
                    max = std::max(max, widgetStagger);
                }
            }
            for (const std::string& widgetId : keys) {
                Motion& f = (*mState.find(widgetId))->mMotionControl;
                const float widgetStagger = f.getMotionStagger();
                if (std::isnan(widgetStagger)) continue;
                const float scale = 1 / (1 - stagger);
                float offset = stagger - stagger * (widgetStagger - min) / (max - min);
                if (flip) offset = stagger - stagger * ((max - widgetStagger) / (max - min));
                f.setStaggerScale(scale);
                f.setStaggerOffset(offset);
            }
        } else {
            for (const std::string& widgetId : keys) {
                const Motion& f = (*mState.find(widgetId))->mMotionControl;
                const float widgetStagger = f.getFinalX() + f.getFinalY();
                min = std::min(min, widgetStagger);
                max = std::max(max, widgetStagger);
            }
            for (const std::string& widgetId : keys) {
                Motion& f = (*mState.find(widgetId))->mMotionControl;
                const float widgetStagger = f.getFinalX() + f.getFinalY();
                float offset = stagger - stagger * (widgetStagger - min) / (max - min);
                if (flip) offset = stagger - stagger * ((max - widgetStagger) / (max - min));
                const float scale = 1 / (1 - stagger);
                f.setStaggerScale(scale);
                f.setStaggerOffset(offset);
            }
        }
    }

    // WidgetState

    Transition::WidgetState::WidgetState() {
        mMotionControl.setStart(mMotionWidgetStart);
        mMotionControl.setEnd(mMotionWidgetEnd);
    }

    void Transition::WidgetState::setKeyPosition(const TypedBundle& prop) {
        auto keyPosition = std::make_shared<MotionKeyPosition>();
        prop.applyDelta(*keyPosition);
        mMotionControl.addKey(keyPosition);
    }

    void Transition::WidgetState::setKeyAttribute(const TypedBundle& prop) {
        auto keyAttributes = std::make_shared<MotionKeyAttributes>();
        prop.applyDelta(*keyAttributes);
        mMotionControl.addKey(keyAttributes);
    }

    void Transition::WidgetState::setKeyAttribute(const TypedBundle& prop, const std::vector<std::optional<CustomVariable>>* custom) {
        auto keyAttributes = std::make_shared<MotionKeyAttributes>();
        prop.applyDelta(*keyAttributes);
        if (custom != nullptr) {
            // Upstream dereferences every entry; an unparsed colour is skipped here.
            for (const auto& c : *custom)
                if (c) keyAttributes->mCustom.put(c->getName(), *c);
        }
        mMotionControl.addKey(keyAttributes);
    }

    void Transition::WidgetState::setKeyCycle(const TypedBundle& prop) {
        auto keyAttributes = std::make_shared<MotionKeyCycle>();
        prop.applyDelta(*keyAttributes);
        mMotionControl.addKey(keyAttributes);
    }

    void Transition::WidgetState::update(ConstraintWidget* child, int state) {
        if (state == START) {
            mStart.update(child);
            mMotionWidgetStart.updateMotion(mMotionWidgetStart);
            mMotionControl.setStart(mMotionWidgetStart);
            mNeedSetup = true;
        } else if (state == END) {
            mEnd.update(child);
            mMotionControl.setEnd(mMotionWidgetEnd);
            mNeedSetup = true;
        }
        mParentWidth = -1;
    }

    WidgetFrame& Transition::WidgetState::getFrame(int type) {
        if (type == START) return mStart;
        if (type == END) return mEnd;
        return mInterpolated;
    }

    void Transition::WidgetState::interpolate(int parentWidth, int parentHeight, float progress, Transition& transition) {
        mParentHeight = parentHeight;
        mParentWidth = parentWidth;
        if (mNeedSetup) {
            mMotionControl.setup(parentWidth, parentHeight, 1, nanoTime());
            mNeedSetup = false;
        }
        WidgetFrame::interpolate(parentWidth, parentHeight, mInterpolated, mStart, mEnd, transition, progress);
        mInterpolated.interpolatedPos = progress;
        mMotionControl.interpolate(mMotionWidgetInterpolated, progress, nanoTime(), mKeyCache);
    }

}
