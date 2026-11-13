#pragma once

// WideNavigationRail.kt / WideNavigationRailState.kt: the Expressive wide navigation rail. It is
// collapsed (96 dp, icons above labels) or expanded (220-360 dp, icons before labels), animating
// its width and its items between the two.

#include "kiln/foundation/Arrangement.h"
#include <kiln/Function.h>
#include "kiln/foundation/Insets.h"
#include "kiln/ui/Configuration.h"
#include "kiln/material3/NavigationItem.h"
#include "kiln/runtime/Task.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    enum class WideNavigationRailValue : std::uint8_t { Collapsed, Expanded };

    // Observes and sets whether a wide navigation rail is expanded. Reading the values subscribes.
    class WideNavigationRailState {
    public:
        virtual ~WideNavigationRailState() = default;
        virtual bool IsAnimating() const = 0;
        virtual WideNavigationRailValue TargetValue() const = 0;    // where it is going
        virtual WideNavigationRailValue CurrentValue() const = 0;   // where it is (fully)
        virtual rt::Task<> Expand() = 0;
        virtual rt::Task<> Collapse() = 0;
        virtual rt::Task<> Toggle() = 0;
        virtual rt::Task<> SnapTo(WideNavigationRailValue value) = 0;
    };
    std::shared_ptr<WideNavigationRailState> RememberWideNavigationRailState(WideNavigationRailValue initialValue = WideNavigationRailValue::Collapsed, rt::Key key = {});

    struct WideNavigationRailColors {
        ui::Color containerColor, contentColor, modalContainerColor, modalScrimColor, modalContentColor;
        WideNavigationRailColors Copy(ui::Color containerColor = ui::Color::Unspecified(), ui::Color contentColor = ui::Color::Unspecified(),
                                      ui::Color modalContainerColor = ui::Color::Unspecified(), ui::Color modalScrimColor = ui::Color::Unspecified(),
                                      ui::Color modalContentColor = ui::Color::Unspecified()) const;
        bool operator==(const WideNavigationRailColors&) const = default;
    };

    namespace WideNavigationRailDefaults {
        foundation::ShapePtr shape();
        inline foundation::Arrangement arrangement() { return foundation::Arrangement::Top(); }
        // The system bars and display cutout at the top, bottom and start.
        foundation::WindowInsets windowInsets();
        foundation::ShapePtr modalCollapsedShape();
        foundation::ShapePtr modalExpandedShape();
        WideNavigationRailColors colors();
        // contentColor and modalContentColor default to the content colours for the containers.
        WideNavigationRailColors colors(ui::Color containerColor, ui::Color contentColor = ui::Color::Unspecified(),
                                        ui::Color modalContainerColor = ui::Color::Unspecified(), ui::Color modalScrimColor = ui::Color::Unspecified(),
                                        ui::Color modalContentColor = ui::Color::Unspecified());
        inline constexpr foundation::PaddingValues ContentPadding{ ui::Dp(0), ui::Dp(44), ui::Dp(0), ui::Dp(0) };   // TopSpace
    }

    namespace WideNavigationRailItemDefaults {
        inline constexpr foundation::PaddingValues IndicatorCollapsedPadding{ ui::Dp(16), ui::Dp(4), ui::Dp(16), ui::Dp(4) };
        inline constexpr foundation::PaddingValues IndicatorExpandedPadding{ ui::Dp(16), ui::Dp(16), ui::Dp(16), ui::Dp(16) };
        // Remembered padding that animates between the two with the item's icon position.
        std::shared_ptr<internal::DynamicPaddingValues> indicatorPadding(bool railExpanded,
                                                                         foundation::PaddingValues collapsedPadding = IndicatorCollapsedPadding,
                                                                         foundation::PaddingValues expandedPadding = IndicatorExpandedPadding);
        inline NavigationItemIconPosition iconPositionFor(bool railExpanded) {
            return railExpanded ? NavigationItemIconPosition::Start : NavigationItemIconPosition::Top;
        }
        NavigationItemColors colors();
        // The disabled colours default to the given unselected ones, faded.
        NavigationItemColors colors(ui::Color selectedIconColor, ui::Color selectedTextColorTopIconPosition = ui::Color::Unspecified(),
                                    ui::Color selectedTextColorStartIconPosition = ui::Color::Unspecified(),
                                    ui::Color selectedIndicatorColor = ui::Color::Unspecified(),
                                    ui::Color unselectedIconColor = ui::Color::Unspecified(), ui::Color unselectedTextColor = ui::Color::Unspecified(),
                                    ui::Color disabledIconColor = ui::Color::Unspecified(), ui::Color disabledTextColor = ui::Color::Unspecified());
    }

    struct WideNavigationRailOptions {
        std::shared_ptr<WideNavigationRailState> state;   // unset: RememberWideNavigationRailState()
        foundation::ShapePtr shape;                       // unset: WideNavigationRailDefaults::shape
        std::optional<WideNavigationRailColors> colors;
        kiln::Function<void()> header;                     // a menu button, a FAB or a logo, 40 dp above the items
        std::optional<foundation::WindowInsets> windowInsets;
        // The items' arrangement below the header (Center centres them in the whole rail).
        foundation::Arrangement arrangement = WideNavigationRailDefaults::arrangement();
        foundation::PaddingValues contentPadding = WideNavigationRailDefaults::ContentPadding;
    };
    void WideNavigationRail(const ui::Modifier& modifier, const WideNavigationRailOptions& options, kiln::Function<void()> content);

    // The expanded modal rail's window.
    struct ModalWideNavigationRailProperties {
        ui::SecureFlagPolicy securePolicy = ui::SecureFlagPolicy::Inherit;
        bool shouldDismissOnBackPress = true;   // back (with its predictive animation) collapses the rail
        bool operator==(const ModalWideNavigationRailProperties&) const = default;
    };

    struct ModalWideNavigationRailOptions {
        std::shared_ptr<WideNavigationRailState> state;   // unset: RememberWideNavigationRailState()
        // true: slides in from offscreen and hides when collapsed; false: stays as a collapsed rail.
        bool hideOnCollapse = false;
        foundation::ShapePtr collapsedShape;   // unset: modalCollapsedShape
        foundation::ShapePtr expandedShape;    // unset: modalExpandedShape
        std::optional<WideNavigationRailColors> colors;
        kiln::Function<void()> header;
        ui::Dp expandedHeaderTopPadding{ 0 };   // aligns the header between the two states
        std::optional<foundation::WindowInsets> windowInsets;
        foundation::Arrangement arrangement = WideNavigationRailDefaults::arrangement();
        ModalWideNavigationRailProperties expandedProperties;
        foundation::PaddingValues contentPadding = WideNavigationRailDefaults::ContentPadding;
    };
    // Collapsed, a wide navigation rail (or nothing, with hideOnCollapse). Expanded, it opens in a
    // window above the app with a scrim that collapses it on a tap; the layout behind is unchanged.
    // Unlike Compose, the content is not movable content: moving between the collapsed rail and
    // the window composes it anew.
    void ModalWideNavigationRail(const ui::Modifier& modifier, const ModalWideNavigationRailOptions& options, kiln::Function<void()> content);

    struct WideNavigationRailItemOptions {
        bool enabled = true;
        std::optional<NavigationItemIconPosition> iconPosition;   // unset: iconPositionFor(railExpanded)
        std::optional<NavigationItemColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        std::shared_ptr<internal::DynamicPaddingValues> indicatorPadding;   // unset: the defaults' indicatorPadding
    };
    // The label (if any) always shows; the item animates between the icon positions.
    void WideNavigationRailItem(bool selected, kiln::Function<void()> onClick, kiln::Function<void()> icon, kiln::Function<void()> label, bool railExpanded,
                                const ui::Modifier& modifier, const WideNavigationRailItemOptions& options);

    namespace internal {
        inline constexpr ui::Dp WNRItemNoLabelIndicatorPadding{ 16 };   // (56 - 24) / 2
        inline constexpr ui::Dp WNRItemHorizontalPadding{ 20 };
        inline constexpr ui::Dp WNRHeaderPadding{ 40 };                 // HeaderSpaceMinimum
        inline constexpr ui::Dp CollapsedRailWidth{ 96 }, ExpandedRailMinWidth{ 220 }, ExpandedRailMaxWidth{ 360 };
        inline constexpr ui::Dp TopIconItemMinHeight{ 64 };             // NavigationRailBaselineItemTokens.ContainerHeight
    }

}
