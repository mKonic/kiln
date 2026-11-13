#include "kiln/material3/ShortNavigationBar.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Scaffold.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/tokens/NavigationBarTokens.h"
#include "kiln/material3/tokens/NavigationBarVerticalItemTokens.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }

        // Items share the width equally and take the tallest item's height.
        class EqualWeightPolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> items, const ui::Constraints& c) override {
                const int width = c.maxWidth;
                int itemHeight = c.minHeight;
                if (items.empty()) return s.Layout(width, itemHeight);
                std::vector<ui::Placeable*> placeables;
                if (!c.HasBoundedWidth()) {
                    // Unbounded: items as wide as they are.
                    for (auto* m : items) placeables.push_back(&m->Measure(c.Constrain(ui::Constraints::FixedHeight(itemHeight))));
                } else {
                    const int itemWidth = width / int(items.size());
                    for (auto* m : items) itemHeight = std::max(itemHeight, std::min(m->MaxIntrinsicHeight(itemWidth), c.maxHeight));
                    for (auto* m : items) placeables.push_back(&m->Measure(c.Constrain(ui::Constraints::Fixed(itemWidth, itemHeight))));
                }
                return s.Layout(width, itemHeight, [placeables](ui::PlacementScope& p) {
                    int x = 0;
                    for (auto* item : placeables) {
                        p.PlaceRelative(*item, x, 0);
                        x += item->Width();
                    }
                });
            }
        };

        // Horizontal padding of a centred bar: 3 items take 60% of the width, 4 take 70%, 5 take
        // 80%, 6 take 90%; more take it all.
        int CenteredContentHorizontalPadding(int itemsCount, int barWidth) {
            if (itemsCount > 6) return 0;
            const float paddingPercentage = (float(100 - 10 * (itemsCount + 3)) / 2.0f) / 100;
            return ui::RoundJava(paddingPercentage * float(barWidth));
        }

        // Items grouped in the middle; an item wider than its share grows (up to an equal share)
        // and the group stays centred.
        class CenteredPolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> items, const ui::Constraints& c) override {
                const int width = c.maxWidth;
                int itemHeight = c.minHeight;
                const int count = int(items.size());
                if (count < 1) return s.Layout(width, itemHeight);
                int barHorizontalPadding = 0;
                std::vector<ui::Placeable*> placeables;
                if (!c.HasBoundedWidth()) {
                    for (auto* m : items) placeables.push_back(&m->Measure(c.Constrain(ui::Constraints::FixedHeight(itemHeight))));
                } else {
                    const int itemMaxWidth = width / count;
                    barHorizontalPadding = CenteredContentHorizontalPadding(count, width);
                    const int itemMinWidth = (width - barHorizontalPadding * 2) / count;
                    for (auto* m : items) itemHeight = std::max(itemHeight, std::min(m->MaxIntrinsicHeight(itemMinWidth), c.maxHeight));
                    for (auto* m : items) {
                        int currentItemWidth = itemMinWidth;
                        const int measurableWidth = m->MaxIntrinsicWidth(c.minHeight);
                        if (currentItemWidth < measurableWidth) {
                            currentItemWidth = std::min(measurableWidth, itemMaxWidth);
                            barHorizontalPadding -= (currentItemWidth - itemMinWidth) / 2;
                        }
                        placeables.push_back(&m->Measure(c.Constrain(ui::Constraints::Fixed(currentItemWidth, itemHeight))));
                    }
                }
                return s.Layout(width, itemHeight, [placeables, barHorizontalPadding](ui::PlacementScope& p) {
                    int x = barHorizontalPadding;
                    for (auto* item : placeables) {
                        p.PlaceRelative(*item, x, 0);
                        x += item->Width();
                    }
                });
            }
        };
    }

    namespace ShortNavigationBarDefaults {
        Color containerColor() { return Value(NavigationBarTokens::ContainerColor); }
        Color contentColor() { return ContentColorFor(containerColor()); }
        WindowInsets windowInsets() { return ScaffoldDefaults::contentWindowInsets().Only(WindowInsetsSides::Horizontal() + WindowInsetsSides::Bottom()); }
    }

    namespace ShortNavigationBarItemDefaults {
        NavigationItemColors colors() {
            const Color inactiveIcon = Value(NavigationBarTokens::ItemInactiveIconColor), inactiveLabel = Value(NavigationBarTokens::ItemInactiveLabelTextColor);
            // The start-position label uses the active icon colour (no token of its own yet).
            return { Value(NavigationBarTokens::ItemActiveIconColor),      Value(NavigationBarTokens::ItemActiveLabelTextColor),
                     Value(NavigationBarTokens::ItemActiveIconColor),      Value(NavigationBarTokens::ItemActiveIndicatorColor),
                     inactiveIcon,                                         inactiveLabel,
                     inactiveIcon.WithAlpha(DisabledAlpha),                inactiveLabel.WithAlpha(DisabledAlpha) };
        }
        NavigationItemColors colors(Color selectedIconColor, Color selectedTextColorTopIconPosition, Color selectedTextColorStartIconPosition,
                                    Color selectedIndicatorColor, Color unselectedIconColor, Color unselectedTextColor, Color disabledIconColor,
                                    Color disabledTextColor) {
            const NavigationItemColors base = colors();
            const Color unselectedIcon = Or(unselectedIconColor, base.unselectedIconColor), unselectedText = Or(unselectedTextColor, base.unselectedTextColor);
            return base.Copy(selectedIconColor, selectedTextColorTopIconPosition, selectedTextColorStartIconPosition, selectedIndicatorColor, unselectedIcon,
                             unselectedText, Or(disabledIconColor, unselectedIcon.WithAlpha(DisabledAlpha)),
                             Or(disabledTextColor, unselectedText.WithAlpha(DisabledAlpha)));
        }
    }

    void ShortNavigationBar(const ui::Modifier& modifier, const ShortNavigationBarOptions& o, kiln::Function<void()> content) {
        SurfaceOptions surface;
        surface.color = o.containerColor.value_or(ShortNavigationBarDefaults::containerColor());
        surface.contentColor = o.contentColor.value_or(ShortNavigationBarDefaults::contentColor());
        const WindowInsets insets = o.windowInsets.value_or(ShortNavigationBarDefaults::windowInsets());
        const auto arrangement = o.arrangement;
        Surface(modifier, surface, [insets, arrangement, content = std::move(content)] {
            std::shared_ptr<ui::MeasurePolicy> policy;
            if (arrangement == ShortNavigationBarArrangement::Centered) policy = std::make_shared<CenteredPolicy>();
            else policy = std::make_shared<EqualWeightPolicy>();
            foundation::Layout(SelectableGroup(DefaultMinSize(WindowInsetsPadding({}, insets), Dp::Unspecified(), NavigationBarTokens::ContainerHeight)), policy,
                               content);
        });
    }

    void ShortNavigationBarItem(bool selected, kiln::Function<void()> onClick, kiln::Function<void()> icon, kiln::Function<void()> label,
                                const ui::Modifier& modifier, const ShortNavigationBarItemOptions& o) {
        const NavigationItemColors colors = o.colors.value_or(ShortNavigationBarItemDefaults::colors());
        const bool top = o.iconPosition == NavigationItemIconPosition::Top;
        internal::NavigationItemParams p;
        p.selected = selected;
        p.onClick = std::move(onClick);
        p.icon = std::move(icon);
        p.labelTextStyle = Value(NavigationBarTokens::LabelTextFont);
        p.indicatorShape = Value(NavigationBarTokens::ItemActiveIndicatorShape);
        p.indicatorWidth = NavigationBarVerticalItemTokens::ActiveIndicatorWidth;
        const Dp horizontal = top ? internal::TopIconIndicatorHorizontalPadding : internal::StartIconIndicatorHorizontalPadding;
        const Dp vertical = top ? internal::TopIconIndicatorVerticalPadding : internal::StartIconIndicatorVerticalPadding;
        p.indicatorPadding = { horizontal, vertical, horizontal, vertical };
        p.indicatorToLabelVerticalPadding = internal::TopIconIndicatorToLabelPadding;
        p.startIconToLabelHorizontalPadding = internal::StartIconToLabelPadding;
        p.topIconItemPadding = label ? PaddingValues{ Dp(0), internal::TopIconItemVerticalPadding, Dp(0), internal::TopIconItemVerticalPadding } : PaddingValues{};
        p.startIconItemPadding = {};
        p.textColor = colors.TextColor(selected, o.enabled, top);
        p.iconColor = colors.IconColor(selected, o.enabled);
        p.indicatorColor = colors.selectedIndicatorColor;
        p.modifier = modifier;
        p.enabled = o.enabled;
        p.label = std::move(label);
        p.iconPosition = o.iconPosition;
        p.interactionSource = o.interactionSource;
        internal::NavigationItem(p);
    }

}
