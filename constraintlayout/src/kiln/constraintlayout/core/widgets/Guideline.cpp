#include "kiln/constraintlayout/core/widgets/Guideline.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

namespace kiln::constraintlayout::core {

    using T = ConstraintAnchor::Type;
    using DB = ConstraintWidget::DimensionBehaviour;

    Guideline::Guideline() {
        mKind |= KIND_GUIDELINE;
        mAnchors.clear();
        mAnchors.push_back(mAnchor);
        for (auto& a : mListAnchors) a = mAnchor;
    }

    void Guideline::copy(const ConstraintWidget& src, const WidgetMap& map) {
        ConstraintWidget::copy(src, map);
        const auto& srcGuideline = static_cast<const Guideline&>(src);
        mRelativePercent = srcGuideline.mRelativePercent;
        mRelativeBegin = srcGuideline.mRelativeBegin;
        mRelativeEnd = srcGuideline.mRelativeEnd;
        mGuidelineUseRtl = srcGuideline.mGuidelineUseRtl;
        setOrientation(srcGuideline.mOrientation);
    }

    int Guideline::getRelativeBehaviour() const {
        if (mRelativePercent != -1) return RELATIVE_PERCENT;
        if (mRelativeBegin != -1) return RELATIVE_BEGIN;
        if (mRelativeEnd != -1) return RELATIVE_END;
        return RELATIVE_UNKNOWN;
    }

    void Guideline::setOrientation(int orientation) {
        if (mOrientation == orientation) return;
        mOrientation = orientation;
        mAnchors.clear();
        mAnchor = mOrientation == VERTICAL ? &mLeft : &mTop;
        mAnchors.push_back(mAnchor);
        for (auto& a : mListAnchors) a = mAnchor;
    }

    ConstraintAnchor* Guideline::getAnchor(ConstraintAnchor::Type anchorType) {
        switch (anchorType) {
            case T::LEFT:
            case T::RIGHT:
                if (mOrientation == VERTICAL) return mAnchor;
                break;
            case T::TOP:
            case T::BOTTOM:
                if (mOrientation == HORIZONTAL) return mAnchor;
                break;
            default: return nullptr;
        }
        return nullptr;
    }

    void Guideline::setGuidePercent(float value) {
        if (value > -1) {
            mRelativePercent = value;
            mRelativeBegin = -1;
            mRelativeEnd = -1;
        }
    }

    void Guideline::setGuideBegin(int value) {
        if (value > -1) {
            mRelativePercent = -1;
            mRelativeBegin = value;
            mRelativeEnd = -1;
        }
    }

    void Guideline::setGuideEnd(int value) {
        if (value > -1) {
            mRelativePercent = -1;
            mRelativeBegin = -1;
            mRelativeEnd = value;
        }
    }

    void Guideline::addToSolver(LinearSystem& system, bool /*optimize*/) {
        auto* parent = static_cast<ConstraintWidgetContainer*>(getParent());
        if (parent == nullptr) return;
        ConstraintAnchor* begin = parent->getAnchor(T::LEFT);
        ConstraintAnchor* end = parent->getAnchor(T::RIGHT);
        bool parentWrapContent = mParent != nullptr && mParent->mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::WRAP_CONTENT;
        if (mOrientation == HORIZONTAL) {
            begin = parent->getAnchor(T::TOP);
            end = parent->getAnchor(T::BOTTOM);
            parentWrapContent = mParent != nullptr && mParent->mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::WRAP_CONTENT;
        }
        if (mResolved && mAnchor->hasFinalValue()) {
            SolverVariable* guide = system.createObjectVariable(mAnchor);
            system.addEquality(guide, mAnchor->getFinalValue());
            if (mRelativeBegin != -1) {
                if (parentWrapContent) system.addGreaterThan(system.createObjectVariable(end), guide, 0, SolverVariable::STRENGTH_EQUALITY);
            } else if (mRelativeEnd != -1) {
                if (parentWrapContent) {
                    SolverVariable* parentRight = system.createObjectVariable(end);
                    system.addGreaterThan(guide, system.createObjectVariable(begin), 0, SolverVariable::STRENGTH_EQUALITY);
                    system.addGreaterThan(parentRight, guide, 0, SolverVariable::STRENGTH_EQUALITY);
                }
            }
            mResolved = false;
            return;
        }
        if (mRelativeBegin != -1) {
            SolverVariable* guide = system.createObjectVariable(mAnchor);
            SolverVariable* parentLeft = system.createObjectVariable(begin);
            system.addEquality(guide, parentLeft, mRelativeBegin, SolverVariable::STRENGTH_FIXED);
            if (parentWrapContent) system.addGreaterThan(system.createObjectVariable(end), guide, 0, SolverVariable::STRENGTH_EQUALITY);
        } else if (mRelativeEnd != -1) {
            SolverVariable* guide = system.createObjectVariable(mAnchor);
            SolverVariable* parentRight = system.createObjectVariable(end);
            system.addEquality(guide, parentRight, -mRelativeEnd, SolverVariable::STRENGTH_FIXED);
            if (parentWrapContent) {
                system.addGreaterThan(guide, system.createObjectVariable(begin), 0, SolverVariable::STRENGTH_EQUALITY);
                system.addGreaterThan(parentRight, guide, 0, SolverVariable::STRENGTH_EQUALITY);
            }
        } else if (mRelativePercent != -1) {
            SolverVariable* guide = system.createObjectVariable(mAnchor);
            SolverVariable* parentRight = system.createObjectVariable(end);
            system.addConstraint(LinearSystem::createRowDimensionPercent(system, guide, parentRight, mRelativePercent));
        }
    }

    void Guideline::updateFromSolver(LinearSystem& system, bool /*optimize*/) {
        if (getParent() == nullptr) return;
        const int value = system.getObjectVariableValue(mAnchor);
        if (mOrientation == VERTICAL) {
            setX(value);
            setY(0);
            setHeight(getParent()->getHeight());
            setWidth(0);
        } else {
            setX(0);
            setY(value);
            setWidth(getParent()->getWidth());
            setHeight(0);
        }
    }

    void Guideline::inferRelativePercentPosition() {
        float percent = float(getX()) / float(getParent()->getWidth());
        if (mOrientation == HORIZONTAL) percent = float(getY()) / float(getParent()->getHeight());
        setGuidePercent(percent);
    }

    void Guideline::inferRelativeBeginPosition() {
        int position = getX();
        if (mOrientation == HORIZONTAL) position = getY();
        setGuideBegin(position);
    }

    void Guideline::inferRelativeEndPosition() {
        int position = getParent()->getWidth() - getX();
        if (mOrientation == HORIZONTAL) position = getParent()->getHeight() - getY();
        setGuideEnd(position);
    }

    void Guideline::cyclePosition() {
        if (mRelativeBegin != -1) inferRelativePercentPosition();
        else if (mRelativePercent != -1) inferRelativeEndPosition();
        else if (mRelativeEnd != -1) inferRelativeBeginPosition();
    }

}
