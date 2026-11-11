#pragma once

// Label.kt: a label (typically a PlainTooltip) above some content, positioned as plain
// tooltips are; shown while the content is pressed, dragged or hovered, or always when
// persistent. Slider thumbs use it for their value.

#include "kiln/foundation/Interaction.h"
#include "kiln/material3/Tooltip.h"

namespace kiln::material3 {

    struct LabelOptions {
        // The content's interactions; null: one of its own (then only isPersistent shows it).
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        bool isPersistent = false;
    };
    void Label(std::function<void(const TooltipScope&)> label, const ui::Modifier& modifier, const LabelOptions& options,
               std::function<void()> content);

}
