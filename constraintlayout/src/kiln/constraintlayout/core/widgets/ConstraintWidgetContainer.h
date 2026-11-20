#pragma once

// ConstraintWidgetContainer (constraintlayout-core): the root of a layout. measure() runs
// BasicMeasure (children measured through the Measurer), layout() builds and solves the system.

#include "kiln/constraintlayout/core/widgets/ChainHead.h"
#include "kiln/constraintlayout/core/widgets/Optimizer.h"
#include "kiln/constraintlayout/core/widgets/WidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/analyzer/BasicMeasure.h"
#include "kiln/constraintlayout/core/widgets/analyzer/DependencyGraph.h"

#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    class Guideline;

    class ConstraintWidgetContainer : public WidgetContainer {
    public:
        ConstraintWidgetContainer() { mKind |= KIND_CONSTRAINT_WIDGET_CONTAINER; }
        ConstraintWidgetContainer(int x, int y, int width, int height) : WidgetContainer(x, y, width, height) {
            mKind |= KIND_CONSTRAINT_WIDGET_CONTAINER;
        }
        ConstraintWidgetContainer(int width, int height) : WidgetContainer(width, height) { mKind |= KIND_CONSTRAINT_WIDGET_CONTAINER; }
        ConstraintWidgetContainer(const std::string& debugName, int width, int height) : WidgetContainer(width, height) {
            mKind |= KIND_CONSTRAINT_WIDGET_CONTAINER;
            setDebugName(debugName);
        }

        BasicMeasure mBasicMeasureSolver{ this };
        DependencyGraph mDependencyGraph{ this };

        void invalidateGraph() { mDependencyGraph.invalidateGraph(); }
        void invalidateMeasures() { mDependencyGraph.invalidateMeasures(); }
        bool directMeasure(bool optimizeWrap) { return mDependencyGraph.directMeasure(optimizeWrap); }
        bool directMeasureSetup(bool optimizeWrap) { return mDependencyGraph.directMeasureSetup(optimizeWrap); }
        bool directMeasureWithOrientation(bool optimizeWrap, int orientation) { return mDependencyGraph.directMeasureWithOrientation(optimizeWrap, orientation); }
        void defineTerminalWidgets() { mDependencyGraph.defineTerminalWidgets(getHorizontalDimensionBehaviour(), getVerticalDimensionBehaviour()); }
        long long measure(int optimizationLevel, int widthMode, int widthSize, int heightMode, int heightSize, int lastMeasureWidth, int lastMeasureHeight,
                          int paddingX, int paddingY);
        void updateHierarchy() { mBasicMeasureSolver.updateHierarchy(this); }
        void setMeasurer(BasicMeasure::Measurer* measurer) {
            mMeasurer = measurer;
            mDependencyGraph.setMeasurer(measurer);
        }
        BasicMeasure::Measurer* getMeasurer() const { return mMeasurer; }

        int mPaddingLeft = 0;
        int mPaddingTop = 0;
        int mPaddingRight = 0;
        int mPaddingBottom = 0;
        int mHorizontalChainsSize = 0;
        int mVerticalChainsSize = 0;
        std::vector<std::unique_ptr<ChainHead>> mVerticalChainsArray = std::vector<std::unique_ptr<ChainHead>>(4);
        std::vector<std::unique_ptr<ChainHead>> mHorizontalChainsArray = std::vector<std::unique_ptr<ChainHead>>(4);
        bool mGroupsWrapOptimized = false;
        bool mHorizontalWrapOptimized = false;
        bool mVerticalWrapOptimized = false;
        int mWrapFixedWidth = 0;
        int mWrapFixedHeight = 0;
        bool mSkipSolver = false;
        BasicMeasure::Measure mMeasure;

        void setOptimizationLevel(int value);
        int getOptimizationLevel() const { return mOptimizationLevel; }
        bool optimizeFor(int feature) const { return (mOptimizationLevel & feature) == feature; }
        std::string getType() const override { return "ConstraintLayout"; }
        void reset() override;
        bool isWidthMeasuredTooSmall() const { return mWidthMeasuredTooSmall; }
        bool isHeightMeasuredTooSmall() const { return mHeightMeasuredTooSmall; }
        void addVerticalWrapMinVariable(ConstraintAnchor* top);
        void addHorizontalWrapMinVariable(ConstraintAnchor* left);
        void addVerticalWrapMaxVariable(ConstraintAnchor* bottom);
        void addHorizontalWrapMaxVariable(ConstraintAnchor* right);
        bool addChildrenToSolver(LinearSystem& system);
        bool updateChildrenFromSolver(LinearSystem& system, bool* flags);
        void updateFromRuns(bool updateHorizontal, bool updateVertical) override;
        void setPadding(int left, int top, int right, int bottom) {
            mPaddingLeft = left;
            mPaddingTop = top;
            mPaddingRight = right;
            mPaddingBottom = bottom;
        }
        void setRtl(bool isRtl) { mIsRtl = isRtl; }
        bool isRtl() const { return mIsRtl; }
        static bool measure(int level, ConstraintWidget* widget, BasicMeasure::Measurer* measurer, BasicMeasure::Measure& measure, int measureStrategy);
        void layout() override;
        bool handlesInternalConstraints() const { return false; }
        std::vector<Guideline*> getVerticalGuidelines();
        std::vector<Guideline*> getHorizontalGuidelines();
        LinearSystem& getSystem() { return mSystem; }
        void addChain(ConstraintWidget* constraintWidget, int type);
        void setPass(int pass) { mPass = pass; }

    protected:
        BasicMeasure::Measurer* mMeasurer = nullptr;
        LinearSystem mSystem;

    private:
        static constexpr int MAX_ITERATIONS = 8;
        void addMinWrap(ConstraintAnchor* constraintAnchor, SolverVariable* parentMin);
        void addMaxWrap(ConstraintAnchor* constraintAnchor, SolverVariable* parentMax);
        void resetChains() { mHorizontalChainsSize = 0; mVerticalChainsSize = 0; }
        void addHorizontalChain(ConstraintWidget* widget);
        void addVerticalChain(ConstraintWidget* widget);

        int mPass = 0;
        bool mIsRtl = false;
        int mOptimizationLevel = Optimizer::OPTIMIZATION_STANDARD;
        bool mWidthMeasuredTooSmall = false;
        bool mHeightMeasuredTooSmall = false;
        // Upstream's WeakReferences: cleared after each solve.
        ConstraintAnchor* mVerticalWrapMin = nullptr;
        ConstraintAnchor* mHorizontalWrapMin = nullptr;
        ConstraintAnchor* mVerticalWrapMax = nullptr;
        ConstraintAnchor* mHorizontalWrapMax = nullptr;
        // Upstream's HashSet; insertion order here.
        std::vector<ConstraintWidget*> mWidgetsToAdd;
    };

}
