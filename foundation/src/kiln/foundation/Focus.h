#pragma once

#include "kiln/foundation/Interaction.h"

// Focus and key modifiers over kiln::ui's focus system (spec 03 req. 4-5, 06 req. 3).

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    using namespace kiln::ui;

    // focusTarget with a focusability and a state callback.
    Modifier Focusable(Modifier m, Focusability f = Focusability::Always,
                       std::function<void(FocusState)> onFocusChange = {},
                       std::shared_ptr<MutableInteractionSource> interactionSource = nullptr);
    // onFocusChanged: reports the state of the first focus target after it (deduplicated).
    Modifier OnFocusChanged(Modifier m, std::function<void(FocusState)> onChange);
    Modifier FocusRequesterModifier(Modifier m, FocusRequester& requester);
    // onKeyEvent / onPreviewKeyEvent: return true to consume.
    Modifier OnKeyEvent(Modifier m, std::function<bool(const KeyEvent&)> onKey,
                        std::function<bool(const KeyEvent&)> onPreview = {});

}
