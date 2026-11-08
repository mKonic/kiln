#pragma once

// FloatingActionButtonMenu.kt: staggered FAB menus and their morphing toggle button.

#include "kiln/foundation/Compose.h"

namespace kiln::material3 {
    namespace internal { struct FabMenuState; }

    struct FloatingActionButtonMenuScope {
        ui::AlignmentH horizontalAlignment = ui::AlignmentH::End();
        std::shared_ptr<internal::FabMenuState> state;
    };
    void FloatingActionButtonMenu(bool expanded, std::function<void()> button, const ui::Modifier& modifier,
                                  ui::AlignmentH horizontalAlignment, std::function<void(const FloatingActionButtonMenuScope&)> content);
    struct FloatingActionButtonMenuItemOptions {
        std::optional<ui::Color> containerColor, contentColor;
    };
    void FloatingActionButtonMenuItem(const FloatingActionButtonMenuScope& scope, std::function<void()> onClick,
                                      std::function<void()> text, std::function<void()> icon, const ui::Modifier& modifier = {},
                                      const FloatingActionButtonMenuItemOptions& options = {});

    struct ToggleFloatingActionButtonScope { float checkedProgress; };
    struct ToggleFloatingActionButtonOptions {
        std::function<ui::Color(float)> containerColor;
        ui::Alignment contentAlignment = ui::Alignment::TopEnd();
        std::function<ui::Dp(float)> containerSize, containerCornerRadius;
    };
    void ToggleFloatingActionButton(bool checked, std::function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                                    const ToggleFloatingActionButtonOptions& options, std::function<void(const ToggleFloatingActionButtonScope&)> content);

    namespace ToggleFloatingActionButtonDefaults {
        std::function<ui::Color(float)> containerColor();
        std::function<ui::Color(float)> containerColor(ui::Color initialColor, ui::Color finalColor);
        std::function<ui::Dp(float)> containerSize();
        std::function<ui::Dp(float)> containerSize(ui::Dp initialSize, ui::Dp finalSize = ui::Dp(56));
        std::function<ui::Dp(float)> containerSizeMedium();
        std::function<ui::Dp(float)> containerSizeLarge();
        std::function<ui::Dp(float)> containerCornerRadius();
        std::function<ui::Dp(float)> containerCornerRadius(ui::Dp initialSize, ui::Dp finalSize = ui::Dp(28));
        std::function<ui::Dp(float)> containerCornerRadiusMedium();
        std::function<ui::Dp(float)> containerCornerRadiusLarge();
        std::function<ui::Color(float)> iconColor();
        std::function<ui::Color(float)> iconColor(ui::Color initialColor, ui::Color finalColor);
        std::function<ui::Dp(float)> iconSize();
        std::function<ui::Dp(float)> iconSize(ui::Dp initialSize, ui::Dp finalSize = ui::Dp(20));
        std::function<ui::Dp(float)> iconSizeMedium();
        std::function<ui::Dp(float)> iconSizeLarge();
        ui::Modifier AnimateIcon(ui::Modifier modifier, float checkedProgress, std::function<ui::Color(float)> color = {},
                                 std::function<ui::Dp(float)> size = {});
    }
}
