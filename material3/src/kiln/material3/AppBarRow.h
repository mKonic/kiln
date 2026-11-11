#pragma once

// AppBarRow.kt, AppBarColumn.kt, AppBarDsl.kt: a row (or column) of app bar items that moves the
// items that do not fit, or are past maxItemCount, into an overflow menu.

#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/IconButton.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Modifier.h"

#include <climits>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::material3 {

    // The overflow menu's state.
    class AppBarMenuState {
    public:
        bool IsShowing() const { return m_Showing.Get(); }   // observable
        void Dismiss() { m_Showing.Set(false); }
        void Show() { m_Showing.Set(true); }

    private:
        rt::State<bool> m_Showing{ false };
    };

    namespace internal {
        // An item: how it shows in the bar and how in the overflow menu.
        struct AppBarItem {
            std::function<void()> appbarContent;
            std::function<void(AppBarMenuState&)> menuContent;
        };
    }

    // AppBarScope (AppBarRowScope, AppBarColumnScope): what the content adds items with.
    class AppBarScope {
    public:
        // An IconButton with a tooltip; in the menu an item that runs onClick and closes the menu.
        void ClickableItem(std::function<void()> onClick, std::function<void()> icon, std::string label, bool enabled = true);
        // An IconToggleButton with a tooltip; in the menu an item that flips it.
        void ToggleableItem(bool checked, std::function<void(bool)> onCheckedChange, std::function<void()> icon, std::string label,
                            bool enabled = true);
        void CustomItem(std::function<void()> appbarContent, std::function<void(AppBarMenuState&)> menuContent);

        const std::vector<internal::AppBarItem>& Items() const { return m_Items; }

    private:
        std::vector<internal::AppBarItem> m_Items;
    };
    using AppBarRowScope = AppBarScope;
    using AppBarColumnScope = AppBarScope;

    // Unset overflowIndicator: AppBarOverflowIndicator. With more items than maxItemCount, at most
    // maxItemCount - 1 show, the last place going to the indicator.
    void AppBarRow(const ui::Modifier& modifier, std::function<void(AppBarMenuState&)> overflowIndicator, int maxItemCount,
                   std::function<void(AppBarScope&)> content);
    inline void AppBarRow(const ui::Modifier& modifier, std::function<void(AppBarScope&)> content) {
        AppBarRow(modifier, {}, INT_MAX, std::move(content));
    }
    void AppBarColumn(const ui::Modifier& modifier, std::function<void(AppBarMenuState&)> overflowIndicator, int maxItemCount,
                      std::function<void(AppBarScope&)> content);
    inline void AppBarColumn(const ui::Modifier& modifier, std::function<void(AppBarScope&)> content) {
        AppBarColumn(modifier, {}, INT_MAX, std::move(content));
    }

    // The default overflow indicator: a "More options" IconButton (with a tooltip) that toggles
    // the menu.
    struct AppBarOverflowIndicatorOptions {
        bool enabled = true;
        foundation::ShapePtr shape;                 // null: IconButtonDefaults::standardShape
        std::optional<IconButtonColors> colors;     // unset: iconButtonColors()
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void AppBarOverflowIndicator(AppBarMenuState& menuState, const ui::Modifier& modifier = {}, const AppBarOverflowIndicatorOptions& options = {});

}
