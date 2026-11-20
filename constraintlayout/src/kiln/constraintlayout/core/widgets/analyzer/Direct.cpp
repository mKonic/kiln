#include "kiln/constraintlayout/core/widgets/analyzer/Direct.h"

#include "kiln/constraintlayout/core/widgets/Barrier.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/Guideline.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;
    using T = ConstraintAnchor::Type;

    namespace {
        BasicMeasure::Measure sMeasure;
    }

    void Direct::solvingPass(ConstraintWidgetContainer* layout, BasicMeasure::Measurer* measurer) {
        const DB horizontal = layout->getHorizontalDimensionBehaviour();
        const DB vertical = layout->getVerticalDimensionBehaviour();
        layout->resetFinalResolution();
        std::vector<ConstraintWidget*>& children = layout->getChildren();
        const std::size_t count = children.size();
        for (std::size_t i = 0; i < count; i++) children[i]->resetFinalResolution();

        const bool isRtl = layout->isRtl();

        // Horizontal: the layout, its vertical guidelines, then everything from them.
        if (horizontal == DB::FIXED) layout->setFinalHorizontal(0, layout->getWidth());
        else layout->setFinalLeft(0);

        bool hasGuideline = false;
        bool hasBarrier = false;
        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            if (child->is(CW::KIND_GUIDELINE)) {
                auto* guideline = static_cast<Guideline*>(child);
                if (guideline->getOrientation() == Guideline::VERTICAL) {
                    if (guideline->getRelativeBegin() != -1) {
                        guideline->setFinalValue(guideline->getRelativeBegin());
                    } else if (guideline->getRelativeEnd() != -1 && layout->isResolvedHorizontally()) {
                        guideline->setFinalValue(layout->getWidth() - guideline->getRelativeEnd());
                    } else if (layout->isResolvedHorizontally()) {
                        guideline->setFinalValue(JavaInt(0.5f + guideline->getRelativePercent() * float(layout->getWidth())));
                    }
                    hasGuideline = true;
                }
            } else if (child->is(CW::KIND_BARRIER)) {
                if (static_cast<Barrier*>(child)->getOrientation() == CW::HORIZONTAL) hasBarrier = true;
            }
        }
        if (hasGuideline) {
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* child = children[i];
                if (child->is(CW::KIND_GUIDELINE) && static_cast<Guideline*>(child)->getOrientation() == Guideline::VERTICAL)
                    horizontalSolvingPass(0, child, measurer, isRtl);
            }
        }
        horizontalSolvingPass(0, layout, measurer, isRtl);
        if (hasBarrier) {
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* child = children[i];
                if (child->is(CW::KIND_BARRIER) && static_cast<Barrier*>(child)->getOrientation() == CW::HORIZONTAL)
                    solveBarrier(0, static_cast<Barrier*>(child), measurer, CW::HORIZONTAL, isRtl);
            }
        }

        // Vertical.
        if (vertical == DB::FIXED) layout->setFinalVertical(0, layout->getHeight());
        else layout->setFinalTop(0);

        hasGuideline = false;
        hasBarrier = false;
        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            if (child->is(CW::KIND_GUIDELINE)) {
                auto* guideline = static_cast<Guideline*>(child);
                if (guideline->getOrientation() == Guideline::HORIZONTAL) {
                    if (guideline->getRelativeBegin() != -1) {
                        guideline->setFinalValue(guideline->getRelativeBegin());
                    } else if (guideline->getRelativeEnd() != -1 && layout->isResolvedVertically()) {
                        guideline->setFinalValue(layout->getHeight() - guideline->getRelativeEnd());
                    } else if (layout->isResolvedVertically()) {
                        guideline->setFinalValue(JavaInt(0.5f + guideline->getRelativePercent() * float(layout->getHeight())));
                    }
                    hasGuideline = true;
                }
            } else if (child->is(CW::KIND_BARRIER)) {
                if (static_cast<Barrier*>(child)->getOrientation() == CW::VERTICAL) hasBarrier = true;
            }
        }
        if (hasGuideline) {
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* child = children[i];
                if (child->is(CW::KIND_GUIDELINE) && static_cast<Guideline*>(child)->getOrientation() == Guideline::HORIZONTAL)
                    verticalSolvingPass(1, child, measurer);
            }
        }
        verticalSolvingPass(0, layout, measurer);
        if (hasBarrier) {
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* child = children[i];
                if (child->is(CW::KIND_BARRIER) && static_cast<Barrier*>(child)->getOrientation() == CW::VERTICAL)
                    solveBarrier(0, static_cast<Barrier*>(child), measurer, CW::VERTICAL, isRtl);
            }
        }

        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* child = children[i];
            if (child->isMeasureRequested() && canMeasure(0, child)) {
                ConstraintWidgetContainer::measure(0, child, measurer, sMeasure, BasicMeasure::Measure::SELF_DIMENSIONS);
                if (child->is(CW::KIND_GUIDELINE)) {
                    if (static_cast<Guideline*>(child)->getOrientation() == Guideline::HORIZONTAL) verticalSolvingPass(0, child, measurer);
                    else horizontalSolvingPass(0, child, measurer, isRtl);
                } else {
                    horizontalSolvingPass(0, child, measurer, isRtl);
                    verticalSolvingPass(0, child, measurer);
                }
            }
        }
    }

    void Direct::solveBarrier(int level, Barrier* barrier, BasicMeasure::Measurer* measurer, int orientation, bool isRtl) {
        if (barrier->allSolved()) {
            if (orientation == CW::HORIZONTAL) horizontalSolvingPass(level + 1, barrier, measurer, isRtl);
            else verticalSolvingPass(level + 1, barrier, measurer);
        }
    }

    namespace {
        // A match-constraint widget Direct can place itself: spread (or gone), no ratio, not in a
        // chain or a virtual layout.
        bool DirectMatchConstraint(const ConstraintWidget* widget, int orientation) {
            if (orientation == CW::HORIZONTAL)
                return widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT && widget->mMatchConstraintMaxWidth >= 0 &&
                       widget->mMatchConstraintMinWidth >= 0 &&
                       (widget->getVisibility() == CW::GONE ||
                        (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD && widget->getDimensionRatio() == 0)) &&
                       !widget->isInHorizontalChain() && !widget->isInVirtualLayout();
            return widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT && widget->mMatchConstraintMaxHeight >= 0 &&
                   widget->mMatchConstraintMinHeight >= 0 &&
                   (widget->getVisibility() == CW::GONE ||
                    (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD && widget->getDimensionRatio() == 0)) &&
                   !widget->isInVerticalChain() && !widget->isInVirtualLayout();
        }
    }

    void Direct::horizontalSolvingPass(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer, bool isRtl) {
        if (layout->isHorizontalSolvingPassDone()) return;   // EARLY_TERMINATION
        if (!layout->is(CW::KIND_CONSTRAINT_WIDGET_CONTAINER) && layout->isMeasureRequested() && canMeasure(level + 1, layout)) {
            BasicMeasure::Measure measure;
            ConstraintWidgetContainer::measure(level + 1, layout, measurer, measure, BasicMeasure::Measure::SELF_DIMENSIONS);
        }

        ConstraintAnchor* left = layout->getAnchor(T::LEFT);
        ConstraintAnchor* right = layout->getAnchor(T::RIGHT);
        const int l = left->getFinalValue();
        const int r = right->getFinalValue();

        auto fromAnchor = [&](ConstraintAnchor* side, int pos, bool allowMatchParent) {
            (void)allowMatchParent;   // APPLY_MATCH_PARENT is off upstream
            const auto dependents = side->getDependents();   // a copy: passes below never connect, but stay safe
            for (ConstraintAnchor* first : dependents) {
                ConstraintWidget* widget = first->mOwner;
                const bool canMeasureWidget = canMeasure(level + 1, widget);
                if (widget->isMeasureRequested() && canMeasureWidget) {
                    BasicMeasure::Measure measure;
                    ConstraintWidgetContainer::measure(level + 1, widget, measurer, measure, BasicMeasure::Measure::SELF_DIMENSIONS);
                }
                const bool bothConnected = (first == &widget->mLeft && widget->mRight.mTarget != nullptr && widget->mRight.mTarget->hasFinalValue()) ||
                                           (first == &widget->mRight && widget->mLeft.mTarget != nullptr && widget->mLeft.mTarget->hasFinalValue());
                if (widget->getHorizontalDimensionBehaviour() != DB::MATCH_CONSTRAINT || canMeasureWidget) {
                    if (widget->isMeasureRequested()) continue;
                    if (first == &widget->mLeft && widget->mRight.mTarget == nullptr) {
                        const int x1 = pos + widget->mLeft.getMargin();
                        const int x2 = x1 + widget->getWidth();
                        widget->setFinalHorizontal(x1, x2);
                        horizontalSolvingPass(level + 1, widget, measurer, isRtl);
                    } else if (first == &widget->mRight && widget->mLeft.mTarget == nullptr) {
                        const int x2 = pos - widget->mRight.getMargin();
                        const int x1 = x2 - widget->getWidth();
                        widget->setFinalHorizontal(x1, x2);
                        horizontalSolvingPass(level + 1, widget, measurer, isRtl);
                    } else if (bothConnected && !widget->isInHorizontalChain()) {
                        solveHorizontalCenterConstraints(level + 1, measurer, widget, isRtl);
                    }
                } else if (DirectMatchConstraint(widget, CW::HORIZONTAL)) {
                    if (bothConnected && !widget->isInHorizontalChain()) solveHorizontalMatchConstraint(level + 1, layout, measurer, widget, isRtl);
                }
            }
        };

        if (left->hasFinalValue()) fromAnchor(left, l, true);
        if (layout->is(CW::KIND_GUIDELINE)) return;
        if (right->hasFinalValue()) fromAnchor(right, r, false);
        layout->markHorizontalSolvingPassDone();
    }

    void Direct::verticalSolvingPass(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer) {
        if (layout->isVerticalSolvingPassDone()) return;   // EARLY_TERMINATION
        if (!layout->is(CW::KIND_CONSTRAINT_WIDGET_CONTAINER) && layout->isMeasureRequested() && canMeasure(level + 1, layout)) {
            BasicMeasure::Measure measure;
            ConstraintWidgetContainer::measure(level + 1, layout, measurer, measure, BasicMeasure::Measure::SELF_DIMENSIONS);
        }

        ConstraintAnchor* top = layout->getAnchor(T::TOP);
        ConstraintAnchor* bottom = layout->getAnchor(T::BOTTOM);
        const int t = top->getFinalValue();
        const int b = bottom->getFinalValue();

        auto fromAnchor = [&](ConstraintAnchor* side, int pos) {
            const auto dependents = side->getDependents();
            for (ConstraintAnchor* first : dependents) {
                ConstraintWidget* widget = first->mOwner;
                const bool canMeasureWidget = canMeasure(level + 1, widget);
                if (widget->isMeasureRequested() && canMeasureWidget) {
                    BasicMeasure::Measure measure;
                    ConstraintWidgetContainer::measure(level + 1, widget, measurer, measure, BasicMeasure::Measure::SELF_DIMENSIONS);
                }
                const bool bothConnected = (first == &widget->mTop && widget->mBottom.mTarget != nullptr && widget->mBottom.mTarget->hasFinalValue()) ||
                                           (first == &widget->mBottom && widget->mTop.mTarget != nullptr && widget->mTop.mTarget->hasFinalValue());
                if (widget->getVerticalDimensionBehaviour() != DB::MATCH_CONSTRAINT || canMeasureWidget) {
                    if (widget->isMeasureRequested()) continue;
                    if (first == &widget->mTop && widget->mBottom.mTarget == nullptr) {
                        const int y1 = pos + widget->mTop.getMargin();
                        const int y2 = y1 + widget->getHeight();
                        widget->setFinalVertical(y1, y2);
                        verticalSolvingPass(level + 1, widget, measurer);
                    } else if (first == &widget->mBottom && widget->mTop.mTarget == nullptr) {
                        const int y2 = pos - widget->mBottom.getMargin();
                        const int y1 = y2 - widget->getHeight();
                        widget->setFinalVertical(y1, y2);
                        verticalSolvingPass(level + 1, widget, measurer);
                    } else if (bothConnected && !widget->isInVerticalChain()) {
                        solveVerticalCenterConstraints(level + 1, measurer, widget);
                    }
                } else if (DirectMatchConstraint(widget, CW::VERTICAL)) {
                    if (bothConnected && !widget->isInVerticalChain()) solveVerticalMatchConstraint(level + 1, layout, measurer, widget);
                }
            }
        };

        if (top->hasFinalValue()) fromAnchor(top, t);
        if (layout->is(CW::KIND_GUIDELINE)) return;
        if (bottom->hasFinalValue()) fromAnchor(bottom, b);

        ConstraintAnchor* baseline = layout->getAnchor(T::BASELINE);
        if (baseline->hasFinalValue()) {
            const int baselineValue = baseline->getFinalValue();
            const auto dependents = baseline->getDependents();
            for (ConstraintAnchor* first : dependents) {
                ConstraintWidget* widget = first->mOwner;
                const bool canMeasureWidget = canMeasure(level + 1, widget);
                if (widget->isMeasureRequested() && canMeasureWidget) {
                    BasicMeasure::Measure measure;
                    ConstraintWidgetContainer::measure(level + 1, widget, measurer, measure, BasicMeasure::Measure::SELF_DIMENSIONS);
                }
                if (widget->getVerticalDimensionBehaviour() != DB::MATCH_CONSTRAINT || canMeasureWidget) {
                    if (widget->isMeasureRequested()) continue;
                    if (first == &widget->mBaseline) {
                        widget->setFinalBaseline(baselineValue + first->getMargin());
                        verticalSolvingPass(level + 1, widget, measurer);
                    }
                }
            }
        }
        layout->markVerticalSolvingPassDone();
    }

    void Direct::solveHorizontalCenterConstraints(int level, BasicMeasure::Measurer* measurer, ConstraintWidget* widget, bool isRtl) {
        float bias = widget->getHorizontalBiasPercent();
        const int start = widget->mLeft.mTarget->getFinalValue();
        const int end = widget->mRight.mTarget->getFinalValue();
        int s1 = start + widget->mLeft.getMargin();
        int s2 = end - widget->mRight.getMargin();
        if (start == end) {
            bias = 0.5f;
            s1 = start;
            s2 = end;
        }
        const int width = widget->getWidth();
        int distance = s2 - s1 - width;
        if (s1 > s2) distance = s1 - s2 - width;
        const int d1 = distance > 0 ? JavaInt(0.5f + bias * float(distance)) : JavaInt(bias * float(distance));
        int x1 = s1 + d1;
        int x2 = x1 + width;
        if (s1 > s2) {
            x1 = s1 + d1;
            x2 = x1 - width;
        }
        widget->setFinalHorizontal(x1, x2);
        horizontalSolvingPass(level + 1, widget, measurer, isRtl);
    }

    void Direct::solveVerticalCenterConstraints(int level, BasicMeasure::Measurer* measurer, ConstraintWidget* widget) {
        float bias = widget->getVerticalBiasPercent();
        const int start = widget->mTop.mTarget->getFinalValue();
        const int end = widget->mBottom.mTarget->getFinalValue();
        int s1 = start + widget->mTop.getMargin();
        int s2 = end - widget->mBottom.getMargin();
        if (start == end) {
            bias = 0.5f;
            s1 = start;
            s2 = end;
        }
        const int height = widget->getHeight();
        int distance = s2 - s1 - height;
        if (s1 > s2) distance = s1 - s2 - height;
        const int d1 = distance > 0 ? JavaInt(0.5f + bias * float(distance)) : JavaInt(bias * float(distance));
        int y1 = s1 + d1;
        int y2 = y1 + height;
        if (s1 > s2) {
            y1 = s1 - d1;
            y2 = y1 - height;
        }
        widget->setFinalVertical(y1, y2);
        verticalSolvingPass(level + 1, widget, measurer);
    }

    void Direct::solveHorizontalMatchConstraint(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer, ConstraintWidget* widget, bool isRtl) {
        const float bias = widget->getHorizontalBiasPercent();
        const int s1 = widget->mLeft.mTarget->getFinalValue() + widget->mLeft.getMargin();
        const int s2 = widget->mRight.mTarget->getFinalValue() - widget->mRight.getMargin();
        if (s2 >= s1) {
            int width = widget->getWidth();
            if (widget->getVisibility() != CW::GONE) {
                if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_PERCENT) {
                    const int parentWidth = layout->is(CW::KIND_CONSTRAINT_WIDGET_CONTAINER) ? layout->getWidth() : layout->getParent()->getWidth();
                    // Upstream uses the bias here, not the percent; kept as is.
                    width = JavaInt(0.5f * widget->getHorizontalBiasPercent() * float(parentWidth));
                } else if (widget->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD) {
                    width = s2 - s1;
                }
                width = std::max(widget->mMatchConstraintMinWidth, width);
                if (widget->mMatchConstraintMaxWidth > 0) width = std::min(widget->mMatchConstraintMaxWidth, width);
            }
            const int distance = s2 - s1 - width;
            const int d1 = JavaInt(0.5f + bias * float(distance));
            const int x1 = s1 + d1;
            const int x2 = x1 + width;
            widget->setFinalHorizontal(x1, x2);
            horizontalSolvingPass(level + 1, widget, measurer, isRtl);
        }
    }

    void Direct::solveVerticalMatchConstraint(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer, ConstraintWidget* widget) {
        const float bias = widget->getVerticalBiasPercent();
        const int s1 = widget->mTop.mTarget->getFinalValue() + widget->mTop.getMargin();
        const int s2 = widget->mBottom.mTarget->getFinalValue() - widget->mBottom.getMargin();
        if (s2 >= s1) {
            int height = widget->getHeight();
            if (widget->getVisibility() != CW::GONE) {
                if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_PERCENT) {
                    const int parentHeight = layout->is(CW::KIND_CONSTRAINT_WIDGET_CONTAINER) ? layout->getHeight() : layout->getParent()->getHeight();
                    height = JavaInt(0.5f * bias * float(parentHeight));
                } else if (widget->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD) {
                    height = s2 - s1;
                }
                height = std::max(widget->mMatchConstraintMinHeight, height);
                if (widget->mMatchConstraintMaxHeight > 0) height = std::min(widget->mMatchConstraintMaxHeight, height);
            }
            const int distance = s2 - s1 - height;
            const int d1 = JavaInt(0.5f + bias * float(distance));
            const int y1 = s1 + d1;
            const int y2 = y1 + height;
            widget->setFinalVertical(y1, y2);
            verticalSolvingPass(level + 1, widget, measurer);
        }
    }

    bool Direct::canMeasure(int /*level*/, ConstraintWidget* layout) {
        const DB horizontalBehaviour = layout->getHorizontalDimensionBehaviour();
        const DB verticalBehaviour = layout->getVerticalDimensionBehaviour();
        // APPLY_MATCH_PARENT is off upstream, so the parent's behaviours are never read.
        const bool isHorizontalFixed =
            horizontalBehaviour == DB::FIXED || layout->isResolvedHorizontally() || horizontalBehaviour == DB::WRAP_CONTENT ||
            (horizontalBehaviour == DB::MATCH_CONSTRAINT && layout->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD &&
             layout->mDimensionRatio == 0 && layout->hasDanglingDimension(CW::HORIZONTAL)) ||
            (horizontalBehaviour == DB::MATCH_CONSTRAINT && layout->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_WRAP &&
             layout->hasResolvedTargets(CW::HORIZONTAL, layout->getWidth()));
        const bool isVerticalFixed =
            verticalBehaviour == DB::FIXED || layout->isResolvedVertically() || verticalBehaviour == DB::WRAP_CONTENT ||
            (verticalBehaviour == DB::MATCH_CONSTRAINT && layout->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD &&
             layout->mDimensionRatio == 0 && layout->hasDanglingDimension(CW::VERTICAL)) ||
            (verticalBehaviour == DB::MATCH_CONSTRAINT && layout->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_WRAP &&
             layout->hasResolvedTargets(CW::VERTICAL, layout->getHeight()));
        if (layout->mDimensionRatio > 0 && (isHorizontalFixed || isVerticalFixed)) return true;
        return isHorizontalFixed && isVerticalFixed;
    }

}
