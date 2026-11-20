// ConstraintWidget's solver side: addToSolver, setupDimensionRatio, applyConstraints and the
// updates from the solver and the runs (upstream ConstraintWidget.java, second half).
#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/Optimizer.h"
#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include <algorithm>
#include <numbers>

namespace kiln::constraintlayout::core {

    using T = ConstraintAnchor::Type;
    using DB = ConstraintWidget::DimensionBehaviour;
    using SV = SolverVariable;

    void ConstraintWidget::addToSolver(LinearSystem& system, bool optimize) {
        SolverVariable* left = system.createObjectVariable(&mLeft);
        SolverVariable* right = system.createObjectVariable(&mRight);
        SolverVariable* top = system.createObjectVariable(&mTop);
        SolverVariable* bottom = system.createObjectVariable(&mBottom);
        SolverVariable* baseline = system.createObjectVariable(&mBaseline);

        bool horizontalParentWrapContent = false;
        bool verticalParentWrapContent = false;
        if (mParent != nullptr) {
            horizontalParentWrapContent = mParent->mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::WRAP_CONTENT;
            verticalParentWrapContent = mParent->mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::WRAP_CONTENT;
            switch (mWrapBehaviorInParent) {
                case WRAP_BEHAVIOR_SKIPPED:
                    horizontalParentWrapContent = false;
                    verticalParentWrapContent = false;
                    break;
                case WRAP_BEHAVIOR_HORIZONTAL_ONLY: verticalParentWrapContent = false; break;
                case WRAP_BEHAVIOR_VERTICAL_ONLY: horizontalParentWrapContent = false; break;
                default: break;
            }
        }

        if (!(mVisibility != GONE || mAnimated || hasDependencies() || mIsInBarrier[HORIZONTAL] || mIsInBarrier[VERTICAL])) return;

        if (mResolvedHorizontal || mResolvedVertical) {
            if (mResolvedHorizontal) {
                system.addEquality(left, mX);
                system.addEquality(right, mX + mWidth);
                if (horizontalParentWrapContent && mParent != nullptr) {
                    if (mOptimizeWrapOnResolved) {
                        auto* container = static_cast<ConstraintWidgetContainer*>(mParent);
                        container->addHorizontalWrapMinVariable(&mLeft);
                        container->addHorizontalWrapMaxVariable(&mRight);
                    } else {
                        system.addGreaterThan(system.createObjectVariable(&mParent->mRight), right, 0, SV::STRENGTH_EQUALITY);
                    }
                }
            }
            if (mResolvedVertical) {
                system.addEquality(top, mY);
                system.addEquality(bottom, mY + mHeight);
                if (mBaseline.hasDependents()) system.addEquality(baseline, mY + mBaselineDistance);
                if (verticalParentWrapContent && mParent != nullptr) {
                    if (mOptimizeWrapOnResolved) {
                        auto* container = static_cast<ConstraintWidgetContainer*>(mParent);
                        container->addVerticalWrapMinVariable(&mTop);
                        container->addVerticalWrapMaxVariable(&mBottom);
                    } else {
                        system.addGreaterThan(system.createObjectVariable(&mParent->mBottom), bottom, 0, SV::STRENGTH_EQUALITY);
                    }
                }
            }
            if (mResolvedHorizontal && mResolvedVertical) {
                mResolvedHorizontal = false;
                mResolvedVertical = false;
                return;
            }
        }

        if (optimize && mHorizontalRun != nullptr && mVerticalRun != nullptr && mHorizontalRun->start.resolved && mHorizontalRun->end.resolved &&
            mVerticalRun->start.resolved && mVerticalRun->end.resolved) {
            system.addEquality(left, mHorizontalRun->start.value);
            system.addEquality(right, mHorizontalRun->end.value);
            system.addEquality(top, mVerticalRun->start.value);
            system.addEquality(bottom, mVerticalRun->end.value);
            system.addEquality(baseline, mVerticalRun->baseline.value);
            if (mParent != nullptr) {
                if (horizontalParentWrapContent && isTerminalWidget[HORIZONTAL] && !isInHorizontalChain()) {
                    SolverVariable* parentMax = system.createObjectVariable(&mParent->mRight);
                    system.addGreaterThan(parentMax, right, 0, SV::STRENGTH_FIXED);
                }
                if (verticalParentWrapContent && isTerminalWidget[VERTICAL] && !isInVerticalChain()) {
                    SolverVariable* parentMax = system.createObjectVariable(&mParent->mBottom);
                    system.addGreaterThan(parentMax, bottom, 0, SV::STRENGTH_FIXED);
                }
            }
            mResolvedHorizontal = false;
            mResolvedVertical = false;
            return;
        }

        bool inHorizontalChain = false;
        bool inVerticalChain = false;
        if (mParent != nullptr) {
            if (isChainHead(HORIZONTAL)) {
                static_cast<ConstraintWidgetContainer*>(mParent)->addChain(this, HORIZONTAL);
                inHorizontalChain = true;
            } else {
                inHorizontalChain = isInHorizontalChain();
            }
            if (isChainHead(VERTICAL)) {
                static_cast<ConstraintWidgetContainer*>(mParent)->addChain(this, VERTICAL);
                inVerticalChain = true;
            } else {
                inVerticalChain = isInVerticalChain();
            }
            if (!inHorizontalChain && horizontalParentWrapContent && mVisibility != GONE && mLeft.mTarget == nullptr && mRight.mTarget == nullptr) {
                SolverVariable* parentRight = system.createObjectVariable(&mParent->mRight);
                system.addGreaterThan(parentRight, right, 0, SV::STRENGTH_LOW);
            }
            // Upstream also requires `mBaseline == null`, which never holds: this never applies.
        }

        int width = mWidth;
        if (width < mMinWidth) width = mMinWidth;
        int height = mHeight;
        if (height < mMinHeight) height = mMinHeight;

        const bool horizontalDimensionFixed = mListDimensionBehaviors[DIMENSION_HORIZONTAL] != DB::MATCH_CONSTRAINT;
        const bool verticalDimensionFixed = mListDimensionBehaviors[DIMENSION_VERTICAL] != DB::MATCH_CONSTRAINT;

        bool useRatio = false;
        mResolvedDimensionRatioSide = mDimensionRatioSide;
        mResolvedDimensionRatio = mDimensionRatio;
        int matchConstraintDefaultWidth = mMatchConstraintDefaultWidth;
        int matchConstraintDefaultHeight = mMatchConstraintDefaultHeight;

        if (mDimensionRatio > 0 && mVisibility != GONE) {
            useRatio = true;
            if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::MATCH_CONSTRAINT && matchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD)
                matchConstraintDefaultWidth = MATCH_CONSTRAINT_RATIO;
            if (mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::MATCH_CONSTRAINT && matchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD)
                matchConstraintDefaultHeight = MATCH_CONSTRAINT_RATIO;

            if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::MATCH_CONSTRAINT && mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::MATCH_CONSTRAINT &&
                matchConstraintDefaultWidth == MATCH_CONSTRAINT_RATIO && matchConstraintDefaultHeight == MATCH_CONSTRAINT_RATIO) {
                setupDimensionRatio(horizontalParentWrapContent, verticalParentWrapContent, horizontalDimensionFixed, verticalDimensionFixed);
            } else if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::MATCH_CONSTRAINT && matchConstraintDefaultWidth == MATCH_CONSTRAINT_RATIO) {
                mResolvedDimensionRatioSide = HORIZONTAL;
                width = JavaInt(mResolvedDimensionRatio * float(mHeight));
                if (mListDimensionBehaviors[DIMENSION_VERTICAL] != DB::MATCH_CONSTRAINT) {
                    matchConstraintDefaultWidth = MATCH_CONSTRAINT_RATIO_RESOLVED;
                    useRatio = false;
                }
            } else if (mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::MATCH_CONSTRAINT && matchConstraintDefaultHeight == MATCH_CONSTRAINT_RATIO) {
                mResolvedDimensionRatioSide = VERTICAL;
                if (mDimensionRatioSide == UNKNOWN) mResolvedDimensionRatio = 1 / mResolvedDimensionRatio;
                height = JavaInt(mResolvedDimensionRatio * float(mWidth));
                if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] != DB::MATCH_CONSTRAINT) {
                    matchConstraintDefaultHeight = MATCH_CONSTRAINT_RATIO_RESOLVED;
                    useRatio = false;
                }
            }
        }

        mResolvedMatchConstraintDefault[HORIZONTAL] = matchConstraintDefaultWidth;
        mResolvedMatchConstraintDefault[VERTICAL] = matchConstraintDefaultHeight;
        mResolvedHasRatio = useRatio;

        const bool useHorizontalRatio = useRatio && (mResolvedDimensionRatioSide == HORIZONTAL || mResolvedDimensionRatioSide == UNKNOWN);
        const bool useVerticalRatio = useRatio && (mResolvedDimensionRatioSide == VERTICAL || mResolvedDimensionRatioSide == UNKNOWN);

        bool wrapContent = mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::WRAP_CONTENT && is(KIND_CONSTRAINT_WIDGET_CONTAINER);
        if (wrapContent) width = 0;

        bool applyPosition = true;
        if (mCenter.isConnected()) applyPosition = false;

        const bool isInHorizontalBarrier = mIsInBarrier[HORIZONTAL];
        const bool isInVerticalBarrier = mIsInBarrier[VERTICAL];

        if (mHorizontalResolution != DIRECT && !mResolvedHorizontal) {
            if (!optimize || !(mHorizontalRun != nullptr && mHorizontalRun->start.resolved && mHorizontalRun->end.resolved)) {
                SolverVariable* parentMax = mParent != nullptr ? system.createObjectVariable(&mParent->mRight) : nullptr;
                SolverVariable* parentMin = mParent != nullptr ? system.createObjectVariable(&mParent->mLeft) : nullptr;
                applyConstraints(system, true, horizontalParentWrapContent, verticalParentWrapContent, isTerminalWidget[HORIZONTAL], parentMin, parentMax,
                                 mListDimensionBehaviors[DIMENSION_HORIZONTAL], wrapContent, &mLeft, &mRight, mX, width, mMinWidth,
                                 mMaxDimension[HORIZONTAL], mHorizontalBiasPercent, useHorizontalRatio,
                                 mListDimensionBehaviors[VERTICAL] == DB::MATCH_CONSTRAINT, inHorizontalChain, inVerticalChain, isInHorizontalBarrier,
                                 matchConstraintDefaultWidth, matchConstraintDefaultHeight, mMatchConstraintMinWidth, mMatchConstraintMaxWidth,
                                 mMatchConstraintPercentWidth, applyPosition);
            } else if (optimize) {
                system.addEquality(left, mHorizontalRun->start.value);
                system.addEquality(right, mHorizontalRun->end.value);
                if (mParent != nullptr) {
                    if (horizontalParentWrapContent && isTerminalWidget[HORIZONTAL] && !isInHorizontalChain()) {
                        SolverVariable* parentMax = system.createObjectVariable(&mParent->mRight);
                        system.addGreaterThan(parentMax, right, 0, SV::STRENGTH_FIXED);
                    }
                }
            }
        }

        bool applyVerticalConstraints = true;
        if (optimize && mVerticalRun != nullptr && mVerticalRun->start.resolved && mVerticalRun->end.resolved) {
            system.addEquality(top, mVerticalRun->start.value);
            system.addEquality(bottom, mVerticalRun->end.value);
            system.addEquality(baseline, mVerticalRun->baseline.value);
            if (mParent != nullptr) {
                if (!inVerticalChain && verticalParentWrapContent && isTerminalWidget[VERTICAL]) {
                    SolverVariable* parentMax = system.createObjectVariable(&mParent->mBottom);
                    system.addGreaterThan(parentMax, bottom, 0, SV::STRENGTH_FIXED);
                }
            }
            applyVerticalConstraints = false;
        }
        if (mVerticalResolution == DIRECT) applyVerticalConstraints = false;
        if (applyVerticalConstraints && !mResolvedVertical) {
            wrapContent = mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::WRAP_CONTENT && is(KIND_CONSTRAINT_WIDGET_CONTAINER);
            if (wrapContent) height = 0;

            SolverVariable* parentMax = mParent != nullptr ? system.createObjectVariable(&mParent->mBottom) : nullptr;
            SolverVariable* parentMin = mParent != nullptr ? system.createObjectVariable(&mParent->mTop) : nullptr;

            if (mBaselineDistance > 0 || mVisibility == GONE) {
                if (mBaseline.mTarget != nullptr) {
                    system.addEquality(baseline, top, getBaselineDistance(), SV::STRENGTH_FIXED);
                    SolverVariable* baselineTarget = system.createObjectVariable(mBaseline.mTarget);
                    const int baselineMargin = mBaseline.getMargin();
                    system.addEquality(baseline, baselineTarget, baselineMargin, SV::STRENGTH_FIXED);
                    applyPosition = false;
                    if (verticalParentWrapContent) {
                        SolverVariable* end = system.createObjectVariable(&mBottom);
                        system.addGreaterThan(parentMax, end, 0, SV::STRENGTH_EQUALITY);
                    }
                } else if (mVisibility == GONE) {
                    system.addEquality(baseline, top, mBaseline.getMargin(), SV::STRENGTH_FIXED);
                } else {
                    system.addEquality(baseline, top, getBaselineDistance(), SV::STRENGTH_FIXED);
                }
            }

            applyConstraints(system, false, verticalParentWrapContent, horizontalParentWrapContent, isTerminalWidget[VERTICAL], parentMin, parentMax,
                             mListDimensionBehaviors[DIMENSION_VERTICAL], wrapContent, &mTop, &mBottom, mY, height, mMinHeight,
                             mMaxDimension[VERTICAL], mVerticalBiasPercent, useVerticalRatio, mListDimensionBehaviors[HORIZONTAL] == DB::MATCH_CONSTRAINT,
                             inVerticalChain, inHorizontalChain, isInVerticalBarrier, matchConstraintDefaultHeight, matchConstraintDefaultWidth,
                             mMatchConstraintMinHeight, mMatchConstraintMaxHeight, mMatchConstraintPercentHeight, applyPosition);
        }

        if (useRatio) {
            const int strength = SV::STRENGTH_FIXED;
            if (mResolvedDimensionRatioSide == VERTICAL) system.addRatio(bottom, top, right, left, mResolvedDimensionRatio, strength);
            else system.addRatio(right, left, bottom, top, mResolvedDimensionRatio, strength);
        }

        if (mCenter.isConnected()) {
            // Math.toRadians(angle + 90) in double, then (float).
            const double radians = double(mCircleConstraintAngle + 90.0f) * (std::numbers::pi / 180.0);   // (float) Math.toRadians(angle + 90)
            system.addCenterPoint(this, mCenter.getTarget()->getOwner(), float(radians), mCenter.getMargin());
        }
        mResolvedHorizontal = false;
        mResolvedVertical = false;
    }

    void ConstraintWidget::setupDimensionRatio(bool /*hParentWrapContent*/, bool /*vParentWrapContent*/, bool horizontalDimensionFixed,
                                               bool verticalDimensionFixed) {
        if (mResolvedDimensionRatioSide == UNKNOWN) {
            if (horizontalDimensionFixed && !verticalDimensionFixed) {
                mResolvedDimensionRatioSide = HORIZONTAL;
            } else if (!horizontalDimensionFixed && verticalDimensionFixed) {
                mResolvedDimensionRatioSide = VERTICAL;
                if (mDimensionRatioSide == UNKNOWN) mResolvedDimensionRatio = 1 / mResolvedDimensionRatio;
            }
        }
        if (mResolvedDimensionRatioSide == HORIZONTAL && !(mTop.isConnected() && mBottom.isConnected())) {
            mResolvedDimensionRatioSide = VERTICAL;
        } else if (mResolvedDimensionRatioSide == VERTICAL && !(mLeft.isConnected() && mRight.isConnected())) {
            mResolvedDimensionRatioSide = HORIZONTAL;
        }
        if (mResolvedDimensionRatioSide == UNKNOWN) {
            if (!(mTop.isConnected() && mBottom.isConnected() && mLeft.isConnected() && mRight.isConnected())) {
                if (mTop.isConnected() && mBottom.isConnected()) {
                    mResolvedDimensionRatioSide = HORIZONTAL;
                } else if (mLeft.isConnected() && mRight.isConnected()) {
                    mResolvedDimensionRatio = 1 / mResolvedDimensionRatio;
                    mResolvedDimensionRatioSide = VERTICAL;
                }
            }
        }
        if (mResolvedDimensionRatioSide == UNKNOWN) {
            if (mMatchConstraintMinWidth > 0 && mMatchConstraintMinHeight == 0) {
                mResolvedDimensionRatioSide = HORIZONTAL;
            } else if (mMatchConstraintMinWidth == 0 && mMatchConstraintMinHeight > 0) {
                mResolvedDimensionRatio = 1 / mResolvedDimensionRatio;
                mResolvedDimensionRatioSide = VERTICAL;
            }
        }
    }

    void ConstraintWidget::applyConstraints(LinearSystem& system, bool isHorizontal, bool parentWrapContent, bool oppositeParentWrapContent,
                                            bool isTerminal, SolverVariable* parentMin, SolverVariable* parentMax, DimensionBehaviour dimensionBehaviour,
                                            bool wrapContent, ConstraintAnchor* beginAnchor, ConstraintAnchor* endAnchor, int beginPosition, int dimension,
                                            int minDimension, int maxDimension, float bias, bool useRatio, bool oppositeVariable, bool inChain,
                                            bool oppositeInChain, bool inBarrier, int matchConstraintDefault, int oppositeMatchConstraintDefault,
                                            int matchMinDimension, int matchMaxDimension, float matchPercentDimension, bool applyPosition) {
        SolverVariable* begin = system.createObjectVariable(beginAnchor);
        SolverVariable* end = system.createObjectVariable(endAnchor);
        SolverVariable* beginTarget = system.createObjectVariable(beginAnchor->getTarget());
        SolverVariable* endTarget = system.createObjectVariable(endAnchor->getTarget());

        const bool isBeginConnected = beginAnchor->isConnected();
        const bool isEndConnected = endAnchor->isConnected();
        const bool isCenterConnected = mCenter.isConnected();

        bool variableSize = false;
        int numConnections = 0;
        if (isBeginConnected) numConnections++;
        if (isEndConnected) numConnections++;
        if (isCenterConnected) numConnections++;

        if (useRatio) matchConstraintDefault = MATCH_CONSTRAINT_RATIO;
        switch (dimensionBehaviour) {
            case DB::FIXED:
            case DB::WRAP_CONTENT:
            case DB::MATCH_PARENT: variableSize = false; break;
            case DB::MATCH_CONSTRAINT: variableSize = matchConstraintDefault != MATCH_CONSTRAINT_RATIO_RESOLVED; break;
        }

        if (mWidthOverride != -1 && isHorizontal) {
            variableSize = false;
            dimension = mWidthOverride;
            mWidthOverride = -1;
        }
        if (mHeightOverride != -1 && !isHorizontal) {
            variableSize = false;
            dimension = mHeightOverride;
            mHeightOverride = -1;
        }

        if (mVisibility == GONE) {
            dimension = 0;
            variableSize = false;
        }

        if (applyPosition) {
            if (!isBeginConnected && !isEndConnected && !isCenterConnected) {
                system.addEquality(begin, beginPosition);
            } else if (isBeginConnected && !isEndConnected) {
                system.addEquality(begin, beginTarget, beginAnchor->getMargin(), SV::STRENGTH_FIXED);
            }
        }

        if (!variableSize) {
            if (wrapContent) {
                system.addEquality(end, begin, 0, SV::STRENGTH_HIGH);
                if (minDimension > 0) system.addGreaterThan(end, begin, minDimension, SV::STRENGTH_FIXED);
                if (maxDimension < INT_MAX) system.addLowerThan(end, begin, maxDimension, SV::STRENGTH_FIXED);
            } else {
                system.addEquality(end, begin, dimension, SV::STRENGTH_FIXED);
            }
        } else {
            if (numConnections != 2 && !useRatio && (matchConstraintDefault == MATCH_CONSTRAINT_WRAP || matchConstraintDefault == MATCH_CONSTRAINT_SPREAD)) {
                variableSize = false;
                int d = std::max(matchMinDimension, dimension);
                if (matchMaxDimension > 0) d = std::min(matchMaxDimension, d);
                system.addEquality(end, begin, d, SV::STRENGTH_FIXED);
            } else {
                if (matchMinDimension == WRAP) matchMinDimension = dimension;
                if (matchMaxDimension == WRAP) matchMaxDimension = dimension;
                if (dimension > 0 && matchConstraintDefault != MATCH_CONSTRAINT_WRAP) {
                    // USE_WRAP_DIMENSION_FOR_SPREAD is false upstream.
                    dimension = 0;
                }
                if (matchMinDimension > 0) {
                    system.addGreaterThan(end, begin, matchMinDimension, SV::STRENGTH_FIXED);
                    dimension = std::max(dimension, matchMinDimension);
                }
                if (matchMaxDimension > 0) {
                    bool applyLimit = true;
                    if (parentWrapContent && matchConstraintDefault == MATCH_CONSTRAINT_WRAP) applyLimit = false;
                    if (applyLimit) system.addLowerThan(end, begin, matchMaxDimension, SV::STRENGTH_FIXED);
                    dimension = std::min(dimension, matchMaxDimension);
                }
                if (matchConstraintDefault == MATCH_CONSTRAINT_WRAP) {
                    if (parentWrapContent) {
                        system.addEquality(end, begin, dimension, SV::STRENGTH_FIXED);
                    } else if (inChain) {
                        system.addEquality(end, begin, dimension, SV::STRENGTH_EQUALITY);
                        system.addLowerThan(end, begin, dimension, SV::STRENGTH_FIXED);
                    } else {
                        system.addEquality(end, begin, dimension, SV::STRENGTH_EQUALITY);
                        system.addLowerThan(end, begin, dimension, SV::STRENGTH_FIXED);
                    }
                } else if (matchConstraintDefault == MATCH_CONSTRAINT_PERCENT) {
                    SolverVariable* percentBegin = nullptr;
                    SolverVariable* percentEnd = nullptr;
                    if (beginAnchor->getType() == T::TOP || beginAnchor->getType() == T::BOTTOM) {
                        percentBegin = system.createObjectVariable(mParent->getAnchor(T::TOP));
                        percentEnd = system.createObjectVariable(mParent->getAnchor(T::BOTTOM));
                    } else {
                        percentBegin = system.createObjectVariable(mParent->getAnchor(T::LEFT));
                        percentEnd = system.createObjectVariable(mParent->getAnchor(T::RIGHT));
                    }
                    system.addConstraint(system.createRow()->createRowDimensionRatio(end, begin, percentEnd, percentBegin, matchPercentDimension));
                    if (parentWrapContent) variableSize = false;
                } else {
                    isTerminal = true;
                }
            }
        }

        if (!applyPosition || inChain) {
            if (numConnections < 2 && parentWrapContent && isTerminal) {
                system.addGreaterThan(begin, parentMin, 0, SV::STRENGTH_FIXED);
                bool applyEnd = isHorizontal || (mBaseline.mTarget == nullptr);
                if (!isHorizontal && mBaseline.mTarget != nullptr) {
                    ConstraintWidget* target = mBaseline.mTarget->mOwner;
                    applyEnd = target->mDimensionRatio != 0 && target->mListDimensionBehaviors[0] == DB::MATCH_CONSTRAINT &&
                               target->mListDimensionBehaviors[1] == DB::MATCH_CONSTRAINT;
                }
                if (applyEnd) system.addGreaterThan(parentMax, end, 0, SV::STRENGTH_FIXED);
            }
            return;
        }

        int wrapStrength = SV::STRENGTH_EQUALITY;
        if (!isBeginConnected && !isEndConnected && !isCenterConnected) {
            // Nothing to do, the position was applied above.
        } else if (isBeginConnected && !isEndConnected) {
            ConstraintWidget* beginWidget = beginAnchor->mTarget->mOwner;
            if (parentWrapContent && beginWidget->is(KIND_BARRIER)) wrapStrength = SV::STRENGTH_FIXED;
        } else if (!isBeginConnected && isEndConnected) {
            system.addEquality(end, endTarget, -endAnchor->getMargin(), SV::STRENGTH_FIXED);
            if (parentWrapContent) {
                if (mOptimizeWrapO && begin->isFinalValue && mParent != nullptr) {
                    auto* container = static_cast<ConstraintWidgetContainer*>(mParent);
                    if (isHorizontal) container->addHorizontalWrapMinVariable(beginAnchor);
                    else container->addVerticalWrapMinVariable(beginAnchor);
                } else {
                    system.addGreaterThan(begin, parentMin, 0, SV::STRENGTH_EQUALITY);
                }
            }
        } else if (isBeginConnected && isEndConnected) {
            bool applyBoundsCheck = true;
            bool applyCentering = false;
            bool applyStrongChecks = false;
            bool applyRangeCheck = false;
            int rangeCheckStrength = SV::STRENGTH_EQUALITY;
            int boundsCheckStrength = SV::STRENGTH_HIGHEST;
            int centeringStrength = SV::STRENGTH_BARRIER;
            if (parentWrapContent) rangeCheckStrength = SV::STRENGTH_EQUALITY;
            ConstraintWidget* beginWidget = beginAnchor->mTarget->mOwner;
            ConstraintWidget* endWidget = endAnchor->mTarget->mOwner;
            ConstraintWidget* parent = getParent();

            if (variableSize) {
                if (matchConstraintDefault == MATCH_CONSTRAINT_SPREAD) {
                    if (matchMaxDimension == 0 && matchMinDimension == 0) {
                        applyStrongChecks = true;
                        rangeCheckStrength = SV::STRENGTH_FIXED;
                        boundsCheckStrength = SV::STRENGTH_FIXED;
                        if (beginTarget->isFinalValue && endTarget->isFinalValue) {
                            system.addEquality(begin, beginTarget, beginAnchor->getMargin(), SV::STRENGTH_FIXED);
                            system.addEquality(end, endTarget, -endAnchor->getMargin(), SV::STRENGTH_FIXED);
                            return;
                        }
                    } else {
                        applyCentering = true;
                        rangeCheckStrength = SV::STRENGTH_EQUALITY;
                        boundsCheckStrength = SV::STRENGTH_EQUALITY;
                        applyBoundsCheck = true;
                        applyRangeCheck = true;
                    }
                    if (beginWidget->is(KIND_BARRIER) || endWidget->is(KIND_BARRIER)) boundsCheckStrength = SV::STRENGTH_HIGHEST;
                } else if (matchConstraintDefault == MATCH_CONSTRAINT_PERCENT) {
                    applyCentering = true;
                    rangeCheckStrength = SV::STRENGTH_EQUALITY;
                    boundsCheckStrength = SV::STRENGTH_EQUALITY;
                    applyBoundsCheck = true;
                    applyRangeCheck = true;
                    if (beginWidget->is(KIND_BARRIER) || endWidget->is(KIND_BARRIER)) boundsCheckStrength = SV::STRENGTH_HIGHEST;
                } else if (matchConstraintDefault == MATCH_CONSTRAINT_WRAP) {
                    applyCentering = true;
                    applyRangeCheck = true;
                    rangeCheckStrength = SV::STRENGTH_FIXED;
                } else if (matchConstraintDefault == MATCH_CONSTRAINT_RATIO) {
                    if (mResolvedDimensionRatioSide == UNKNOWN) {
                        applyCentering = true;
                        applyRangeCheck = true;
                        applyStrongChecks = true;
                        rangeCheckStrength = SV::STRENGTH_FIXED;
                        boundsCheckStrength = SV::STRENGTH_EQUALITY;
                        if (oppositeInChain) {
                            boundsCheckStrength = SV::STRENGTH_EQUALITY;
                            centeringStrength = SV::STRENGTH_HIGHEST;
                            if (parentWrapContent) centeringStrength = SV::STRENGTH_EQUALITY;
                        } else {
                            centeringStrength = SV::STRENGTH_FIXED;
                        }
                    } else {
                        applyCentering = true;
                        applyRangeCheck = true;
                        applyStrongChecks = true;
                        if (useRatio) {
                            const bool otherSideInvariable =
                                oppositeMatchConstraintDefault == MATCH_CONSTRAINT_PERCENT || oppositeMatchConstraintDefault == MATCH_CONSTRAINT_WRAP;
                            if (!otherSideInvariable) {
                                rangeCheckStrength = SV::STRENGTH_FIXED;
                                boundsCheckStrength = SV::STRENGTH_EQUALITY;
                            }
                        } else {
                            rangeCheckStrength = SV::STRENGTH_EQUALITY;
                            if (matchMaxDimension > 0) {
                                boundsCheckStrength = SV::STRENGTH_EQUALITY;
                            } else if (matchMaxDimension == 0 && matchMinDimension == 0) {
                                if (!oppositeInChain) {
                                    boundsCheckStrength = SV::STRENGTH_FIXED;
                                } else {
                                    if (beginWidget != parent && endWidget != parent) rangeCheckStrength = SV::STRENGTH_HIGHEST;
                                    else rangeCheckStrength = SV::STRENGTH_EQUALITY;
                                    boundsCheckStrength = SV::STRENGTH_HIGHEST;
                                }
                            }
                        }
                    }
                }
            } else {
                applyCentering = true;
                applyRangeCheck = true;
                if (beginTarget->isFinalValue && endTarget->isFinalValue) {
                    system.addCentering(begin, beginTarget, beginAnchor->getMargin(), bias, endTarget, end, endAnchor->getMargin(), SV::STRENGTH_FIXED);
                    if (parentWrapContent && isTerminal) {
                        int margin = 0;
                        if (endAnchor->mTarget != nullptr) margin = endAnchor->getMargin();
                        if (endTarget != parentMax) system.addGreaterThan(parentMax, end, margin, wrapStrength);
                    }
                    return;
                }
            }

            if (applyRangeCheck && beginTarget == endTarget && beginWidget != parent) {
                applyRangeCheck = false;
                applyBoundsCheck = false;
            }

            if (applyCentering) {
                if (!variableSize && !oppositeVariable && !oppositeInChain && beginTarget == parentMin && endTarget == parentMax) {
                    centeringStrength = SV::STRENGTH_FIXED;
                    rangeCheckStrength = SV::STRENGTH_FIXED;
                    applyBoundsCheck = false;
                    parentWrapContent = false;
                }
                system.addCentering(begin, beginTarget, beginAnchor->getMargin(), bias, endTarget, end, endAnchor->getMargin(), centeringStrength);
            }

            if (mVisibility == GONE && !endAnchor->hasDependents()) return;

            if (applyRangeCheck) {
                if (parentWrapContent && beginTarget != endTarget && !variableSize) {
                    if (beginWidget->is(KIND_BARRIER) || endWidget->is(KIND_BARRIER)) rangeCheckStrength = SV::STRENGTH_BARRIER;
                }
                system.addGreaterThan(begin, beginTarget, beginAnchor->getMargin(), rangeCheckStrength);
                system.addLowerThan(end, endTarget, -endAnchor->getMargin(), rangeCheckStrength);
            }

            if (parentWrapContent && inBarrier && !(beginWidget->is(KIND_BARRIER) || endWidget->is(KIND_BARRIER)) && !(endWidget == parent)) {
                boundsCheckStrength = SV::STRENGTH_BARRIER;
                rangeCheckStrength = SV::STRENGTH_BARRIER;
                applyBoundsCheck = true;
            }

            if (applyBoundsCheck) {
                if (applyStrongChecks && (!oppositeInChain || oppositeParentWrapContent)) {
                    int strength = boundsCheckStrength;
                    if (beginWidget == parent || endWidget == parent) strength = SV::STRENGTH_BARRIER;
                    if (beginWidget->is(KIND_GUIDELINE) || endWidget->is(KIND_GUIDELINE)) strength = SV::STRENGTH_EQUALITY;
                    if (beginWidget->is(KIND_BARRIER) || endWidget->is(KIND_BARRIER)) strength = SV::STRENGTH_EQUALITY;
                    if (oppositeInChain) strength = SV::STRENGTH_EQUALITY;
                    boundsCheckStrength = std::max(strength, boundsCheckStrength);
                }
                if (parentWrapContent) {
                    boundsCheckStrength = std::min(rangeCheckStrength, boundsCheckStrength);
                    if (useRatio && !oppositeInChain && (beginWidget == parent || endWidget == parent)) boundsCheckStrength = SV::STRENGTH_HIGHEST;
                }
                system.addEquality(begin, beginTarget, beginAnchor->getMargin(), boundsCheckStrength);
                system.addEquality(end, endTarget, -endAnchor->getMargin(), boundsCheckStrength);
            }

            if (parentWrapContent) {
                int margin = 0;
                if (parentMin == beginTarget) margin = beginAnchor->getMargin();
                if (beginTarget != parentMin) system.addGreaterThan(begin, parentMin, margin, wrapStrength);
            }

            if (parentWrapContent && variableSize && minDimension == 0 && matchMinDimension == 0) {
                if (variableSize && matchConstraintDefault == MATCH_CONSTRAINT_RATIO) system.addGreaterThan(end, begin, 0, SV::STRENGTH_FIXED);
                else system.addGreaterThan(end, begin, 0, wrapStrength);
            }
        }

        if (parentWrapContent && isTerminal) {
            int margin = 0;
            if (endAnchor->mTarget != nullptr) margin = endAnchor->getMargin();
            if (endTarget != parentMax) {
                if (mOptimizeWrapO && end->isFinalValue && mParent != nullptr) {
                    auto* container = static_cast<ConstraintWidgetContainer*>(mParent);
                    if (isHorizontal) container->addHorizontalWrapMaxVariable(endAnchor);
                    else container->addVerticalWrapMaxVariable(endAnchor);
                    return;
                }
                system.addGreaterThan(parentMax, end, margin, wrapStrength);
            }
        }
    }

    void ConstraintWidget::updateFromSolver(LinearSystem& system, bool optimize) {
        int left = system.getObjectVariableValue(&mLeft);
        int top = system.getObjectVariableValue(&mTop);
        int right = system.getObjectVariableValue(&mRight);
        int bottom = system.getObjectVariableValue(&mBottom);
        if (optimize && mHorizontalRun != nullptr && mHorizontalRun->start.resolved && mHorizontalRun->end.resolved) {
            left = mHorizontalRun->start.value;
            right = mHorizontalRun->end.value;
        }
        if (optimize && mVerticalRun != nullptr && mVerticalRun->start.resolved && mVerticalRun->end.resolved) {
            top = mVerticalRun->start.value;
            bottom = mVerticalRun->end.value;
        }
        // Java int subtraction wraps; compute in 64 bits and keep the sign test.
        const long long w = (long long)right - left;
        const long long h = (long long)bottom - top;
        if (int(w) < 0 || int(h) < 0 || left == INT_MIN || left == INT_MAX || top == INT_MIN || top == INT_MAX || right == INT_MIN || right == INT_MAX ||
            bottom == INT_MIN || bottom == INT_MAX) {
            left = 0;
            top = 0;
            right = 0;
            bottom = 0;
        }
        setFrame(left, top, right, bottom);
    }

    void ConstraintWidget::updateFromRuns(bool updateHorizontal, bool updateVertical) {
        updateHorizontal &= mHorizontalRun->isResolved();
        updateVertical &= mVerticalRun->isResolved();
        int left = mHorizontalRun->start.value;
        int top = mVerticalRun->start.value;
        int right = mHorizontalRun->end.value;
        int bottom = mVerticalRun->end.value;
        int w = int((long long)right - left);
        int h = int((long long)bottom - top);
        if (w < 0 || h < 0 || left == INT_MIN || left == INT_MAX || top == INT_MIN || top == INT_MAX || right == INT_MIN || right == INT_MAX ||
            bottom == INT_MIN || bottom == INT_MAX) {
            left = 0;
            top = 0;
            right = 0;
            bottom = 0;
        }
        w = right - left;
        h = bottom - top;
        if (updateHorizontal) mX = left;
        if (updateVertical) mY = top;
        if (mVisibility == GONE) {
            mWidth = 0;
            mHeight = 0;
            return;
        }
        if (updateHorizontal) {
            if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::FIXED && w < mWidth) w = mWidth;
            mWidth = w;
            if (mWidth < mMinWidth) mWidth = mMinWidth;
        }
        if (updateVertical) {
            if (mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::FIXED && h < mHeight) h = mHeight;
            mHeight = h;
            if (mHeight < mMinHeight) mHeight = mMinHeight;
        }
    }

    void ConstraintWidget::addChildrenToSolverByDependency(ConstraintWidgetContainer* container, LinearSystem& system,
                                                           std::unordered_set<ConstraintWidget*>& widgets, int orientation, bool addSelf) {
        if (addSelf) {
            if (!widgets.contains(this)) return;
            Optimizer::checkMatchParent(container, system, this);
            widgets.erase(this);
            addToSolver(system, container->optimizeFor(Optimizer::OPTIMIZATION_GRAPH));
        }
        auto visit = [&](ConstraintAnchor& a) {
            for (ConstraintAnchor* anchor : std::vector<ConstraintAnchor*>(a.getDependents()))
                anchor->mOwner->addChildrenToSolverByDependency(container, system, widgets, orientation, true);
        };
        if (orientation == HORIZONTAL) {
            visit(mLeft);
            visit(mRight);
        } else {
            visit(mTop);
            visit(mBottom);
            visit(mBaseline);
        }
    }

}
