#pragma once

// NavigationItem.kt: the item shared by the short navigation bar and the wide navigation rail.
// Its icon sits above the label (Top) or before it (Start); a pill indicator behind the icon (or
// the icon and label) widens and fades in when selected.

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/text/TextStyle.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace kiln::material3 {

    enum class NavigationItemIconPosition : std::uint8_t { Top, Start };

    struct NavigationItemColors {
        ui::Color selectedIconColor, selectedTextColorTopIconPosition, selectedTextColorStartIconPosition, selectedIndicatorColor,
            unselectedIconColor, unselectedTextColor, disabledIconColor, disabledTextColor;
        // Unspecified arguments keep this one's colours.
        NavigationItemColors Copy(ui::Color selectedIconColor = ui::Color::Unspecified(),
                                  ui::Color selectedTextColorTopIconPosition = ui::Color::Unspecified(),
                                  ui::Color selectedTextColorStartIconPosition = ui::Color::Unspecified(),
                                  ui::Color selectedIndicatorColor = ui::Color::Unspecified(), ui::Color unselectedIconColor = ui::Color::Unspecified(),
                                  ui::Color unselectedTextColor = ui::Color::Unspecified(), ui::Color disabledIconColor = ui::Color::Unspecified(),
                                  ui::Color disabledTextColor = ui::Color::Unspecified()) const;
        ui::Color IconColor(bool selected, bool enabled) const;
        ui::Color TextColor(bool selected, bool enabled, bool isIconPositionTop = true) const;
        bool operator==(const NavigationItemColors&) const = default;
    };

    namespace internal {

        // PaddingValues between a collapsed and an expanded value, at a progress the animated
        // item sets while measuring (DynamicPaddingValues). Equal values make it fixed.
        struct DynamicPaddingValues {
            foundation::PaddingValues collapsed, expanded;
            bool isExpanded = false;   // as created
            float progress = 0;        // 0..1
            DynamicPaddingValues(foundation::PaddingValues collapsed, foundation::PaddingValues expanded, bool isExpanded)
                : collapsed(collapsed), expanded(expanded), isExpanded(isExpanded), progress(isExpanded ? 1.0f : 0.0f) {}
            explicit DynamicPaddingValues(foundation::PaddingValues fixed) : collapsed(fixed), expanded(fixed) {}
            foundation::PaddingValues Value() const;
        };

        struct NavigationItemParams {
            bool selected = false;
            kiln::Function<void()> onClick, icon;
            text::TextStyle labelTextStyle;
            foundation::ShapePtr indicatorShape;
            ui::Dp indicatorWidth{ 0 };
            foundation::PaddingValues indicatorPadding;
            ui::Dp indicatorToLabelVerticalPadding{ 0 }, startIconToLabelHorizontalPadding{ 0 };
            foundation::PaddingValues topIconItemPadding, startIconItemPadding;
            ui::Color textColor, iconColor, indicatorColor;
            ui::Modifier modifier;
            bool enabled = true;
            kiln::Function<void()> label;
            NavigationItemIconPosition iconPosition = NavigationItemIconPosition::Top;
            std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        };
        void NavigationItem(const NavigationItemParams& params);

        struct AnimatedNavigationItemParams {
            bool selected = false;
            kiln::Function<void()> onClick, icon;
            foundation::ShapePtr indicatorShape;
            ui::Dp topIconIndicatorWidth{ 0 };
            text::TextStyle topIconLabelTextStyle, startIconLabelTextStyle;
            std::shared_ptr<DynamicPaddingValues> indicatorPadding;
            ui::Dp topIconIndicatorToLabelVerticalPadding{ 0 };
            foundation::PaddingValues noLabelIndicatorPadding;
            ui::Dp startIconToLabelHorizontalPadding{ 0 };
            foundation::PaddingValues contentPadding;
            ui::Color textColor, iconColor, indicatorColor;
            ui::Modifier modifier;
            bool enabled = true;
            kiln::Function<void()> label;
            NavigationItemIconPosition iconPosition = NavigationItemIconPosition::Top;
            std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        };
        // Animates its layout between the Top and Start icon positions when iconPosition changes.
        void AnimatedNavigationItem(const AnimatedNavigationItemParams& params);

    }

}
