#include "kiln/material3/WideNavigationRail.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Scaffold.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/AnimatedProgress.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/NavigationRailBaselineItemTokens.h"
#include "kiln/material3/tokens/NavigationRailCollapsedTokens.h"
#include "kiln/material3/tokens/NavigationRailColorTokens.h"
#include "kiln/material3/tokens/NavigationRailExpandedTokens.h"
#include "kiln/material3/tokens/NavigationRailHorizontalItemTokens.h"
#include "kiln/material3/tokens/NavigationRailVerticalItemTokens.h"
#include "kiln/material3/tokens/ScrimTokens.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }

        constexpr float CollapsedValue = 0, ExpandedValue = 1;

        // WideNavigationRailStateImpl: an animatable between 0 (collapsed) and 1 (expanded).
        class StateImpl final : public WideNavigationRailState {
        public:
            StateImpl(WideNavigationRailValue initial, anim::SpecPtr spec)
                : m_Value(std::make_shared<anim::Animatable>(initial == WideNavigationRailValue::Expanded ? ExpandedValue : CollapsedValue)),
                  m_Spec(std::move(spec)) {}
            bool IsAnimating() const override { return m_Value->IsRunning(); }
            WideNavigationRailValue TargetValue() const override { return Of(m_Value->Target()[0]); }
            WideNavigationRailValue CurrentValue() const override { return Of(m_Value->Float()); }
            rt::Task<> Expand() override { co_await m_Value->AnimateTo(ExpandedValue, m_Spec); }
            rt::Task<> Collapse() override { co_await m_Value->AnimateTo(CollapsedValue, m_Spec); }
            rt::Task<> Toggle() override {
                co_await m_Value->AnimateTo(TargetValue() == WideNavigationRailValue::Expanded ? CollapsedValue : ExpandedValue, m_Spec);
            }
            rt::Task<> SnapTo(WideNavigationRailValue value) override {
                m_Value->SnapTo(value == WideNavigationRailValue::Expanded ? ExpandedValue : CollapsedValue);
                co_return;
            }

        private:
            static WideNavigationRailValue Of(float v) { return v == ExpandedValue ? WideNavigationRailValue::Expanded : WideNavigationRailValue::Collapsed; }
            std::shared_ptr<anim::Animatable> m_Value;
            anim::SpecPtr m_Spec;
        };

        // Widths the rail keeps between measures, so collapsing from a wider than minimum
        // expanded width does not jump.
        struct RailWidths {
            int current = 0, actualMaxExpanded = 0;
        };

        struct RailParams {
            bool hasHeader = false, expanded = false;
            std::shared_ptr<anim::Animatable> minWidth, widthFullRange, itemVerticalSpacedBy, itemMinHeight;   // dp
            Dp minimumA11ySize{ 0 }, expandedRailMinWidth{ 0 };
            Arrangement arrangement;
            PaddingValues contentPadding;
            std::shared_ptr<RailWidths> widths;
        };

        class RailPolicy final : public ui::MeasurePolicy {
        public:
            explicit RailPolicy(RailParams p) : m_P(std::move(p)) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> measurables, const ui::Constraints& c) override {
                const RailParams& p = m_P;
                const int height = c.maxHeight;
                int itemsCount = int(measurables.size());
                int actualExpandedMinWidth = c.minWidth;
                int actualMinWidth;
                if (c.minWidth == 0) {
                    actualExpandedMinWidth = std::min(s.RoundToPx(p.expandedRailMinWidth), c.maxWidth);
                    actualMinWidth = std::min(s.RoundToPx(Dp(p.minWidth->Float())), c.maxWidth);
                } else {
                    actualMinWidth = c.minWidth;
                }
                if (itemsCount < 1) return s.Layout(actualMinWidth, height);   // an empty rail
                const ui::Constraints loose = c.CopyMaxDimensions();
                std::size_t first = 0;
                int constraintsOffset = 0;
                ui::Placeable* header = nullptr;
                if (p.hasHeader) {
                    // The header is always the first child.
                    header = &measurables[0]->Measure(loose);
                    first = 1;
                    --itemsCount;
                    constraintsOffset = header->Height();
                }
                const int minimum = s.RoundToPx(p.minimumA11ySize);
                const int itemMinHeight = s.RoundToPx(Dp(p.itemMinHeight->Float()));
                const int itemMaxWidth = std::max(p.expanded ? loose.maxWidth : actualMinWidth, minimum);
                const int itemMaxHeight = std::max(loose.maxHeight, itemMinHeight);
                int expandedItemMaxWidth = 0;
                std::vector<ui::Placeable*> items;
                for (std::size_t i = first; i < measurables.size(); ++i) {
                    ui::Placeable& item = measurables[i]->Measure(
                        loose.Offset(0, -constraintsOffset).Constrain(ui::Constraints{ minimum, itemMaxWidth, itemMinHeight, itemMaxHeight }));
                    if (p.expanded) expandedItemMaxWidth = std::max(expandedItemMaxWidth, item.Width());
                    constraintsOffset = item.Height();
                    items.push_back(&item);
                }

                int width = actualMinWidth;
                const int widthFullRange = s.RoundToPx(Dp(p.widthFullRange->Float()));
                // A collapsed rail is a fixed width; an expanded one as wide as its widest element.
                if (p.expanded) {
                    const int widest = std::max(expandedItemMaxWidth, header ? header->Width() : 0);
                    if (widest > actualMinWidth && widest > actualExpandedMinWidth) {
                        const int widthConstrain = std::min(std::max(widest, actualExpandedMinWidth), c.maxWidth);
                        // widthFullRange: no jump when the expanded width passes the minimum.
                        width = std::min(widthFullRange, widthConstrain);
                        p.widths->actualMaxExpanded = width;
                    }
                } else if (p.widths->actualMaxExpanded > 0) {
                    width = std::clamp(widthFullRange, actualMinWidth, std::max(p.widths->current, actualMinWidth));
                }
                p.widths->current = width;

                const int spacedBy = s.RoundToPx(Dp(p.itemVerticalSpacedBy->Float()));
                const int topPadding = s.RoundToPx(p.contentPadding.top), bottomPadding = s.RoundToPx(p.contentPadding.bottom);
                const ui::Density density = s;
                return s.Layout(width, height, [=, arrangement = p.arrangement](ui::PlacementScope& place) {
                    int headerOffset = 0;
                    if (header && header->Height() > 0) {
                        place.PlaceRelative(*header, 0, 0);
                        headerOffset += header->Height() + density.RoundToPx(internal::WNRHeaderPadding);
                    }
                    if (items.empty()) return;
                    const bool centered = arrangement == Arrangement::Center();
                    // Centred items centre in the whole container (its padding included), not
                    // in the space below the header.
                    const int layoutSize = centered ? height + topPadding + bottomPadding : height - headerOffset;
                    std::vector<int> sizes(items.size()), y(items.size());
                    for (std::size_t i = 0; i < items.size(); ++i) sizes[i] = items[i]->Height() + (i + 1 < items.size() ? spacedBy : 0);
                    arrangement.Arrange(density, layoutSize, sizes, ui::LayoutDirection::Ltr, y);
                    const int offset = centered ? -topPadding : headerOffset;
                    for (std::size_t i = 0; i < items.size(); ++i) place.PlaceRelative(*items[i], 0, y[i] + offset);
                });
            }

        private:
            RailParams m_P;
        };

        struct LayoutParams {
            bool isModal = false, expanded = false;
            Color containerColor, contentColor;
            ShapePtr shape;
            std::function<void()> header;
            WindowInsets windowInsets;
            Arrangement arrangement;
            PaddingValues contentPadding;
            Dp collapsedRailWidth, expandedRailMinWidth, expandedRailMaxWidth;
        };

        void WideNavigationRailLayout(const ui::Modifier& modifier, const LayoutParams& lp, std::function<void()> content) {
            auto& c = *rt::Composition::Current();
            const auto widths = c.Remember<std::shared_ptr<RailWidths>>([] { return std::make_shared<RailWidths>(); });
            const Dp minimumA11ySize = c.Read(LocalMinimumInteractiveComponentSize);
            const anim::SpecPtr spec = Value(MotionSchemeKeyTokens::DefaultSpatial), modalSpec = Value(MotionSchemeKeyTokens::FastSpatial);
            const anim::SpecPtr widthSpec = lp.isModal ? modalSpec : spec;
            RailParams p;
            p.hasHeader = bool(lp.header);
            p.expanded = lp.expanded;
            p.minWidth = internal::AnimatedFloat((!lp.expanded ? lp.collapsedRailWidth : lp.expandedRailMinWidth).value, widthSpec, rt::Key(1));
            p.widthFullRange = internal::AnimatedFloat((!lp.expanded ? lp.collapsedRailWidth : lp.expandedRailMaxWidth).value, widthSpec, rt::Key(2));
            p.itemVerticalSpacedBy = internal::AnimatedFloat(!lp.expanded ? NavigationRailCollapsedTokens::ItemVerticalSpace.value : 0.0f, spec, rt::Key(3));
            p.itemMinHeight = internal::AnimatedFloat(!lp.expanded ? internal::TopIconItemMinHeight.value : minimumA11ySize.value, spec, rt::Key(4));
            p.minimumA11ySize = minimumA11ySize;
            p.expandedRailMinWidth = lp.expandedRailMinWidth;
            p.arrangement = lp.arrangement;
            p.contentPadding = lp.contentPadding;
            p.widths = widths;
            SurfaceOptions surface;
            surface.color = lp.containerColor;
            surface.contentColor = lp.contentColor;
            surface.shape = lp.shape;
            const auto policy = std::make_shared<RailPolicy>(std::move(p));
            Surface(modifier, surface, [=, header = lp.header, insets = lp.windowInsets, maxWidth = lp.expandedRailMaxWidth, padding = lp.contentPadding] {
                ui::Modifier m = WindowInsetsPadding(FillMax({}, Axis::Vertical), insets);
                m = SizeIn(std::move(m), Dp::Unspecified(), Dp::Unspecified(), maxWidth, Dp::Unspecified());
                m = Padding(std::move(m), padding);
                m = Semantics(SelectableGroup(std::move(m)), [](ui::SemanticsConfiguration& s) { s.isTraversalGroup = true; });
                foundation::Layout(m, policy, [=] {
                    if (header) Box({}, Alignment::TopStart(), false, header);
                    content();
                });
            });
        }
    }

    std::shared_ptr<WideNavigationRailState> RememberWideNavigationRailState(WideNavigationRailValue initialValue) {
        const anim::SpecPtr spec = Value(MotionSchemeKeyTokens::DefaultSpatial);
        rt::Saver<std::shared_ptr<WideNavigationRailState>> saver{
            [](const std::shared_ptr<WideNavigationRailState>& s) { return rt::SavedValue(double(s->TargetValue() == WideNavigationRailValue::Expanded)); },
            [spec](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<WideNavigationRailState>> {
                if (auto n = rt::SavedNumber(saved))
                    return std::make_shared<StateImpl>(*n != 0 ? WideNavigationRailValue::Expanded : WideNavigationRailValue::Collapsed, spec);
                return std::nullopt;
            }
        };
        return rt::RememberSaveable<std::shared_ptr<WideNavigationRailState>>([=] { return std::make_shared<StateImpl>(initialValue, spec); }, saver);
    }

    WideNavigationRailColors WideNavigationRailColors::Copy(Color a, Color b, Color c, Color d, Color e) const {
        return { Or(a, containerColor), Or(b, contentColor), Or(c, modalContainerColor), Or(d, modalScrimColor), Or(e, modalContentColor) };
    }

    namespace WideNavigationRailDefaults {
        ShapePtr shape() { return Value(NavigationRailCollapsedTokens::ContainerShape); }
        WindowInsets windowInsets() { return ScaffoldDefaults::contentWindowInsets().Only(WindowInsetsSides::Vertical() + WindowInsetsSides::Start()); }
        ShapePtr modalCollapsedShape() { return shape(); }
        ShapePtr modalExpandedShape() { return Value(NavigationRailExpandedTokens::ModalContainerShape); }
        WideNavigationRailColors colors() {
            const Color container = Value(NavigationRailCollapsedTokens::ContainerColor), modal = Value(NavigationRailExpandedTokens::ModalContainerColor);
            return { container, ContentColorFor(container), modal, Value(ScrimTokens::ContainerColor).WithAlpha(ScrimTokens::ContainerOpacity),
                     ContentColorFor(modal) };
        }
        WideNavigationRailColors colors(Color containerColor, Color contentColor, Color modalContainerColor, Color modalScrimColor, Color modalContentColor) {
            const WideNavigationRailColors base = colors();
            const Color container = Or(containerColor, base.containerColor), modal = Or(modalContainerColor, base.modalContainerColor);
            return base.Copy(container, Or(contentColor, ContentColorFor(container)), modal, modalScrimColor, Or(modalContentColor, ContentColorFor(modal)));
        }
    }

    namespace WideNavigationRailItemDefaults {
        std::shared_ptr<internal::DynamicPaddingValues> indicatorPadding(bool railExpanded, PaddingValues collapsedPadding, PaddingValues expandedPadding) {
            auto& c = *rt::Composition::Current();
            auto& padding = c.Remember<std::shared_ptr<internal::DynamicPaddingValues>>(
                [=] { return std::make_shared<internal::DynamicPaddingValues>(collapsedPadding, expandedPadding, railExpanded); });
            // remember(collapsedPadding, expandedPadding, railExpanded)
            if (!(padding->collapsed == collapsedPadding) || !(padding->expanded == expandedPadding) || padding->isExpanded != railExpanded) {
                padding = std::make_shared<internal::DynamicPaddingValues>(collapsedPadding, expandedPadding, railExpanded);
            }
            return padding;
        }
        NavigationItemColors colors() {
            const Color inactiveIcon = Value(NavigationRailColorTokens::ItemInactiveIcon), inactiveLabel = Value(NavigationRailColorTokens::ItemInactiveLabelText);
            // The start-position label uses the active icon colour (no token of its own yet).
            return { Value(NavigationRailColorTokens::ItemActiveIcon),     Value(NavigationRailColorTokens::ItemActiveLabelText),
                     Value(NavigationRailColorTokens::ItemActiveIcon),     Value(NavigationRailColorTokens::ItemActiveIndicator),
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

    void WideNavigationRail(const ui::Modifier& modifier, const WideNavigationRailOptions& o, std::function<void()> content) {
        const auto state = o.state ? o.state : RememberWideNavigationRailState();
        const WideNavigationRailColors colors = o.colors.value_or(WideNavigationRailDefaults::colors());
        LayoutParams lp;
        lp.isModal = false;
        lp.expanded = state->TargetValue() == WideNavigationRailValue::Expanded;
        lp.containerColor = colors.containerColor;
        lp.contentColor = colors.contentColor;
        lp.shape = o.shape ? o.shape : WideNavigationRailDefaults::shape();
        lp.header = o.header;
        lp.windowInsets = o.windowInsets.value_or(WideNavigationRailDefaults::windowInsets());
        lp.arrangement = o.arrangement;
        lp.contentPadding = o.contentPadding;
        lp.collapsedRailWidth = internal::CollapsedRailWidth;
        lp.expandedRailMinWidth = internal::ExpandedRailMinWidth;
        lp.expandedRailMaxWidth = internal::ExpandedRailMaxWidth;
        WideNavigationRailLayout(modifier, lp, std::move(content));
    }

    void WideNavigationRailItem(bool selected, std::function<void()> onClick, std::function<void()> icon, std::function<void()> label, bool railExpanded,
                                const ui::Modifier& modifier, const WideNavigationRailItemOptions& o) {
        const NavigationItemIconPosition position = o.iconPosition.value_or(WideNavigationRailItemDefaults::iconPositionFor(railExpanded));
        const NavigationItemColors colors = o.colors.value_or(WideNavigationRailItemDefaults::colors());
        const bool top = position == NavigationItemIconPosition::Top;
        internal::AnimatedNavigationItemParams p;
        p.selected = selected;
        p.onClick = std::move(onClick);
        p.icon = std::move(icon);
        p.indicatorShape = Value(NavigationRailBaselineItemTokens::ActiveIndicatorShape);
        p.topIconIndicatorWidth = NavigationRailVerticalItemTokens::ActiveIndicatorWidth;
        p.topIconLabelTextStyle = Value(NavigationRailVerticalItemTokens::LabelTextFont);
        p.startIconLabelTextStyle = Value(NavigationRailHorizontalItemTokens::LabelTextFont);
        p.indicatorPadding = o.indicatorPadding ? o.indicatorPadding : WideNavigationRailItemDefaults::indicatorPadding(railExpanded);
        p.topIconIndicatorToLabelVerticalPadding = NavigationRailVerticalItemTokens::IconLabelSpace;
        const Dp noLabel = internal::WNRItemNoLabelIndicatorPadding;
        p.noLabelIndicatorPadding = { noLabel, noLabel, noLabel, noLabel };
        p.startIconToLabelHorizontalPadding = NavigationRailHorizontalItemTokens::IconLabelSpace;
        const Dp h = internal::WNRItemHorizontalPadding;
        p.contentPadding = top || !label ? PaddingValues{ h, Dp(0), h, Dp(0) } : PaddingValues{ h, Dp(0), Dp(0), Dp(0) };
        p.textColor = colors.TextColor(selected, o.enabled, top);
        p.iconColor = colors.IconColor(selected, o.enabled);
        p.indicatorColor = colors.selectedIndicatorColor;
        p.modifier = modifier;
        p.enabled = o.enabled;
        p.label = std::move(label);
        p.iconPosition = position;
        p.interactionSource = o.interactionSource;
        internal::AnimatedNavigationItem(p);
    }

}
