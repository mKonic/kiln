#include "kiln/constraintlayout/core/state/Dimension.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

namespace kiln::constraintlayout::core::state {

    void Dimension::apply(State& /*state*/, ConstraintWidget* constraintWidget, int orientation) const {
        using DB = ConstraintWidget::DimensionBehaviour;
        if (!mRatioString.empty()) constraintWidget->setDimensionRatio(mRatioString);
        if (orientation == ConstraintWidget::HORIZONTAL) {
            if (mIsSuggested) {
                constraintWidget->setHorizontalDimensionBehaviour(DB::MATCH_CONSTRAINT);
                int type = ConstraintWidget::MATCH_CONSTRAINT_SPREAD;
                if (mInitialValue == Value::WRAP_DIMENSION) type = ConstraintWidget::MATCH_CONSTRAINT_WRAP;
                else if (mInitialValue == Value::PERCENT_DIMENSION) type = ConstraintWidget::MATCH_CONSTRAINT_PERCENT;
                constraintWidget->setHorizontalMatchStyle(type, mMin, mMax, mPercent);
            } else {
                if (mMin > 0) constraintWidget->setMinWidth(mMin);
                if (mMax < INT_MAX) constraintWidget->setMaxWidth(mMax);
                if (mInitialValue == Value::WRAP_DIMENSION) {
                    constraintWidget->setHorizontalDimensionBehaviour(DB::WRAP_CONTENT);
                } else if (mInitialValue == Value::PARENT_DIMENSION) {
                    constraintWidget->setHorizontalDimensionBehaviour(DB::MATCH_PARENT);
                } else if (mInitialValue == Value::NONE) {
                    constraintWidget->setHorizontalDimensionBehaviour(DB::FIXED);
                    constraintWidget->setWidth(mValue);
                }
            }
        } else {
            if (mIsSuggested) {
                constraintWidget->setVerticalDimensionBehaviour(DB::MATCH_CONSTRAINT);
                int type = ConstraintWidget::MATCH_CONSTRAINT_SPREAD;
                if (mInitialValue == Value::WRAP_DIMENSION) type = ConstraintWidget::MATCH_CONSTRAINT_WRAP;
                else if (mInitialValue == Value::PERCENT_DIMENSION) type = ConstraintWidget::MATCH_CONSTRAINT_PERCENT;
                constraintWidget->setVerticalMatchStyle(type, mMin, mMax, mPercent);
            } else {
                if (mMin > 0) constraintWidget->setMinHeight(mMin);
                if (mMax < INT_MAX) constraintWidget->setMaxHeight(mMax);
                if (mInitialValue == Value::WRAP_DIMENSION) {
                    constraintWidget->setVerticalDimensionBehaviour(DB::WRAP_CONTENT);
                } else if (mInitialValue == Value::PARENT_DIMENSION) {
                    constraintWidget->setVerticalDimensionBehaviour(DB::MATCH_PARENT);
                } else if (mInitialValue == Value::NONE) {
                    constraintWidget->setVerticalDimensionBehaviour(DB::FIXED);
                    constraintWidget->setHeight(mValue);
                }
            }
        }
    }

}
