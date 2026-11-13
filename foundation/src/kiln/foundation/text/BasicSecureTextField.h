#pragma once

// BasicSecureTextField (BasicSecureTextField.kt): a single line password field. Each character
// shows as the obfuscation character (U+2022); with RevealLastTyped, or System when the
// show-passwords setting allows it for the keyboard used, the last typed character stays
// readable for 1.5 s or until the next edit or focus loss. Cut and copy are off.

#include "kiln/foundation/text/BasicTextField.h"
#include <kiln/Function.h>

namespace kiln::foundation {

    struct BasicSecureTextFieldOptions {
        bool enabled = true;
        bool readOnly = false;
        InputTransformationPtr inputTransformation;
        TextStyle textStyle;
        // KeyboardOptions.SecureTextField: no autocorrect, the password keyboard.
        foundation::KeyboardOptions keyboardOptions = [] {
            foundation::KeyboardOptions k;
            k.autoCorrectEnabled = false;
            k.keyboardType = ui::KeyboardType::Password;
            return k;
        }();
        kiln::Function<void(kiln::Function<void()> performDefaultAction)> onKeyboardAction;
        kiln::Function<void(const text::TextLayoutResult&)> onTextLayout;
        std::shared_ptr<MutableInteractionSource> interactionSource;
        gfx::Brush cursorBrush = gfx::Brush::SolidColor(Color(0xFF000000u));
        kiln::Function<void(const kiln::Function<void()>& innerTextField)> decorator;
        TextObfuscationMode textObfuscationMode = TextObfuscationMode::System;
        char32_t textObfuscationCharacter = U'•';
        ScrollState* scrollState = nullptr;
    };

    void BasicSecureTextField(TextFieldState& state, const Modifier& modifier = {}, BasicSecureTextFieldOptions options = {}, rt::Key key = {});

}
