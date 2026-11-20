#include "kiln/constraintlayout/core/widgets/Optimizer.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

namespace kiln::constraintlayout::core {

    void Optimizer::checkMatchParent(ConstraintWidgetContainer* container, LinearSystem& system, ConstraintWidget* widget) {
        using DB = ConstraintWidget::DimensionBehaviour;
        widget->mHorizontalResolution = ConstraintWidget::UNKNOWN;
        widget->mVerticalResolution = ConstraintWidget::UNKNOWN;
        if (container->mListDimensionBehaviors[ConstraintWidget::DIMENSION_HORIZONTAL] != DB::WRAP_CONTENT &&
            widget->mListDimensionBehaviors[ConstraintWidget::DIMENSION_HORIZONTAL] == DB::MATCH_PARENT) {
            const int left = widget->mLeft.mMargin;
            const int right = container->getWidth() - widget->mRight.mMargin;
            // createObjectVariable sets the anchor's variable (upstream also assigns it).
            system.addEquality(system.createObjectVariable(&widget->mLeft), left);
            system.addEquality(system.createObjectVariable(&widget->mRight), right);
            widget->mHorizontalResolution = ConstraintWidget::DIRECT;
            widget->setHorizontalDimension(left, right);
        }
        if (container->mListDimensionBehaviors[ConstraintWidget::DIMENSION_VERTICAL] != DB::WRAP_CONTENT &&
            widget->mListDimensionBehaviors[ConstraintWidget::DIMENSION_VERTICAL] == DB::MATCH_PARENT) {
            const int top = widget->mTop.mMargin;
            const int bottom = container->getHeight() - widget->mBottom.mMargin;
            system.addEquality(system.createObjectVariable(&widget->mTop), top);
            system.addEquality(system.createObjectVariable(&widget->mBottom), bottom);
            if (widget->mBaselineDistance > 0 || widget->getVisibility() == ConstraintWidget::GONE)
                system.addEquality(system.createObjectVariable(&widget->mBaseline), top + widget->mBaselineDistance);
            widget->mVerticalResolution = ConstraintWidget::DIRECT;
            widget->setVerticalDimension(top, bottom);
        }
    }

}
