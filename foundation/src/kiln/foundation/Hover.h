#pragma once

// hoverable (Hoverable.kt): hover enter / exit of a mouse or stylus as Hover interactions.
// pointerHoverIcon (PointerIcon.kt): the icon shown while hovering, the deepest node's winning
// (overrideDescendants makes an ancestor's win over its descendants').

#include "kiln/foundation/Interaction.h"

namespace kiln::foundation {

    // android.view.PointerIcon types.
    enum class PointerIcon : int { Default = 1000, Hand = 1002, Crosshair = 1007, Text = 1008 };

    Modifier Hoverable(Modifier m, std::shared_ptr<MutableInteractionSource> interactionSource, bool enabled = true);
    Modifier PointerHoverIcon(Modifier m, PointerIcon icon, bool overrideDescendants = false);

}
