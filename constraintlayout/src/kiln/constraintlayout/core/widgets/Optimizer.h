#pragma once

// Optimizer (constraintlayout-core): the optimization flags, and MATCH_PARENT children set directly.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

namespace kiln::constraintlayout::core {

    struct Optimizer {
        static constexpr int OPTIMIZATION_NONE = 0;
        static constexpr int OPTIMIZATION_DIRECT = 1;
        static constexpr int OPTIMIZATION_BARRIER = 1 << 1;
        static constexpr int OPTIMIZATION_CHAIN = 1 << 2;
        static constexpr int OPTIMIZATION_DIMENSIONS = 1 << 3;
        static constexpr int OPTIMIZATION_RATIO = 1 << 4;
        static constexpr int OPTIMIZATION_GROUPS = 1 << 5;
        static constexpr int OPTIMIZATION_GRAPH = 1 << 6;
        static constexpr int OPTIMIZATION_GRAPH_WRAP = 1 << 7;
        static constexpr int OPTIMIZATION_CACHE_MEASURES = 1 << 8;
        static constexpr int OPTIMIZATION_DEPENDENCY_ORDERING = 1 << 9;
        static constexpr int OPTIMIZATION_GROUPING = 1 << 10;
        static constexpr int OPTIMIZATION_STANDARD = OPTIMIZATION_DIRECT | OPTIMIZATION_CACHE_MEASURES;

        static constexpr int FLAG_USE_OPTIMIZE = 0, FLAG_CHAIN_DANGLING = 1, FLAG_RECOMPUTE_BOUNDS = 2;
        static inline bool sFlags[3] = {};

        static void checkMatchParent(ConstraintWidgetContainer* container, LinearSystem& system, ConstraintWidget* widget);
        static bool enabled(int optimizationLevel, int optimization) { return (optimizationLevel & optimization) == optimization; }
    };

}
