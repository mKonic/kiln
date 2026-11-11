#pragma once

// TextField.kt / OutlinedTextField.kt (state-based overloads): Material text fields over a
// TextFieldState. A filled field sits on a container with an indicator line; an outlined one
// in an outline that opens for its label. The label sits inside the field (filled) or in the
// outline (outlined) and moves out of the input area once the field is focused or holds text;
// the placeholder shows while it is empty and focused. Errors colour the field and announce a
// default error message.

#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Scroll.h"
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
        std::function<void(const TextFieldLabelScope&)> label;
        std::function<void()> placeholder, leadingIcon, trailingIcon, prefix, suffix, supportingText;
        bool isError = false;
        foundation::InputTransformationPtr inputTransformation;
        foundation::OutputTransformationPtr outputTransformation;
        foundation::KeyboardOptions keyboardOptions;
        std::function<void(std::function<void()> performDefaultAction)> onKeyboardAction;
        foundation::TextFieldLineLimits lineLimits = foundation::TextFieldLineLimits::MultiLine();
        std::function<void(const text::TextLayoutResult&)> onTextLayout;
        foundation::ScrollState* scrollState = nullptr;   // null: remembered
        foundation::ShapePtr shape;                       // null: the variant's
        std::optional<TextFieldColors> colors;            // unset: the variant's colors()
        std::optional<foundation::PaddingValues> contentPadding;   // unset: with a label only for Inside labels
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };

    void TextField(foundation::TextFieldState& state, const ui::Modifier& modifier = {}, const TextFieldOptions& options = {});
    void OutlinedTextField(foundation::TextFieldState& state, const ui::Modifier& modifier = {}, const TextFieldOptions& options = {});

}
