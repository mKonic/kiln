#include "kiln/material3/AppBarRow.h"
#include "kiln/foundation/Compose.h"
#include "kiln/material3/Menu.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;

    namespace {
        // An icon button with a tooltip naming the item.
        void WithTooltip(const std::string& label, std::function<void()> button) {
            TooltipBox(TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above),
                       [label](const TooltipScope& scope) { internal::PlainTooltipInternal(scope, label, {}, {}, [label] { Text(label); }); },
                       RememberTooltipState(), {}, {}, std::move(button));
        }

        // A plain menu item that runs `onClick` and closes the menu.
        void MenuItem(const std::string& label, bool enabled, std::function<void()> onClick, AppBarMenuState& state) {
            DropdownMenuItemOptions o;
            o.enabled = enabled;
            auto* menu = &state;
            DropdownMenuItem([label] { Text(label); }, [onClick, menu] {
                onClick();
                menu->Dismiss();
            }, {}, o);
        }

        // AppBarOverflowState: how many items the last measure had and showed (the rest go to
        // the menu).
        struct OverflowState {
            rt::State<int> totalItemCount{ 0 }, visibleItemCount{ 0 };
        };
        std::shared_ptr<OverflowState> RememberOverflowState() {
            rt::Saver<std::shared_ptr<OverflowState>> saver{
                [](const std::shared_ptr<OverflowState>& s) {
                    return rt::SavedValue(rt::SavedValue::List{ rt::SavedValue(s->totalItemCount.Peek()), rt::SavedValue(s->visibleItemCount.Peek()) });
                },
                [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<OverflowState>> {
                    const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                    if (!list || list->size() != 2) return std::nullopt;
                    const auto* total = std::get_if<std::int64_t>(&(*list)[0].v);
                    const auto* visible = std::get_if<std::int64_t>(&(*list)[1].v);
                    if (!total || !visible) return std::nullopt;
                    auto s = std::make_shared<OverflowState>();
                    s->totalItemCount.Set(int(*total));
                    s->visibleItemCount.Set(int(*visible));
                    return s;
                }
            };
            return rt::RememberSaveable<std::shared_ptr<OverflowState>>([] { return std::make_shared<OverflowState>(); }, saver);
        }

        int SubtractConstraintSafely(int value, int n) { return value == Constraints::Infinity ? value : value - n; }

        // OverflowMeasurePolicy: items along the axis while they fit (the last may use the
        // indicator's space), then the indicator.
        class OverflowMeasurePolicy final : public MultiContentMeasurePolicy {
        public:
            OverflowMeasurePolicy(std::shared_ptr<OverflowState> state, int maxItemCount, bool vertical)
                : m_State(std::move(state)), m_MaxItemCount(maxItemCount), m_Vertical(vertical) {}
            int MaxItemCount() const { return m_MaxItemCount; }

            MeasureResult Measure(MeasureScope& s, const std::vector<std::vector<Measurable*>>& contents, const Constraints& constraints) override {
                Constraints loose = constraints;
                loose.minWidth = loose.minHeight = 0;
                const auto& content = contents[0];
                const auto& overflow = contents[1];
                m_State->totalItemCount.Set(int(content.size()));

                // First, reserve space for the overflow indicator
                int reservedOverflowSpace = 0;
                for (Measurable* m : overflow)
                    reservedOverflowSpace = std::max(reservedOverflowSpace, m_Vertical ? m->MaxIntrinsicHeight(constraints.maxWidth)
                                                                                       : m->MaxIntrinsicWidth(constraints.maxHeight));
                int remainingSpace = SubtractConstraintSafely(m_Vertical ? constraints.maxHeight : constraints.maxWidth, reservedOverflowSpace);

                int currentSpace = 0;
                std::vector<Placeable*> placeables;
                // Measure content until it doesn't fit
                for (std::size_t i = 0; i < content.size(); ++i) {
                    Placeable& p = content[i]->Measure(loose);
                    const bool isLastContent = i + 1 == content.size();
                    if (!isLastContent && int(i) == m_MaxItemCount - 1) break;
                    const int space = m_Vertical ? p.Height() : p.Width();
                    const bool fits = space <= remainingSpace || (isLastContent && space <= remainingSpace + reservedOverflowSpace);
                    if (!fits) break;
                    placeables.push_back(&p);
                    currentSpace += space;
                    remainingSpace = SubtractConstraintSafely(remainingSpace, space);
                }
                m_State->visibleItemCount.Set(int(placeables.size()));

                // Measure overflow if needed
                std::vector<Placeable*> overflowPlaceables;
                if (placeables.size() != content.size()) {
                    Constraints c = loose;
                    (m_Vertical ? c.maxHeight : c.maxWidth) = remainingSpace + reservedOverflowSpace;
                    for (Measurable* m : overflow) overflowPlaceables.push_back(&m->Measure(c));
                }
                int overflowSpace = 0, cross = 0;
                for (Placeable* p : overflowPlaceables) {
                    overflowSpace = std::max(overflowSpace, m_Vertical ? p->Height() : p->Width());
                    cross = std::max(cross, m_Vertical ? p->Width() : p->Height());
                }
                for (Placeable* p : placeables) cross = std::max(cross, m_Vertical ? p->Width() : p->Height());
                currentSpace += overflowSpace;

                const bool vertical = m_Vertical;
                const int width = constraints.ConstrainWidth(vertical ? cross : currentSpace);
                const int height = constraints.ConstrainHeight(vertical ? currentSpace : cross);
                return s.Layout(width, height, [placeables, overflowPlaceables, vertical](PlacementScope& p) {
                    int at = 0;
                    for (Placeable* item : placeables) {
                        p.PlaceRelative(*item, vertical ? 0 : at, vertical ? at : 0);
                        at += vertical ? item->Height() : item->Width();
                    }
                    for (Placeable* item : overflowPlaceables) p.PlaceRelative(*item, vertical ? 0 : at, vertical ? at : 0);
                });
            }

        private:
            std::shared_ptr<OverflowState> m_State;
            int m_MaxItemCount;
            bool m_Vertical;
        };

        void AppBarLayout(const Modifier& modifier, std::function<void(AppBarMenuState&)> overflowIndicator, int maxItemCount,
                          const std::function<void(AppBarScope&)>& content, bool vertical) {
            auto& c = *rt::Composition::Current();
            auto scope = std::make_shared<AppBarScope>();
            if (content) content(*scope);
            auto menu = c.Remember<std::shared_ptr<AppBarMenuState>>([] { return std::make_shared<AppBarMenuState>(); });
            auto overflow = RememberOverflowState();
            // remember(overflowState, maxItemCount)
            auto& policy = c.Remember<std::shared_ptr<OverflowMeasurePolicy>>([] { return std::shared_ptr<OverflowMeasurePolicy>(); });
            if (!policy || policy->MaxItemCount() != maxItemCount) policy = std::make_shared<OverflowMeasurePolicy>(overflow, maxItemCount, vertical);
            if (!overflowIndicator) overflowIndicator = [](AppBarMenuState& state) { AppBarOverflowIndicator(state); };
            Layout(modifier,
                   { [scope] { for (const auto& item : scope->Items()) item.appbarContent(); },
                     [scope, menu, overflow, overflowIndicator] {
                         Box({}, Alignment::TopStart(), false, [=] {
                             overflowIndicator(*menu);
                             DropdownMenu(menu->IsShowing(), [menu] { menu->Dismiss(); }, {}, {}, [=] {
                                 const int total = std::min<int>(overflow->totalItemCount.Get(), int(scope->Items().size()));
                                 for (int i = overflow->visibleItemCount.Get(); i < total; ++i) scope->Items()[std::size_t(i)].menuContent(*menu);
                             });
                         });
                     } },
                   policy);
        }
    }

    void AppBarScope::ClickableItem(std::function<void()> onClick, std::function<void()> icon, std::string label, bool enabled) {
        m_Items.push_back({ [=] {
                               // Icon buttons should have a tooltip associated with them.
                               WithTooltip(label, [=] {
                                   IconButtonOptions o;
                                   o.enabled = enabled;
                                   IconButton(onClick, {}, o, icon);
                               });
                           },
                            [=](AppBarMenuState& state) { MenuItem(label, enabled, onClick, state); } });
    }

    void AppBarScope::ToggleableItem(bool checked, std::function<void(bool)> onCheckedChange, std::function<void()> icon, std::string label,
                                     bool enabled) {
        m_Items.push_back({ [=] {
                               WithTooltip(label, [=] {
                                   IconToggleButtonOptions o;
                                   o.enabled = enabled;
                                   IconToggleButton(checked, onCheckedChange, {}, o, icon);
                               });
                           },
                            [=](AppBarMenuState& state) { MenuItem(label, enabled, [=] { onCheckedChange(!checked); }, state); } });
    }

    void AppBarScope::CustomItem(std::function<void()> appbarContent, std::function<void(AppBarMenuState&)> menuContent) {
        m_Items.push_back({ std::move(appbarContent), std::move(menuContent) });
    }

    void AppBarRow(const Modifier& modifier, std::function<void(AppBarMenuState&)> overflowIndicator, int maxItemCount,
                   std::function<void(AppBarScope&)> content) {
        AppBarLayout(modifier, std::move(overflowIndicator), maxItemCount, content, false);
    }

    void AppBarColumn(const Modifier& modifier, std::function<void(AppBarMenuState&)> overflowIndicator, int maxItemCount,
                      std::function<void(AppBarScope&)> content) {
        AppBarLayout(modifier, std::move(overflowIndicator), maxItemCount, content, true);
    }

    void AppBarOverflowIndicator(AppBarMenuState& menuState, const Modifier& modifier, const AppBarOverflowIndicatorOptions& options) {
        const std::string contentDescription = GetString(Strings::FloatingToolbarMoreOptions);
        IconButtonOptions o;
        o.enabled = options.enabled;
        o.shape = options.shape ? options.shape : IconButtonDefaults::standardShape();
        o.colors = options.colors ? *options.colors : IconButtonDefaults::iconButtonColors();
        o.interactionSource = options.interactionSource;
        auto* state = &menuState;
        WithTooltip(contentDescription, [=] {
            IconButton([state] { if (state->IsShowing()) state->Dismiss(); else state->Show(); }, modifier, o,
                       [contentDescription] { Icon(internal::Icons::Filled::MoreVert(), contentDescription); });
        });
    }

}
