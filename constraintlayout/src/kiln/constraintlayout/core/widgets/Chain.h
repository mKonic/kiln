#pragma once

// Chain (constraintlayout-core): the equations for a container's chains (spread, spread inside
// and packed, with weights).

#include "kiln/constraintlayout/core/widgets/ChainHead.h"

#include <vector>

namespace kiln::constraintlayout::core {

    struct Chain {
        static constexpr bool USE_CHAIN_OPTIMIZATION = false;
        // `widgets`: only chains starting at one of these, or all when null.
        static void applyChainConstraints(ConstraintWidgetContainer* container, LinearSystem& system, const std::vector<ConstraintWidget*>* widgets,
                                          int orientation);
        static void applyChainConstraints(ConstraintWidgetContainer* container, LinearSystem& system, int orientation, int offset, ChainHead& chainHead);
    };

}
