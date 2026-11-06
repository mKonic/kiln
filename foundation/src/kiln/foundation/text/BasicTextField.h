#pragma once

// BasicTextField (spec 04): the node Android sees as a text editor. Focus starts a platform text
// input session (EditorInfo from the options, an InputConnection editing this state); taps place
// the cursor; hardware keys edit; the caret is 2 dp wide and blinks 500/500 ms while focused.
// Not yet: selection gestures and handles, magnifier, undo, text toolbar, autofill, semantics.

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/ui/TextInput.h"

namespace kiln::foundation {

    struct TextFieldOptions {
        TextStyle style;
        ui::ImeOptions ime;
        Color cursorColor{ 0xFF6750A4u };
        Color selectionColor{ 0x666750A4u };   // primary @ 0.4 (TextSelectionColors)
        std::function<void(ui::ImeAction)> onImeAction;
        bool enabled = true;
    };

    void BasicTextField(TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {}, rt::Key key = {});

}
