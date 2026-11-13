#include "kiln/material3/TabRow.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/material3/Divider.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Tab.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/PrimaryNavigationTabTokens.h"
#include "kiln/material3/tokens/SecondaryNavigationTabTokens.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    using internal::HorizontalTextPadding;

    namespace {
        Dp ToDp(const ui::Density& d, int px) { return Dp(float(px) / d.density); }
        constexpr Dp MinIndicatorWidth{ 24 };   // the minimum touch target the indicator keeps

        using Positions = rt::State<std::vector<TabPosition>>;

        // tabIndicatorLayout: the indicator's own measure, with the tab positions.
        struct IndicatorLayoutParams {
            Positions* positions = nullptr;
            TabIndicatorScope::IndicatorMeasure measure;
            bool operator==(const IndicatorLayoutParams&) const { return false; }   // lambdas never compare equal
        };
        struct IndicatorLayoutNode final : ui::ModifierNode, ui::LayoutModifier {
            IndicatorLayoutParams params;
            explicit IndicatorLayoutNode(IndicatorLayoutParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                return params.measure(s, m, c, params.positions->Get());
            }
        };

        rt::Task<> AnimateDp(std::shared_ptr<anim::Animatable> a, float target, anim::SpecPtr spec) { co_await a->AnimateTo(target, std::move(spec)); }

        // TabIndicatorOffsetNode: the selected tab's offset and width (or content width), each
        // animated from the first value it saw.
        struct IndicatorOffsetParams {
            Positions* positions = nullptr;
            int selectedTabIndex = 0;
            bool followContentSize = false;
            anim::SpecPtr spec;
            bool operator==(const IndicatorOffsetParams&) const = default;
        };
        struct IndicatorOffsetNode final : ui::ModifierNode, ui::LayoutModifier {
            IndicatorOffsetParams params;
            std::shared_ptr<anim::Animatable> offsetAnim, widthAnim;
            std::optional<float> initialOffset, initialWidth;   // dp
            rt::Job offsetJob, widthJob;
            explicit IndicatorOffsetNode(IndicatorOffsetParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            void OnDetach() override {
                // The node's coroutine scope ends with it.
                offsetJob.Cancel();
                widthJob.Cancel();
            }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                const auto& positions = params.positions->Get();
                if (positions.empty() || params.selectedTabIndex < 0 || std::size_t(params.selectedTabIndex) >= positions.size()) return s.Layout(0, 0);
                const TabPosition& tab = positions[std::size_t(params.selectedTabIndex)];
                const float currentTabWidth = (params.followContentSize ? tab.contentWidth : tab.width).value;
                if (initialWidth) {
                    if (!widthAnim) widthAnim = std::make_shared<anim::Animatable>(*initialWidth);
                    if (currentTabWidth != widthAnim->Target()[0]) widthJob = rt::Launch(rt::Dispatcher::Main(), AnimateDp(widthAnim, currentTabWidth, params.spec));
                } else {
                    initialWidth = currentTabWidth;
                }
                const float indicatorOffset = tab.left.value;
                if (initialOffset) {
                    if (!offsetAnim) offsetAnim = std::make_shared<anim::Animatable>(*initialOffset);
                    if (indicatorOffset != offsetAnim->Target()[0]) offsetJob = rt::Launch(rt::Dispatcher::Main(), AnimateDp(offsetAnim, indicatorOffset, params.spec));
                } else {
                    initialOffset = indicatorOffset;
                }
                const float off = offsetAnim ? offsetAnim->Float() : indicatorOffset;
                const float offset = s.Direction() == ui::LayoutDirection::Ltr ? off : -off;
                const float width = widthAnim ? widthAnim->Float() : currentTabWidth;
                const int w = s.RoundToPx(Dp(width));
                ui::Placeable& p = m.Measure(ui::Constraints{ w, w, c.minHeight, c.maxHeight });
                const int x = s.RoundToPx(Dp(offset));
                return s.Layout(p.Width(), p.Height(), [&p, x](ui::PlacementScope& place) { place.Place(p, x, 0); });
            }
        };

        class ScopeImpl final : public TabIndicatorScope {
        public:
            Positions positions{ std::vector<TabPosition>{} };
            anim::SpecPtr spec;
            ui::Modifier TabIndicatorLayout(ui::Modifier m, IndicatorMeasure measure) override {
                return m.With<IndicatorLayoutNode>(IndicatorLayoutParams{ &positions, std::move(measure) });
            }
            ui::Modifier TabIndicatorOffset(ui::Modifier m, int selectedTabIndex, bool matchContentSize) override {
                return m.With<IndicatorOffsetNode>(IndicatorOffsetParams{ &positions, selectedTabIndex, matchContentSize, spec });
            }
            void SetTabPositions(std::vector<TabPosition> p) { if (!(positions.Peek() == p)) positions.Set(std::move(p)); }
        };

        std::shared_ptr<ScopeImpl> RememberScope() {
            auto& c = *rt::Composition::Current();
            auto scope = c.Remember<std::shared_ptr<ScopeImpl>>([] { return std::make_shared<ScopeImpl>(); });
            scope->spec = Value(MotionSchemeKeyTokens::DefaultSpatial);
            return scope;
        }

        // TabRowImpl: tabs sharing the width, the divider at the bottom, the indicator over it.
        class FixedPolicy final : public MultiContentMeasurePolicy {
        public:
            explicit FixedPolicy(std::shared_ptr<ScopeImpl> scope) : m_Scope(std::move(scope)) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, const std::vector<std::vector<ui::Measurable*>>& contents, const ui::Constraints& c) override {
                const auto& tabs = contents[0];
                const int rowWidth = c.maxWidth;
                const int count = int(tabs.size());
                const int tabWidth = count > 0 ? rowWidth / count : 0;
                int rowHeight = 0;
                for (auto* t : tabs) rowHeight = std::max(rowHeight, t->MaxIntrinsicHeight(tabWidth));
                std::vector<TabPosition> positions;
                for (int i = 0; i < count; ++i) {
                    const Dp contentWidth = Dp(ToDp(s, std::min(tabs[std::size_t(i)]->MaxIntrinsicWidth(rowHeight), tabWidth)).value - HorizontalTextPadding.value * 2);
                    positions.push_back({ Dp(ToDp(s, tabWidth).value * float(i)), ToDp(s, tabWidth), Dp(std::max(contentWidth.value, MinIndicatorWidth.value)) });
                }
                m_Scope->SetTabPositions(std::move(positions));
                std::vector<ui::Placeable*> tabPlaceables, dividers, indicators;
                for (auto* t : tabs) tabPlaceables.push_back(&t->Measure(ui::Constraints::Fixed(tabWidth, rowHeight)));
                for (auto* d : contents[1]) dividers.push_back(&d->Measure(ui::Constraints{ c.minWidth, c.maxWidth, 0, c.maxHeight }));
                for (auto* d : contents[2]) indicators.push_back(&d->Measure(ui::Constraints{ tabWidth, tabWidth, 0, rowHeight }));
                return s.Layout(rowWidth, rowHeight, [=](ui::PlacementScope& p) {
                    for (std::size_t i = 0; i < tabPlaceables.size(); ++i) p.PlaceRelative(*tabPlaceables[i], int(i) * tabWidth, 0);
                    for (auto* d : dividers) p.PlaceRelative(*d, 0, rowHeight - d->Height());
                    for (auto* d : indicators) p.PlaceRelative(*d, 0, rowHeight - d->Height());
                });
            }

        private:
            std::shared_ptr<ScopeImpl> m_Scope;
        };

        void TabRowImpl(const ui::Modifier& modifier, Color containerColor, Color contentColor, kiln::Function<void(TabIndicatorScope&)> indicator,
                        kiln::Function<void()> divider, kiln::Function<void()> tabs) {
            SurfaceOptions surface;
            surface.color = containerColor;
            surface.contentColor = contentColor;
            Surface(SelectableGroup(modifier), surface, [=] {
                const auto scope = RememberScope();
                foundation::Layout(FillMax({}, Axis::Horizontal), { tabs, divider, [scope, indicator] { indicator(*scope); } }, std::make_shared<FixedPolicy>(scope));
            });
        }

        rt::Task<> ScrollTo(ScrollState* state, int value, anim::SpecPtr spec) { co_await state->AnimateScrollTo(value, std::move(spec)); }

        // ScrollableTabData: scrolls the selected tab towards the middle when it changes.
        struct ScrollableTabData {
            ScrollState* scrollState = nullptr;
            std::shared_ptr<internal::CoroutineScope> scope;
            anim::SpecPtr spec;
            std::optional<int> selectedTab;

            void OnLaidOut(const ui::Density& d, int edgeOffset, const std::vector<TabPosition>& positions, int selected) {
                if (selectedTab == selected) return;
                selectedTab = selected;
                if (selected < 0 || std::size_t(selected) >= positions.size()) return;
                const int offset = TabOffset(d, edgeOffset, positions, positions[std::size_t(selected)]);
                if (scrollState->Value() != offset) scope->Launch(ScrollTo(scrollState, offset, spec));
            }
            // The scroll that centres the tab, clamped to what can scroll.
            int TabOffset(const ui::Density& d, int edgeOffset, const std::vector<TabPosition>& positions, const TabPosition& tab) const {
                const int totalTabRowWidth = d.RoundToPx(positions.back().Right()) + edgeOffset;
                const int visibleWidth = totalTabRowWidth - scrollState->MaxValue();
                const int tabOffset = d.RoundToPx(tab.left);
                const int scrollerCenter = visibleWidth / 2;
                const int tabWidth = d.RoundToPx(tab.width);
                const int centeredTabOffset = tabOffset - (scrollerCenter - tabWidth / 2);
                const int availableSpace = std::max(totalTabRowWidth - visibleWidth, 0);
                return std::clamp(centeredTabOffset, 0, availableSpace);
            }
        };

        class ScrollablePolicy final : public MultiContentMeasurePolicy {
        public:
            ScrollablePolicy(std::shared_ptr<ScopeImpl> scope, std::shared_ptr<ScrollableTabData> data, int selected, Dp edgePadding, Dp minTabWidth)
                : m_Scope(std::move(scope)), m_Data(std::move(data)), m_Selected(selected), m_EdgePadding(edgePadding), m_MinTabWidth(minTabWidth) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, const std::vector<std::vector<ui::Measurable*>>& contents, const ui::Constraints& c) override {
                const auto& tabs = contents[0];
                const int padding = s.RoundToPx(m_EdgePadding);
                int layoutHeight = 0;
                for (auto* t : tabs) layoutHeight = std::max(layoutHeight, t->MaxIntrinsicHeight(ui::Constraints::Infinity));
                int layoutWidth = padding * 2;
                const ui::Constraints tabConstraints{ s.RoundToPx(m_MinTabWidth), c.maxWidth, layoutHeight, layoutHeight };
                std::vector<ui::Placeable*> tabPlaceables;
                for (auto* t : tabs) tabPlaceables.push_back(&t->Measure(tabConstraints));
                // The indicator widths come from the content, not the minimum width above.
                std::vector<int> indicatorWidth;
                for (auto* t : tabs) indicatorWidth.push_back(t->MaxIntrinsicWidth(ui::Constraints::Infinity));
                std::vector<TabPosition> positions;
                Dp left = m_EdgePadding;
                for (std::size_t i = 0; i < tabs.size(); ++i) {
                    const Dp tabWidth = Dp(std::max(m_MinTabWidth.value, ToDp(s, tabPlaceables[i]->Width()).value));
                    layoutWidth += s.RoundToPx(tabWidth);
                    const Dp contentWidth = Dp(std::max(ToDp(s, indicatorWidth[i]).value - HorizontalTextPadding.value * 2, MinIndicatorWidth.value));
                    positions.push_back({ left, tabWidth, contentWidth });
                    left = Dp(left.value + tabWidth.value);
                }
                m_Scope->SetTabPositions(positions);
                const bool valid = m_Selected >= 0 && std::size_t(m_Selected) < positions.size();
                std::vector<ui::Placeable*> indicators;
                const int indicatorMax = valid ? s.RoundToPx(positions[std::size_t(m_Selected)].contentWidth) : 0;
                for (auto* d : contents[1]) indicators.push_back(&d->Measure(ui::Constraints{ 0, indicatorMax, 0, layoutHeight }));
                const ui::Density density = s;
                const int selectedWidth = valid ? s.RoundToPx(positions[std::size_t(m_Selected)].width) : 0;
                return s.Layout(layoutWidth, layoutHeight, [=, data = m_Data, edge = m_EdgePadding, selected = m_Selected](ui::PlacementScope& p) {
                    Dp x = edge;
                    for (std::size_t i = 0; i < tabPlaceables.size(); ++i) {
                        p.PlaceRelative(*tabPlaceables[i], density.RoundToPx(x), 0);
                        x = Dp(x.value + positions[i].width.value);
                    }
                    for (auto* d : indicators) p.PlaceRelative(*d, std::max(0, (selectedWidth - d->Width()) / 2), layoutHeight - d->Height());
                    data->OnLaidOut(density, padding, positions, selected);
                });
            }

        private:
            std::shared_ptr<ScopeImpl> m_Scope;
            std::shared_ptr<ScrollableTabData> m_Data;
            int m_Selected;
            Dp m_EdgePadding, m_MinTabWidth;
        };

        void ScrollableTabRowImpl(int selectedTabIndex, const ui::Modifier& modifier, Color containerColor, Color contentColor, Dp edgePadding,
                                  Dp minTabWidth, ScrollState* scrollState, kiln::Function<void(TabIndicatorScope&)> indicator,
                                  kiln::Function<void()> divider, kiln::Function<void()> tabs) {
            SurfaceOptions surface;
            surface.color = containerColor;
            surface.contentColor = contentColor;
            Surface(modifier, surface, [=] {
                auto& c = *rt::Composition::Current();
                ScrollState* state = scrollState ? scrollState : &RememberScrollState();
                const auto coroutineScope = internal::RememberCoroutineScope();
                auto& data = c.Remember<std::shared_ptr<ScrollableTabData>>([] { return std::make_shared<ScrollableTabData>(); }, rt::Keys(state));
                data->scrollState = state;
                data->scope = coroutineScope;
                data->spec = Value(MotionSchemeKeyTokens::DefaultSpatial);
                const auto scope = RememberScope();
                const auto tabData = data;
                Box({}, Alignment::BottomStart(), false, [=] {
                    divider();
                    ui::Modifier m = WrapContent(FillMax({}, Axis::Horizontal), Axis::Both, Alignment::CenterStart());
                    m = ClipToBounds(SelectableGroup(HorizontalScroll(std::move(m), *state)));
                    foundation::Layout(m, { tabs, [scope, indicator] { indicator(*scope); } },
                                       std::make_shared<ScrollablePolicy>(scope, tabData, selectedTabIndex, edgePadding, minTabWidth));
                });
            });
        }

        kiln::Function<void()> DividerOr(const kiln::Function<void()>& divider) {
            return divider ? divider : [] { HorizontalDivider(); };
        }
        kiln::Function<void(TabIndicatorScope&)> PrimaryIndicatorFor(int selected) {
            return [selected](TabIndicatorScope& s) { TabRowDefaults::PrimaryIndicator(s.TabIndicatorOffset({}, selected, true), Dp::Unspecified()); };
        }
        kiln::Function<void(TabIndicatorScope&)> SecondaryIndicatorFor(int selected) {
            return [selected](TabIndicatorScope& s) { TabRowDefaults::SecondaryIndicator(s.TabIndicatorOffset({}, selected, false)); };
        }
    }

    namespace TabRowDefaults {
        Color primaryContainerColor() { return Value(PrimaryNavigationTabTokens::ContainerColor); }
        Color secondaryContainerColor() { return Value(SecondaryNavigationTabTokens::ContainerColor); }
        Color primaryContentColor() { return Value(PrimaryNavigationTabTokens::ActiveLabelTextColor); }
        Color secondaryContentColor() { return Value(SecondaryNavigationTabTokens::ActiveLabelTextColor); }

        void PrimaryIndicator(const ui::Modifier& modifier, Dp width, Dp height, std::optional<Color> color, ShapePtr shape) {
            const Color c = color.value_or(Value(PrimaryNavigationTabTokens::ActiveIndicatorColor));
            const ShapePtr s = shape ? shape : PrimaryNavigationTabTokens::ActiveIndicatorShape.ToShape();
            Spacer(Background(SizeIn(modifier, width, height, width, height, false), c, s));
        }
        void SecondaryIndicator(const ui::Modifier& modifier, Dp height, std::optional<Color> color) {
            const Color c = color.value_or(Value(PrimaryNavigationTabTokens::ActiveIndicatorColor));
            Box(Background(Height(FillMax(modifier, Axis::Horizontal), height), c), Alignment::TopStart(), false, {});
        }
    }

    void PrimaryTabRow(int selectedTabIndex, const ui::Modifier& modifier, const TabRowOptions& o, kiln::Function<void()> tabs) {
        const Color container = o.containerColor.value_or(TabRowDefaults::primaryContainerColor());
        TabRowImpl(modifier, container, o.contentColor.value_or(TabRowDefaults::primaryContentColor()),
                   o.indicator ? o.indicator : PrimaryIndicatorFor(selectedTabIndex), DividerOr(o.divider), std::move(tabs));
    }
    void SecondaryTabRow(int selectedTabIndex, const ui::Modifier& modifier, const TabRowOptions& o, kiln::Function<void()> tabs) {
        const Color container = o.containerColor.value_or(TabRowDefaults::secondaryContainerColor());
        TabRowImpl(modifier, container, o.contentColor.value_or(TabRowDefaults::secondaryContentColor()),
                   o.indicator ? o.indicator : SecondaryIndicatorFor(selectedTabIndex), DividerOr(o.divider), std::move(tabs));
    }
    void PrimaryScrollableTabRow(int selectedTabIndex, const ui::Modifier& modifier, const ScrollableTabRowOptions& o, kiln::Function<void()> tabs) {
        ScrollableTabRowImpl(selectedTabIndex, modifier, o.containerColor.value_or(TabRowDefaults::primaryContainerColor()),
                             o.contentColor.value_or(TabRowDefaults::primaryContentColor()), o.edgePadding, o.minTabWidth, o.scrollState,
                             o.indicator ? o.indicator : PrimaryIndicatorFor(selectedTabIndex), DividerOr(o.divider), std::move(tabs));
    }
    void SecondaryScrollableTabRow(int selectedTabIndex, const ui::Modifier& modifier, const ScrollableTabRowOptions& o, kiln::Function<void()> tabs) {
        ScrollableTabRowImpl(selectedTabIndex, modifier, o.containerColor.value_or(TabRowDefaults::secondaryContainerColor()),
                             o.contentColor.value_or(TabRowDefaults::secondaryContentColor()), o.edgePadding, o.minTabWidth, o.scrollState,
                             o.indicator ? o.indicator : SecondaryIndicatorFor(selectedTabIndex), DividerOr(o.divider), std::move(tabs));
    }

}
