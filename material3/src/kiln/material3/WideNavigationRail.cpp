#include "kiln/material3/WideNavigationRail.h"
#include <kiln/Function.h>

#include "kiln/animation/Easing.h"
#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Dialog.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Scaffold.h"
#include "kiln/material3/Scrim.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/AnimatedProgress.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/DraggableAnchors.h"
#include "kiln/material3/internal/GetString.h"
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
#include <cmath>

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
            kiln::Function<void()> header;
            WindowInsets windowInsets;
            Arrangement arrangement;
            PaddingValues contentPadding;
            Dp collapsedRailWidth, expandedRailMinWidth, expandedRailMaxWidth;
        };

        void WideNavigationRailLayout(const ui::Modifier& modifier, const LayoutParams& lp, kiln::Function<void()> content) {
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

    std::shared_ptr<WideNavigationRailState> RememberWideNavigationRailState(WideNavigationRailValue initialValue, rt::Key key) {
        const anim::SpecPtr spec = Value(MotionSchemeKeyTokens::DefaultSpatial);
        rt::Saver<std::shared_ptr<WideNavigationRailState>> saver{
            [](const std::shared_ptr<WideNavigationRailState>& s) { return rt::SavedValue(double(s->TargetValue() == WideNavigationRailValue::Expanded)); },
            [spec](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<WideNavigationRailState>> {
                if (auto n = rt::SavedNumber(saved))
                    return std::make_shared<StateImpl>(*n != 0 ? WideNavigationRailValue::Expanded : WideNavigationRailValue::Collapsed, spec);
                return std::nullopt;
            }
        };
        return rt::RememberSaveable<std::shared_ptr<WideNavigationRailState>>([=] { return std::make_shared<StateImpl>(initialValue, spec); }, saver, {}, key);
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

    void WideNavigationRail(const ui::Modifier& modifier, const WideNavigationRailOptions& o, kiln::Function<void()> content) {
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

    void WideNavigationRailItem(bool selected, kiln::Function<void()> onClick, kiln::Function<void()> icon, kiln::Function<void()> label, bool railExpanded,
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


    namespace {
        using Value_ = WideNavigationRailValue;

        // ModalWideNavigationRailState: the expanded modal rail's position, an anchored drag
        // between offscreen (or in place) and open.
        class ModalState final : public WideNavigationRailState {
        public:
            ModalState(const std::shared_ptr<WideNavigationRailState>& state, anim::SpecPtr spec) : spec(std::move(spec)), anchored(state->TargetValue()) {}
            bool IsAnimating() const override { return anchored.IsAnimationRunning(); }
            Value_ TargetValue() const override { return anchored.TargetValue(); }
            // The settled value: where the rail was before a swipe or an animation started.
            Value_ CurrentValue() const override { return anchored.SettledValue(); }
            rt::Task<> Expand() override { co_await anchored.AnimateTo(Value_::Expanded, spec); }
            rt::Task<> Collapse() override { co_await anchored.AnimateTo(Value_::Collapsed, spec); }
            rt::Task<> Toggle() override {
                co_await anchored.AnimateTo(TargetValue() == Value_::Expanded ? Value_::Collapsed : Value_::Expanded, spec);
            }
            rt::Task<> SnapTo(Value_ value) override { co_await anchored.SnapTo(value); }
            float CurrentOffset() const { return anchored.Offset(); }   // NaN before the anchors are set

            anim::SpecPtr spec;
            AnchoredDraggableState<Value_> anchored;
        };

        // RailPredictiveBackState: whether the back swipe comes from the rail's side.
        struct PredictiveBackState {
            rt::State<bool> swipeEdgeMatchesRail{ true };
            void Update(bool isSwipeEdgeLeft, bool isRtl) { swipeEdgeMatchesRail.Set((isSwipeEdgeLeft && !isRtl) || (!isSwipeEdgeLeft && isRtl)); }
        };

        constexpr Dp PredictiveBackMaxScaleXDistance{ 24 }, PredictiveBackMaxScaleYDistance{ 48 };
        constexpr float PredictiveBackPivotFractionY = 0.5f;

        float PredictiveBackScaleX(float progress, bool swipeEdgeMatchesRail, float width, float maxPx) {
            if (std::isnan(width) || width == 0) return 1;
            const float direction = swipeEdgeMatchesRail ? 1.0f : -1.0f;
            return 1 + direction * (std::min(maxPx, width) * progress) / width;
        }
        float PredictiveBackScaleY(float progress, float height, float maxPx) {
            if (std::isnan(height) || height == 0) return 1;
            return 1 - std::min(maxPx, height) * progress / height;
        }

        // animateFloatAsState(if (expanded) 1 else 0) with the derived states read from it: the
        // caller recomposes when they change, not every frame.
        struct PositionProgress {
            std::shared_ptr<anim::Animatable> value;
            rt::State<bool> isCollapsed, modalExpanded;
            explicit PositionProgress(bool expanded)
                : value(std::make_shared<anim::Animatable>(expanded ? 1.0f : 0.0f)), isCollapsed(!expanded), modalExpanded(expanded) {}
            void Derive(float v) {
                isCollapsed.Set(v == 0);
                modalExpanded.Set(v >= 0.3f);
            }
        };
        rt::Task<> AnimatePosition(std::shared_ptr<PositionProgress> p, float target, anim::SpecPtr spec) {
            co_await p->value->AnimateTo(anim::Vec{ target, 0, 0, 0 }, std::move(spec), std::nullopt, [p](const anim::Vec& v) { p->Derive(v[0]); });
            p->Derive(p->value->Float());
        }

        rt::Task<> CollapseRail(std::shared_ptr<WideNavigationRailState> state) { co_await state->Collapse(); }
        rt::Task<> ExpandRail(std::shared_ptr<WideNavigationRailState> state) { co_await state->Expand(); }
        // animateToDismiss: the scrim's tap.
        rt::Task<> Dismiss(std::shared_ptr<ModalState> modal, std::shared_ptr<WideNavigationRailState> state, bool hideOnCollapse) {
            if (hideOnCollapse) co_await modal->Collapse();
            co_await state->Collapse();
        }
        // modalAnimateToDismiss: after a swipe settles, a modal rail swiped away collapses.
        rt::Task<> AfterSwipe(std::shared_ptr<ModalState> modal, std::shared_ptr<WideNavigationRailState> state, bool hideOnCollapse) {
            if (hideOnCollapse && modal->TargetValue() != Value_::Expanded) co_await state->Collapse();
        }
        rt::Task<> AnimateToZero(std::shared_ptr<anim::Animatable> a) { co_await a->AnimateTo(0.0f); }

        // The dialog's back callback: the rail follows the gesture, collapses when it completes
        // and springs back when it is cancelled.
        rt::Task<> PredictiveBack(BackProgress& events, std::shared_ptr<anim::Animatable> progress, std::shared_ptr<PredictiveBackState> backState,
                                  bool rtl, std::shared_ptr<internal::CoroutineScope> scope, std::shared_ptr<WideNavigationRailState> state) {
            struct OnCancel {
                std::shared_ptr<internal::CoroutineScope> scope;
                std::shared_ptr<anim::Animatable> progress;
                bool completed = false;
                ~OnCancel() { if (!completed) scope->Launch(AnimateToZero(progress)); }
            } onCancel{ scope, progress };
            static const anim::Easing easing = anim::Easing::CubicBezier(0.1f, 0.1f, 0.0f, 1.0f);   // PredictiveBack.transform
            while (auto e = co_await events.Next()) {
                backState->Update(e->swipeEdge == ui::SwipeEdge::Left, rtl);
                progress->SnapTo(easing.Transform(e->progress));
            }
            onCancel.completed = true;
            scope->Launch(CollapseRail(state));
        }
        // shouldDismissOnBackPress = false: the window still holds back, doing nothing.
        rt::Task<> HoldBack(BackProgress& events) {
            while (co_await events.Next()) {}
        }

        struct ModalContentParams {
            bool expanded = false, isStandaloneModal = false, gesturesEnabled = false;
            std::shared_ptr<anim::Animatable> predictiveBackProgress;
            std::shared_ptr<PredictiveBackState> predictiveBackState;
            std::shared_ptr<ModalState> railState;
            std::shared_ptr<WideNavigationRailState> state;
            Color containerColor, contentColor;
            ShapePtr shape;
            kiln::Function<void()> header;
            WindowInsets windowInsets;
            Arrangement arrangement;
            PaddingValues contentPadding;
        };

        void ModalWideNavigationRailContent(const ui::Modifier& modifier, const ModalContentParams& p, kiln::Function<void()> content) {
            auto& c = *rt::Composition::Current();
            const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
            const ui::Density density = c.Read(ui::LocalDensity);
            const std::string paneTitle = GetString(Strings::WideNavigationRailPaneTitle);
            const auto size = c.Remember<std::shared_ptr<rt::State<ui::IntSize>>>([] { return std::make_shared<rt::State<ui::IntSize>>(ui::IntSize{}); });
            const auto progress = p.predictiveBackProgress;
            const auto backState = p.predictiveBackState;
            const auto railState = p.railState;
            const float maxX = density.ToPx(PredictiveBackMaxScaleXDistance), maxY = density.ToPx(PredictiveBackMaxScaleYDistance);

            ui::Modifier m = SizeIn(modifier, Dp::Unspecified(), Dp::Unspecified(), internal::ExpandedRailMaxWidth, Dp::Unspecified());
            m = FillMax(std::move(m), Axis::Vertical);
            m = Semantics(std::move(m), [paneTitle](ui::SemanticsConfiguration& s) { s.paneTitle = paneTitle; });
            m = OnSizeChanged(std::move(m), [size](ui::IntSize s) { size->Set(s); });
            m = GraphicsLayer(std::move(m), [=](GraphicsLayerParams& l) {
                const float pr = progress->Float();
                if (pr <= 0) return;
                const float offset = railState->CurrentOffset();
                const ui::IntSize sz = size->Get();
                if (!std::isnan(offset) && sz.width != 0) {
                    // The predictive back animation.
                    l.scaleX = PredictiveBackScaleX(pr, backState->swipeEdgeMatchesRail.Get(), float(sz.width), maxX);
                    l.scaleY = PredictiveBackScaleY(pr, float(sz.height), maxY);
                    l.originX = rtl ? 1.0f : 0.0f;
                    l.originY = PredictiveBackPivotFractionY;
                }
            });
            const bool standalone = p.isStandaloneModal;
            const auto anchors = std::make_shared<const internal::AnchorsCalculation<Value_>>([standalone, railState](ui::IntSize railSize, const ui::Constraints&) {
                const float width = float(railSize.width);
                DraggableAnchors<Value_> a{ { Value_::Collapsed, standalone ? -width : 0.0f }, { Value_::Expanded, 0.0f } };
                return std::pair{ std::move(a), railState->TargetValue() };
            });
            m = internal::DraggableAnchors(std::move(m), railState->anchored, Orientation::Horizontal, anchors);
            AnchoredDraggableOptions drag;
            drag.enabled = p.gesturesEnabled;
            drag.positionalThreshold = [](float distance) { return distance * 0.5f; };
            drag.snapAnimationSpec = railState->spec;
            drag.afterFling = [railState, state = p.state, standalone] { return AfterSwipe(railState, state, standalone); };
            m = AnchoredDraggable(std::move(m), railState->anchored, Orientation::Horizontal, drag);

            SurfaceOptions surface;
            surface.shape = p.shape;
            surface.color = p.containerColor;
            surface.contentColor = p.contentColor;
            LayoutParams lp;
            lp.isModal = true;
            lp.expanded = p.expanded;
            lp.containerColor = p.containerColor;
            lp.contentColor = p.contentColor;
            lp.shape = p.shape;
            lp.header = p.header;
            lp.windowInsets = p.windowInsets;
            lp.arrangement = p.arrangement;
            lp.contentPadding = p.contentPadding;
            lp.collapsedRailWidth = internal::CollapsedRailWidth;
            lp.expandedRailMinWidth = internal::ExpandedRailMinWidth;
            lp.expandedRailMaxWidth = internal::ExpandedRailMaxWidth;
            Surface(m, surface, [=] {
                // Keeps the content's aspect and alignment while the container scales.
                ui::Modifier inner = GraphicsLayer(ui::Modifier{}, [=](GraphicsLayerParams& l) {
                    const float pr = progress->Float();
                    if (pr <= 0) return;
                    const ui::IntSize sz = size->Get();
                    const float sx = PredictiveBackScaleX(pr, backState->swipeEdgeMatchesRail.Get(), float(sz.width), maxX);
                    const float sy = PredictiveBackScaleY(pr, float(sz.height), maxY);
                    l.scaleX = sx != 0 ? sy / sx : 1.0f;
                    l.originX = rtl ? 0.0f : 1.0f;
                    l.originY = PredictiveBackPivotFractionY;
                });
                WideNavigationRailLayout(inner, lp, content);
            });
        }
    }

    void ModalWideNavigationRail(const ui::Modifier& modifier, const ModalWideNavigationRailOptions& o, kiln::Function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto state = o.state ? o.state : RememberWideNavigationRailState();
        const WideNavigationRailColors colors = o.colors.value_or(WideNavigationRailDefaults::colors());
        const bool hideOnCollapse = o.hideOnCollapse;
        // rememberContent: with a collapsed rail kept, the items are movable content, keeping their
        // state as they move into the expanded rail's window and back. The latest content runs.
        if (!hideOnCollapse) {
            const auto latest = c.Remember<std::shared_ptr<kiln::Function<void()>>>([] { return std::make_shared<kiln::Function<void()>>(); });
            *latest = std::move(content);
            const auto& movable = c.Remember<rt::MovableContent<>>([&] { return rt::MovableContent<>(kiln::Function<void()>([latest] { if (*latest) (*latest)(); })); });
            content = [movable] { movable(); };
        }
        const bool targetExpanded = state->TargetValue() == Value_::Expanded;

        const auto position = c.Remember<std::shared_ptr<PositionProgress>>([targetExpanded] { return std::make_shared<PositionProgress>(targetExpanded); });
        const anim::SpecPtr effects = Value(MotionSchemeKeyTokens::DefaultEffects);
        c.LaunchedEffect(rt::Keys(targetExpanded), [position, targetExpanded, effects]() -> rt::Task<> {
            return AnimatePosition(position, targetExpanded ? 1.0f : 0.0f, effects);
        });
        const bool isCollapsed = position->isCollapsed.Get(), modalExpanded = position->modalExpanded.Get();

        const anim::SpecPtr modalSpec = Value(MotionSchemeKeyTokens::DefaultSpatial);
        const auto modalState = c.Remember<std::shared_ptr<ModalState>>([&] { return std::make_shared<ModalState>(state, modalSpec); }, rt::Keys(state.get()));
        const auto scope = internal::RememberCoroutineScope();
        const auto animateToDismiss = [scope, modalState, state, hideOnCollapse] { scope->Launch(Dismiss(modalState, state, hideOnCollapse)); };

        const ShapePtr collapsedShape = o.collapsedShape ? o.collapsedShape : WideNavigationRailDefaults::modalCollapsedShape();
        const ShapePtr expandedShape = o.expandedShape ? o.expandedShape : WideNavigationRailDefaults::modalExpandedShape();
        const WindowInsets insets = o.windowInsets.value_or(WideNavigationRailDefaults::windowInsets());

        // A collapsed rail unless it hides: kept while expanded so the screen's layout stays.
        if (!hideOnCollapse) {
            LayoutParams lp;
            lp.containerColor = colors.containerColor;
            lp.contentColor = colors.contentColor;
            lp.shape = collapsedShape;
            lp.header = o.header;
            lp.windowInsets = insets;
            lp.arrangement = o.arrangement;
            lp.contentPadding = o.contentPadding;
            lp.collapsedRailWidth = internal::CollapsedRailWidth;
            lp.expandedRailMinWidth = internal::ExpandedRailMinWidth;
            lp.expandedRailMaxWidth = internal::ExpandedRailMaxWidth;
            // The content shows here only while collapsed, so it does not change this rail.
            WideNavigationRailLayout(modifier, lp, [isCollapsed, content] { if (isCollapsed) content(); });
        }

        // The latest target, sent to the modal rail while it is shown (a conflated channel).
        const auto sent = c.Remember<std::shared_ptr<std::optional<bool>>>([] { return std::make_shared<std::optional<bool>>(); });

        if (isCollapsed) return;
        // The expanded modal rail, in a window.
        auto& cc = *rt::Composition::Current();
        const auto backProgress = cc.Remember<std::shared_ptr<anim::Animatable>>([] { return std::make_shared<anim::Animatable>(0.0f); });
        const auto backState = cc.Remember<std::shared_ptr<PredictiveBackState>>([] { return std::make_shared<PredictiveBackState>(); });
        const auto dialogScope = internal::RememberCoroutineScope();
        if (hideOnCollapse) {
            cc.SideEffect([sent, targetExpanded, scope, modalState] {
                if (*sent == targetExpanded) return;
                *sent = targetExpanded;
                if (targetExpanded) scope->Launch(ExpandRail(modalState));
                else scope->Launch(CollapseRail(modalState));
            });
        }
        const bool rtl = cc.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
        DialogProperties properties;
        properties.dismissOnBackPress = o.expandedProperties.shouldDismissOnBackPress;
        properties.dismissOnClickOutside = false;
        properties.securePolicy = o.expandedProperties.securePolicy;
        properties.usePlatformDefaultWidth = false;   // edge to edge, full size
        properties.decorFitsSystemWindows = false;
        properties.scrimAlpha = 0;                   // the rail draws its own scrim
        kiln::Function<rt::Task<>(BackProgress&)> onBack;
        if (o.expandedProperties.shouldDismissOnBackPress)
            onBack = [backProgress, backState, rtl, dialogScope, state](BackProgress& events) {
                return PredictiveBack(events, backProgress, backState, rtl, dialogScope, state);
            };
        else
            onBack = [](BackProgress& events) { return HoldBack(events); };

        ModalContentParams p;
        p.expanded = hideOnCollapse || modalExpanded;
        p.isStandaloneModal = hideOnCollapse;
        p.gesturesEnabled = hideOnCollapse;
        p.predictiveBackProgress = backProgress;
        p.predictiveBackState = backState;
        p.railState = modalState;
        p.state = state;
        p.containerColor = colors.modalContainerColor;
        p.contentColor = colors.modalContentColor;
        p.shape = expandedShape;
        const auto header = o.header;
        const Dp headerTop = o.expandedHeaderTopPadding;
        p.header = [header, headerTop] { Box(Padding({}, PaddingValues{ Dp(0), headerTop, Dp(0), Dp(0) }), Alignment::TopStart(), false, [header] { if (header) header(); }); };
        p.windowInsets = insets;
        p.arrangement = o.arrangement;
        p.contentPadding = o.contentPadding;
        const Color scrimColor = colors.modalScrimColor;
        Dialog([dialogScope, state] { dialogScope->Launch(CollapseRail(state)); }, properties, onBack, [=] {
            ui::Modifier box = OnKeyEvent(ImePadding(FillMax({}, Axis::Both)), [dialogScope, state](const ui::KeyEvent& e) {
                if (e.type == ui::KeyEventType::KeyUp && e.keyCode == ui::keys::Escape) {
                    dialogScope->Launch(CollapseRail(state));
                    return true;
                }
                return false;
            });
            Box(box, Alignment::TopStart(), false, [=] {
                const bool scrimVisible = hideOnCollapse ? modalState->TargetValue() != Value_::Collapsed : modalExpanded;
                const auto alpha = internal::AnimatedProgress(scrimVisible, Value(MotionSchemeKeyTokens::DefaultEffects));
                ScrimOptions scrim;
                scrim.onClick = animateToDismiss;
                scrim.alpha = [alpha] { return alpha->Float(); };
                scrim.color = scrimColor;
                Scrim(GetString(Strings::CloseRail), {}, scrim);
                ModalWideNavigationRailContent(modifier, p, content);
            });
        });
    }

}
