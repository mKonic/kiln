#pragma once

// NavigationRail.kt: the side navigation rail for tablets and desktops, with an optional header
// (a FAB or a logo) above 3-7 items. Each item is a selectable tab whose pill indicator widens and
// fades in when selected, with the label shown always or only while selected.

#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct NavigationRailItemColors {
        ui::Color selectedIconColor, selectedTextColor, selectedIndicatorColor, unselectedIconColor, unselectedTextColor, disabledIconColor,
            disabledTextColor;
        // Unspecified arguments keep this one's colours.
        NavigationRailItemColors Copy(ui::Color selectedIconColor = ui::Color::Unspecified(), ui::Color selectedTextColor = ui::Color::Unspecified(),
                                      ui::Color selectedIndicatorColor = ui::Color::Unspecified(),
                                      ui::Color unselectedIconColor = ui::Color::Unspecified(),
                                      ui::Color unselectedTextColor = ui::Color::Unspecified(), ui::Color disabledIconColor = ui::Color::Unspecified(),
                                      ui::Color disabledTextColor = ui::Color::Unspecified()) const;
        ui::Color IconColor(bool selected, bool enabled) const;
        ui::Color TextColor(bool selected, bool enabled) const;
        ui::Color IndicatorColor() const { return selectedIndicatorColor; }
        bool operator==(const NavigationRailItemColors&) const = default;
    };

    namespace NavigationRailDefaults {
        ui::Color ContainerColor();
        // The system bars and display cutout at the top, bottom and start.
        foundation::WindowInsets windowInsets();
    }

    namespace NavigationRailItemDefaults {
        NavigationRailItemColors colors();
        NavigationRailItemColors colors(ui::Color selectedIconColor, ui::Color selectedTextColor = ui::Color::Unspecified(),
                                        ui::Color indicatorColor = ui::Color::Unspecified(), ui::Color unselectedIconColor = ui::Color::Unspecified(),
                                        ui::Color unselectedTextColor = ui::Color::Unspecified(),
                                        ui::Color disabledIconColor = ui::Color::Unspecified(), ui::Color disabledTextColor = ui::Color::Unspecified());
    }

    struct NavigationRailOptions {
        std::optional<ui::Color> containerColor;   // unset: NavigationRailDefaults::ContainerColor
        std::optional<ui::Color> contentColor;     // unset: contentColorFor(containerColor)
        std::function<void()> header;              // a FAB or a logo, 8 dp above the items
        std::optional<foundation::WindowInsets> windowInsets;   // unset: NavigationRailDefaults
    };
    // A full-height column at least 80 dp wide, items centred and 4 dp apart.
    void NavigationRail(const ui::Modifier& modifier, const NavigationRailOptions& options, std::function<void()> content);

    struct NavigationRailItemOptions {
        bool enabled = true;
        std::function<void()> label;
        bool alwaysShowLabel = true;               // false: the label shows only while selected
        std::optional<NavigationRailItemColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    // An item of a NavigationRail, at least 80 dp wide and 56 dp tall.
    void NavigationRailItem(bool selected, std::function<void()> onClick, std::function<void()> icon, const ui::Modifier& modifier,
                            const NavigationRailItemOptions& options);

    namespace internal {
        inline constexpr ui::Dp NavigationRailVerticalPadding{ 4 }, NavigationRailHeaderPadding{ 8 }, NavigationRailItemVerticalPadding{ 4 };
        inline constexpr ui::Dp NavigationRailItemWidth{ 80 };    // NavigationRailCollapsedTokens.NarrowContainerWidth
        inline constexpr ui::Dp NavigationRailItemHeight{ 56 };   // NavigationRailVerticalItemTokens.ActiveIndicatorWidth
    }

}
