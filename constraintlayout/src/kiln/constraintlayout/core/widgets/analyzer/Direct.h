#pragma once

// Direct (constraintlayout-core): resolves widgets whose positions follow directly from resolved
// anchors (Optimizer::OPTIMIZATION_DIRECT, on by default), before the solver. Upstream's
// solveChain is left out: it only runs with Chain::USE_CHAIN_OPTIMIZATION, which is off.

#include "kiln/constraintlayout/core/widgets/analyzer/BasicMeasure.h"

namespace kiln::constraintlayout::core {

    class Barrier;

    struct Direct {
        static void solvingPass(ConstraintWidgetContainer* layout, BasicMeasure::Measurer* measurer);

    private:
        static void solveBarrier(int level, Barrier* barrier, BasicMeasure::Measurer* measurer, int orientation, bool isRtl);
        static void horizontalSolvingPass(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer, bool isRtl);
        static void verticalSolvingPass(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer);
        static void solveHorizontalCenterConstraints(int level, BasicMeasure::Measurer* measurer, ConstraintWidget* widget, bool isRtl);
        static void solveVerticalCenterConstraints(int level, BasicMeasure::Measurer* measurer, ConstraintWidget* widget);
        static void solveHorizontalMatchConstraint(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer, ConstraintWidget* widget, bool isRtl);
        static void solveVerticalMatchConstraint(int level, ConstraintWidget* layout, BasicMeasure::Measurer* measurer, ConstraintWidget* widget);
        static bool canMeasure(int level, ConstraintWidget* layout);
    };

}
