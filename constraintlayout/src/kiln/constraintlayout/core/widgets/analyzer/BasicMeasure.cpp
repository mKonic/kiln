#include "kiln/constraintlayout/core/widgets/analyzer/BasicMeasure.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"
#include "kiln/constraintlayout/core/widgets/analyzer/WidgetRuns.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using CW = ConstraintWidget;

    void BasicMeasure::updateHierarchy(ConstraintWidgetContainer* layout) {
        mVariableDimensionsWidgets.clear();
        for (ConstraintWidget* widget : layout->mChildren) {
            if (widget->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT || widget->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT)
                mVariableDimensionsWidgets.push_back(widget);
        }
        layout->invalidateGraph();
    }

    void BasicMeasure::measureChildren(ConstraintWidgetContainer* layout) {
        const bool optimize = layout->optimizeFor(Optimizer::OPTIMIZATION_GRAPH);
        Measurer* measurer = layout->getMeasurer();
        for (ConstraintWidget* child : layout->mChildren) {
            if (child->is(CW::KIND_GUIDELINE)) continue;
            if (child->is(CW::KIND_BARRIER)) continue;
            if (child->isInVirtualLayout()) continue;
            if (optimize && child->mHorizontalRun != nullptr && child->mVerticalRun != nullptr && child->mHorizontalRun->mDimension.resolved &&
                child->mVerticalRun->mDimension.resolved)
                continue;
            const DB widthBehavior = child->getDimensionBehaviour(CW::HORIZONTAL);
            const DB heightBehavior = child->getDimensionBehaviour(CW::VERTICAL);
            bool skip = widthBehavior == DB::MATCH_CONSTRAINT && child->mMatchConstraintDefaultWidth != CW::MATCH_CONSTRAINT_WRAP &&
                        heightBehavior == DB::MATCH_CONSTRAINT && child->mMatchConstraintDefaultHeight != CW::MATCH_CONSTRAINT_WRAP;
            if (!skip && layout->optimizeFor(Optimizer::OPTIMIZATION_DIRECT) && !child->is(CW::KIND_VIRTUAL_LAYOUT)) {
                if (widthBehavior == DB::MATCH_CONSTRAINT && child->mMatchConstraintDefaultWidth == CW::MATCH_CONSTRAINT_SPREAD &&
                    heightBehavior != DB::MATCH_CONSTRAINT && !child->isInHorizontalChain())
                    skip = true;
                // Upstream checks the horizontal chain here too.
                if (heightBehavior == DB::MATCH_CONSTRAINT && child->mMatchConstraintDefaultHeight == CW::MATCH_CONSTRAINT_SPREAD &&
                    widthBehavior != DB::MATCH_CONSTRAINT && !child->isInHorizontalChain())
                    skip = true;
                if ((widthBehavior == DB::MATCH_CONSTRAINT || heightBehavior == DB::MATCH_CONSTRAINT) && child->mDimensionRatio > 0) skip = true;
            }
            if (skip) continue;
            measure(measurer, child, Measure::SELF_DIMENSIONS);
        }
        measurer->didMeasures();
    }

    void BasicMeasure::solveLinearSystem(ConstraintWidgetContainer* layout, int pass, int w, int h) {
        const int minWidth = layout->getMinWidth();
        const int minHeight = layout->getMinHeight();
        layout->setMinWidth(0);
        layout->setMinHeight(0);
        layout->setWidth(w);
        layout->setHeight(h);
        layout->setMinWidth(minWidth);
        layout->setMinHeight(minHeight);
        mConstraintWidgetContainer->setPass(pass);
        mConstraintWidgetContainer->layout();
    }

    long long BasicMeasure::solverMeasure(ConstraintWidgetContainer* layout, int optimizationLevel, int /*paddingX*/, int /*paddingY*/, int widthMode,
                                          int widthSize, int heightMode, int heightSize, int /*lastMeasureWidth*/, int /*lastMeasureHeight*/) {
        Measurer* measurer = layout->getMeasurer();
        const int childCount = int(layout->mChildren.size());
        const int startingWidth = layout->getWidth();
        const int startingHeight = layout->getHeight();

        const bool optimizeWrap = Optimizer::enabled(optimizationLevel, Optimizer::OPTIMIZATION_GRAPH_WRAP);
        bool optimize = optimizeWrap || Optimizer::enabled(optimizationLevel, Optimizer::OPTIMIZATION_GRAPH);
        if (optimize) {
            for (int i = 0; i < childCount; i++) {
                ConstraintWidget* child = layout->mChildren[std::size_t(i)];
                const bool matchWidth = child->getHorizontalDimensionBehaviour() == DB::MATCH_CONSTRAINT;
                const bool matchHeight = child->getVerticalDimensionBehaviour() == DB::MATCH_CONSTRAINT;
                const bool ratio = matchWidth && matchHeight && child->getDimensionRatio() > 0;
                if (child->isInHorizontalChain() && ratio) { optimize = false; break; }
                if (child->isInVerticalChain() && ratio) { optimize = false; break; }
                if (child->is(CW::KIND_VIRTUAL_LAYOUT)) { optimize = false; break; }
                if (child->isInHorizontalChain() || child->isInVerticalChain()) { optimize = false; break; }
            }
        }

        bool allSolved = false;
        optimize &= (widthMode == EXACTLY && heightMode == EXACTLY) || optimizeWrap;
        int computations = 0;
        if (optimize) {
            widthSize = std::min(layout->getMaxWidth(), widthSize);
            heightSize = std::min(layout->getMaxHeight(), heightSize);
            if (widthMode == EXACTLY && layout->getWidth() != widthSize) {
                layout->setWidth(widthSize);
                layout->invalidateGraph();
            }
            if (heightMode == EXACTLY && layout->getHeight() != heightSize) {
                layout->setHeight(heightSize);
                layout->invalidateGraph();
            }
            if (widthMode == EXACTLY && heightMode == EXACTLY) {
                allSolved = layout->directMeasure(optimizeWrap);
                computations = 2;
            } else {
                allSolved = layout->directMeasureSetup(optimizeWrap);
                if (widthMode == EXACTLY) {
                    allSolved &= layout->directMeasureWithOrientation(optimizeWrap, CW::HORIZONTAL);
                    computations++;
                }
                if (heightMode == EXACTLY) {
                    allSolved &= layout->directMeasureWithOrientation(optimizeWrap, CW::VERTICAL);
                    computations++;
                }
            }
            if (allSolved) layout->updateFromRuns(widthMode == EXACTLY, heightMode == EXACTLY);
        }

        if (!allSolved || computations != 2) {
            const int optimizations = layout->getOptimizationLevel();
            if (childCount > 0) measureChildren(layout);
            updateHierarchy(layout);
            const int sizeDependentWidgetsCount = int(mVariableDimensionsWidgets.size());
            if (childCount > 0) solveLinearSystem(layout, 0, startingWidth, startingHeight);

            if (sizeDependentWidgetsCount > 0) {
                bool needSolverPass = false;
                const bool containerWrapWidth = layout->getHorizontalDimensionBehaviour() == DB::WRAP_CONTENT;
                const bool containerWrapHeight = layout->getVerticalDimensionBehaviour() == DB::WRAP_CONTENT;
                int minWidth = std::max(layout->getWidth(), mConstraintWidgetContainer->getMinWidth());
                int minHeight = std::max(layout->getHeight(), mConstraintWidgetContainer->getMinHeight());

                for (int i = 0; i < sizeDependentWidgetsCount; i++) {
                    ConstraintWidget* widget = mVariableDimensionsWidgets[std::size_t(i)];
                    if (!widget->is(CW::KIND_VIRTUAL_LAYOUT)) continue;
                    const int preWidth = widget->getWidth();
                    const int preHeight = widget->getHeight();
                    needSolverPass |= measure(measurer, widget, Measure::TRY_GIVEN_DIMENSIONS);
                    const int measuredWidth = widget->getWidth();
                    const int measuredHeight = widget->getHeight();
                    if (measuredWidth != preWidth) {
                        widget->setWidth(measuredWidth);
                        if (containerWrapWidth && widget->getRight() > minWidth) {
                            const int w = widget->getRight() + widget->getAnchor(ConstraintAnchor::Type::RIGHT)->getMargin();
                            minWidth = std::max(minWidth, w);
                        }
                        needSolverPass = true;
                    }
                    if (measuredHeight != preHeight) {
                        widget->setHeight(measuredHeight);
                        if (containerWrapHeight && widget->getBottom() > minHeight) {
                            const int h = widget->getBottom() + widget->getAnchor(ConstraintAnchor::Type::BOTTOM)->getMargin();
                            minHeight = std::max(minHeight, h);
                        }
                        needSolverPass = true;
                    }
                    needSolverPass |= static_cast<VirtualLayout*>(widget)->needSolverPass();
                }

                const int maxIterations = 2;
                for (int j = 0; j < maxIterations; j++) {
                    for (int i = 0; i < sizeDependentWidgetsCount; i++) {
                        ConstraintWidget* widget = mVariableDimensionsWidgets[std::size_t(i)];
                        if ((widget->is(CW::KIND_HELPER_WIDGET) && !widget->is(CW::KIND_VIRTUAL_LAYOUT)) || widget->is(CW::KIND_GUIDELINE)) continue;
                        if (widget->getVisibility() == CW::GONE) continue;
                        if (optimize && widget->mHorizontalRun->mDimension.resolved && widget->mVerticalRun->mDimension.resolved) continue;
                        if (widget->is(CW::KIND_VIRTUAL_LAYOUT)) continue;

                        const int preWidth = widget->getWidth();
                        const int preHeight = widget->getHeight();
                        const int preBaselineDistance = widget->getBaselineDistance();
                        int measureStrategy = Measure::TRY_GIVEN_DIMENSIONS;
                        if (j == maxIterations - 1) measureStrategy = Measure::USE_GIVEN_DIMENSIONS;
                        const bool hasMeasure = measure(measurer, widget, measureStrategy);
                        needSolverPass |= hasMeasure;
                        const int measuredWidth = widget->getWidth();
                        const int measuredHeight = widget->getHeight();
                        if (measuredWidth != preWidth) {
                            widget->setWidth(measuredWidth);
                            if (containerWrapWidth && widget->getRight() > minWidth) {
                                const int w = widget->getRight() + widget->getAnchor(ConstraintAnchor::Type::RIGHT)->getMargin();
                                minWidth = std::max(minWidth, w);
                            }
                            needSolverPass = true;
                        }
                        if (measuredHeight != preHeight) {
                            widget->setHeight(measuredHeight);
                            if (containerWrapHeight && widget->getBottom() > minHeight) {
                                const int h = widget->getBottom() + widget->getAnchor(ConstraintAnchor::Type::BOTTOM)->getMargin();
                                minHeight = std::max(minHeight, h);
                            }
                            needSolverPass = true;
                        }
                        if (widget->hasBaseline() && preBaselineDistance != widget->getBaselineDistance()) needSolverPass = true;
                    }
                    if (needSolverPass) {
                        solveLinearSystem(layout, 1 + j, startingWidth, startingHeight);
                        needSolverPass = false;
                    } else {
                        break;
                    }
                }
            }
            layout->setOptimizationLevel(optimizations);
        }
        return 0;
    }

    bool BasicMeasure::measure(Measurer* measurer, ConstraintWidget* widget, int measureStrategy) {
        mMeasure.horizontalBehavior = widget->getHorizontalDimensionBehaviour();
        mMeasure.verticalBehavior = widget->getVerticalDimensionBehaviour();
        mMeasure.horizontalDimension = widget->getWidth();
        mMeasure.verticalDimension = widget->getHeight();
        mMeasure.measuredNeedsSolverPass = false;
        mMeasure.measureStrategy = measureStrategy;

        const bool horizontalMatchConstraints = mMeasure.horizontalBehavior == DB::MATCH_CONSTRAINT;
        const bool verticalMatchConstraints = mMeasure.verticalBehavior == DB::MATCH_CONSTRAINT;
        const bool horizontalUseRatio = horizontalMatchConstraints && widget->mDimensionRatio > 0;
        const bool verticalUseRatio = verticalMatchConstraints && widget->mDimensionRatio > 0;
        if (horizontalUseRatio && widget->mResolvedMatchConstraintDefault[CW::HORIZONTAL] == CW::MATCH_CONSTRAINT_RATIO_RESOLVED)
            mMeasure.horizontalBehavior = DB::FIXED;
        if (verticalUseRatio && widget->mResolvedMatchConstraintDefault[CW::VERTICAL] == CW::MATCH_CONSTRAINT_RATIO_RESOLVED)
            mMeasure.verticalBehavior = DB::FIXED;

        measurer->measure(widget, mMeasure);
        widget->setWidth(mMeasure.measuredWidth);
        widget->setHeight(mMeasure.measuredHeight);
        widget->setHasBaseline(mMeasure.measuredHasBaseline);
        widget->setBaselineDistance(mMeasure.measuredBaseline);
        mMeasure.measureStrategy = Measure::SELF_DIMENSIONS;
        return mMeasure.measuredNeedsSolverPass;
    }

}
