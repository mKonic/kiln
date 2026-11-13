#pragma once

// BasicTextField (spec 04; Compose's state-based BasicTextField): an editable text over a
// TextFieldState. The decorator (focus, the IME session, semantics, keys, taps), the core (cursor,
// selection highlight, scrolling to keep the cursor in view) and the text layout follow Compose's
// TextFieldDecoratorModifier, TextFieldCoreModifier and TextFieldTextLayoutModifier. The cursor is
// 2 dp wide and blinks 500/500 ms while focused; line limits and the 10-character minimum width
// size the field like Compose.

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/foundation/text/input/Transformations.h"
#include "kiln/graphics/Brush.h"
#include "kiln/ui/TextInput.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    struct BasicTextFieldOptions {
        bool enabled = true;
        bool readOnly = false;
        InputTransformationPtr inputTransformation;
        TextStyle textStyle;
        foundation::KeyboardOptions keyboardOptions;
        // KeyboardActionHandler: the IME action (or Enter in a single line field); call
        // performDefaultAction for the default (Next / Previous move focus, Done hides the keyboard).
        kiln::Function<void(kiln::Function<void()> performDefaultAction)> onKeyboardAction;
        TextFieldLineLimits lineLimits = TextFieldLineLimits::MultiLine();
        kiln::Function<void(const text::TextLayoutResult&)> onTextLayout;
        std::shared_ptr<MutableInteractionSource> interactionSource;
        gfx::Brush cursorBrush = gfx::Brush::SolidColor(Color(0xFF000000u));
        OutputTransformationPtr outputTransformation;
        // TextFieldDecorator: draws around the text and calls innerTextField exactly once.
        kiln::Function<void(const kiln::Function<void()>& innerTextField)> decorator;
        ScrollState* scrollState = nullptr;   // null: remembered by the field
        // Checks the text with the platform's spell checker as it changes: misspelled words get
        // a wavy red underline, and a tap on one selects it and offers its suggestions in the
        // toolbar. Never for a password; nothing where the platform has no spell checker.
        bool spellCheck = false;
        // Internal (BasicSecureTextField): a codepoint transformation and password semantics.
        CodepointTransformationPtr codepointTransformation;
        bool isPassword = false;
    };

    // LocalAutofillHighlightColor: drawn over a field autofill filled until its text changes.
    inline rt::Local<Color> LocalAutofillHighlightColor{ Color(0x4DFFEB3Bu) };

    void BasicTextField(TextFieldState& state, const Modifier& modifier = {}, BasicTextFieldOptions options = {}, rt::Key key = {});

    // InterceptPlatformTextInput with an interceptor that never lets a request through: text
    // fields inside start no input session and show no keyboard (hardware keys still edit them).
    inline rt::Local<bool> LocalPlatformTextInputBlocked{ false };
    inline void BlockPlatformTextInput(kiln::Function<void()> content) {
        rt::Composition::Current()->Provide(LocalPlatformTextInputBlocked, true, std::move(content));
    }

}
