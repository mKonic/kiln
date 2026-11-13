#pragma once

// SegmentedButton.kt: overlapping single- and multi-choice outlined buttons.

#include "kiln/foundation/Border.h"
#include <kiln/Function.h>
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"

#include <optional>

namespace kiln::material3 {
    struct SegmentedButtonColors {
        ui::Color activeContainerColor = ui::Color::Unspecified(), activeContentColor = ui::Color::Unspecified(), activeBorderColor = ui::Color::Unspecified();
        ui::Color inactiveContainerColor = ui::Color::Unspecified(), inactiveContentColor = ui::Color::Unspecified(), inactiveBorderColor = ui::Color::Unspecified();
        ui::Color disabledActiveContainerColor = ui::Color::Unspecified(), disabledActiveContentColor = ui::Color::Unspecified(), disabledActiveBorderColor = ui::Color::Unspecified();
        ui::Color disabledInactiveContainerColor = ui::Color::Unspecified(), disabledInactiveContentColor = ui::Color::Unspecified(), disabledInactiveBorderColor = ui::Color::Unspecified();
        SegmentedButtonColors Copy(const SegmentedButtonColors& overrides) const;
        ui::Color ContainerColor(bool enabled, bool active) const;
        ui::Color ContentColor(bool enabled, bool active) const;
        ui::Color BorderColor(bool enabled, bool active) const;
        bool operator==(const SegmentedButtonColors&) const = default;
    };

    struct SegmentedButtonOptions {
        bool enabled = true;
        std::optional<SegmentedButtonColors> colors;
        std::optional<foundation::BorderStroke> border;
        std::optional<foundation::PaddingValues> contentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        // Unset uses the animated checkmark. An engaged empty function omits the icon.
        std::optional<kiln::Function<void()>> icon;
    };

    void SingleChoiceSegmentedButtonRow(const ui::Modifier& modifier, kiln::Function<void()> content, ui::Dp space = ui::Dp(1));
    void MultiChoiceSegmentedButtonRow(const ui::Modifier& modifier, kiln::Function<void()> content, ui::Dp space = ui::Dp(1));
    // Use inside the matching row. Each button receives equal weight automatically.
    void SegmentedButton(bool selected, kiln::Function<void()> onClick, foundation::ShapePtr shape, kiln::Function<void()> label,
                         const ui::Modifier& modifier = {}, const SegmentedButtonOptions& options = {});
    void SegmentedButton(bool checked, kiln::Function<void(bool)> onCheckedChange, foundation::ShapePtr shape, kiln::Function<void()> label,
                         const ui::Modifier& modifier = {}, const SegmentedButtonOptions& options = {});

    namespace SegmentedButtonDefaults {
        inline constexpr ui::Dp BorderWidth{ 1 }, IconSize{ 18 };
        extern const foundation::PaddingValues ContentPadding;
        SegmentedButtonColors colors();
        SegmentedButtonColors colors(const SegmentedButtonColors& overrides);
        std::shared_ptr<const foundation::CornerBasedShape> baseShape();
        foundation::ShapePtr itemShape(int index, int count, std::shared_ptr<const foundation::CornerBasedShape> base = {});
        foundation::BorderStroke borderStroke(ui::Color color, ui::Dp width = BorderWidth);
        void ActiveIcon();
        void Icon(bool active, kiln::Function<void()> activeContent = ActiveIcon, kiln::Function<void()> inactiveContent = {});
    }
}
