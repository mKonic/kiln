#pragma once

// NavigationBar.kt: the bottom navigation bar and its items. Each item is a selectable tab whose
// pill indicator widens and fades in when selected, with the label shown always or only while
// selected (the icon then moves up to make room).

#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct NavigationBarItemColors {
        ui::Color selectedIconColor, selectedTextColor, selectedIndicatorColor, unselectedIconColor, unselectedTextColor, disabledIconColor,
            disabledTextColor;
        // Unspecified arguments keep this one's colours.
        NavigationBarItemColors Copy(ui::Color selectedIconColor = ui::Color::Unspecified(), ui::Color selectedTextColor = ui::Color::Unspecified(),
                                     ui::Color selectedIndicatorColor = ui::Color::Unspecified(),
                                     ui::Color unselectedIconColor = ui::Color::Unspecified(),
                                     ui::Color unselectedTextColor = ui::Color::Unspecified(), ui::Color disabledIconColor = ui::Color::Unspecified(),
                                     ui::Color disabledTextColor = ui::Color::Unspecified()) const;
        ui::Color IconColor(bool selected, bool enabled) const;
        ui::Color TextColor(bool selected, bool enabled) const;
        ui::Color IndicatorColor() const { return selectedIndicatorColor; }
        bool operator==(const NavigationBarItemColors&) const = default;
    };

    namespace NavigationBarDefaults {
        inline constexpr ui::Dp Elevation{ 0 };   // ElevationTokens.Level0
        ui::Color containerColor();
        // The system bars and display cutout at the bottom and the sides.
        foundation::WindowInsets windowInsets();
    }

    namespace NavigationBarItemDefaults {
        NavigationBarItemColors colors();
        NavigationBarItemColors colors(ui::Color selectedIconColor, ui::Color selectedTextColor = ui::Color::Unspecified(),
                                       ui::Color indicatorColor = ui::Color::Unspecified(), ui::Color unselectedIconColor = ui::Color::Unspecified(),
                                       ui::Color unselectedTextColor = ui::Color::Unspecified(),
                                       ui::Color disabledIconColor = ui::Color::Unspecified(), ui::Color disabledTextColor = ui::Color::Unspecified());
    }

    struct NavigationBarOptions {
        std::optional<ui::Color> containerColor;   // unset: NavigationBarDefaults::containerColor
        std::optional<ui::Color> contentColor;     // unset: contentColorFor(containerColor)
        ui::Dp tonalElevation = NavigationBarDefaults::Elevation;
        std::optional<foundation::WindowInsets> windowInsets;   // unset: NavigationBarDefaults
    };
    // A full-width row at least 80 dp tall, items 8 dp apart; content is 3-5 NavigationBarItems.
    void NavigationBar(const ui::Modifier& modifier, const NavigationBarOptions& options, std::function<void()> content);

    struct NavigationBarItemOptions {
        bool enabled = true;
        std::function<void()> label;
        bool alwaysShowLabel = true;               // false: the label shows only while selected
        std::optional<NavigationBarItemColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    // An item of a NavigationBar's row (it takes an equal share of the width).
    void NavigationBarItem(bool selected, std::function<void()> onClick, std::function<void()> icon, const ui::Modifier& modifier,
                           const NavigationBarItemOptions& options);

    namespace internal {
        inline constexpr ui::Dp NavigationBarItemHorizontalPadding{ 8 }, NavigationBarIndicatorToLabelPadding{ 4 },
            NavigationBarItemToIconMinimumPadding{ 44 };
        inline constexpr ui::Dp NavigationBarIndicatorVerticalPadding{ (32 - 24) / 2.0f };   // (indicator height - icon) / 2
    }

}
