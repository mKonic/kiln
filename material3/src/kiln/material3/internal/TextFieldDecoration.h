#pragma once

// CommonDecorationBox (TextFieldImpl.kt): the label, placeholder, icons, prefix, suffix and
// supporting text around a text field's inner field, animating between focused, empty and
// filled, laid out inside the container (filled) or with the label in the outline's cutout.

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/TextFieldColors.h"

#include <functional>
#include <memory>
#include <string>

namespace kiln::material3::internal {

    struct DecorationBoxSlots {
        kiln::Function<void(const TextFieldLabelScope&)> label;
        kiln::Function<void()> placeholder, leadingIcon, trailingIcon, prefix, suffix, supportingText;
    };

    void CommonDecorationBox(const std::string& visualText, const kiln::Function<void()>& innerTextField, const TextFieldLabelPosition& labelPosition,
                             const DecorationBoxSlots& slots, bool singleLine, bool enabled, bool isError,
                             std::shared_ptr<foundation::MutableInteractionSource> interactionSource, const foundation::PaddingValues& contentPadding,
                             const TextFieldColors& colors, const kiln::Function<void()>& container);

    // textFieldHorizontalIconPadding: the space an icon's touch target leaves around its 24 dp glyph.
    ui::Dp TextFieldHorizontalIconPadding();
    // minimizedLabelHalfHeight: half of bodySmall's line height.
    ui::Dp MinimizedLabelHalfHeight();
    // topPaddingForLabelCutout: room above an outlined field for the label in its outline.
    ui::Modifier TopPaddingForLabelCutout(ui::Modifier m, bool hasLabel, const TextFieldLabelPosition& position);
    // defaultErrorSemantics
    ui::Modifier DefaultErrorSemantics(ui::Modifier m, bool isError, const std::string& defaultErrorMessage);

}
