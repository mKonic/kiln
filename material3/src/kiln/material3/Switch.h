#pragma once

// Switch.kt: static state colors, animated/pressed thumb geometry and optional thumb content.

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>

#include <optional>

namespace kiln::material3 {
    struct SwitchColors {
        ui::Color checkedThumbColor = ui::Color::Unspecified();
        ui::Color checkedTrackColor = ui::Color::Unspecified();
        ui::Color checkedBorderColor = ui::Color::Unspecified();
        ui::Color checkedIconColor = ui::Color::Unspecified();
        ui::Color uncheckedThumbColor = ui::Color::Unspecified();
        ui::Color uncheckedTrackColor = ui::Color::Unspecified();
        ui::Color uncheckedBorderColor = ui::Color::Unspecified();
        ui::Color uncheckedIconColor = ui::Color::Unspecified();
        ui::Color disabledCheckedThumbColor = ui::Color::Unspecified();
        ui::Color disabledCheckedTrackColor = ui::Color::Unspecified();
        ui::Color disabledCheckedBorderColor = ui::Color::Unspecified();
        ui::Color disabledCheckedIconColor = ui::Color::Unspecified();
        ui::Color disabledUncheckedThumbColor = ui::Color::Unspecified();
        ui::Color disabledUncheckedTrackColor = ui::Color::Unspecified();
        ui::Color disabledUncheckedBorderColor = ui::Color::Unspecified();
        ui::Color disabledUncheckedIconColor = ui::Color::Unspecified();
        SwitchColors Copy(const SwitchColors& overrides) const;
        ui::Color ThumbColor(bool enabled, bool checked) const;
        ui::Color TrackColor(bool enabled, bool checked) const;
        ui::Color BorderColor(bool enabled, bool checked) const;
        ui::Color IconColor(bool enabled, bool checked) const;
        bool operator==(const SwitchColors&) const = default;
    };
    struct SwitchOptions {
        bool enabled = true;
        std::optional<SwitchColors> colors;
        kiln::Function<void()> thumbContent;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void Switch(bool checked, kiln::Function<void(bool)> onCheckedChange, const ui::Modifier& modifier = {}, const SwitchOptions& options = {});
    namespace SwitchDefaults {
        inline constexpr ui::Dp IconSize{ 16 };
        SwitchColors colors();
        SwitchColors colors(const SwitchColors& overrides);
    }
}
