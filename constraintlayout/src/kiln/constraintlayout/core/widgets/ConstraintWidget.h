#pragma once

// constraintlayout-core widgets: ConstraintAnchor and ConstraintWidget, a line port. Names follow
// upstream. Java's instanceof becomes kind flags (ConstraintWidget::mKind); a Java null String is
// an empty string; an anchor's dependents keep their insertion order (upstream's identity HashSet
// has none to keep).

#include "kiln/constraintlayout/core/LinearSystem.h"

#include <array>
#include <climits>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kiln::constraintlayout::core {

    class ConstraintWidget;
    class ConstraintWidgetContainer;
    class WidgetGroup;
    class WidgetRun;
    class HorizontalWidgetRun;
    class VerticalWidgetRun;
    class ChainRun;

    using WidgetMap = std::unordered_map<const ConstraintWidget*, ConstraintWidget*>;

    class ConstraintAnchor {
    public:
        enum class Type { NONE, LEFT, TOP, RIGHT, BOTTOM, BASELINE, CENTER, CENTER_X, CENTER_Y };
        static constexpr int UNSET_GONE_MARGIN = INT_MIN;

        ConstraintAnchor(ConstraintWidget* owner, Type type) : mOwner(owner), mType(type) {}
        // Detaches from its target, and every anchor that targets it from it (Java's GC never
        // frees a target something still points at).
        ~ConstraintAnchor();
        ConstraintAnchor(const ConstraintAnchor&) = delete;
        ConstraintAnchor& operator=(const ConstraintAnchor&) = delete;

        void findDependents(int orientation, std::vector<std::shared_ptr<WidgetGroup>>& list, WidgetGroup* group);
        const std::vector<ConstraintAnchor*>& getDependents() const { return mDependents; }
        bool hasDependents() const { return !mDependents.empty(); }
        bool hasCenteredDependents() const;
        void setFinalValue(int finalValue) { mFinalValue = finalValue; mHasFinalValue = true; }
        int getFinalValue() const { return mHasFinalValue ? mFinalValue : 0; }
        void resetFinalResolution() { mHasFinalValue = false; mFinalValue = 0; }
        bool hasFinalValue() const { return mHasFinalValue; }
        void copyFrom(const ConstraintAnchor& source, const WidgetMap& map);
        SolverVariable* getSolverVariable() const { return mSolverVariable.get(); }
        void resetSolverVariable(Cache* cache);
        ConstraintWidget* getOwner() const { return mOwner; }
        Type getType() const { return mType; }
        int getMargin() const;
        ConstraintAnchor* getTarget() const { return mTarget; }
        void reset();
        bool connect(ConstraintAnchor* toAnchor, int margin, int goneMargin, bool forceConnection);
        bool connect(ConstraintAnchor* toAnchor, int margin) { return connect(toAnchor, margin, UNSET_GONE_MARGIN, false); }
        bool isConnected() const { return mTarget != nullptr; }
        bool isValidConnection(ConstraintAnchor* anchor) const;
        bool isSideAnchor() const;
        bool isSimilarDimensionConnection(ConstraintAnchor* anchor) const;
        void setMargin(int margin) { if (isConnected()) mMargin = margin; }
        void setGoneMargin(int margin) { if (isConnected()) mGoneMargin = margin; }
        bool isVerticalAnchor() const;
        std::string toString() const;
        bool isConnectionAllowed(ConstraintWidget* target, ConstraintAnchor* anchor) const { (void)anchor; return isConnectionAllowed(target); }
        bool isConnectionAllowed(ConstraintWidget* target) const;
        ConstraintAnchor* getOpposite() const;

        ConstraintWidget* const mOwner;
        const Type mType;
        ConstraintAnchor* mTarget = nullptr;
        int mMargin = 0;
        int mGoneMargin = UNSET_GONE_MARGIN;

    private:
        // Assigns mTarget, keeping mTargetedBy in step. mTargetedBy is for lifetime only:
        // unlike mDependents, reset() does not clear it.
        void setTarget(ConstraintAnchor* target);
        std::vector<ConstraintAnchor*> mTargetedBy;
        bool isConnectionToMe(ConstraintWidget* target, std::unordered_set<ConstraintWidget*>& checked) const;
        void addDependent(ConstraintAnchor* a);
        void removeDependent(ConstraintAnchor* a);
        std::vector<ConstraintAnchor*> mDependents;
        int mFinalValue = 0;
        bool mHasFinalValue = false;
        std::unique_ptr<SolverVariable> mSolverVariable;
    };

    class ConstraintWidget {
    public:
        // instanceof, as flags (a subclass sets its own and its bases').
        enum Kind : unsigned {
            KIND_WIDGET_CONTAINER = 1u << 0,
            KIND_CONSTRAINT_WIDGET_CONTAINER = 1u << 1,
            KIND_GUIDELINE = 1u << 2,
            KIND_HELPER_WIDGET = 1u << 3,   // HelperWidget, the only Helper
            KIND_BARRIER = 1u << 4,
            KIND_VIRTUAL_LAYOUT = 1u << 5,
            KIND_FLOW = 1u << 6,
            KIND_PLACEHOLDER = 1u << 7,
        };
        bool is(unsigned kind) const { return (mKind & kind) != 0; }
        unsigned mKind = 0;

        static constexpr int SOLVER = 1, DIRECT = 2;
        static constexpr int MATCH_CONSTRAINT_SPREAD = 0, MATCH_CONSTRAINT_WRAP = 1, MATCH_CONSTRAINT_PERCENT = 2, MATCH_CONSTRAINT_RATIO = 3,
                             MATCH_CONSTRAINT_RATIO_RESOLVED = 4;
        static constexpr int UNKNOWN = -1, HORIZONTAL = 0, VERTICAL = 1, BOTH = 2;
        static constexpr int VISIBLE = 0, INVISIBLE = 4, GONE = 8;
        static constexpr int CHAIN_SPREAD = 0, CHAIN_SPREAD_INSIDE = 1, CHAIN_PACKED = 2;
        static constexpr int WRAP_BEHAVIOR_INCLUDED = 0, WRAP_BEHAVIOR_HORIZONTAL_ONLY = 1, WRAP_BEHAVIOR_VERTICAL_ONLY = 2, WRAP_BEHAVIOR_SKIPPED = 3;
        static constexpr int ANCHOR_LEFT = 0, ANCHOR_RIGHT = 1, ANCHOR_TOP = 2, ANCHOR_BOTTOM = 3, ANCHOR_BASELINE = 4;
        static constexpr int DIMENSION_HORIZONTAL = 0, DIMENSION_VERTICAL = 1;
        static constexpr float DEFAULT_BIAS = 0.5f;
        enum class DimensionBehaviour { FIXED, WRAP_CONTENT, MATCH_CONSTRAINT, MATCH_PARENT };

        ConstraintWidget();
        explicit ConstraintWidget(const std::string& debugName);
        ConstraintWidget(int x, int y, int width, int height);
        ConstraintWidget(const std::string& debugName, int x, int y, int width, int height);
        ConstraintWidget(int width, int height);
        ConstraintWidget(const std::string& debugName, int width, int height);
        virtual ~ConstraintWidget();
        ConstraintWidget(const ConstraintWidget&) = delete;
        ConstraintWidget& operator=(const ConstraintWidget&) = delete;

        // --- analyzer state ---
        bool measured = false;
        std::array<WidgetRun*, 2> run{};
        std::shared_ptr<ChainRun> horizontalChainRun;
        std::shared_ptr<ChainRun> verticalChainRun;
        std::shared_ptr<HorizontalWidgetRun> mHorizontalRun;
        std::shared_ptr<VerticalWidgetRun> mVerticalRun;
        std::array<bool, 2> isTerminalWidget{ true, true };
        bool mResolvedHasRatio = false;
        std::string stringId;
        WidgetRun* getRun(int orientation) const;
        void ensureWidgetRuns();

        // --- final values from the direct resolution ---
        void setFinalFrame(int left, int top, int right, int bottom, int baseline, int orientation);
        void setFinalLeft(int x1) { mLeft.setFinalValue(x1); mX = x1; }
        void setFinalTop(int y1) { mTop.setFinalValue(y1); mY = y1; }
        void resetSolvingPassFlag() { mHorizontalSolvingPass = false; mVerticalSolvingPass = false; }
        bool isHorizontalSolvingPassDone() const { return mHorizontalSolvingPass; }
        bool isVerticalSolvingPassDone() const { return mVerticalSolvingPass; }
        void markHorizontalSolvingPassDone() { mHorizontalSolvingPass = true; }
        void markVerticalSolvingPassDone() { mVerticalSolvingPass = true; }
        void setFinalHorizontal(int x1, int x2);
        void setFinalVertical(int y1, int y2);
        void setFinalBaseline(int baselineValue);
        virtual bool isResolvedHorizontally() const { return mResolvedHorizontal || (mLeft.hasFinalValue() && mRight.hasFinalValue()); }
        virtual bool isResolvedVertically() const { return mResolvedVertical || (mTop.hasFinalValue() && mBottom.hasFinalValue()); }
        void resetFinalResolution();
        void ensureMeasureRequested() { mMeasureRequested = true; }
        bool hasDependencies() const;
        bool hasDanglingDimension(int orientation) const;
        bool hasResolvedTargets(int orientation, int size) const;

        int mHorizontalResolution = UNKNOWN;
        int mVerticalResolution = UNKNOWN;
        int mMatchConstraintDefaultWidth = MATCH_CONSTRAINT_SPREAD;
        int mMatchConstraintDefaultHeight = MATCH_CONSTRAINT_SPREAD;
        std::array<int, 2> mResolvedMatchConstraintDefault{};
        int mMatchConstraintMinWidth = 0;
        int mMatchConstraintMaxWidth = 0;
        float mMatchConstraintPercentWidth = 1;
        int mMatchConstraintMinHeight = 0;
        int mMatchConstraintMaxHeight = 0;
        float mMatchConstraintPercentHeight = 1;
        bool mIsWidthWrapContent = false;
        bool mIsHeightWrapContent = false;
        int mResolvedDimensionRatioSide = UNKNOWN;
        float mResolvedDimensionRatio = 1.0f;
        float mCircleConstraintAngle = NAN;

        bool isInVirtualLayout() const { return mInVirtualLayout; }
        void setInVirtualLayout(bool v) { mInVirtualLayout = v; }
        int getMaxHeight() const { return mMaxDimension[VERTICAL]; }
        int getMaxWidth() const { return mMaxDimension[HORIZONTAL]; }
        void setMaxWidth(int w) { mMaxDimension[HORIZONTAL] = w; }
        void setMaxHeight(int h) { mMaxDimension[VERTICAL] = h; }
        bool isSpreadWidth() const;
        bool isSpreadHeight() const;
        void setHasBaseline(bool v) { mHasBaseline = v; }
        bool getHasBaseline() const { return mHasBaseline; }
        bool isInPlaceholder() const { return mInPlaceholder; }
        void setInPlaceholder(bool v) { mInPlaceholder = v; }
        void setInBarrier(int orientation, bool value) { mIsInBarrier[std::size_t(orientation)] = value; }
        bool isInBarrier(int orientation) const { return mIsInBarrier[std::size_t(orientation)]; }
        void setMeasureRequested(bool v) { mMeasureRequested = v; }
        bool isMeasureRequested() const { return mMeasureRequested && mVisibility != GONE; }
        void setWrapBehaviorInParent(int behavior) { if (behavior >= 0 && behavior <= WRAP_BEHAVIOR_SKIPPED) mWrapBehaviorInParent = behavior; }
        int getWrapBehaviorInParent() const { return mWrapBehaviorInParent; }
        int getLastHorizontalMeasureSpec() const { return mLastHorizontalMeasureSpec; }
        int getLastVerticalMeasureSpec() const { return mLastVerticalMeasureSpec; }
        void setLastMeasureSpec(int horizontal, int vertical) {
            mLastHorizontalMeasureSpec = horizontal;
            mLastVerticalMeasureSpec = vertical;
            setMeasureRequested(false);
        }

        // --- anchors ---
        ConstraintAnchor mLeft{ this, ConstraintAnchor::Type::LEFT };
        ConstraintAnchor mTop{ this, ConstraintAnchor::Type::TOP };
        ConstraintAnchor mRight{ this, ConstraintAnchor::Type::RIGHT };
        ConstraintAnchor mBottom{ this, ConstraintAnchor::Type::BOTTOM };
        ConstraintAnchor mBaseline{ this, ConstraintAnchor::Type::BASELINE };
        ConstraintAnchor mCenterX{ this, ConstraintAnchor::Type::CENTER_X };
        ConstraintAnchor mCenterY{ this, ConstraintAnchor::Type::CENTER_Y };
        ConstraintAnchor mCenter{ this, ConstraintAnchor::Type::CENTER };
        std::array<ConstraintAnchor*, 6> mListAnchors{ &mLeft, &mRight, &mTop, &mBottom, &mBaseline, &mCenter };
        std::vector<ConstraintAnchor*> mAnchors;
        std::array<DimensionBehaviour, 2> mListDimensionBehaviors{ DimensionBehaviour::FIXED, DimensionBehaviour::FIXED };
        ConstraintWidget* mParent = nullptr;
        int mWidth = 0;
        int mHeight = 0;
        float mDimensionRatio = 0;
        int mDimensionRatioSide = UNKNOWN;
        int mX = 0;
        int mY = 0;
        int mRelX = 0;
        int mRelY = 0;
        int mOffsetX = 0;
        int mOffsetY = 0;
        int mBaselineDistance = 0;
        int mMinWidth = 0;
        int mMinHeight = 0;
        float mHorizontalBiasPercent = DEFAULT_BIAS;
        float mVerticalBiasPercent = DEFAULT_BIAS;
        int mDistToTop = 0, mDistToLeft = 0, mDistToRight = 0, mDistToBottom = 0;
        bool mLeftHasCentered = false, mRightHasCentered = false, mTopHasCentered = false, mBottomHasCentered = false;
        bool mHorizontalWrapVisited = false, mVerticalWrapVisited = false;
        bool mGroupsToSolver = false;
        int mHorizontalChainStyle = CHAIN_SPREAD;
        int mVerticalChainStyle = CHAIN_SPREAD;
        bool mHorizontalChainFixedPosition = false, mVerticalChainFixedPosition = false;
        std::array<float, 2> mWeight{ float(UNKNOWN), float(UNKNOWN) };
        std::array<ConstraintWidget*, 2> mListNextMatchConstraintsWidget{};
        std::array<ConstraintWidget*, 2> mNextChainWidget{};
        ConstraintWidget* mHorizontalNextWidget = nullptr;
        ConstraintWidget* mVerticalNextWidget = nullptr;
        int horizontalGroup = -1;
        int verticalGroup = -1;

        virtual void reset();
        bool oppositeDimensionDependsOn(int orientation) const;
        bool oppositeDimensionsTied() const;
        bool hasDimensionOverride() const { return mWidthOverride != -1 || mHeightOverride != -1; }
        virtual void resetSolverVariables(Cache* cache);
        bool isRoot() const { return mParent == nullptr; }
        ConstraintWidget* getParent() const { return mParent; }
        void setParent(ConstraintWidget* widget) { mParent = widget; }
        void setWidthWrapContent(bool v) { mIsWidthWrapContent = v; }
        bool isWidthWrapContent() const { return mIsWidthWrapContent; }
        void setHeightWrapContent(bool v) { mIsHeightWrapContent = v; }
        bool isHeightWrapContent() const { return mIsHeightWrapContent; }
        void connectCircularConstraint(ConstraintWidget* target, float angle, int radius);
        virtual std::string getType() const { return mType; }
        void setType(const std::string& type) { mType = type; }
        void setVisibility(int visibility) { mVisibility = visibility; }
        int getVisibility() const { return mVisibility; }
        void setAnimated(bool v) { mAnimated = v; }
        bool isAnimated() const { return mAnimated; }
        const std::string& getDebugName() const { return mDebugName; }
        void setDebugName(const std::string& name) { mDebugName = name; }
        void setDebugSolverName(LinearSystem& system, const std::string& name);
        void createObjectVariables(LinearSystem& system);
        std::string toString() const;
        int getX() const;
        int getY() const;
        int getWidth() const { return mVisibility == GONE ? 0 : mWidth; }
        int getOptimizerWrapWidth();
        int getOptimizerWrapHeight();
        int getHeight() const { return mVisibility == GONE ? 0 : mHeight; }
        int getLength(int orientation) const;
        int getRootX() const { return mX + mOffsetX; }
        int getRootY() const { return mY + mOffsetY; }
        int getMinWidth() const { return mMinWidth; }
        int getMinHeight() const { return mMinHeight; }
        int getLeft() const { return getX(); }
        int getTop() const { return getY(); }
        int getRight() const { return getX() + mWidth; }
        int getBottom() const { return getY() + mHeight; }
        int getHorizontalMargin() const { return mLeft.mMargin + mRight.mMargin; }
        int getVerticalMargin() const { return mTop.mMargin + mBottom.mMargin; }
        float getHorizontalBiasPercent() const { return mHorizontalBiasPercent; }
        float getVerticalBiasPercent() const { return mVerticalBiasPercent; }
        float getBiasPercent(int orientation) const;
        bool hasBaseline() const { return mHasBaseline; }
        int getBaselineDistance() const { return mBaselineDistance; }
        void* getCompanionWidget() const { return mCompanionWidget; }
        const std::vector<ConstraintAnchor*>& getAnchors() const { return mAnchors; }
        void setX(int x) { mX = x; }
        void setY(int y) { mY = y; }
        void setOrigin(int x, int y) { mX = x; mY = y; }
        virtual void setOffset(int x, int y) { mOffsetX = x; mOffsetY = y; }
        void setGoneMargin(ConstraintAnchor::Type type, int goneMargin);
        void setWidth(int w) { mWidth = w; if (mWidth < mMinWidth) mWidth = mMinWidth; }
        void setHeight(int h) { mHeight = h; if (mHeight < mMinHeight) mHeight = mMinHeight; }
        void setLength(int length, int orientation);
        void setHorizontalMatchStyle(int horizontalMatchStyle, int min, int max, float percent);
        void setVerticalMatchStyle(int verticalMatchStyle, int min, int max, float percent);
        void setDimensionRatio(const std::string& ratio);
        void setDimensionRatio(float ratio, int dimensionRatioSide) { mDimensionRatio = ratio; mDimensionRatioSide = dimensionRatioSide; }
        float getDimensionRatio() const { return mDimensionRatio; }
        int getDimensionRatioSide() const { return mDimensionRatioSide; }
        void setHorizontalBiasPercent(float v) { mHorizontalBiasPercent = v; }
        void setVerticalBiasPercent(float v) { mVerticalBiasPercent = v; }
        void setMinWidth(int w) { mMinWidth = w < 0 ? 0 : w; }
        void setMinHeight(int h) { mMinHeight = h < 0 ? 0 : h; }
        void setDimension(int w, int h);
        void setFrame(int left, int top, int right, int bottom);
        void setFrame(int start, int end, int orientation);
        void setHorizontalDimension(int left, int right);
        void setVerticalDimension(int top, int bottom);
        int getRelativePositioning(int orientation) const;
        void setRelativePositioning(int offset, int orientation);
        void setBaselineDistance(int baseline) { mBaselineDistance = baseline; mHasBaseline = baseline > 0; }
        void setCompanionWidget(void* companion) { mCompanionWidget = companion; }
        void setContainerItemSkip(int skip) { mContainerItemSkip = skip >= 0 ? skip : 0; }
        int getContainerItemSkip() const { return mContainerItemSkip; }
        void setHorizontalWeight(float w) { mWeight[DIMENSION_HORIZONTAL] = w; }
        void setVerticalWeight(float w) { mWeight[DIMENSION_VERTICAL] = w; }
        void setHorizontalChainStyle(int s) { mHorizontalChainStyle = s; }
        int getHorizontalChainStyle() const { return mHorizontalChainStyle; }
        void setVerticalChainStyle(int s) { mVerticalChainStyle = s; }
        int getVerticalChainStyle() const { return mVerticalChainStyle; }
        virtual bool allowedInBarrier() const { return mVisibility != GONE; }
        void immediateConnect(ConstraintAnchor::Type startType, ConstraintWidget* target, ConstraintAnchor::Type endType, int margin, int goneMargin);
        void connect(ConstraintAnchor* from, ConstraintAnchor* to, int margin);
        void connect(ConstraintAnchor::Type constraintFrom, ConstraintWidget* target, ConstraintAnchor::Type constraintTo) {
            connect(constraintFrom, target, constraintTo, 0);
        }
        void connect(ConstraintAnchor::Type constraintFrom, ConstraintWidget* target, ConstraintAnchor::Type constraintTo, int margin);
        void resetAllConstraints();
        void resetAnchor(ConstraintAnchor* anchor);
        void resetAnchors();
        virtual ConstraintAnchor* getAnchor(ConstraintAnchor::Type anchorType);
        DimensionBehaviour getHorizontalDimensionBehaviour() const { return mListDimensionBehaviors[DIMENSION_HORIZONTAL]; }
        DimensionBehaviour getVerticalDimensionBehaviour() const { return mListDimensionBehaviors[DIMENSION_VERTICAL]; }
        DimensionBehaviour getDimensionBehaviour(int orientation) const {
            return orientation == HORIZONTAL ? getHorizontalDimensionBehaviour() : getVerticalDimensionBehaviour();
        }
        void setHorizontalDimensionBehaviour(DimensionBehaviour b) { mListDimensionBehaviors[DIMENSION_HORIZONTAL] = b; }
        void setVerticalDimensionBehaviour(DimensionBehaviour b) { mListDimensionBehaviors[DIMENSION_VERTICAL] = b; }
        bool isInHorizontalChain() const;
        ConstraintWidget* getPreviousChainMember(int orientation) const;
        ConstraintWidget* getNextChainMember(int orientation) const;
        ConstraintWidget* getHorizontalChainControlWidget();
        bool isInVerticalChain() const;
        ConstraintWidget* getVerticalChainControlWidget();
        virtual void addToSolver(LinearSystem& system, bool optimize);
        bool addFirst() const { return is(KIND_VIRTUAL_LAYOUT) || is(KIND_GUIDELINE); }
        void setupDimensionRatio(bool hParentWrapContent, bool vParentWrapContent, bool horizontalDimensionFixed, bool verticalDimensionFixed);
        virtual void updateFromSolver(LinearSystem& system, bool optimize);
        virtual void copy(const ConstraintWidget& src, const WidgetMap& map);
        virtual void updateFromRuns(bool updateHorizontal, bool updateVertical);
        void addChildrenToSolverByDependency(ConstraintWidgetContainer* container, LinearSystem& system,
                                             std::unordered_set<ConstraintWidget*>& widgets, int orientation, bool addSelf);

    protected:
        bool mResolvedHorizontal = false;
        bool mResolvedVertical = false;

    private:
        static constexpr int WRAP = -2;
        static constexpr bool USE_WRAP_DIMENSION_FOR_SPREAD = false;
        void addAnchors();
        bool isChainHead(int orientation) const;
        void applyConstraints(LinearSystem& system, bool isHorizontal, bool parentWrapContent, bool oppositeParentWrapContent, bool isTerminal,
                              SolverVariable* parentMin, SolverVariable* parentMax, DimensionBehaviour dimensionBehaviour, bool wrapContent,
                              ConstraintAnchor* beginAnchor, ConstraintAnchor* endAnchor, int beginPosition, int dimension, int minDimension,
                              int maxDimension, float bias, bool useRatio, bool oppositeVariable, bool inChain, bool oppositeInChain, bool inBarrier,
                              int matchConstraintDefault, int oppositeMatchConstraintDefault, int matchMinDimension, int matchMaxDimension,
                              float matchPercentDimension, bool applyPosition);

        bool mMeasureRequested = true;
        bool mOptimizeWrapO = false;
        bool mOptimizeWrapOnResolved = true;
        int mWidthOverride = -1;
        int mHeightOverride = -1;
        bool mHorizontalSolvingPass = false;
        bool mVerticalSolvingPass = false;
        int mWrapBehaviorInParent = WRAP_BEHAVIOR_INCLUDED;
        std::array<int, 2> mMaxDimension{ INT_MAX, INT_MAX };
        bool mHasBaseline = false;
        bool mInPlaceholder = false;
        bool mInVirtualLayout = false;
        int mLastHorizontalMeasureSpec = 0;
        int mLastVerticalMeasureSpec = 0;
        std::array<bool, 2> mIsInBarrier{};
        void* mCompanionWidget = nullptr;
        int mContainerItemSkip = 0;
        int mVisibility = VISIBLE;
        bool mAnimated = false;
        std::string mDebugName;
        std::string mType;
    };

}
