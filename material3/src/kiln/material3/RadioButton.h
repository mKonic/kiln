#pragma once

// RadioButton.kt: selectable semantics, animated dot and enabled colors, static disabled colors.

#include "kiln/foundation/Interaction.h"

#include <optional>

namespace kiln::material3 {
    struct RadioButtonColors {
        ui::Color selectedColor, unselectedColor, disabledSelectedColor, disabledUnselectedColor;
        RadioButtonColors Copy(ui::Color selected = ui::Color::Unspecified(), ui::Color unselected = ui::Color::Unspecified(),
                               ui::Color disabledSelected = ui::Color::Unspecified(), ui::Color disabledUnselected = ui::Color::Unspecified()) const;
        ui::Color RadioColor(bool enabled, bool selected) const;
        bool operator==(const RadioButtonColors&) const = default;
    };
    struct RadioButtonOptions {
        bool enabled = true;
        std::optional<RadioButtonColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    // An empty onClick leaves the radio purely visual, without input/selection semantics or a
    // minimum interactive allocation. A parent selectable row can then own that behavior.
    void RadioButton(bool selected, std::function<void()> onClick, const ui::Modifier& modifier = {}, const RadioButtonOptions& options = {});
    namespace RadioButtonDefaults {
        RadioButtonColors colors();
    }
}
