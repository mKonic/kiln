#pragma once

// TextFieldCursorHandle / TextFieldSelectionHandles (BasicTextField.kt with Android's
// CursorHandle and SelectionHandle): 25 dp handles in 40 dp touch targets, dragged to move the
// cursor or the selection ends; tapping the cursor handle toggles the toolbar. Each sits in its own
// popup, so nothing drawn after the field covers it.

#include "kiln/foundation/text/input/TextFieldSelectionState.h"

#include <functional>

namespace kiln::foundation {

    // The field's text content (`content`, sized as usual) with the handles in popups, placed in
    // the content's (the core node's) coordinates.
    void TextFieldWithHandles(TextFieldSelectionState& selection, bool showHandles, bool showCursorHandle, std::function<void()> content);

}
