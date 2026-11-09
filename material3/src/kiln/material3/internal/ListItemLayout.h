#pragma once

#include "kiln/foundation/Compose.h"
#include "kiln/material3/ListItem.h"

#include <array>

namespace kiln::material3::internal {
    // The source uses five multi-content slots and measures only each slot's first node.
    // Private wrappers preserve empty slots and leave extra emitted nodes unplaced.
    void ListItemLayout(const ui::Modifier& modifier, bool legacy, Dp verticalPadding,
                        ListItemVerticalAlignment alignment, std::array<std::function<void()>, 5> slots);
}
