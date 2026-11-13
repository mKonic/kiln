#pragma once

// TabRow.kt: rows of tabs with an indicator under the selected one. Fixed rows share the width
// equally; scrollable rows size tabs to their content (at least 90 dp), start 52 dp in and scroll
// the selected tab towards the middle. Primary indicators match the tab's content width, secondary
// ones the whole tab.

#include "kiln/foundation/Layouts.h"
#include <kiln/Function.h>
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/State.h"

#include <functional>
#include <optional>
#include <vector>

namespace kiln::material3 {

    // A tab's place in its row, from the row's start.
    struct TabPosition {
        ui::Dp left{ 0 }, width{ 0 }, contentWidth{ 0 };   // contentWidth: at least 24 dp
        ui::Dp Right() const { return ui::Dp(left.value + width.value); }
        bool operator==(const TabPosition&) const = default;
    };

    // What an indicator can lay itself out with.
    class TabIndicatorScope {
    public:
        virtual ~TabIndicatorScope() = default;
        using IndicatorMeasure = kiln::Function<ui::MeasureResult(ui::MeasureScope&, ui::Measurable&, const ui::Constraints&, const std::vector<TabPosition>&)>;
        // A layout modifier given the tab positions (and remeasured when they change).
        virtual ui::Modifier TabIndicatorLayout(ui::Modifier m, IndicatorMeasure measure) = 0;
        // The default placement: the selected tab's offset and width (or content width), animated.
        virtual ui::Modifier TabIndicatorOffset(ui::Modifier m, int selectedTabIndex, bool matchContentSize = false) = 0;
    };

    namespace TabRowDefaults {
        inline constexpr ui::Dp ScrollableTabRowMinTabWidth{ 90 };
        inline constexpr ui::Dp ScrollableTabRowEdgeStartPadding{ 52 };
        ui::Color primaryContainerColor();
        ui::Color secondaryContainerColor();
        ui::Color primaryContentColor();
        ui::Color secondaryContentColor();
        // At the bottom of the row, over the divider: 3 dp tall, rounded at the top. An
        // Unspecified width takes the space the indicator is given.
        void PrimaryIndicator(const ui::Modifier& modifier = {}, ui::Dp width = ui::Dp(24), ui::Dp height = ui::Dp(3),
                              std::optional<ui::Color> color = std::nullopt, foundation::ShapePtr shape = nullptr);
        // The full width it is given, 3 dp tall.
        void SecondaryIndicator(const ui::Modifier& modifier = {}, ui::Dp height = ui::Dp(3), std::optional<ui::Color> color = std::nullopt);
    }

    struct TabRowOptions {
        std::optional<ui::Color> containerColor;   // unset: the primary / secondary container colour
        std::optional<ui::Color> contentColor;     // unset: the primary / secondary content colour
        kiln::Function<void(TabIndicatorScope&)> indicator;   // unset: the primary / secondary indicator
        kiln::Function<void()> divider;             // unset: HorizontalDivider; `[] {}` for none
    };
    void PrimaryTabRow(int selectedTabIndex, const ui::Modifier& modifier, const TabRowOptions& options, kiln::Function<void()> tabs);
    void SecondaryTabRow(int selectedTabIndex, const ui::Modifier& modifier, const TabRowOptions& options, kiln::Function<void()> tabs);

    struct ScrollableTabRowOptions : TabRowOptions {
        foundation::ScrollState* scrollState = nullptr;   // unset: a remembered one
        ui::Dp edgePadding = TabRowDefaults::ScrollableTabRowEdgeStartPadding;
        ui::Dp minTabWidth = TabRowDefaults::ScrollableTabRowMinTabWidth;
    };
    void PrimaryScrollableTabRow(int selectedTabIndex, const ui::Modifier& modifier, const ScrollableTabRowOptions& options, kiln::Function<void()> tabs);
    void SecondaryScrollableTabRow(int selectedTabIndex, const ui::Modifier& modifier, const ScrollableTabRowOptions& options,
                                   kiln::Function<void()> tabs);

}
