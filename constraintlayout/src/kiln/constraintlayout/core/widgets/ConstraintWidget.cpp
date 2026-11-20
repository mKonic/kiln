#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace kiln::constraintlayout::core {

    using T = ConstraintAnchor::Type;

    ConstraintWidget::ConstraintWidget() { addAnchors(); }
    ConstraintWidget::ConstraintWidget(const std::string& debugName) {
        addAnchors();
        setDebugName(debugName);
    }
    ConstraintWidget::ConstraintWidget(int x, int y, int width, int height) : mWidth(width), mHeight(height), mX(x), mY(y) { addAnchors(); }
    ConstraintWidget::ConstraintWidget(const std::string& debugName, int x, int y, int width, int height) : ConstraintWidget(x, y, width, height) {
        setDebugName(debugName);
    }
    ConstraintWidget::ConstraintWidget(int width, int height) : ConstraintWidget(0, 0, width, height) {}
    ConstraintWidget::ConstraintWidget(const std::string& debugName, int width, int height) : ConstraintWidget(width, height) { setDebugName(debugName); }

    ConstraintWidget::~ConstraintWidget() = default;

    WidgetRun* ConstraintWidget::getRun(int orientation) const {
        if (orientation == HORIZONTAL) return mHorizontalRun.get();
        if (orientation == VERTICAL) return mVerticalRun.get();
        return nullptr;
    }

    void ConstraintWidget::ensureWidgetRuns() {
        if (mHorizontalRun == nullptr) mHorizontalRun = std::make_shared<HorizontalWidgetRun>(this);
        if (mVerticalRun == nullptr) mVerticalRun = std::make_shared<VerticalWidgetRun>(this);
    }

    void ConstraintWidget::setFinalFrame(int left, int top, int right, int bottom, int baseline, int orientation) {
        setFrame(left, top, right, bottom);
        setBaselineDistance(baseline);
        if (orientation == HORIZONTAL) {
            mResolvedHorizontal = true;
            mResolvedVertical = false;
        } else if (orientation == VERTICAL) {
            mResolvedHorizontal = false;
            mResolvedVertical = true;
        } else if (orientation == BOTH) {
            mResolvedHorizontal = true;
            mResolvedVertical = true;
        } else {
            mResolvedHorizontal = false;
            mResolvedVertical = false;
        }
    }

    void ConstraintWidget::setFinalHorizontal(int x1, int x2) {
        if (mResolvedHorizontal) return;
        mLeft.setFinalValue(x1);
        mRight.setFinalValue(x2);
        mX = x1;
        mWidth = x2 - x1;
        mResolvedHorizontal = true;
    }

    void ConstraintWidget::setFinalVertical(int y1, int y2) {
        if (mResolvedVertical) return;
        mTop.setFinalValue(y1);
        mBottom.setFinalValue(y2);
        mY = y1;
        mHeight = y2 - y1;
        if (mHasBaseline) mBaseline.setFinalValue(y1 + mBaselineDistance);
        mResolvedVertical = true;
    }

    void ConstraintWidget::setFinalBaseline(int baselineValue) {
        if (!mHasBaseline) return;
        const int y1 = baselineValue - mBaselineDistance;
        const int y2 = y1 + mHeight;
        mY = y1;
        mTop.setFinalValue(y1);
        mBottom.setFinalValue(y2);
        mBaseline.setFinalValue(baselineValue);
        mResolvedVertical = true;
    }

    void ConstraintWidget::resetFinalResolution() {
        mResolvedHorizontal = false;
        mResolvedVertical = false;
        mHorizontalSolvingPass = false;
        mVerticalSolvingPass = false;
        for (ConstraintAnchor* anchor : mAnchors) anchor->resetFinalResolution();
    }

    bool ConstraintWidget::hasDependencies() const {
        for (ConstraintAnchor* anchor : mAnchors)
            if (anchor->hasDependents()) return true;
        return false;
    }

    bool ConstraintWidget::hasDanglingDimension(int orientation) const {
        if (orientation == HORIZONTAL) {
            const int horizontalTargets = (mLeft.mTarget != nullptr ? 1 : 0) + (mRight.mTarget != nullptr ? 1 : 0);
            return horizontalTargets < 2;
        }
        const int verticalTargets = (mTop.mTarget != nullptr ? 1 : 0) + (mBottom.mTarget != nullptr ? 1 : 0) + (mBaseline.mTarget != nullptr ? 1 : 0);
        return verticalTargets < 2;
    }

    bool ConstraintWidget::hasResolvedTargets(int orientation, int size) const {
        if (orientation == HORIZONTAL) {
            if (mLeft.mTarget != nullptr && mLeft.mTarget->hasFinalValue() && mRight.mTarget != nullptr && mRight.mTarget->hasFinalValue())
                return ((mRight.mTarget->getFinalValue() - mRight.getMargin()) - (mLeft.mTarget->getFinalValue() + mLeft.getMargin())) >= size;
        } else {
            if (mTop.mTarget != nullptr && mTop.mTarget->hasFinalValue() && mBottom.mTarget != nullptr && mBottom.mTarget->hasFinalValue())
                return ((mBottom.mTarget->getFinalValue() - mBottom.getMargin()) - (mTop.mTarget->getFinalValue() + mTop.getMargin())) >= size;
        }
        return false;
    }

    bool ConstraintWidget::isSpreadWidth() const {
        return mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD && mDimensionRatio == 0 && mMatchConstraintMinWidth == 0 &&
               mMatchConstraintMaxWidth == 0 && mListDimensionBehaviors[HORIZONTAL] == DimensionBehaviour::MATCH_CONSTRAINT;
    }

    bool ConstraintWidget::isSpreadHeight() const {
        return mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD && mDimensionRatio == 0 && mMatchConstraintMinHeight == 0 &&
               mMatchConstraintMaxHeight == 0 && mListDimensionBehaviors[VERTICAL] == DimensionBehaviour::MATCH_CONSTRAINT;
    }

    void ConstraintWidget::reset() {
        mLeft.reset();
        mTop.reset();
        mRight.reset();
        mBottom.reset();
        mBaseline.reset();
        mCenterX.reset();
        mCenterY.reset();
        mCenter.reset();
        mParent = nullptr;
        mCircleConstraintAngle = NAN;
        mWidth = 0;
        mHeight = 0;
        mDimensionRatio = 0;
        mDimensionRatioSide = UNKNOWN;
        mX = 0;
        mY = 0;
        mOffsetX = 0;
        mOffsetY = 0;
        mBaselineDistance = 0;
        mMinWidth = 0;
        mMinHeight = 0;
        mHorizontalBiasPercent = DEFAULT_BIAS;
        mVerticalBiasPercent = DEFAULT_BIAS;
        mListDimensionBehaviors[DIMENSION_HORIZONTAL] = DimensionBehaviour::FIXED;
        mListDimensionBehaviors[DIMENSION_VERTICAL] = DimensionBehaviour::FIXED;
        mCompanionWidget = nullptr;
        mContainerItemSkip = 0;
        mVisibility = VISIBLE;
        mType.clear();
        mHorizontalWrapVisited = false;
        mVerticalWrapVisited = false;
        mHorizontalChainStyle = CHAIN_SPREAD;
        mVerticalChainStyle = CHAIN_SPREAD;
        mHorizontalChainFixedPosition = false;
        mVerticalChainFixedPosition = false;
        mWeight[DIMENSION_HORIZONTAL] = float(UNKNOWN);
        mWeight[DIMENSION_VERTICAL] = float(UNKNOWN);
        mHorizontalResolution = UNKNOWN;
        mVerticalResolution = UNKNOWN;
        mMaxDimension[HORIZONTAL] = INT_MAX;
        mMaxDimension[VERTICAL] = INT_MAX;
        mMatchConstraintDefaultWidth = MATCH_CONSTRAINT_SPREAD;
        mMatchConstraintDefaultHeight = MATCH_CONSTRAINT_SPREAD;
        mMatchConstraintPercentWidth = 1;
        mMatchConstraintPercentHeight = 1;
        mMatchConstraintMaxWidth = INT_MAX;
        mMatchConstraintMaxHeight = INT_MAX;
        mMatchConstraintMinWidth = 0;
        mMatchConstraintMinHeight = 0;
        mResolvedHasRatio = false;
        mResolvedDimensionRatioSide = UNKNOWN;
        mResolvedDimensionRatio = 1.0f;
        mGroupsToSolver = false;
        isTerminalWidget[HORIZONTAL] = true;
        isTerminalWidget[VERTICAL] = true;
        mInVirtualLayout = false;
        mIsInBarrier[HORIZONTAL] = false;
        mIsInBarrier[VERTICAL] = false;
        mMeasureRequested = true;
        mResolvedMatchConstraintDefault[HORIZONTAL] = 0;
        mResolvedMatchConstraintDefault[VERTICAL] = 0;
        mWidthOverride = -1;
        mHeightOverride = -1;
    }

    bool ConstraintWidget::oppositeDimensionDependsOn(int orientation) const {
        const int oppositeOrientation = (orientation == HORIZONTAL) ? VERTICAL : HORIZONTAL;
        return mListDimensionBehaviors[std::size_t(orientation)] == DimensionBehaviour::MATCH_CONSTRAINT &&
               mListDimensionBehaviors[std::size_t(oppositeOrientation)] == DimensionBehaviour::MATCH_CONSTRAINT;
    }

    bool ConstraintWidget::oppositeDimensionsTied() const {
        return mListDimensionBehaviors[HORIZONTAL] == DimensionBehaviour::MATCH_CONSTRAINT &&
               mListDimensionBehaviors[VERTICAL] == DimensionBehaviour::MATCH_CONSTRAINT;
    }

    void ConstraintWidget::resetSolverVariables(Cache* cache) {
        mLeft.resetSolverVariable(cache);
        mTop.resetSolverVariable(cache);
        mRight.resetSolverVariable(cache);
        mBottom.resetSolverVariable(cache);
        mBaseline.resetSolverVariable(cache);
        mCenter.resetSolverVariable(cache);
        mCenterX.resetSolverVariable(cache);
        mCenterY.resetSolverVariable(cache);
    }

    void ConstraintWidget::addAnchors() {
        mAnchors.push_back(&mLeft);
        mAnchors.push_back(&mTop);
        mAnchors.push_back(&mRight);
        mAnchors.push_back(&mBottom);
        mAnchors.push_back(&mCenterX);
        mAnchors.push_back(&mCenterY);
        mAnchors.push_back(&mCenter);
        mAnchors.push_back(&mBaseline);
    }

    void ConstraintWidget::connectCircularConstraint(ConstraintWidget* target, float angle, int radius) {
        immediateConnect(T::CENTER, target, T::CENTER, radius, 0);
        mCircleConstraintAngle = angle;
    }

    void ConstraintWidget::setDebugSolverName(LinearSystem& system, const std::string& name) {
        mDebugName = name;
        system.createObjectVariable(&mLeft)->setName(name + ".left");
        system.createObjectVariable(&mTop)->setName(name + ".top");
        system.createObjectVariable(&mRight)->setName(name + ".right");
        system.createObjectVariable(&mBottom)->setName(name + ".bottom");
        system.createObjectVariable(&mBaseline)->setName(name + ".baseline");
    }

    void ConstraintWidget::createObjectVariables(LinearSystem& system) {
        system.createObjectVariable(&mLeft);
        system.createObjectVariable(&mTop);
        system.createObjectVariable(&mRight);
        system.createObjectVariable(&mBottom);
        if (mBaselineDistance > 0) system.createObjectVariable(&mBaseline);
    }

    std::string ConstraintWidget::toString() const {
        return (!mType.empty() ? "type: " + mType + " " : std::string()) + (!mDebugName.empty() ? "id: " + mDebugName + " " : std::string()) + "(" +
               std::to_string(mX) + ", " + std::to_string(mY) + ") - (" + std::to_string(mWidth) + " x " + std::to_string(mHeight) + ")";
    }

    int ConstraintWidget::getX() const {
        if (mParent != nullptr && mParent->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) return static_cast<ConstraintWidgetContainer*>(mParent)->mPaddingLeft + mX;
        return mX;
    }

    int ConstraintWidget::getY() const {
        if (mParent != nullptr && mParent->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) return static_cast<ConstraintWidgetContainer*>(mParent)->mPaddingTop + mY;
        return mY;
    }

    int ConstraintWidget::getOptimizerWrapWidth() {
        int w = mWidth;
        if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DimensionBehaviour::MATCH_CONSTRAINT) {
            if (mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_WRAP) {
                w = std::max(mMatchConstraintMinWidth, w);
            } else if (mMatchConstraintMinWidth > 0) {
                w = mMatchConstraintMinWidth;
                mWidth = w;
            } else {
                w = 0;
            }
            if (mMatchConstraintMaxWidth > 0 && mMatchConstraintMaxWidth < w) w = mMatchConstraintMaxWidth;
        }
        return w;
    }

    int ConstraintWidget::getOptimizerWrapHeight() {
        int h = mHeight;
        if (mListDimensionBehaviors[DIMENSION_VERTICAL] == DimensionBehaviour::MATCH_CONSTRAINT) {
            if (mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_WRAP) {
                h = std::max(mMatchConstraintMinHeight, h);
            } else if (mMatchConstraintMinHeight > 0) {
                h = mMatchConstraintMinHeight;
                mHeight = h;
            } else {
                h = 0;
            }
            if (mMatchConstraintMaxHeight > 0 && mMatchConstraintMaxHeight < h) h = mMatchConstraintMaxHeight;
        }
        return h;
    }

    int ConstraintWidget::getLength(int orientation) const {
        if (orientation == HORIZONTAL) return getWidth();
        if (orientation == VERTICAL) return getHeight();
        return 0;
    }

    float ConstraintWidget::getBiasPercent(int orientation) const {
        if (orientation == HORIZONTAL) return mHorizontalBiasPercent;
        if (orientation == VERTICAL) return mVerticalBiasPercent;
        return float(UNKNOWN);
    }

    void ConstraintWidget::setGoneMargin(ConstraintAnchor::Type type, int goneMargin) {
        switch (type) {
            case T::LEFT: mLeft.mGoneMargin = goneMargin; break;
            case T::TOP: mTop.mGoneMargin = goneMargin; break;
            case T::RIGHT: mRight.mGoneMargin = goneMargin; break;
            case T::BOTTOM: mBottom.mGoneMargin = goneMargin; break;
            case T::BASELINE: mBaseline.mGoneMargin = goneMargin; break;
            default: break;
        }
    }

    void ConstraintWidget::setLength(int length, int orientation) {
        if (orientation == HORIZONTAL) setWidth(length);
        else if (orientation == VERTICAL) setHeight(length);
    }

    void ConstraintWidget::setHorizontalMatchStyle(int horizontalMatchStyle, int min, int max, float percent) {
        mMatchConstraintDefaultWidth = horizontalMatchStyle;
        mMatchConstraintMinWidth = min;
        mMatchConstraintMaxWidth = (max == INT_MAX) ? 0 : max;
        mMatchConstraintPercentWidth = percent;
        if (percent > 0 && percent < 1 && mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD) mMatchConstraintDefaultWidth = MATCH_CONSTRAINT_PERCENT;
    }

    void ConstraintWidget::setVerticalMatchStyle(int verticalMatchStyle, int min, int max, float percent) {
        mMatchConstraintDefaultHeight = verticalMatchStyle;
        mMatchConstraintMinHeight = min;
        mMatchConstraintMaxHeight = (max == INT_MAX) ? 0 : max;
        mMatchConstraintPercentHeight = percent;
        if (percent > 0 && percent < 1 && mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD) mMatchConstraintDefaultHeight = MATCH_CONSTRAINT_PERCENT;
    }

    namespace {
        // Float.parseFloat: the whole string (trimmed) must be a number, else nullopt.
        bool ParseJavaFloat(std::string s, float& out) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            if (!s.empty() && (s.back() == 'f' || s.back() == 'F' || s.back() == 'd' || s.back() == 'D')) s.pop_back();
            if (s.empty()) return false;
            char* end = nullptr;
            const float v = std::strtof(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0') return false;
            out = v;
            return true;
        }
        bool EqualsIgnoreCase(const std::string& a, const char* b) {
            const std::string bb(b);
            if (a.size() != bb.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(bb[i]))) return false;
            return true;
        }
    }

    void ConstraintWidget::setDimensionRatio(const std::string& ratio) {
        if (ratio.empty()) {
            mDimensionRatio = 0;
            return;
        }
        int dimensionRatioSide = UNKNOWN;
        float dimensionRatio = 0;
        const int len = int(ratio.size());
        int commaIndex = int(ratio.find(','));
        if (ratio.find(',') == std::string::npos) commaIndex = -1;
        if (commaIndex > 0 && commaIndex < len - 1) {
            const std::string dimension = ratio.substr(0, std::size_t(commaIndex));
            if (EqualsIgnoreCase(dimension, "W")) dimensionRatioSide = HORIZONTAL;
            else if (EqualsIgnoreCase(dimension, "H")) dimensionRatioSide = VERTICAL;
            commaIndex++;
        } else {
            commaIndex = 0;
        }
        const std::size_t colon = ratio.find(':');
        const int colonIndex = colon == std::string::npos ? -1 : int(colon);
        if (colonIndex >= 0 && colonIndex < len - 1) {
            const std::string nominator = colonIndex >= commaIndex ? ratio.substr(std::size_t(commaIndex), std::size_t(colonIndex - commaIndex)) : std::string();
            const std::string denominator = ratio.substr(std::size_t(colonIndex + 1));
            if (!nominator.empty() && !denominator.empty()) {
                float nominatorValue = 0, denominatorValue = 0;
                if (ParseJavaFloat(nominator, nominatorValue) && ParseJavaFloat(denominator, denominatorValue)) {
                    if (nominatorValue > 0 && denominatorValue > 0) {
                        if (dimensionRatioSide == VERTICAL) dimensionRatio = std::abs(denominatorValue / nominatorValue);
                        else dimensionRatio = std::abs(nominatorValue / denominatorValue);
                    }
                }
            }
        } else {
            const std::string r = ratio.substr(std::size_t(commaIndex));
            if (!r.empty()) {
                float v = 0;
                if (ParseJavaFloat(r, v)) dimensionRatio = v;
            }
        }
        if (dimensionRatio > 0) {
            mDimensionRatio = dimensionRatio;
            mDimensionRatioSide = dimensionRatioSide;
        }
    }

    void ConstraintWidget::setDimension(int w, int h) {
        mWidth = w;
        if (mWidth < mMinWidth) mWidth = mMinWidth;
        mHeight = h;
        if (mHeight < mMinHeight) mHeight = mMinHeight;
    }

    void ConstraintWidget::setFrame(int left, int top, int right, int bottom) {
        int w = right - left;
        int h = bottom - top;
        mX = left;
        mY = top;
        if (mVisibility == GONE) {
            mWidth = 0;
            mHeight = 0;
            return;
        }
        if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DimensionBehaviour::FIXED && w < mWidth) w = mWidth;
        if (mListDimensionBehaviors[DIMENSION_VERTICAL] == DimensionBehaviour::FIXED && h < mHeight) h = mHeight;
        mWidth = w;
        mHeight = h;
        if (mHeight < mMinHeight) mHeight = mMinHeight;
        if (mWidth < mMinWidth) mWidth = mMinWidth;
        if (mMatchConstraintMaxWidth > 0 && mListDimensionBehaviors[HORIZONTAL] == DimensionBehaviour::MATCH_CONSTRAINT)
            mWidth = std::min(mWidth, mMatchConstraintMaxWidth);
        if (mMatchConstraintMaxHeight > 0 && mListDimensionBehaviors[VERTICAL] == DimensionBehaviour::MATCH_CONSTRAINT)
            mHeight = std::min(mHeight, mMatchConstraintMaxHeight);
        if (w != mWidth) mWidthOverride = mWidth;
        if (h != mHeight) mHeightOverride = mHeight;
    }

    void ConstraintWidget::setFrame(int start, int end, int orientation) {
        if (orientation == HORIZONTAL) setHorizontalDimension(start, end);
        else if (orientation == VERTICAL) setVerticalDimension(start, end);
    }

    void ConstraintWidget::setHorizontalDimension(int left, int right) {
        mX = left;
        mWidth = right - left;
        if (mWidth < mMinWidth) mWidth = mMinWidth;
    }

    void ConstraintWidget::setVerticalDimension(int top, int bottom) {
        mY = top;
        mHeight = bottom - top;
        if (mHeight < mMinHeight) mHeight = mMinHeight;
    }

    int ConstraintWidget::getRelativePositioning(int orientation) const {
        if (orientation == HORIZONTAL) return mRelX;
        if (orientation == VERTICAL) return mRelY;
        return 0;
    }

    void ConstraintWidget::setRelativePositioning(int offset, int orientation) {
        if (orientation == HORIZONTAL) mRelX = offset;
        else if (orientation == VERTICAL) mRelY = offset;
    }

    void ConstraintWidget::immediateConnect(ConstraintAnchor::Type startType, ConstraintWidget* target, ConstraintAnchor::Type endType, int margin,
                                            int goneMargin) {
        ConstraintAnchor* startAnchor = getAnchor(startType);
        ConstraintAnchor* endAnchor = target->getAnchor(endType);
        startAnchor->connect(endAnchor, margin, goneMargin, true);
    }

    void ConstraintWidget::connect(ConstraintAnchor* from, ConstraintAnchor* to, int margin) {
        if (from->getOwner() == this) connect(from->getType(), to->getOwner(), to->getType(), margin);
    }

    void ConstraintWidget::connect(ConstraintAnchor::Type constraintFrom, ConstraintWidget* target, ConstraintAnchor::Type constraintTo, int margin) {
        if (constraintFrom == T::CENTER) {
            if (constraintTo == T::CENTER) {
                ConstraintAnchor* left = getAnchor(T::LEFT);
                ConstraintAnchor* right = getAnchor(T::RIGHT);
                ConstraintAnchor* top = getAnchor(T::TOP);
                ConstraintAnchor* bottom = getAnchor(T::BOTTOM);
                bool centerX = false;
                bool centerY = false;
                if ((left != nullptr && left->isConnected()) || (right != nullptr && right->isConnected())) {
                } else {
                    connect(T::LEFT, target, T::LEFT, 0);
                    connect(T::RIGHT, target, T::RIGHT, 0);
                    centerX = true;
                }
                if ((top != nullptr && top->isConnected()) || (bottom != nullptr && bottom->isConnected())) {
                } else {
                    connect(T::TOP, target, T::TOP, 0);
                    connect(T::BOTTOM, target, T::BOTTOM, 0);
                    centerY = true;
                }
                if (centerX && centerY) getAnchor(T::CENTER)->connect(target->getAnchor(T::CENTER), 0);
                else if (centerX) getAnchor(T::CENTER_X)->connect(target->getAnchor(T::CENTER_X), 0);
                else if (centerY) getAnchor(T::CENTER_Y)->connect(target->getAnchor(T::CENTER_Y), 0);
            } else if (constraintTo == T::LEFT || constraintTo == T::RIGHT) {
                connect(T::LEFT, target, constraintTo, 0);
                connect(T::RIGHT, target, constraintTo, 0);
                getAnchor(T::CENTER)->connect(target->getAnchor(constraintTo), 0);
            } else if (constraintTo == T::TOP || constraintTo == T::BOTTOM) {
                connect(T::TOP, target, constraintTo, 0);
                connect(T::BOTTOM, target, constraintTo, 0);
                getAnchor(T::CENTER)->connect(target->getAnchor(constraintTo), 0);
            }
        } else if (constraintFrom == T::CENTER_X && (constraintTo == T::LEFT || constraintTo == T::RIGHT)) {
            ConstraintAnchor* left = getAnchor(T::LEFT);
            ConstraintAnchor* targetAnchor = target->getAnchor(constraintTo);
            ConstraintAnchor* right = getAnchor(T::RIGHT);
            left->connect(targetAnchor, 0);
            right->connect(targetAnchor, 0);
            getAnchor(T::CENTER_X)->connect(targetAnchor, 0);
        } else if (constraintFrom == T::CENTER_Y && (constraintTo == T::TOP || constraintTo == T::BOTTOM)) {
            ConstraintAnchor* targetAnchor = target->getAnchor(constraintTo);
            getAnchor(T::TOP)->connect(targetAnchor, 0);
            getAnchor(T::BOTTOM)->connect(targetAnchor, 0);
            getAnchor(T::CENTER_Y)->connect(targetAnchor, 0);
        } else if (constraintFrom == T::CENTER_X && constraintTo == T::CENTER_X) {
            getAnchor(T::LEFT)->connect(target->getAnchor(T::LEFT), 0);
            getAnchor(T::RIGHT)->connect(target->getAnchor(T::RIGHT), 0);
            getAnchor(T::CENTER_X)->connect(target->getAnchor(constraintTo), 0);
        } else if (constraintFrom == T::CENTER_Y && constraintTo == T::CENTER_Y) {
            getAnchor(T::TOP)->connect(target->getAnchor(T::TOP), 0);
            getAnchor(T::BOTTOM)->connect(target->getAnchor(T::BOTTOM), 0);
            getAnchor(T::CENTER_Y)->connect(target->getAnchor(constraintTo), 0);
        } else {
            ConstraintAnchor* fromAnchor = getAnchor(constraintFrom);
            ConstraintAnchor* toAnchor = target->getAnchor(constraintTo);
            if (fromAnchor->isValidConnection(toAnchor)) {
                if (constraintFrom == T::BASELINE) {
                    ConstraintAnchor* top = getAnchor(T::TOP);
                    ConstraintAnchor* bottom = getAnchor(T::BOTTOM);
                    if (top != nullptr) top->reset();
                    if (bottom != nullptr) bottom->reset();
                } else if (constraintFrom == T::TOP || constraintFrom == T::BOTTOM) {
                    ConstraintAnchor* baseline = getAnchor(T::BASELINE);
                    if (baseline != nullptr) baseline->reset();
                    ConstraintAnchor* center = getAnchor(T::CENTER);
                    if (center->getTarget() != toAnchor) center->reset();
                    ConstraintAnchor* opposite = getAnchor(constraintFrom)->getOpposite();
                    ConstraintAnchor* centerY = getAnchor(T::CENTER_Y);
                    if (centerY->isConnected()) {
                        opposite->reset();
                        centerY->reset();
                    }
                } else if (constraintFrom == T::LEFT || constraintFrom == T::RIGHT) {
                    ConstraintAnchor* center = getAnchor(T::CENTER);
                    if (center->getTarget() != toAnchor) center->reset();
                    ConstraintAnchor* opposite = getAnchor(constraintFrom)->getOpposite();
                    ConstraintAnchor* centerX = getAnchor(T::CENTER_X);
                    if (centerX->isConnected()) {
                        opposite->reset();
                        centerX->reset();
                    }
                }
                fromAnchor->connect(toAnchor, margin);
            }
        }
    }

    void ConstraintWidget::resetAllConstraints() {
        resetAnchors();
        setVerticalBiasPercent(DEFAULT_BIAS);
        setHorizontalBiasPercent(DEFAULT_BIAS);
    }

    void ConstraintWidget::resetAnchor(ConstraintAnchor* anchor) {
        if (getParent() != nullptr && getParent()->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) {
            if (static_cast<ConstraintWidgetContainer*>(getParent())->handlesInternalConstraints()) return;
        }
        ConstraintAnchor* left = getAnchor(T::LEFT);
        ConstraintAnchor* right = getAnchor(T::RIGHT);
        ConstraintAnchor* top = getAnchor(T::TOP);
        ConstraintAnchor* bottom = getAnchor(T::BOTTOM);
        ConstraintAnchor* center = getAnchor(T::CENTER);
        ConstraintAnchor* centerX = getAnchor(T::CENTER_X);
        ConstraintAnchor* centerY = getAnchor(T::CENTER_Y);
        if (anchor == center) {
            if (left->isConnected() && right->isConnected() && left->getTarget() == right->getTarget()) {
                left->reset();
                right->reset();
            }
            if (top->isConnected() && bottom->isConnected() && top->getTarget() == bottom->getTarget()) {
                top->reset();
                bottom->reset();
            }
            mHorizontalBiasPercent = 0.5f;
            mVerticalBiasPercent = 0.5f;
        } else if (anchor == centerX) {
            if (left->isConnected() && right->isConnected() && left->getTarget()->getOwner() == right->getTarget()->getOwner()) {
                left->reset();
                right->reset();
            }
            mHorizontalBiasPercent = 0.5f;
        } else if (anchor == centerY) {
            if (top->isConnected() && bottom->isConnected() && top->getTarget()->getOwner() == bottom->getTarget()->getOwner()) {
                top->reset();
                bottom->reset();
            }
            mVerticalBiasPercent = 0.5f;
        } else if (anchor == left || anchor == right) {
            if (left->isConnected() && left->getTarget() == right->getTarget()) center->reset();
        } else if (anchor == top || anchor == bottom) {
            if (top->isConnected() && top->getTarget() == bottom->getTarget()) center->reset();
        }
        anchor->reset();
    }

    void ConstraintWidget::resetAnchors() {
        ConstraintWidget* parent = getParent();
        if (parent != nullptr && parent->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) {
            if (static_cast<ConstraintWidgetContainer*>(parent)->handlesInternalConstraints()) return;
        }
        for (ConstraintAnchor* anchor : mAnchors) anchor->reset();
    }

    ConstraintAnchor* ConstraintWidget::getAnchor(ConstraintAnchor::Type anchorType) {
        switch (anchorType) {
            case T::LEFT: return &mLeft;
            case T::TOP: return &mTop;
            case T::RIGHT: return &mRight;
            case T::BOTTOM: return &mBottom;
            case T::BASELINE: return &mBaseline;
            case T::CENTER_X: return &mCenterX;
            case T::CENTER_Y: return &mCenterY;
            case T::CENTER: return &mCenter;
            case T::NONE: return nullptr;
        }
        return nullptr;
    }

    bool ConstraintWidget::isInHorizontalChain() const {
        return (mLeft.mTarget != nullptr && mLeft.mTarget->mTarget == &mLeft) || (mRight.mTarget != nullptr && mRight.mTarget->mTarget == &mRight);
    }

    ConstraintWidget* ConstraintWidget::getPreviousChainMember(int orientation) const {
        if (orientation == HORIZONTAL) {
            if (mLeft.mTarget != nullptr && mLeft.mTarget->mTarget == &mLeft) return mLeft.mTarget->mOwner;
        } else if (orientation == VERTICAL) {
            if (mTop.mTarget != nullptr && mTop.mTarget->mTarget == &mTop) return mTop.mTarget->mOwner;
        }
        return nullptr;
    }

    ConstraintWidget* ConstraintWidget::getNextChainMember(int orientation) const {
        if (orientation == HORIZONTAL) {
            if (mRight.mTarget != nullptr && mRight.mTarget->mTarget == &mRight) return mRight.mTarget->mOwner;
        } else if (orientation == VERTICAL) {
            if (mBottom.mTarget != nullptr && mBottom.mTarget->mTarget == &mBottom) return mBottom.mTarget->mOwner;
        }
        return nullptr;
    }

    ConstraintWidget* ConstraintWidget::getHorizontalChainControlWidget() {
        ConstraintWidget* found = nullptr;
        if (isInHorizontalChain()) {
            ConstraintWidget* tmp = this;
            while (found == nullptr && tmp != nullptr) {
                ConstraintAnchor* anchor = tmp->getAnchor(T::LEFT);
                ConstraintAnchor* targetOwner = (anchor == nullptr) ? nullptr : anchor->getTarget();
                ConstraintWidget* target = (targetOwner == nullptr) ? nullptr : targetOwner->getOwner();
                if (target == getParent()) {
                    found = tmp;
                    break;
                }
                ConstraintAnchor* targetAnchor = (target == nullptr) ? nullptr : target->getAnchor(T::RIGHT)->getTarget();
                if (targetAnchor != nullptr && targetAnchor->getOwner() != tmp) found = tmp;
                else tmp = target;
            }
        }
        return found;
    }

    bool ConstraintWidget::isInVerticalChain() const {
        return (mTop.mTarget != nullptr && mTop.mTarget->mTarget == &mTop) || (mBottom.mTarget != nullptr && mBottom.mTarget->mTarget == &mBottom);
    }

    ConstraintWidget* ConstraintWidget::getVerticalChainControlWidget() {
        ConstraintWidget* found = nullptr;
        if (isInVerticalChain()) {
            ConstraintWidget* tmp = this;
            while (found == nullptr && tmp != nullptr) {
                ConstraintAnchor* anchor = tmp->getAnchor(T::TOP);
                ConstraintAnchor* targetOwner = (anchor == nullptr) ? nullptr : anchor->getTarget();
                ConstraintWidget* target = (targetOwner == nullptr) ? nullptr : targetOwner->getOwner();
                if (target == getParent()) {
                    found = tmp;
                    break;
                }
                ConstraintAnchor* targetAnchor = (target == nullptr) ? nullptr : target->getAnchor(T::BOTTOM)->getTarget();
                if (targetAnchor != nullptr && targetAnchor->getOwner() != tmp) found = tmp;
                else tmp = target;
            }
        }
        return found;
    }

    bool ConstraintWidget::isChainHead(int orientation) const {
        const int offset = orientation * 2;
        const ConstraintAnchor* a = mListAnchors[std::size_t(offset)];
        const ConstraintAnchor* b = mListAnchors[std::size_t(offset + 1)];
        return (a->mTarget != nullptr && a->mTarget->mTarget != a) && (b->mTarget != nullptr && b->mTarget->mTarget == b);
    }

    void ConstraintWidget::copy(const ConstraintWidget& src, const WidgetMap& map) {
        mHorizontalResolution = src.mHorizontalResolution;
        mVerticalResolution = src.mVerticalResolution;
        mMatchConstraintDefaultWidth = src.mMatchConstraintDefaultWidth;
        mMatchConstraintDefaultHeight = src.mMatchConstraintDefaultHeight;
        mResolvedMatchConstraintDefault = src.mResolvedMatchConstraintDefault;
        mMatchConstraintMinWidth = src.mMatchConstraintMinWidth;
        mMatchConstraintMaxWidth = src.mMatchConstraintMaxWidth;
        mMatchConstraintMinHeight = src.mMatchConstraintMinHeight;
        mMatchConstraintMaxHeight = src.mMatchConstraintMaxHeight;
        mMatchConstraintPercentHeight = src.mMatchConstraintPercentHeight;
        mIsWidthWrapContent = src.mIsWidthWrapContent;
        mIsHeightWrapContent = src.mIsHeightWrapContent;
        mResolvedDimensionRatioSide = src.mResolvedDimensionRatioSide;
        mResolvedDimensionRatio = src.mResolvedDimensionRatio;
        mMaxDimension = src.mMaxDimension;
        mCircleConstraintAngle = src.mCircleConstraintAngle;
        mHasBaseline = src.mHasBaseline;
        mInPlaceholder = src.mInPlaceholder;
        mLeft.reset();
        mTop.reset();
        mRight.reset();
        mBottom.reset();
        mBaseline.reset();
        mCenterX.reset();
        mCenterY.reset();
        mCenter.reset();
        // Upstream copies its own behaviours (Arrays.copyOf(mListDimensionBehaviors)), not src's.
        // Upstream tests its own parent, not src's, before mapping src's.
        auto mapped = [&](const ConstraintWidget* w) -> ConstraintWidget* {
            auto it = map.find(w);
            return it == map.end() ? nullptr : it->second;
        };
        mParent = (mParent == nullptr) ? nullptr : mapped(src.mParent);
        mWidth = src.mWidth;
        mHeight = src.mHeight;
        mDimensionRatio = src.mDimensionRatio;
        mDimensionRatioSide = src.mDimensionRatioSide;
        mX = src.mX;
        mY = src.mY;
        mRelX = src.mRelX;
        mRelY = src.mRelY;
        mOffsetX = src.mOffsetX;
        mOffsetY = src.mOffsetY;
        mBaselineDistance = src.mBaselineDistance;
        mMinWidth = src.mMinWidth;
        mMinHeight = src.mMinHeight;
        mHorizontalBiasPercent = src.mHorizontalBiasPercent;
        mVerticalBiasPercent = src.mVerticalBiasPercent;
        mCompanionWidget = src.mCompanionWidget;
        mContainerItemSkip = src.mContainerItemSkip;
        mVisibility = src.mVisibility;
        mAnimated = src.mAnimated;
        mDebugName = src.mDebugName;
        mType = src.mType;
        mDistToTop = src.mDistToTop;
        mDistToLeft = src.mDistToLeft;
        mDistToRight = src.mDistToRight;
        mDistToBottom = src.mDistToBottom;
        mLeftHasCentered = src.mLeftHasCentered;
        mRightHasCentered = src.mRightHasCentered;
        mTopHasCentered = src.mTopHasCentered;
        mBottomHasCentered = src.mBottomHasCentered;
        mHorizontalWrapVisited = src.mHorizontalWrapVisited;
        mVerticalWrapVisited = src.mVerticalWrapVisited;
        mHorizontalChainStyle = src.mHorizontalChainStyle;
        mVerticalChainStyle = src.mVerticalChainStyle;
        mHorizontalChainFixedPosition = src.mHorizontalChainFixedPosition;
        mVerticalChainFixedPosition = src.mVerticalChainFixedPosition;
        mWeight = src.mWeight;
        mListNextMatchConstraintsWidget = src.mListNextMatchConstraintsWidget;
        mNextChainWidget = src.mNextChainWidget;
        mHorizontalNextWidget = (src.mHorizontalNextWidget == nullptr) ? nullptr : mapped(src.mHorizontalNextWidget);
        mVerticalNextWidget = (src.mVerticalNextWidget == nullptr) ? nullptr : mapped(src.mVerticalNextWidget);
    }

}
