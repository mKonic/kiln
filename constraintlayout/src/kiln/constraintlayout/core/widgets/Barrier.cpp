#include "kiln/constraintlayout/core/widgets/Barrier.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using T = ConstraintAnchor::Type;
    using SV = SolverVariable;

    void Barrier::copy(const ConstraintWidget& src, const WidgetMap& map) {
        HelperWidget::copy(src, map);
        const auto& srcBarrier = static_cast<const Barrier&>(src);
        mBarrierType = srcBarrier.mBarrierType;
        mAllowsGoneWidget = srcBarrier.mAllowsGoneWidget;
        mMargin = srcBarrier.mMargin;
    }

    std::string Barrier::toString() const {
        std::string debug = "[Barrier] " + getDebugName() + " {";
        for (int i = 0; i < mWidgetsCount; i++) {
            if (i > 0) debug += ", ";
            debug += mWidgets[std::size_t(i)]->getDebugName();
        }
        return debug + "}";
    }

    void Barrier::markWidgets() {
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (!mAllowsGoneWidget && !widget->allowedInBarrier()) continue;
            if (mBarrierType == LEFT || mBarrierType == RIGHT) widget->setInBarrier(HORIZONTAL, true);
            else if (mBarrierType == TOP || mBarrierType == BOTTOM) widget->setInBarrier(VERTICAL, true);
        }
    }

    void Barrier::addToSolver(LinearSystem& system, bool /*optimize*/) {
        mListAnchors[LEFT] = &mLeft;
        mListAnchors[TOP] = &mTop;
        mListAnchors[RIGHT] = &mRight;
        mListAnchors[BOTTOM] = &mBottom;
        for (ConstraintAnchor* a : mListAnchors) system.createObjectVariable(a);
        ConstraintAnchor* position = nullptr;
        if (mBarrierType >= 0 && mBarrierType < 4) position = mListAnchors[std::size_t(mBarrierType)];
        else return;

        // USE_RESOLUTION
        if (!mResolved) allSolved();
        if (mResolved) {
            mResolved = false;
            if (mBarrierType == LEFT || mBarrierType == RIGHT) {
                system.addEquality(mLeft.getSolverVariable(), mX);
                system.addEquality(mRight.getSolverVariable(), mX);
            } else if (mBarrierType == TOP || mBarrierType == BOTTOM) {
                system.addEquality(mTop.getSolverVariable(), mY);
                system.addEquality(mBottom.getSolverVariable(), mY);
            }
            return;
        }

        bool hasMatchConstraintWidgets = false;
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (!mAllowsGoneWidget && !widget->allowedInBarrier()) continue;
            if ((mBarrierType == LEFT || mBarrierType == RIGHT) && widget->getHorizontalDimensionBehaviour() == DimensionBehaviour::MATCH_CONSTRAINT &&
                widget->mLeft.mTarget != nullptr && widget->mRight.mTarget != nullptr) {
                hasMatchConstraintWidgets = true;
                break;
            } else if ((mBarrierType == TOP || mBarrierType == BOTTOM) &&
                       widget->getVerticalDimensionBehaviour() == DimensionBehaviour::MATCH_CONSTRAINT && widget->mTop.mTarget != nullptr &&
                       widget->mBottom.mTarget != nullptr) {
                hasMatchConstraintWidgets = true;
                break;
            }
        }

        const bool hasHorizontalCenteredDependents = mLeft.hasCenteredDependents() || mRight.hasCenteredDependents();
        const bool hasVerticalCenteredDependents = mTop.hasCenteredDependents() || mBottom.hasCenteredDependents();
        const bool applyEqualityOnReferences =
            !hasMatchConstraintWidgets && ((mBarrierType == LEFT && hasHorizontalCenteredDependents) || (mBarrierType == TOP && hasVerticalCenteredDependents) ||
                                           (mBarrierType == RIGHT && hasHorizontalCenteredDependents) || (mBarrierType == BOTTOM && hasVerticalCenteredDependents));

        int equalityOnReferencesStrength = SV::STRENGTH_EQUALITY;
        if (!applyEqualityOnReferences) equalityOnReferencesStrength = SV::STRENGTH_HIGHEST;
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (!mAllowsGoneWidget && !widget->allowedInBarrier()) continue;
            ConstraintAnchor* anchor = widget->mListAnchors[std::size_t(mBarrierType)];
            SolverVariable* target = system.createObjectVariable(anchor);
            int margin = 0;
            if (anchor->mTarget != nullptr && anchor->mTarget->mOwner == this) margin += anchor->mMargin;
            if (mBarrierType == LEFT || mBarrierType == TOP) system.addLowerBarrier(position->getSolverVariable(), target, mMargin - margin, hasMatchConstraintWidgets);
            else system.addGreaterBarrier(position->getSolverVariable(), target, mMargin + margin, hasMatchConstraintWidgets);
            // USE_RELAX_GONE is false upstream.
            system.addEquality(position->getSolverVariable(), target, mMargin + margin, equalityOnReferencesStrength);
        }

        const int barrierParentStrength = SV::STRENGTH_HIGHEST;
        const int barrierParentStrengthOpposite = SV::STRENGTH_NONE;
        if (mBarrierType == LEFT) {
            system.addEquality(mRight.getSolverVariable(), mLeft.getSolverVariable(), 0, SV::STRENGTH_FIXED);
            system.addEquality(mLeft.getSolverVariable(), mParent->mRight.getSolverVariable(), 0, barrierParentStrength);
            system.addEquality(mLeft.getSolverVariable(), mParent->mLeft.getSolverVariable(), 0, barrierParentStrengthOpposite);
        } else if (mBarrierType == RIGHT) {
            system.addEquality(mLeft.getSolverVariable(), mRight.getSolverVariable(), 0, SV::STRENGTH_FIXED);
            system.addEquality(mLeft.getSolverVariable(), mParent->mLeft.getSolverVariable(), 0, barrierParentStrength);
            system.addEquality(mLeft.getSolverVariable(), mParent->mRight.getSolverVariable(), 0, barrierParentStrengthOpposite);
        } else if (mBarrierType == TOP) {
            system.addEquality(mBottom.getSolverVariable(), mTop.getSolverVariable(), 0, SV::STRENGTH_FIXED);
            system.addEquality(mTop.getSolverVariable(), mParent->mBottom.getSolverVariable(), 0, barrierParentStrength);
            system.addEquality(mTop.getSolverVariable(), mParent->mTop.getSolverVariable(), 0, barrierParentStrengthOpposite);
        } else if (mBarrierType == BOTTOM) {
            system.addEquality(mTop.getSolverVariable(), mBottom.getSolverVariable(), 0, SV::STRENGTH_FIXED);
            system.addEquality(mTop.getSolverVariable(), mParent->mTop.getSolverVariable(), 0, barrierParentStrength);
            system.addEquality(mTop.getSolverVariable(), mParent->mBottom.getSolverVariable(), 0, barrierParentStrengthOpposite);
        }
    }

    int Barrier::getOrientation() const {
        switch (mBarrierType) {
            case LEFT:
            case RIGHT: return HORIZONTAL;
            case TOP:
            case BOTTOM: return VERTICAL;
            default: return UNKNOWN;
        }
    }

    bool Barrier::allSolved() {
        bool hasAllWidgetsResolved = true;
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (!mAllowsGoneWidget && !widget->allowedInBarrier()) continue;
            if ((mBarrierType == LEFT || mBarrierType == RIGHT) && !widget->isResolvedHorizontally()) hasAllWidgetsResolved = false;
            else if ((mBarrierType == TOP || mBarrierType == BOTTOM) && !widget->isResolvedVertically()) hasAllWidgetsResolved = false;
        }
        if (hasAllWidgetsResolved && mWidgetsCount > 0) {
            int barrierPosition = 0;
            bool initialized = false;
            for (int i = 0; i < mWidgetsCount; i++) {
                ConstraintWidget* widget = mWidgets[std::size_t(i)];
                if (!mAllowsGoneWidget && !widget->allowedInBarrier()) continue;
                if (!initialized) {
                    if (mBarrierType == LEFT) barrierPosition = widget->getAnchor(T::LEFT)->getFinalValue();
                    else if (mBarrierType == RIGHT) barrierPosition = widget->getAnchor(T::RIGHT)->getFinalValue();
                    else if (mBarrierType == TOP) barrierPosition = widget->getAnchor(T::TOP)->getFinalValue();
                    else if (mBarrierType == BOTTOM) barrierPosition = widget->getAnchor(T::BOTTOM)->getFinalValue();
                    initialized = true;
                }
                if (mBarrierType == LEFT) barrierPosition = std::min(barrierPosition, widget->getAnchor(T::LEFT)->getFinalValue());
                else if (mBarrierType == RIGHT) barrierPosition = std::max(barrierPosition, widget->getAnchor(T::RIGHT)->getFinalValue());
                else if (mBarrierType == TOP) barrierPosition = std::min(barrierPosition, widget->getAnchor(T::TOP)->getFinalValue());
                else if (mBarrierType == BOTTOM) barrierPosition = std::max(barrierPosition, widget->getAnchor(T::BOTTOM)->getFinalValue());
            }
            barrierPosition += mMargin;
            if (mBarrierType == LEFT || mBarrierType == RIGHT) setFinalHorizontal(barrierPosition, barrierPosition);
            else setFinalVertical(barrierPosition, barrierPosition);
            mResolved = true;
            return true;
        }
        return false;
    }

}
