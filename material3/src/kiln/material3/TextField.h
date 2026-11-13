#pragma once

// TextField.kt / OutlinedTextField.kt (state-based overloads): Material text fields over a
// TextFieldState. A filled field sits on a container with an indicator line; an outlined one
// in an outline that opens for its label. The label sits inside the field (filled) or in the
// outline (outlined) and moves out of the input area once the field is focused or holds text;
// the placeholder shows while it is empty and focused. Errors colour the field and announce a
// default error message.

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/text/BasicSecureTextField.h"
#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/material3/TextFieldColors.h"
#include "kiln/material3/TextFieldDefaults.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct TextFieldOptions {
        bool enabled = true;
        bool readOnly = false;
        std::optional<text::TextStyle> textStyle;   // unset: LocalTextStyle
        // Unset: Inside for TextField, Cutout for OutlinedTextField.
        std::optional<TextFieldLabelPosition> labelPosition;
        kiln::Function<void(const TextFieldLabelScope&)> label;
        kiln::Function<void()> placeholder, leadingIcon, trailingIcon, prefix, suffix, supportingText;
        bool isError = false;
        foundation::InputTransformationPtr inputTransformation;
        foundation::OutputTransformationPtr outputTransformation;
        foundation::KeyboardOptions keyboardOptions;
        kiln::Function<void(kiln::Function<void()> performDefaultAction)> onKeyboardAction;
        foundation::TextFieldLineLimits lineLimits = foundation::TextFieldLineLimits::MultiLine();
        kiln::Function<void(const text::TextLayoutResult&)> onTextLayout;
        foundation::ScrollState* scrollState = nullptr;   // null: remembered
        foundation::ShapePtr shape;                       // null: the variant's
        std::optional<TextFieldColors> colors;            // unset: the variant's colors()
        std::optional<foundation::PaddingValues> contentPadding;   // unset: with a label only for Inside labels
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    void TextField(foundation::TextFieldState& state, const ui::Modifier& modifier = {}, const TextFieldOptions& options = {});
    void OutlinedTextField(foundation::TextFieldState& state, const ui::Modifier& modifier = {}, const TextFieldOptions& options = {});

    // SecureTextField.kt: a single line password field (BasicSecureTextField) with the same
    // decorations. Unset options take the secure defaults (the password keyboard without
    // autocorrect; obfuscation as the system does it, with U+2022).
    struct SecureTextFieldOptions {
        bool enabled = true;
        std::optional<text::TextStyle> textStyle;
        std::optional<TextFieldLabelPosition> labelPosition;
        kiln::Function<void(const TextFieldLabelScope&)> label;
        kiln::Function<void()> placeholder, leadingIcon, trailingIcon, prefix, suffix, supportingText;
        bool isError = false;
        foundation::InputTransformationPtr inputTransformation;
        foundation::TextObfuscationMode textObfuscationMode = foundation::TextObfuscationMode::System;
        char32_t textObfuscationCharacter = U'\u2022';
        std::optional<foundation::KeyboardOptions> keyboardOptions;
        kiln::Function<void(kiln::Function<void()> performDefaultAction)> onKeyboardAction;
        kiln::Function<void(const text::TextLayoutResult&)> onTextLayout;
        foundation::ShapePtr shape;
        std::optional<TextFieldColors> colors;
        // Unset: SecureTextField pads for a label unless there is none or it is Above;
        // OutlinedSecureTextField as OutlinedTextField does.
        std::optional<foundation::PaddingValues> contentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void SecureTextField(foundation::TextFieldState& state, const ui::Modifier& modifier = {}, const SecureTextFieldOptions& options = {});
    void OutlinedSecureTextField(foundation::TextFieldState& state, const ui::Modifier& modifier = {}, const SecureTextFieldOptions& options = {});

}
