#pragma once

// ChainHead (constraintlayout-core): a chain's first, last and visible widgets, its match
// constraint widgets and weights, found from its first widget.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <vector>

namespace kiln::constraintlayout::core {

    class ChainHead {
    public:
        ChainHead(ConstraintWidget* first, int orientation, bool isRtl) : mFirst(first), mOrientation(orientation), mIsRtl(isRtl) {}

        ConstraintWidget* getFirst() const { return mFirst; }
        ConstraintWidget* getFirstVisibleWidget() const { return mFirstVisibleWidget; }
        ConstraintWidget* getLast() const { return mLast; }
        ConstraintWidget* getLastVisibleWidget() const { return mLastVisibleWidget; }
        ConstraintWidget* getHead() const { return mHead; }
        ConstraintWidget* getFirstMatchConstraintWidget() const { return mFirstMatchConstraintWidget; }
        ConstraintWidget* getLastMatchConstraintWidget() const { return mLastMatchConstraintWidget; }
        float getTotalWeight() const { return mTotalWeight; }
        void define() {
            if (!mDefined) defineChainProperties();
            mDefined = true;
        }

        ConstraintWidget* mFirst = nullptr;
        ConstraintWidget* mFirstVisibleWidget = nullptr;
        ConstraintWidget* mLast = nullptr;
        ConstraintWidget* mLastVisibleWidget = nullptr;
        ConstraintWidget* mHead = nullptr;
        ConstraintWidget* mFirstMatchConstraintWidget = nullptr;
        ConstraintWidget* mLastMatchConstraintWidget = nullptr;
        // Upstream's list is null until the first one; empty here means null.
        std::vector<ConstraintWidget*> mWeightedMatchConstraintsWidgets;
        int mWidgetsCount = 0;
        int mWidgetsMatchCount = 0;
        float mTotalWeight = 0.0f;
        int mVisibleWidgets = 0;
        int mTotalSize = 0;
        int mTotalMargins = 0;
        bool mOptimizable = false;
        bool mHasUndefinedWeights = false;
        bool mHasDefinedWeights = false;
        bool mHasComplexMatchWeights = false;
        bool mHasRatio = false;

    private:
        static bool isMatchConstraintEqualityCandidate(const ConstraintWidget* widget, int orientation);
        void defineChainProperties();
        int mOrientation;
        bool mIsRtl = false;
        bool mDefined = false;
    };

}
