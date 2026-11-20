#pragma once

// BasicMeasure (constraintlayout-core): measures a container's children through the Measurer and
// runs the solver passes until match-constraint children settle.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <vector>

namespace kiln::constraintlayout::core {

    class BasicMeasure {
    public:
        static constexpr int UNSPECIFIED = 0;
        static constexpr int EXACTLY = 1 << 30;
        static constexpr int AT_MOST = INT_MIN;   // 2 << 30 in Java int
        static constexpr int MATCH_PARENT = -1;
        static constexpr int WRAP_CONTENT = -2;
        static constexpr int FIXED = -3;

        struct Measure {
            static constexpr int SELF_DIMENSIONS = 0, TRY_GIVEN_DIMENSIONS = 1, USE_GIVEN_DIMENSIONS = 2;
            ConstraintWidget::DimensionBehaviour horizontalBehavior = ConstraintWidget::DimensionBehaviour::FIXED;
            ConstraintWidget::DimensionBehaviour verticalBehavior = ConstraintWidget::DimensionBehaviour::FIXED;
            int horizontalDimension = 0;
            int verticalDimension = 0;
            int measuredWidth = 0;
            int measuredHeight = 0;
            int measuredBaseline = 0;
            bool measuredHasBaseline = false;
            bool measuredNeedsSolverPass = false;
            int measureStrategy = 0;
        };

        class Measurer {
        public:
            virtual ~Measurer() = default;
            virtual void measure(ConstraintWidget* widget, Measure& measure) = 0;
            virtual void didMeasures() = 0;
        };

        explicit BasicMeasure(ConstraintWidgetContainer* container) : mConstraintWidgetContainer(container) {}
        void updateHierarchy(ConstraintWidgetContainer* layout);
        long long solverMeasure(ConstraintWidgetContainer* layout, int optimizationLevel, int paddingX, int paddingY, int widthMode, int widthSize,
                                int heightMode, int heightSize, int lastMeasureWidth, int lastMeasureHeight);

    private:
        void measureChildren(ConstraintWidgetContainer* layout);
        void solveLinearSystem(ConstraintWidgetContainer* layout, int pass, int w, int h);
        bool measure(Measurer* measurer, ConstraintWidget* widget, int measureStrategy);

        std::vector<ConstraintWidget*> mVariableDimensionsWidgets;
        Measure mMeasure;
        ConstraintWidgetContainer* mConstraintWidgetContainer;
    };

}
