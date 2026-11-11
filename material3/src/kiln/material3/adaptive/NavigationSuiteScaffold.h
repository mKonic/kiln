#pragma once

// material3-adaptive-navigation-suite NavigationSuiteScaffold.kt: a scaffold that puts the
// navigation component the window calls for (a short navigation bar, a wide navigation rail, or
// the older bar / rail / permanent drawer) beside or below the content, and can hide it with an
// animation. NavigationSuite picks the component; NavigationSuiteItem the matching item.
//
// Unlike Compose, NavigationSuite's content is not movable content: switching the suite type
// composes the items anew.

#include "kiln/foundation/Arrangement.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/material3/NavigationBar.h"
#include "kiln/material3/NavigationDrawer.h"
#include "kiln/material3/NavigationItem.h"
#include "kiln/material3/NavigationRail.h"
#include "kiln/material3/WideNavigationRail.h"
#include "kiln/material3/adaptive/WindowAdaptiveInfo.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Modifier.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::material3::adaptive {

    enum class NavigationSuiteScaffoldValue : std::uint8_t { Visible, Hidden };

    // Shows or hides the scaffold's navigation component. Reading the values subscribes.
    class NavigationSuiteScaffoldState {
    public:
        virtual ~NavigationSuiteScaffoldState() = default;
        virtual bool IsAnimating() const = 0;
        virtual NavigationSuiteScaffoldValue TargetValue() const = 0;
        virtual NavigationSuiteScaffoldValue CurrentValue() const = 0;
        virtual rt::Task<> Hide() = 0;
        virtual rt::Task<> Show() = 0;
        virtual rt::Task<> Toggle() = 0;
        virtual rt::Task<> SnapTo(NavigationSuiteScaffoldValue targetValue) = 0;
    };
    // rememberNavigationSuiteScaffoldState: the target value survives re-creation.
    std::shared_ptr<NavigationSuiteScaffoldState> RememberNavigationSuiteScaffoldState(
        NavigationSuiteScaffoldValue initialValue = NavigationSuiteScaffoldValue::Visible, rt::Key key = {});

    enum class NavigationSuiteType : std::uint8_t {
        ShortNavigationBarCompact,     // a short bar, items with the icon on top
        ShortNavigationBarMedium,      // a short bar, items with the icon at the start
        WideNavigationRailCollapsed,
        WideNavigationRailExpanded,
        NavigationBar,                 // prefer ShortNavigationBarCompact
        NavigationRail,                // prefer WideNavigationRailCollapsed
        NavigationDrawer,              // prefer WideNavigationRailExpanded (a permanent drawer sheet)
        None,
    };
    const char* ToString(NavigationSuiteType type);   // its name, as Compose's toString

    struct NavigationSuiteColors {
        ui::Color shortNavigationBarContainerColor, shortNavigationBarContentColor;
        WideNavigationRailColors wideNavigationRailColors;
        ui::Color navigationBarContainerColor, navigationBarContentColor;
        ui::Color navigationRailContainerColor, navigationRailContentColor;
        ui::Color navigationDrawerContainerColor, navigationDrawerContentColor;
        bool operator==(const NavigationSuiteColors&) const = default;
    };

    struct NavigationSuiteItemColors {
        NavigationBarItemColors navigationBarItemColors;
        NavigationRailItemColors navigationRailItemColors;
        NavigationDrawerItemColors navigationDrawerItemColors;
        bool operator==(const NavigationSuiteItemColors&) const = default;
    };

    namespace NavigationSuiteScaffoldDefaults {
        // Compact width: a short bar with top icons; tabletop or compact height: a short bar with
        // start icons; otherwise a collapsed wide rail.
        NavigationSuiteType navigationSuiteType(const WindowAdaptiveInfo& adaptiveInfo);
        // The older choice: a navigation bar when compact on any axis or tabletop, else a rail.
        NavigationSuiteType calculateFromAdaptiveInfo(const WindowAdaptiveInfo& adaptiveInfo);
        ui::Color containerColor();   // colorScheme.background
        ui::Color contentColor();     // colorScheme.onBackground
        inline constexpr ui::AlignmentH primaryActionContentAlignment = ui::AlignmentH::End();
    }

    namespace NavigationSuiteDefaults {
        inline foundation::Arrangement verticalArrangement() { return foundation::Arrangement::Top(); }
        NavigationSuiteColors colors();
        NavigationSuiteItemColors itemColors();
    }

    struct NavigationSuiteOptions {
        std::optional<NavigationSuiteColors> colors;   // unset: NavigationSuiteDefaults::colors()
        // Of the items in vertical components; Top, Center or Bottom.
        foundation::Arrangement verticalArrangement = NavigationSuiteDefaults::verticalArrangement();
        // A FAB, say: the header of vertical components (horizontal ones leave it to the scaffold).
        std::function<void()> primaryActionContent;
    };
    // The navigation component for `navigationSuiteType`, holding `content` (NavigationSuiteItems).
    void NavigationSuite(NavigationSuiteType navigationSuiteType, const ui::Modifier& modifier, const NavigationSuiteOptions& options,
                         std::function<void()> content);

    struct NavigationSuiteItemOptions {
        std::optional<NavigationSuiteType> navigationSuiteType;   // unset: navigationSuiteType(currentWindowAdaptiveInfo())
        bool enabled = true;
        std::function<void()> badge;
        std::optional<NavigationItemColors> colors;   // unset: each item's defaults
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    // The item matching the suite type: a short bar item, a wide rail item, or the older bar /
    // rail / drawer item (the bar's is a short bar item padded to look the same).
    void NavigationSuiteItem(bool selected, std::function<void()> onClick, std::function<void()> icon, std::function<void()> label,
                             const ui::Modifier& modifier, const NavigationSuiteItemOptions& options);

    // Places the navigation suite at the bottom (bars) or the start (rails, drawer), the content
    // in the rest, and the primary action above a bar; hiding slides the suite out and gives its
    // space to the content.
    void NavigationSuiteScaffoldLayout(std::function<void()> navigationSuite, NavigationSuiteType navigationSuiteType,
                                       std::shared_ptr<NavigationSuiteScaffoldState> state, std::function<void()> primaryActionContent,
                                       ui::AlignmentH primaryActionContentHorizontalAlignment, std::function<void()> content);

    struct NavigationSuiteScaffoldOptions {
        std::optional<NavigationSuiteType> navigationSuiteType;   // unset: navigationSuiteType(currentWindowAdaptiveInfo())
        std::optional<NavigationSuiteColors> navigationSuiteColors;
        std::optional<ui::Color> containerColor, contentColor;   // unset: the scaffold defaults
        std::shared_ptr<NavigationSuiteScaffoldState> state;      // null: remembered
        foundation::Arrangement navigationItemVerticalArrangement = NavigationSuiteDefaults::verticalArrangement();
        std::function<void()> primaryActionContent;
        ui::AlignmentH primaryActionContentHorizontalAlignment = NavigationSuiteScaffoldDefaults::primaryActionContentAlignment;
    };
    // navigationItems: NavigationSuiteItems. The content consumes the insets the suite takes.
    void NavigationSuiteScaffold(std::function<void()> navigationItems, const ui::Modifier& modifier, const NavigationSuiteScaffoldOptions& options,
                                 std::function<void()> content);

    // --- The scope form (NavigationSuiteScope.item) --------------------------------------------

    struct NavigationSuiteScopeItemOptions {
        bool enabled = true;
        std::function<void()> label;
        bool alwaysShowLabel = true;   // the bar and rail only
        std::function<void()> badge;
        std::optional<NavigationSuiteItemColors> colors;   // unset: NavigationSuiteDefaults::itemColors()
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        ui::Modifier modifier;
    };
    class NavigationSuiteScope {
    public:
        void Item(bool selected, std::function<void()> onClick, std::function<void()> icon, const NavigationSuiteScopeItemOptions& options = {});

        struct ItemData {
            bool selected;
            std::function<void()> onClick, icon;
            NavigationSuiteScopeItemOptions options;
        };
        std::vector<ItemData> items;
    };

    // NavigationSuite(layoutType, colors) { item(...) }: the scope's items in the component for
    // `layoutType` (None shows nothing).
    void NavigationSuite(const ui::Modifier& modifier, NavigationSuiteType layoutType, const std::optional<NavigationSuiteColors>& colors,
                         std::function<void(NavigationSuiteScope&)> content);

    struct NavigationSuiteScaffoldScopeOptions {
        std::optional<NavigationSuiteType> layoutType;   // unset: calculateFromAdaptiveInfo(currentWindowAdaptiveInfo())
        std::optional<NavigationSuiteColors> navigationSuiteColors;
        std::optional<ui::Color> containerColor, contentColor;
        std::shared_ptr<NavigationSuiteScaffoldState> state;
    };
    void NavigationSuiteScaffold(std::function<void(NavigationSuiteScope&)> navigationSuiteItems, const ui::Modifier& modifier,
                                 const NavigationSuiteScaffoldScopeOptions& options, std::function<void()> content = {});

    namespace internal {
        inline constexpr ui::Dp ShortNavigationBarHeight{ 64 }, TallNavigationBarHeight{ 80 }, PrimaryActionContentPadding{ 16 };
    }

}
