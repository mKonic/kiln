#pragma once

// DependencyGraph (constraintlayout-core): resolves a container's widgets directly from their
// dependencies, without the solver, where it can (Optimizer::OPTIMIZATION_GRAPH).

#include "kiln/constraintlayout/core/widgets/analyzer/BasicMeasure.h"

#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    class WidgetRun;
    class RunGroup;
    class DependencyNode;

    class DependencyGraph {
    public:
        explicit DependencyGraph(ConstraintWidgetContainer* container);
        ~DependencyGraph();

        void setMeasurer(BasicMeasure::Measurer* measurer) { mMeasurer = measurer; }
        void defineTerminalWidgets(ConstraintWidget::DimensionBehaviour horizontalBehavior, ConstraintWidget::DimensionBehaviour verticalBehavior);
        bool directMeasure(bool optimizeWrap);
        bool directMeasureSetup(bool optimizeWrap);
        bool directMeasureWithOrientation(bool optimizeWrap, int orientation);
        void measureWidgets();
        void invalidateGraph() { mNeedBuildGraph = true; }
        void invalidateMeasures() { mNeedRedoMeasures = true; }
        void buildGraph();
        void buildGraph(std::vector<std::shared_ptr<WidgetRun>>& runs);

    private:
        static constexpr bool USE_GROUPS = true;
        int computeWrap(ConstraintWidgetContainer* container, int orientation);
        void measure(ConstraintWidget* widget, ConstraintWidget::DimensionBehaviour horizontalBehavior, int horizontalDimension,
                     ConstraintWidget::DimensionBehaviour verticalBehavior, int verticalDimension);
        bool basicMeasureWidgets(ConstraintWidgetContainer* constraintWidgetContainer);
        void applyGroup(DependencyNode* node, int orientation, int direction, DependencyNode* end, std::vector<std::unique_ptr<RunGroup>>& groups,
                        RunGroup* group);
        void findGroup(WidgetRun* run, int orientation, std::vector<std::unique_ptr<RunGroup>>& groups);

        ConstraintWidgetContainer* mWidgetcontainer;
        bool mNeedBuildGraph = true;
        bool mNeedRedoMeasures = true;
        ConstraintWidgetContainer* mContainer;
        std::vector<std::shared_ptr<WidgetRun>> mRuns;
        BasicMeasure::Measurer* mMeasurer = nullptr;
        BasicMeasure::Measure mMeasure;
        std::vector<std::unique_ptr<RunGroup>> mGroups;
    };

}
