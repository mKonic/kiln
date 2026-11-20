#include "kiln/constraintlayout/core/widgets/ChainHead.h"

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    bool ChainHead::isMatchConstraintEqualityCandidate(const ConstraintWidget* widget, int orientation) {
        return widget->getVisibility() != CW::GONE && widget->mListDimensionBehaviors[std::size_t(orientation)] == DB::MATCH_CONSTRAINT &&
               (widget->mResolvedMatchConstraintDefault[std::size_t(orientation)] == CW::MATCH_CONSTRAINT_SPREAD ||
                widget->mResolvedMatchConstraintDefault[std::size_t(orientation)] == CW::MATCH_CONSTRAINT_RATIO);
    }

    void ChainHead::defineChainProperties() {
        const int offset = mOrientation * 2;
        const std::size_t o = std::size_t(mOrientation);
        ConstraintWidget* lastVisited = mFirst;
        mOptimizable = true;
        ConstraintWidget* widget = mFirst;
        ConstraintWidget* next = mFirst;
        bool done = false;
        while (!done) {
            mWidgetsCount++;
            widget->mNextChainWidget[o] = nullptr;
            widget->mListNextMatchConstraintsWidget[o] = nullptr;
            if (widget->getVisibility() != CW::GONE) {
                mVisibleWidgets++;
                if (widget->getDimensionBehaviour(mOrientation) != DB::MATCH_CONSTRAINT) mTotalSize += widget->getLength(mOrientation);
                mTotalSize += widget->mListAnchors[std::size_t(offset)]->getMargin();
                mTotalSize += widget->mListAnchors[std::size_t(offset + 1)]->getMargin();
                mTotalMargins += widget->mListAnchors[std::size_t(offset)]->getMargin();
                mTotalMargins += widget->mListAnchors[std::size_t(offset + 1)]->getMargin();
                if (mFirstVisibleWidget == nullptr) mFirstVisibleWidget = widget;
                mLastVisibleWidget = widget;
                if (widget->mListDimensionBehaviors[o] == DB::MATCH_CONSTRAINT) {
                    const int def = widget->mResolvedMatchConstraintDefault[o];
                    if (def == CW::MATCH_CONSTRAINT_SPREAD || def == CW::MATCH_CONSTRAINT_RATIO || def == CW::MATCH_CONSTRAINT_PERCENT) {
                        mWidgetsMatchCount++;
                        const float weight = widget->mWeight[o];
                        if (weight > 0) mTotalWeight += widget->mWeight[o];
                        if (isMatchConstraintEqualityCandidate(widget, mOrientation)) {
                            if (weight < 0) mHasUndefinedWeights = true;
                            else mHasDefinedWeights = true;
                            mWeightedMatchConstraintsWidgets.push_back(widget);
                        }
                        if (mFirstMatchConstraintWidget == nullptr) mFirstMatchConstraintWidget = widget;
                        if (mLastMatchConstraintWidget != nullptr) mLastMatchConstraintWidget->mListNextMatchConstraintsWidget[o] = widget;
                        mLastMatchConstraintWidget = widget;
                    }
                    if (mOrientation == CW::HORIZONTAL) {
                        if (widget->mMatchConstraintDefaultWidth != CW::MATCH_CONSTRAINT_SPREAD) mOptimizable = false;
                        else if (widget->mMatchConstraintMinWidth != 0 || widget->mMatchConstraintMaxWidth != 0) mOptimizable = false;
                    } else {
                        if (widget->mMatchConstraintDefaultHeight != CW::MATCH_CONSTRAINT_SPREAD) mOptimizable = false;
                        else if (widget->mMatchConstraintMinHeight != 0 || widget->mMatchConstraintMaxHeight != 0) mOptimizable = false;
                    }
                    if (widget->mDimensionRatio != 0.0f) {
                        mOptimizable = false;
                        mHasRatio = true;
                    }
                }
            }
            if (lastVisited != widget) lastVisited->mNextChainWidget[o] = widget;
            lastVisited = widget;
            ConstraintAnchor* nextAnchor = widget->mListAnchors[std::size_t(offset + 1)]->mTarget;
            if (nextAnchor != nullptr) {
                next = nextAnchor->mOwner;
                if (next->mListAnchors[std::size_t(offset)]->mTarget == nullptr || next->mListAnchors[std::size_t(offset)]->mTarget->mOwner != widget) next = nullptr;
            } else {
                next = nullptr;
            }
            if (next != nullptr) widget = next;
            else done = true;
        }
        if (mFirstVisibleWidget != nullptr) mTotalSize -= mFirstVisibleWidget->mListAnchors[std::size_t(offset)]->getMargin();
        if (mLastVisibleWidget != nullptr) mTotalSize -= mLastVisibleWidget->mListAnchors[std::size_t(offset + 1)]->getMargin();
        mLast = widget;
        if (mOrientation == CW::HORIZONTAL && mIsRtl) mHead = mLast;
        else mHead = mFirst;
        mHasComplexMatchWeights = mHasDefinedWeights && mHasUndefinedWeights;
    }

}
