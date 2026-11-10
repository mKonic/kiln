#pragma once

// ShortNavigationBar.kt: the Expressive short (64 dp) navigation bar. Items share the width
// equally (small screens, icon above the label) or sit grouped in the middle (medium screens,
// icon before the label).

#include "kiln/foundation/Insets.h"
#include "kiln/material3/NavigationItem.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    enum class ShortNavigationBarArrangement : std::uint8_t { EqualWeight, Centered };

    namespace ShortNavigationBarDefaults {
        ui::Color containerColor();
        ui::Color contentColor();   // contentColorFor(containerColor)
        inline constexpr ShortNavigationBarArrangement arrangement = ShortNavigationBarArrangement::EqualWeight;
        // The system bars and display cutout at the bottom and the sides.
        foundation::WindowInsets windowInsets();
    }

    namespace ShortNavigationBarItemDefaults {
        NavigationItemColors colors();
        // The disabled colours default to the given unselected ones, faded.
        NavigationItemColors colors(ui::Color selectedIconColor, ui::Color selectedTextColorTopIconPosition = ui::Color::Unspecified(),
                                    ui::Color selectedTextColorStartIconPosition = ui::Color::Unspecified(),
                                    ui::Color selectedIndicatorColor = ui::Color::Unspecified(),
                                    ui::Color unselectedIconColor = ui::Color::Unspecified(), ui::Color unselectedTextColor = ui::Color::Unspecified(),
                                    ui::Color disabledIconColor = ui::Color::Unspecified(), ui::Color disabledTextColor = ui::Color::Unspecified());
    }

    struct ShortNavigationBarOptions {
        std::optional<ui::Color> containerColor;   // unset: ShortNavigationBarDefaults
        std::optional<ui::Color> contentColor;
        std::optional<foundation::WindowInsets> windowInsets;
        ShortNavigationBarArrangement arrangement = ShortNavigationBarDefaults::arrangement;
    };
    // Content is 3-6 ShortNavigationBarItems.
    void ShortNavigationBar(const ui::Modifier& modifier, const ShortNavigationBarOptions& options, std::function<void()> content);

    struct ShortNavigationBarItemOptions {
        bool enabled = true;
        NavigationItemIconPosition iconPosition = NavigationItemIconPosition::Top;
        std::optional<NavigationItemColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    // The label (if any) always shows.
    void ShortNavigationBarItem(bool selected, std::function<void()> onClick, std::function<void()> icon, std::function<void()> label,
                                const ui::Modifier& modifier, const ShortNavigationBarItemOptions& options);

    namespace internal {
        inline constexpr ui::Dp TopIconItemVerticalPadding{ 6 };          // NavigationBarVerticalItemTokens.ContainerBetweenSpace
        inline constexpr ui::Dp TopIconIndicatorVerticalPadding{ 4 };     // (32 - 24) / 2
        inline constexpr ui::Dp TopIconIndicatorHorizontalPadding{ 16 };  // (56 - 24) / 2
        inline constexpr ui::Dp StartIconIndicatorVerticalPadding{ 8 };   // (40 - 24) / 2
        inline constexpr ui::Dp TopIconIndicatorToLabelPadding{ 4 };
        inline constexpr ui::Dp StartIconIndicatorHorizontalPadding{ 16 };   // ActiveIndicatorLeadingSpace
        inline constexpr ui::Dp StartIconToLabelPadding{ 4 };                // ItemActiveIndicatorIconLabelSpace
    }

}
