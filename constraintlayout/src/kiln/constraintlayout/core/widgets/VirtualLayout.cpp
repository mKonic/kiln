#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;

    void VirtualLayout::applyRtl(bool isRtl) {
        if (mPaddingStart > 0 || mPaddingEnd > 0) {
            if (isRtl) {
                mResolvedPaddingLeft = mPaddingEnd;
                mResolvedPaddingRight = mPaddingStart;
            } else {
                mResolvedPaddingLeft = mPaddingStart;
                mResolvedPaddingRight = mPaddingEnd;
            }
        }
    }

    void VirtualLayout::captureWidgets() {
        for (int i = 0; i < mWidgetsCount; i++)
            if (ConstraintWidget* widget = mWidgets[std::size_t(i)]) widget->setInVirtualLayout(true);
    }

    bool VirtualLayout::measureChildren() {
        BasicMeasure::Measurer* measurer = nullptr;
        if (mParent != nullptr) measurer = static_cast<ConstraintWidgetContainer*>(mParent)->getMeasurer();
        if (measurer == nullptr) return false;
        for (int i = 0; i < mWidgetsCount; i++) {
            ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (widget == nullptr) continue;
            if (widget->is(KIND_GUIDELINE)) continue;
            DB widthBehavior = widget->getDimensionBehaviour(HORIZONTAL);
            DB heightBehavior = widget->getDimensionBehaviour(VERTICAL);
            const bool skip = widthBehavior == DB::MATCH_CONSTRAINT && widget->mMatchConstraintDefaultWidth != MATCH_CONSTRAINT_WRAP &&
                              heightBehavior == DB::MATCH_CONSTRAINT && widget->mMatchConstraintDefaultHeight != MATCH_CONSTRAINT_WRAP;
            if (skip) continue;
            if (widthBehavior == DB::MATCH_CONSTRAINT) widthBehavior = DB::WRAP_CONTENT;
            if (heightBehavior == DB::MATCH_CONSTRAINT) heightBehavior = DB::WRAP_CONTENT;
            mMeasure.horizontalBehavior = widthBehavior;
            mMeasure.verticalBehavior = heightBehavior;
            mMeasure.horizontalDimension = widget->getWidth();
            mMeasure.verticalDimension = widget->getHeight();
            measurer->measure(widget, mMeasure);
            widget->setWidth(mMeasure.measuredWidth);
            widget->setHeight(mMeasure.measuredHeight);
            widget->setBaselineDistance(mMeasure.measuredBaseline);
        }
        return true;
    }

    void VirtualLayout::measure(ConstraintWidget* widget, DimensionBehaviour horizontalBehavior, int horizontalDimension, DimensionBehaviour verticalBehavior,
                                int verticalDimension) {
        while (mMeasurer == nullptr && getParent() != nullptr) mMeasurer = static_cast<ConstraintWidgetContainer*>(getParent())->getMeasurer();
        mMeasure.horizontalBehavior = horizontalBehavior;
        mMeasure.verticalBehavior = verticalBehavior;
        mMeasure.horizontalDimension = horizontalDimension;
        mMeasure.verticalDimension = verticalDimension;
        mMeasurer->measure(widget, mMeasure);
        widget->setWidth(mMeasure.measuredWidth);
        widget->setHeight(mMeasure.measuredHeight);
        widget->setHasBaseline(mMeasure.measuredHasBaseline);
        widget->setBaselineDistance(mMeasure.measuredBaseline);
    }

    bool VirtualLayout::contains(const std::vector<ConstraintWidget*>& widgets) const {
        for (int i = 0; i < mWidgetsCount; i++)
            if (std::find(widgets.begin(), widgets.end(), mWidgets[std::size_t(i)]) != widgets.end()) return true;
        return false;
    }

    void Placeholder::measure(int widthMode, int widthSize, int heightMode, int heightSize) {
        int width = 0;
        int height = 0;
        width += getPaddingLeft() + getPaddingRight();
        height += getPaddingTop() + getPaddingBottom();
        if (mWidgetsCount > 0) {
            width += mWidgets[0]->getWidth();
            height += mWidgets[0]->getHeight();
        }
        width = std::max(getMinWidth(), width);
        height = std::max(getMinHeight(), height);
        int measuredWidth = 0;
        int measuredHeight = 0;
        if (widthMode == BasicMeasure::EXACTLY) measuredWidth = widthSize;
        else if (widthMode == BasicMeasure::AT_MOST) measuredWidth = std::min(width, widthSize);
        else if (widthMode == BasicMeasure::UNSPECIFIED) measuredWidth = width;
        if (heightMode == BasicMeasure::EXACTLY) measuredHeight = heightSize;
        else if (heightMode == BasicMeasure::AT_MOST) measuredHeight = std::min(height, heightSize);
        else if (heightMode == BasicMeasure::UNSPECIFIED) measuredHeight = height;
        setMeasure(measuredWidth, measuredHeight);
        setWidth(measuredWidth);
        setHeight(measuredHeight);
        needsCallbackFromSolver(mWidgetsCount > 0);
    }

    void Placeholder::addToSolver(LinearSystem& system, bool optimize) {
        VirtualLayout::addToSolver(system, optimize);
        if (mWidgetsCount > 0) {
            using T = ConstraintAnchor::Type;
            ConstraintWidget* widget = mWidgets[0];
            widget->resetAllConstraints();
            widget->connect(T::LEFT, this, T::LEFT);
            widget->connect(T::RIGHT, this, T::RIGHT);
            widget->connect(T::TOP, this, T::TOP);
            widget->connect(T::BOTTOM, this, T::BOTTOM);
        }
    }

}
