#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include "kiln/constraintlayout/core/widgets/Barrier.h"
#include "kiln/constraintlayout/core/widgets/Chain.h"
#include "kiln/constraintlayout/core/widgets/Guideline.h"
#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"
#include "kiln/constraintlayout/core/widgets/analyzer/Direct.h"
#include "kiln/constraintlayout/core/widgets/analyzer/Grouping.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    using DB = ConstraintWidget::DimensionBehaviour;
    using SV = SolverVariable;

    long long ConstraintWidgetContainer::measure(int optimizationLevel, int widthMode, int widthSize, int heightMode, int heightSize, int lastMeasureWidth,
                                                 int lastMeasureHeight, int paddingX, int paddingY) {
        mPaddingLeft = paddingX;
        mPaddingTop = paddingY;
        return mBasicMeasureSolver.solverMeasure(this, optimizationLevel, paddingX, paddingY, widthMode, widthSize, heightMode, heightSize,
                                                 lastMeasureWidth, lastMeasureHeight);
    }

    void ConstraintWidgetContainer::setOptimizationLevel(int value) {
        mOptimizationLevel = value;
        LinearSystem::USE_DEPENDENCY_ORDERING = optimizeFor(Optimizer::OPTIMIZATION_DEPENDENCY_ORDERING);
    }

    void ConstraintWidgetContainer::reset() {
        mSystem.reset();
        mPaddingLeft = 0;
        mPaddingRight = 0;
        mPaddingTop = 0;
        mPaddingBottom = 0;
        mSkipSolver = false;
        WidgetContainer::reset();
    }

    void ConstraintWidgetContainer::addVerticalWrapMinVariable(ConstraintAnchor* top) {
        if (mVerticalWrapMin == nullptr || top->getFinalValue() > mVerticalWrapMin->getFinalValue()) mVerticalWrapMin = top;
    }
    void ConstraintWidgetContainer::addHorizontalWrapMinVariable(ConstraintAnchor* left) {
        if (mHorizontalWrapMin == nullptr || left->getFinalValue() > mHorizontalWrapMin->getFinalValue()) mHorizontalWrapMin = left;
    }
    void ConstraintWidgetContainer::addVerticalWrapMaxVariable(ConstraintAnchor* bottom) {
        if (mVerticalWrapMax == nullptr || bottom->getFinalValue() > mVerticalWrapMax->getFinalValue()) mVerticalWrapMax = bottom;
    }
    void ConstraintWidgetContainer::addHorizontalWrapMaxVariable(ConstraintAnchor* right) {
        if (mHorizontalWrapMax == nullptr || right->getFinalValue() > mHorizontalWrapMax->getFinalValue()) mHorizontalWrapMax = right;
    }

    void ConstraintWidgetContainer::addMinWrap(ConstraintAnchor* constraintAnchor, SolverVariable* parentMin) {
        SolverVariable* variable = mSystem.createObjectVariable(constraintAnchor);
        mSystem.addGreaterThan(variable, parentMin, 0, SV::STRENGTH_EQUALITY);
    }

    void ConstraintWidgetContainer::addMaxWrap(ConstraintAnchor* constraintAnchor, SolverVariable* parentMax) {
        SolverVariable* variable = mSystem.createObjectVariable(constraintAnchor);
        mSystem.addGreaterThan(parentMax, variable, 0, SV::STRENGTH_EQUALITY);
    }

    bool ConstraintWidgetContainer::addChildrenToSolver(LinearSystem& system) {
        const bool optimize = optimizeFor(Optimizer::OPTIMIZATION_GRAPH);
        addToSolver(system, optimize);
        const std::size_t count = mChildren.size();
        bool hasBarriers = false;
        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* widget = mChildren[i];
            widget->setInBarrier(HORIZONTAL, false);
            widget->setInBarrier(VERTICAL, false);
            if (widget->is(KIND_BARRIER)) hasBarriers = true;
        }
        if (hasBarriers) {
            for (std::size_t i = 0; i < count; i++)
                if (mChildren[i]->is(KIND_BARRIER)) static_cast<Barrier*>(mChildren[i])->markWidgets();
        }

        mWidgetsToAdd.clear();
        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* widget = mChildren[i];
            if (widget->addFirst()) {
                if (widget->is(KIND_VIRTUAL_LAYOUT)) {
                    if (std::find(mWidgetsToAdd.begin(), mWidgetsToAdd.end(), widget) == mWidgetsToAdd.end()) mWidgetsToAdd.push_back(widget);
                } else {
                    widget->addToSolver(system, optimize);
                }
            }
        }
        // Virtual layouts are added once their own virtual layout children are.
        while (!mWidgetsToAdd.empty()) {
            const std::size_t numLayouts = mWidgetsToAdd.size();
            for (ConstraintWidget* widget : mWidgetsToAdd) {
                auto* layout = static_cast<VirtualLayout*>(widget);
                if (layout->contains(mWidgetsToAdd)) {
                    layout->addToSolver(system, optimize);
                    std::erase(mWidgetsToAdd, static_cast<ConstraintWidget*>(layout));
                    break;
                }
            }
            if (numLayouts == mWidgetsToAdd.size()) {
                for (ConstraintWidget* widget : mWidgetsToAdd) widget->addToSolver(system, optimize);
                mWidgetsToAdd.clear();
            }
        }

        if (LinearSystem::USE_DEPENDENCY_ORDERING) {
            std::unordered_set<ConstraintWidget*> widgetsToAdd;
            std::vector<ConstraintWidget*> order;
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* widget = mChildren[i];
                if (!widget->addFirst() && widgetsToAdd.insert(widget).second) order.push_back(widget);
            }
            int orientation = VERTICAL;
            if (getHorizontalDimensionBehaviour() == DB::WRAP_CONTENT) orientation = HORIZONTAL;
            addChildrenToSolverByDependency(this, system, widgetsToAdd, orientation, false);
            for (ConstraintWidget* widget : order) {
                if (!widgetsToAdd.contains(widget)) continue;
                Optimizer::checkMatchParent(this, system, widget);
                widget->addToSolver(system, optimize);
            }
        } else {
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* widget = mChildren[i];
                if (widget->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) {
                    const DB horizontalBehaviour = widget->mListDimensionBehaviors[DIMENSION_HORIZONTAL];
                    const DB verticalBehaviour = widget->mListDimensionBehaviors[DIMENSION_VERTICAL];
                    if (horizontalBehaviour == DB::WRAP_CONTENT) widget->setHorizontalDimensionBehaviour(DB::FIXED);
                    if (verticalBehaviour == DB::WRAP_CONTENT) widget->setVerticalDimensionBehaviour(DB::FIXED);
                    widget->addToSolver(system, optimize);
                    if (horizontalBehaviour == DB::WRAP_CONTENT) widget->setHorizontalDimensionBehaviour(horizontalBehaviour);
                    if (verticalBehaviour == DB::WRAP_CONTENT) widget->setVerticalDimensionBehaviour(verticalBehaviour);
                } else {
                    Optimizer::checkMatchParent(this, system, widget);
                    if (!widget->addFirst()) widget->addToSolver(system, optimize);
                }
            }
        }

        if (mHorizontalChainsSize > 0) Chain::applyChainConstraints(this, system, nullptr, HORIZONTAL);
        if (mVerticalChainsSize > 0) Chain::applyChainConstraints(this, system, nullptr, VERTICAL);
        return true;
    }

    bool ConstraintWidgetContainer::updateChildrenFromSolver(LinearSystem& system, bool* flags) {
        flags[Optimizer::FLAG_RECOMPUTE_BOUNDS] = false;
        const bool optimize = optimizeFor(Optimizer::OPTIMIZATION_GRAPH);
        updateFromSolver(system, optimize);
        bool hasOverride = false;
        for (ConstraintWidget* widget : mChildren) {
            widget->updateFromSolver(system, optimize);
            if (widget->hasDimensionOverride()) hasOverride = true;
        }
        return hasOverride;
    }

    void ConstraintWidgetContainer::updateFromRuns(bool updateHorizontal, bool updateVertical) {
        WidgetContainer::updateFromRuns(updateHorizontal, updateVertical);
        for (ConstraintWidget* widget : mChildren) widget->updateFromRuns(updateHorizontal, updateVertical);
    }

    bool ConstraintWidgetContainer::measure(int /*level*/, ConstraintWidget* widget, BasicMeasure::Measurer* measurer, BasicMeasure::Measure& measure,
                                            int measureStrategy) {
        if (measurer == nullptr) return false;
        if (widget->getVisibility() == GONE || widget->is(KIND_GUIDELINE) || widget->is(KIND_BARRIER)) {
            measure.measuredWidth = 0;
            measure.measuredHeight = 0;
            return false;
        }

        measure.horizontalBehavior = widget->getHorizontalDimensionBehaviour();
        measure.verticalBehavior = widget->getVerticalDimensionBehaviour();
        measure.horizontalDimension = widget->getWidth();
        measure.verticalDimension = widget->getHeight();
        measure.measuredNeedsSolverPass = false;
        measure.measureStrategy = measureStrategy;

        bool horizontalMatchConstraints = measure.horizontalBehavior == DB::MATCH_CONSTRAINT;
        bool verticalMatchConstraints = measure.verticalBehavior == DB::MATCH_CONSTRAINT;
        const bool horizontalUseRatio = horizontalMatchConstraints && widget->mDimensionRatio > 0;
        const bool verticalUseRatio = verticalMatchConstraints && widget->mDimensionRatio > 0;

        if (horizontalMatchConstraints && widget->hasDanglingDimension(HORIZONTAL) && widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD &&
            !horizontalUseRatio) {
            horizontalMatchConstraints = false;
            measure.horizontalBehavior = DB::WRAP_CONTENT;
            if (verticalMatchConstraints && widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD) measure.horizontalBehavior = DB::FIXED;
        }
        if (verticalMatchConstraints && widget->hasDanglingDimension(VERTICAL) && widget->mMatchConstraintDefaultHeight == MATCH_CONSTRAINT_SPREAD &&
            !verticalUseRatio) {
            verticalMatchConstraints = false;
            measure.verticalBehavior = DB::WRAP_CONTENT;
            if (horizontalMatchConstraints && widget->mMatchConstraintDefaultWidth == MATCH_CONSTRAINT_SPREAD) measure.verticalBehavior = DB::FIXED;
        }

        if (widget->isResolvedHorizontally()) {
            horizontalMatchConstraints = false;
            measure.horizontalBehavior = DB::FIXED;
        }
        if (widget->isResolvedVertically()) {
            verticalMatchConstraints = false;
            measure.verticalBehavior = DB::FIXED;
        }

        if (horizontalUseRatio) {
            if (widget->mResolvedMatchConstraintDefault[HORIZONTAL] == MATCH_CONSTRAINT_RATIO_RESOLVED) {
                measure.horizontalBehavior = DB::FIXED;
            } else if (!verticalMatchConstraints) {
                int measuredHeight;
                if (measure.verticalBehavior == DB::FIXED) {
                    measuredHeight = measure.verticalDimension;
                } else {
                    measure.horizontalBehavior = DB::WRAP_CONTENT;
                    measurer->measure(widget, measure);
                    measuredHeight = measure.measuredHeight;
                }
                measure.horizontalBehavior = DB::FIXED;
                measure.horizontalDimension = JavaInt(widget->getDimensionRatio() * float(measuredHeight));
            }
        }
        if (verticalUseRatio) {
            if (widget->mResolvedMatchConstraintDefault[VERTICAL] == MATCH_CONSTRAINT_RATIO_RESOLVED) {
                measure.verticalBehavior = DB::FIXED;
            } else if (!horizontalMatchConstraints) {
                int measuredWidth;
                if (measure.horizontalBehavior == DB::FIXED) {
                    measuredWidth = measure.horizontalDimension;
                } else {
                    measure.verticalBehavior = DB::WRAP_CONTENT;
                    measurer->measure(widget, measure);
                    measuredWidth = measure.measuredWidth;
                }
                measure.verticalBehavior = DB::FIXED;
                if (widget->getDimensionRatioSide() == -1) measure.verticalDimension = JavaInt(float(measuredWidth) / widget->getDimensionRatio());
                else measure.verticalDimension = JavaInt(widget->getDimensionRatio() * float(measuredWidth));
            }
        }

        measurer->measure(widget, measure);
        widget->setWidth(measure.measuredWidth);
        widget->setHeight(measure.measuredHeight);
        widget->setHasBaseline(measure.measuredHasBaseline);
        widget->setBaselineDistance(measure.measuredBaseline);
        measure.measureStrategy = BasicMeasure::Measure::SELF_DIMENSIONS;
        return measure.measuredNeedsSolverPass;
    }

    void ConstraintWidgetContainer::layout() {
        mX = 0;
        mY = 0;
        mWidthMeasuredTooSmall = false;
        mHeightMeasuredTooSmall = false;
        const std::size_t count = mChildren.size();

        int preW = std::max(0, getWidth());
        int preH = std::max(0, getHeight());
        const DB originalVerticalDimensionBehaviour = mListDimensionBehaviors[DIMENSION_VERTICAL];
        const DB originalHorizontalDimensionBehaviour = mListDimensionBehaviors[DIMENSION_HORIZONTAL];

        bool wrap_override = false;

        if (mPass == 0 && Optimizer::enabled(mOptimizationLevel, Optimizer::OPTIMIZATION_DIRECT)) {
            Direct::solvingPass(this, getMeasurer());
            for (std::size_t i = 0; i < count; i++) {
                ConstraintWidget* child = mChildren[i];
                if (child->isMeasureRequested() && !child->is(KIND_GUIDELINE) && !child->is(KIND_BARRIER) && !child->is(KIND_VIRTUAL_LAYOUT) &&
                    !child->isInVirtualLayout()) {
                    const DB widthBehavior = child->getDimensionBehaviour(HORIZONTAL);
                    const DB heightBehavior = child->getDimensionBehaviour(VERTICAL);
                    const bool skip = widthBehavior == DB::MATCH_CONSTRAINT && child->mMatchConstraintDefaultWidth != MATCH_CONSTRAINT_WRAP &&
                                      heightBehavior == DB::MATCH_CONSTRAINT && child->mMatchConstraintDefaultHeight != MATCH_CONSTRAINT_WRAP;
                    if (!skip) {
                        BasicMeasure::Measure m;
                        ConstraintWidgetContainer::measure(0, child, mMeasurer, m, BasicMeasure::Measure::SELF_DIMENSIONS);
                    }
                }
            }
        }

        if (count > 2 && (originalHorizontalDimensionBehaviour == DB::WRAP_CONTENT || originalVerticalDimensionBehaviour == DB::WRAP_CONTENT) &&
            Optimizer::enabled(mOptimizationLevel, Optimizer::OPTIMIZATION_GROUPING)) {
            if (Grouping::simpleSolvingPass(this, getMeasurer())) {
                if (originalHorizontalDimensionBehaviour == DB::WRAP_CONTENT) {
                    if (preW < getWidth() && preW > 0) {
                        setWidth(preW);
                        mWidthMeasuredTooSmall = true;
                    } else {
                        preW = getWidth();
                    }
                }
                if (originalVerticalDimensionBehaviour == DB::WRAP_CONTENT) {
                    if (preH < getHeight() && preH > 0) {
                        setHeight(preH);
                        mHeightMeasuredTooSmall = true;
                    } else {
                        preH = getHeight();
                    }
                }
                wrap_override = true;
            }
        }
        const bool useGraphOptimizer = optimizeFor(Optimizer::OPTIMIZATION_GRAPH) || optimizeFor(Optimizer::OPTIMIZATION_GRAPH_WRAP);

        mSystem.graphOptimizer = false;
        mSystem.newgraphOptimizer = false;
        if (mOptimizationLevel != Optimizer::OPTIMIZATION_NONE && useGraphOptimizer) mSystem.newgraphOptimizer = true;

        int countSolve = 0;
        const bool hasWrapContent = getHorizontalDimensionBehaviour() == DB::WRAP_CONTENT || getVerticalDimensionBehaviour() == DB::WRAP_CONTENT;

        resetChains();
        countSolve = 0;

        for (std::size_t i = 0; i < count; i++) {
            ConstraintWidget* widget = mChildren[i];
            if (widget->is(KIND_WIDGET_CONTAINER)) static_cast<WidgetContainer*>(widget)->layout();
        }
        const bool optimize = optimizeFor(Optimizer::OPTIMIZATION_GRAPH);

        bool needsSolving = true;
        while (needsSolving) {
            countSolve++;
            {
                mSystem.reset();
                resetChains();
                createObjectVariables(mSystem);
                for (std::size_t i = 0; i < count; i++) mChildren[i]->createObjectVariables(mSystem);
                needsSolving = addChildrenToSolver(mSystem);
                if (mVerticalWrapMin != nullptr) {
                    addMinWrap(mVerticalWrapMin, mSystem.createObjectVariable(&mTop));
                    mVerticalWrapMin = nullptr;
                }
                if (mVerticalWrapMax != nullptr) {
                    addMaxWrap(mVerticalWrapMax, mSystem.createObjectVariable(&mBottom));
                    mVerticalWrapMax = nullptr;
                }
                if (mHorizontalWrapMin != nullptr) {
                    addMinWrap(mHorizontalWrapMin, mSystem.createObjectVariable(&mLeft));
                    mHorizontalWrapMin = nullptr;
                }
                if (mHorizontalWrapMax != nullptr) {
                    addMaxWrap(mHorizontalWrapMax, mSystem.createObjectVariable(&mRight));
                    mHorizontalWrapMax = nullptr;
                }
                if (needsSolving) mSystem.minimize();
            }
            if (needsSolving) {
                needsSolving = updateChildrenFromSolver(mSystem, Optimizer::sFlags);
            } else {
                updateFromSolver(mSystem, optimize);
                for (std::size_t i = 0; i < count; i++) mChildren[i]->updateFromSolver(mSystem, optimize);
                needsSolving = false;
            }

            if (hasWrapContent && countSolve < MAX_ITERATIONS && Optimizer::sFlags[Optimizer::FLAG_RECOMPUTE_BOUNDS]) {
                int maxX = 0;
                int maxY = 0;
                for (std::size_t i = 0; i < count; i++) {
                    ConstraintWidget* widget = mChildren[i];
                    maxX = std::max(maxX, widget->mX + widget->getWidth());
                    maxY = std::max(maxY, widget->mY + widget->getHeight());
                }
                maxX = std::max(mMinWidth, maxX);
                maxY = std::max(mMinHeight, maxY);
                if (originalHorizontalDimensionBehaviour == DB::WRAP_CONTENT) {
                    if (getWidth() < maxX) {
                        setWidth(maxX);
                        mListDimensionBehaviors[DIMENSION_HORIZONTAL] = DB::WRAP_CONTENT;
                        wrap_override = true;
                        needsSolving = true;
                    }
                }
                if (originalVerticalDimensionBehaviour == DB::WRAP_CONTENT) {
                    if (getHeight() < maxY) {
                        setHeight(maxY);
                        mListDimensionBehaviors[DIMENSION_VERTICAL] = DB::WRAP_CONTENT;
                        wrap_override = true;
                        needsSolving = true;
                    }
                }
            }
            {
                const int width = std::max(mMinWidth, getWidth());
                if (width > getWidth()) {
                    setWidth(width);
                    mListDimensionBehaviors[DIMENSION_HORIZONTAL] = DB::FIXED;
                    wrap_override = true;
                    needsSolving = true;
                }
                const int height = std::max(mMinHeight, getHeight());
                if (height > getHeight()) {
                    setHeight(height);
                    mListDimensionBehaviors[DIMENSION_VERTICAL] = DB::FIXED;
                    wrap_override = true;
                    needsSolving = true;
                }
                if (!wrap_override) {
                    if (mListDimensionBehaviors[DIMENSION_HORIZONTAL] == DB::WRAP_CONTENT && preW > 0) {
                        if (getWidth() > preW) {
                            mWidthMeasuredTooSmall = true;
                            wrap_override = true;
                            mListDimensionBehaviors[DIMENSION_HORIZONTAL] = DB::FIXED;
                            setWidth(preW);
                            needsSolving = true;
                        }
                    }
                    if (mListDimensionBehaviors[DIMENSION_VERTICAL] == DB::WRAP_CONTENT && preH > 0) {
                        if (getHeight() > preH) {
                            mHeightMeasuredTooSmall = true;
                            wrap_override = true;
                            mListDimensionBehaviors[DIMENSION_VERTICAL] = DB::FIXED;
                            setHeight(preH);
                            needsSolving = true;
                        }
                    }
                }
                if (countSolve > MAX_ITERATIONS) needsSolving = false;
            }
        }
        if (wrap_override) {
            mListDimensionBehaviors[DIMENSION_HORIZONTAL] = originalHorizontalDimensionBehaviour;
            mListDimensionBehaviors[DIMENSION_VERTICAL] = originalVerticalDimensionBehaviour;
        }
        resetSolverVariables(&mSystem.getCache());
    }

    std::vector<Guideline*> ConstraintWidgetContainer::getVerticalGuidelines() {
        std::vector<Guideline*> guidelines;
        for (ConstraintWidget* widget : mChildren)
            if (widget->is(KIND_GUIDELINE) && static_cast<Guideline*>(widget)->getOrientation() == Guideline::VERTICAL)
                guidelines.push_back(static_cast<Guideline*>(widget));
        return guidelines;
    }

    std::vector<Guideline*> ConstraintWidgetContainer::getHorizontalGuidelines() {
        std::vector<Guideline*> guidelines;
        for (ConstraintWidget* widget : mChildren)
            if (widget->is(KIND_GUIDELINE) && static_cast<Guideline*>(widget)->getOrientation() == Guideline::HORIZONTAL)
                guidelines.push_back(static_cast<Guideline*>(widget));
        return guidelines;
    }

    void ConstraintWidgetContainer::addChain(ConstraintWidget* constraintWidget, int type) {
        if (type == HORIZONTAL) addHorizontalChain(constraintWidget);
        else if (type == VERTICAL) addVerticalChain(constraintWidget);
    }

    void ConstraintWidgetContainer::addHorizontalChain(ConstraintWidget* widget) {
        if (mHorizontalChainsSize + 1 >= int(mHorizontalChainsArray.size())) mHorizontalChainsArray.resize(mHorizontalChainsArray.size() * 2);
        mHorizontalChainsArray[std::size_t(mHorizontalChainsSize)] = std::make_unique<ChainHead>(widget, HORIZONTAL, isRtl());
        mHorizontalChainsSize++;
    }

    void ConstraintWidgetContainer::addVerticalChain(ConstraintWidget* widget) {
        if (mVerticalChainsSize + 1 >= int(mVerticalChainsArray.size())) mVerticalChainsArray.resize(mVerticalChainsArray.size() * 2);
        mVerticalChainsArray[std::size_t(mVerticalChainsSize)] = std::make_unique<ChainHead>(widget, VERTICAL, isRtl());
        mVerticalChainsSize++;
    }

}
