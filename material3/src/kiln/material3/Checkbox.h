#pragma once

// Checkbox.kt: nullable callbacks, tri-state morphs, configurable strokes and thirteen colors.

#include "kiln/foundation/Interaction.h"
#include "kiln/graphics/Path.h"

#include <optional>

namespace kiln::material3 {
    enum class ToggleableState { Off = 0, On = 1, Indeterminate = 2 };
    struct CheckboxColors {
        ui::Color checkedCheckmarkColor = ui::Color::Unspecified();
        ui::Color uncheckedCheckmarkColor = ui::Color::Unspecified();
        ui::Color checkedBoxColor = ui::Color::Unspecified();
        ui::Color uncheckedBoxColor = ui::Color::Unspecified();
        ui::Color disabledCheckedBoxColor = ui::Color::Unspecified();
        ui::Color disabledUncheckedBoxColor = ui::Color::Unspecified();
        ui::Color disabledIndeterminateBoxColor = ui::Color::Unspecified();
        ui::Color checkedBorderColor = ui::Color::Unspecified();
        ui::Color uncheckedBorderColor = ui::Color::Unspecified();
        ui::Color disabledBorderColor = ui::Color::Unspecified();
        ui::Color disabledUncheckedBorderColor = ui::Color::Unspecified();
        ui::Color disabledIndeterminateBorderColor = ui::Color::Unspecified();
        ui::Color disabledCheckmarkColor = ui::Color::Unspecified();
        CheckboxColors Copy(const CheckboxColors& overrides) const;
        ui::Color CheckmarkColor(bool enabled, ToggleableState state) const;
        ui::Color BoxColor(bool enabled, ToggleableState state) const;
        ui::Color BorderColor(bool enabled, ToggleableState state) const;
        ui::Color IndicatorColor(ToggleableState state) const;
        bool operator==(const CheckboxColors&) const = default;
    };
    struct CheckboxOptions {
        bool enabled = true;
        std::optional<CheckboxColors> colors;
        std::optional<gfx::Stroke> checkmarkStroke, outlineStroke;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void Checkbox(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier = {}, const CheckboxOptions& options = {});
    void TriStateCheckbox(ToggleableState state, std::function<void()> onClick, const ui::Modifier& modifier = {}, const CheckboxOptions& options = {});
    namespace CheckboxDefaults {
        inline constexpr ui::Dp StrokeWidth{ 2 };
        CheckboxColors colors();
        CheckboxColors colors(const CheckboxColors& overrides);
        CheckboxColors colors(ui::Color checkedColor, ui::Color uncheckedColor = ui::Color::Unspecified(), ui::Color checkmarkColor = ui::Color::Unspecified(),
                              ui::Color disabledCheckedColor = ui::Color::Unspecified(), ui::Color disabledUncheckedColor = ui::Color::Unspecified(),
                              ui::Color disabledIndeterminateColor = ui::Color::Unspecified());
    }
}
