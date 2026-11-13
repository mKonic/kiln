#pragma once

// FloatingActionButtonMenu.kt: staggered FAB menus and their morphing toggle button.

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>

namespace kiln::material3 {
    namespace internal { struct FabMenuState; }

    struct FloatingActionButtonMenuScope {
        ui::AlignmentH horizontalAlignment = ui::AlignmentH::End();
        std::shared_ptr<internal::FabMenuState> state;
    };
    void FloatingActionButtonMenu(bool expanded, kiln::Function<void()> button, const ui::Modifier& modifier,
                                  ui::AlignmentH horizontalAlignment, kiln::Function<void(const FloatingActionButtonMenuScope&)> content);
    struct FloatingActionButtonMenuItemOptions {
        std::optional<ui::Color> containerColor, contentColor;
    };
    void FloatingActionButtonMenuItem(const FloatingActionButtonMenuScope& scope, kiln::Function<void()> onClick,
                                      kiln::Function<void()> text, kiln::Function<void()> icon, const ui::Modifier& modifier = {},
                                      const FloatingActionButtonMenuItemOptions& options = {});

    struct ToggleFloatingActionButtonScope { float checkedProgress; };
    struct ToggleFloatingActionButtonOptions {
        kiln::Function<ui::Color(float)> containerColor;
        ui::Alignment contentAlignment = ui::Alignment::TopEnd();
        kiln::Function<ui::Dp(float)> containerSize, containerCornerRadius;
    };
    void ToggleFloatingActionButton(bool checked, kiln::Function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                    const ToggleFloatingActionButtonOptions& options, kiln::Function<void(const ToggleFloatingActionButtonScope&)> content);

    namespace ToggleFloatingActionButtonDefaults {
        kiln::Function<ui::Color(float)> containerColor();
        kiln::Function<ui::Color(float)> containerColor(ui::Color initialColor, ui::Color finalColor);
        kiln::Function<ui::Dp(float)> containerSize();
        kiln::Function<ui::Dp(float)> containerSize(ui::Dp initialSize, ui::Dp finalSize = ui::Dp(56));
        kiln::Function<ui::Dp(float)> containerSizeMedium();
        kiln::Function<ui::Dp(float)> containerSizeLarge();
        kiln::Function<ui::Dp(float)> containerCornerRadius();
        kiln::Function<ui::Dp(float)> containerCornerRadius(ui::Dp initialSize, ui::Dp finalSize = ui::Dp(28));
        kiln::Function<ui::Dp(float)> containerCornerRadiusMedium();
        kiln::Function<ui::Dp(float)> containerCornerRadiusLarge();
        kiln::Function<ui::Color(float)> iconColor();
        kiln::Function<ui::Color(float)> iconColor(ui::Color initialColor, ui::Color finalColor);
        kiln::Function<ui::Dp(float)> iconSize();
        kiln::Function<ui::Dp(float)> iconSize(ui::Dp initialSize, ui::Dp finalSize = ui::Dp(20));
        kiln::Function<ui::Dp(float)> iconSizeMedium();
        kiln::Function<ui::Dp(float)> iconSizeLarge();
        ui::Modifier AnimateIcon(ui::Modifier modifier, float checkedProgress, kiln::Function<ui::Color(float)> color = {},
                                 kiln::Function<ui::Dp(float)> size = {});
    }
}
