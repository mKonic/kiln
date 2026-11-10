#include "kiln/material3/NavigationItem.h"

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/AnimatedProgress.h"
#include "kiln/material3/internal/MappedInteractionSource.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    using ui::LayoutDirection;

    namespace {
        constexpr Dp IndicatorVerticalOffset{ 12 };

        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }

        // calculateStartPadding / calculateEndPadding.
        Dp StartOf(const PaddingValues& p, LayoutDirection d) { return p.rtlAware || d == LayoutDirection::Ltr ? p.start : p.end; }
        Dp EndOf(const PaddingValues& p, LayoutDirection d) { return p.rtlAware || d == LayoutDirection::Ltr ? p.end : p.start; }
        Dp Horizontal(const PaddingValues& p, LayoutDirection d) { return Dp(StartOf(p, d).value + EndOf(p, d).value); }
        Dp Vertical(const PaddingValues& p) { return Dp(p.top.value + p.bottom.value); }
        Dp Add(Dp a, Dp b) { return Dp(a.value + b.value); }
        int LerpInt(int start, int stop, float fraction) { return start + ui::RoundJava(float(stop - start) * fraction); }
        Dp LerpDp(Dp a, Dp b, float f) { return Dp(a.value + (b.value - a.value) * f); }

        ui::Placeable& MeasureFixed(ui::Measurable* m, const ui::Constraints& loose, int width, int height) {
            return m->Measure(loose.Constrain(ui::Constraints::Fixed(width, height)));
        }

        // The children of an item's layout: the indicator ripple, the indicator, the icon and the
        // label (if any), in that order.
        enum Child : std::size_t { RippleChild, IndicatorChild, IconChild, LabelChild };

        ui::MeasureResult PlaceIcon(ui::MeasureScope& s, bool centered, ui::Placeable& icon, ui::Placeable& ripple, ui::Placeable& indicator,
                                    const PaddingValues& contentPadding, const ui::Constraints& c) {
            const LayoutDirection dir = s.Direction();
            const int startPadding = s.RoundToPx(StartOf(contentPadding, dir)), endPadding = s.RoundToPx(EndOf(contentPadding, dir));
            const int topPadding = s.RoundToPx(contentPadding.top), bottomPadding = s.RoundToPx(contentPadding.bottom);
            const int noPaddingsWidth = ripple.Width(), noPaddingsHeight = ripple.Height();
            const int width = c.ConstrainWidth(noPaddingsWidth + startPadding + endPadding);
            const int height = c.ConstrainHeight(noPaddingsHeight + topPadding + bottomPadding);
            const int horizontalOffset = (centered ? (width - ripple.Width()) / 2 : 0) + startPadding;
            const int verticalOffset = (centered ? (height - ripple.Height()) / 2 : 0) + topPadding;
            return s.Layout(width, height, [&, horizontalOffset, verticalOffset, noPaddingsWidth, noPaddingsHeight](ui::PlacementScope& p) {
                p.PlaceRelative(indicator, horizontalOffset + (noPaddingsWidth - indicator.Width()) / 2, verticalOffset + (noPaddingsHeight - indicator.Height()) / 2);
                p.PlaceRelative(icon, horizontalOffset + (noPaddingsWidth - icon.Width()) / 2, verticalOffset + (noPaddingsHeight - icon.Height()) / 2);
                p.PlaceRelative(ripple, horizontalOffset + (noPaddingsWidth - ripple.Width()) / 2, verticalOffset + (noPaddingsHeight - ripple.Height()) / 2);
            });
        }

        ui::MeasureResult PlaceLabelAndTopIcon(ui::MeasureScope& s, ui::Placeable& label, ui::Placeable& icon, ui::Placeable& ripple, ui::Placeable& indicator,
                                               const ui::Constraints& c, Dp indicatorToLabel, Dp indicatorPaddingTop, Dp indicatorPaddingBottom,
                                               Dp itemTop, Dp itemBottom, Dp itemStart, Dp itemEnd) {
            const int width = c.ConstrainWidth(std::max(label.Width(), ripple.Width()));
            const float contentHeight = float(ripple.Height()) + s.ToPx(indicatorToLabel) + float(label.Height());
            const int height = c.ConstrainHeight(ui::RoundJava(contentHeight + s.ToPx(itemTop) + s.ToPx(itemBottom)));
            const int iconY = s.RoundToPx(Add(itemTop, indicatorPaddingTop));
            const int iconX = (width - icon.Width()) / 2, indicatorX = (width - indicator.Width()) / 2;
            const int indicatorY = iconY - s.RoundToPx(indicatorPaddingTop);
            const int labelX = (width - label.Width()) / 2;
            // The label a fixed padding below the icon.
            const int labelY = iconY + icon.Height() + s.RoundToPx(Add(indicatorPaddingBottom, indicatorToLabel));
            const int rippleX = (width - ripple.Width()) / 2, rippleY = indicatorY;
            const int startPaddingX = s.RoundToPx(itemStart);
            const int contentWidth = width + startPaddingX + s.RoundToPx(itemEnd);
            return s.Layout(contentWidth, height, [&, startPaddingX, indicatorX, indicatorY, labelX, labelY, iconX, iconY, rippleX, rippleY](ui::PlacementScope& p) {
                p.PlaceRelative(indicator, startPaddingX + indicatorX, indicatorY);
                p.PlaceRelative(label, startPaddingX + labelX, labelY);
                p.PlaceRelative(icon, startPaddingX + iconX, iconY);
                p.PlaceRelative(ripple, startPaddingX + rippleX, rippleY);
            });
        }

        ui::MeasureResult PlaceLabelAndStartIcon(ui::MeasureScope& s, ui::Placeable& label, ui::Placeable& icon, ui::Placeable& ripple,
                                                 ui::Placeable& indicator, const ui::Constraints& c, Dp startIconToLabel, const PaddingValues& itemPadding) {
            const int contentWidth = c.ConstrainWidth(ripple.Width()), contentHeight = c.ConstrainHeight(ripple.Height());
            const int indicatorX = (contentWidth - indicator.Width()) / 2, indicatorY = (contentHeight - indicator.Height()) / 2;
            const int iconY = (contentHeight - icon.Height()) / 2, labelY = (contentHeight - label.Height()) / 2;
            const int toLabel = s.RoundToPx(startIconToLabel);
            const int itemContentWidth = icon.Width() + toLabel + label.Width();
            const int iconX = (contentWidth - itemContentWidth) / 2, labelX = iconX + icon.Width() + toLabel;
            const int rippleX = (contentWidth - ripple.Width()) / 2, rippleY = (contentHeight - ripple.Height()) / 2;
            const LayoutDirection dir = s.Direction();
            const int startPadding = s.RoundToPx(StartOf(itemPadding, dir)), endPadding = s.RoundToPx(EndOf(itemPadding, dir));
            const int topPadding = s.RoundToPx(itemPadding.top), bottomPadding = s.RoundToPx(itemPadding.bottom);
            return s.Layout(contentWidth + startPadding + endPadding, contentHeight + topPadding + bottomPadding,
                            [&, startPadding, topPadding, indicatorX, indicatorY, labelX, labelY, iconX, iconY, rippleX, rippleY](ui::PlacementScope& p) {
                                p.PlaceRelative(indicator, startPadding + indicatorX, topPadding + indicatorY);
                                p.PlaceRelative(label, startPadding + labelX, topPadding + labelY);
                                p.PlaceRelative(icon, startPadding + iconX, topPadding + iconY);
                                p.PlaceRelative(ripple, startPadding + rippleX, topPadding + rippleY);
                            });
        }

        float Progress(const std::shared_ptr<anim::Animatable>& a) { return std::max(0.0f, a->Float()); }

        class TopIconOrIconOnlyPolicy final : public ui::MeasurePolicy {
        public:
            TopIconOrIconOnlyPolicy(bool hasLabel, std::shared_ptr<anim::Animatable> indicatorProgress, PaddingValues indicatorPadding,
                                    Dp indicatorToLabel, PaddingValues itemPadding, bool noLabelCentered)
                : m_HasLabel(hasLabel), m_Progress(std::move(indicatorProgress)), m_IndicatorPadding(indicatorPadding), m_IndicatorToLabel(indicatorToLabel),
                  m_ItemPadding(itemPadding), m_NoLabelCentered(noLabelCentered) {}

            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                const float progress = Progress(m_Progress);
                const ui::Constraints loose = c.CopyMaxDimensions();
                const int horizontal = s.RoundToPx(Horizontal(m_IndicatorPadding, s.Direction())), vertical = s.RoundToPx(Vertical(m_IndicatorPadding));
                // The icon leaves room for the indicator around it.
                ui::Placeable& icon = children[IconChild]->Measure(loose.Offset(-horizontal, -vertical));
                const int totalIndicatorWidth = icon.Width() + horizontal, indicatorHeight = icon.Height() + vertical;
                const int animatedIndicatorWidth = ui::RoundJava(float(totalIndicatorWidth) * progress);
                ui::Placeable& ripple = MeasureFixed(children[RippleChild], loose, totalIndicatorWidth, indicatorHeight);
                ui::Placeable& indicator = MeasureFixed(children[IndicatorChild], loose, animatedIndicatorWidth, indicatorHeight);
                if (!m_HasLabel) return PlaceIcon(s, m_NoLabelCentered, icon, ripple, indicator, m_ItemPadding, c);
                // The label leaves room for the indicator and the padding above it.
                ui::Placeable& label = children[LabelChild]->Measure(loose.Offset(0, -(indicator.Height() + s.RoundToPx(m_IndicatorToLabel))));
                const LayoutDirection dir = s.Direction();
                return PlaceLabelAndTopIcon(s, label, icon, ripple, indicator, c, m_IndicatorToLabel, m_IndicatorPadding.top, m_IndicatorPadding.bottom,
                                            m_ItemPadding.top, m_ItemPadding.bottom, StartOf(m_ItemPadding, dir), EndOf(m_ItemPadding, dir));
            }
            int MaxIntrinsicHeight(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int width) override {
                const int iconHeight = children[IconChild]->MaxIntrinsicHeight(width);
                const int labelHeight = children.size() > LabelChild ? children[LabelChild]->MaxIntrinsicHeight(width) : 0;
                const int paddings = s.RoundToPx(Dp(Vertical(m_ItemPadding).value + Vertical(m_IndicatorPadding).value + m_IndicatorToLabel.value));
                return iconHeight + labelHeight + paddings;
            }

        private:
            bool m_HasLabel;
            std::shared_ptr<anim::Animatable> m_Progress;
            PaddingValues m_IndicatorPadding;
            Dp m_IndicatorToLabel;
            PaddingValues m_ItemPadding;
            bool m_NoLabelCentered;
        };

        class StartIconPolicy final : public ui::MeasurePolicy {
        public:
            StartIconPolicy(std::shared_ptr<anim::Animatable> indicatorProgress, PaddingValues indicatorPadding, Dp startIconToLabel, PaddingValues itemPadding)
                : m_Progress(std::move(indicatorProgress)), m_IndicatorPadding(indicatorPadding), m_StartIconToLabel(startIconToLabel), m_ItemPadding(itemPadding) {}

            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                const float progress = Progress(m_Progress);
                const ui::Constraints loose = c.CopyMaxDimensions();
                ui::Placeable& icon = children[IconChild]->Measure(loose);
                // The label leaves room for the icon and the padding before it.
                ui::Placeable& label = children[LabelChild]->Measure(loose.Offset(-(icon.Width() + s.RoundToPx(m_StartIconToLabel)), 0));
                const Dp horizontal = Horizontal(m_IndicatorPadding, s.Direction());
                const int totalIndicatorWidth = icon.Width() + label.Width() + s.RoundToPx(Add(m_StartIconToLabel, horizontal));
                const int indicatorHeight = std::max(icon.Height(), label.Height()) + s.RoundToPx(Vertical(m_IndicatorPadding));
                const int animatedIndicatorWidth = ui::RoundJava(float(totalIndicatorWidth) * progress);
                ui::Placeable& ripple = MeasureFixed(children[RippleChild], loose, totalIndicatorWidth, indicatorHeight);
                ui::Placeable& indicator = MeasureFixed(children[IndicatorChild], loose, animatedIndicatorWidth, indicatorHeight);
                return PlaceLabelAndStartIcon(s, label, icon, ripple, indicator, c, m_StartIconToLabel, m_ItemPadding);
            }
            int MaxIntrinsicWidth(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int height) override {
                const LayoutDirection dir = s.Direction();
                const int iconWidth = children[IconChild]->MaxIntrinsicWidth(height), labelWidth = children[LabelChild]->MaxIntrinsicWidth(height);
                const int paddings =
                    s.RoundToPx(Dp(Horizontal(m_IndicatorPadding, dir).value + m_StartIconToLabel.value + Horizontal(m_ItemPadding, dir).value));
                return iconWidth + labelWidth + paddings;
            }
            int MaxIntrinsicHeight(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int width) override {
                const int iconHeight = children[IconChild]->MaxIntrinsicHeight(width), labelHeight = children[LabelChild]->MaxIntrinsicHeight(width);
                // As in Compose: the item's start and end padding, not its top and bottom.
                const int paddings = s.RoundToPx(Dp(Vertical(m_IndicatorPadding).value + Horizontal(m_ItemPadding, s.Direction()).value));
                return std::max(iconHeight, labelHeight) + paddings;
            }

        private:
            std::shared_ptr<anim::Animatable> m_Progress;
            PaddingValues m_IndicatorPadding;
            Dp m_StartIconToLabel;
            PaddingValues m_ItemPadding;
        };

        class AnimatedPolicy final : public ui::MeasurePolicy {
        public:
            AnimatedPolicy(NavigationItemIconPosition iconPosition, std::shared_ptr<anim::Animatable> iconPositionProgress,
                           std::shared_ptr<anim::Animatable> indicatorProgress, std::shared_ptr<internal::DynamicPaddingValues> indicatorPadding,
                           Dp topIconIndicatorToLabel, Dp startIconToLabel, PaddingValues contentPadding, bool topStyleAllowed,
                           std::shared_ptr<rt::State<bool>> topStyle)
                : m_IconPosition(iconPosition), m_IconPositionProgress(std::move(iconPositionProgress)), m_Progress(std::move(indicatorProgress)),
                  m_IndicatorPadding(std::move(indicatorPadding)), m_TopIconIndicatorToLabel(topIconIndicatorToLabel), m_StartIconToLabel(startIconToLabel),
                  m_ContentPadding(contentPadding), m_TopStyleAllowed(topStyleAllowed), m_TopStyle(std::move(topStyle)) {}

            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                const float progress = Progress(m_Progress), positionProgress = Progress(m_IconPositionProgress);
                // The label's text style follows the position halfway through (derivedStateOf).
                const bool topStyle = m_TopStyleAllowed && positionProgress < 0.5f;
                if (m_TopStyle->Peek() != topStyle) m_TopStyle->Set(topStyle);
                const ui::Constraints loose = c.CopyMaxDimensions();
                ui::Placeable& icon = children[IconChild]->Measure(loose);
                ui::Placeable& label = children[LabelChild]->Measure(loose);
                m_IndicatorPadding->progress = std::clamp(positionProgress, 0.0f, 1.0f);
                const PaddingValues indicatorPadding = m_IndicatorPadding->Value();
                const LayoutDirection dir = s.Direction();
                const int horizontal = s.RoundToPx(Horizontal(indicatorPadding, dir)), vertical = s.RoundToPx(Vertical(indicatorPadding));
                const int topIconIndicatorWidth = icon.Width(), topIconIndicatorHeight = icon.Height();
                const int startIconIndicatorWidth = icon.Width() + label.Width() + s.RoundToPx(m_StartIconToLabel);
                const int startIconIndicatorHeight = std::max(icon.Height(), label.Height());
                const int indicatorWidth = LerpInt(topIconIndicatorWidth, startIconIndicatorWidth, positionProgress) + horizontal;
                const int animatedIndicatorWidth = ui::RoundJava(float(indicatorWidth) * progress);
                const int indicatorHeight = LerpInt(topIconIndicatorHeight, startIconIndicatorHeight, positionProgress) + vertical;
                ui::Placeable& ripple = MeasureFixed(children[RippleChild], loose, indicatorWidth, indicatorHeight);
                ui::Placeable& indicator = MeasureFixed(children[IndicatorChild], loose, animatedIndicatorWidth, indicatorHeight);
                return Place(s, positionProgress, label, icon, ripple, indicator, indicatorPadding, topIconIndicatorWidth, loose);
            }
            int MaxIntrinsicWidth(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int height) override {
                const LayoutDirection dir = s.Direction();
                const int iconWidth = children[IconChild]->MaxIntrinsicWidth(height), labelWidth = children[LabelChild]->MaxIntrinsicWidth(height);
                const PaddingValues indicatorPadding = m_IndicatorPadding->Value();
                const int indicatorHorizontal = s.RoundToPx(Horizontal(indicatorPadding, dir)), itemHorizontal = s.RoundToPx(Horizontal(m_ContentPadding, dir));
                if (m_IconPosition == NavigationItemIconPosition::Top) return std::max(labelWidth, iconWidth + indicatorHorizontal + itemHorizontal);
                return iconWidth + labelWidth + indicatorHorizontal + s.RoundToPx(m_StartIconToLabel) + itemHorizontal;
            }

        private:
            ui::MeasureResult Place(ui::MeasureScope& s, float positionProgress, ui::Placeable& label, ui::Placeable& icon, ui::Placeable& ripple,
                                    ui::Placeable& indicator, const PaddingValues& indicatorPadding, int topIconIndicatorWidth, const ui::Constraints& c) const {
                const bool isTop = m_IconPosition == NavigationItemIconPosition::Top;
                const LayoutDirection dir = s.Direction();
                const int startPadding = s.RoundToPx(StartOf(m_ContentPadding, dir)), endPadding = s.RoundToPx(EndOf(m_ContentPadding, dir));
                const int topPadding = s.RoundToPx(m_ContentPadding.top), bottomPadding = s.RoundToPx(m_ContentPadding.bottom);
                const int indicatorHorizontal = s.RoundToPx(Horizontal(indicatorPadding, dir));
                const int widthTopIcon =
                    c.ConstrainWidth(std::max(label.Width() + startPadding + endPadding, topIconIndicatorWidth + startPadding + endPadding + indicatorHorizontal));
                const int widthStartIcon = c.ConstrainWidth(ripple.Width() + startPadding + endPadding);
                const float width = float(widthTopIcon) + float(widthStartIcon - widthTopIcon) * positionProgress;
                const int heightTopIcon = c.ConstrainHeight(
                    ui::RoundJava(float(ripple.Height()) + s.ToPx(m_TopIconIndicatorToLabel) + float(label.Height()) + float(topPadding) + float(bottomPadding)));
                const int heightStartIcon = c.ConstrainHeight(ripple.Height() + topPadding + bottomPadding);
                const int height = LerpInt(heightTopIcon, heightStartIcon, positionProgress);

                const int iconX = s.RoundToPx(StartOf(indicatorPadding, dir));
                const int iconYTopIcon = s.RoundToPx(indicatorPadding.top);
                const int iconYStartIcon = (height - icon.Height()) / 2 - iconYTopIcon;
                const int iconY = LerpInt(0, iconYStartIcon, positionProgress) + iconYTopIcon;
                const int labelXTopIcon = (icon.Width() + indicatorHorizontal - label.Width()) / 2;
                const int labelYTopIcon = iconY + icon.Height() + s.RoundToPx(Add(indicatorPadding.bottom, m_TopIconIndicatorToLabel));
                const float labelXStartIconHorizontalOffset = isTop && positionProgress > 0 ? 0.0f : float(startPadding) * (1 - positionProgress);
                const float labelXStartIcon = float(iconX + icon.Width() + s.RoundToPx(m_StartIconToLabel)) - labelXStartIconHorizontalOffset;
                const int labelYStartIcon = (height - label.Height()) / 2;
                const float labelX = positionProgress < 0.5f ? float(labelXTopIcon) : labelXStartIcon * positionProgress;
                const int labelY = positionProgress < 0.5f ? labelYTopIcon : labelYStartIcon;
                // The label's fade (alpha 4 (progress - 0.5)²) is its own layer, set where it is composed.
                return s.Layout(ui::RoundJava(width), height, [&, startPadding, topPadding, iconX, iconY, labelX, labelY](ui::PlacementScope& p) {
                    p.PlaceRelative(indicator, startPadding, topPadding);
                    p.PlaceRelative(icon, startPadding + iconX, topPadding + iconY);
                    p.PlaceRelative(label, startPadding + int(labelX), topPadding + labelY);
                    p.PlaceRelative(ripple, startPadding, topPadding);
                });
            }

            NavigationItemIconPosition m_IconPosition;
            std::shared_ptr<anim::Animatable> m_IconPositionProgress, m_Progress;
            std::shared_ptr<internal::DynamicPaddingValues> m_IndicatorPadding;
            Dp m_TopIconIndicatorToLabel, m_StartIconToLabel;
            PaddingValues m_ContentPadding;
            bool m_TopStyleAllowed;
            std::shared_ptr<rt::State<bool>> m_TopStyle;
        };

        // The ripple and the indicator are separate: the indicator's width animates.
        void IndicatorRipple(const std::shared_ptr<MutableInteractionSource>& source, const ShapePtr& shape) {
            RippleOptions ripple;
            ripple.focusRingShape = shape;
            Box(Ripple(ripple)->Create(Clip(ui::Modifier{}, shape), source), Alignment::TopStart(), false, {});
        }
        void Indicator(Color color, const ShapePtr& shape, std::shared_ptr<anim::Animatable> progress) {
            Box(Background(GraphicsLayer(ui::Modifier{},
                                         [progress](GraphicsLayerParams& p) {
                                             p.compositingStrategy = CompositingStrategy::ModulateAlpha;
                                             p.alpha = Progress(progress);
                                         }),
                           color, shape),
                Alignment::TopStart(), false, {});
        }

        // The selectable item box, at least the minimum interactive size, recording its width.
        ui::Modifier ItemModifier(const ui::Modifier& modifier, bool selected, std::function<void()> onClick, bool enabled,
                                  const std::shared_ptr<MutableInteractionSource>& source, const std::shared_ptr<rt::State<int>>& width) {
            ClickableOptions click;
            click.enabled = enabled;
            click.role = ui::Role::Tab;
            click.interactionSource = source;
            click.indication = nullptr;
            click.useLocalIndication = false;
            const Dp minimum = rt::Composition::Current()->Read(LocalMinimumInteractiveComponentSize);
            ui::Modifier m = Selectable(modifier, selected, std::move(onClick), click);
            m = DefaultMinSize(std::move(m), minimum, minimum);
            return OnSizeChanged(std::move(m), [width](ui::IntSize size) { width->Set(size.width); });
        }

        // Presses mapped into the indicator's coordinates, so only the pill shows the ripple.
        std::shared_ptr<MutableInteractionSource> PillSource(const std::shared_ptr<MutableInteractionSource>& source, const std::shared_ptr<rt::State<int>>& width,
                                                             Dp indicatorWidth) {
            const ui::Density density = rt::Composition::Current()->Read(ui::LocalDensity);
            return internal::RememberMappedInteractionSource(source, [width, density, indicatorWidth] {
                return ui::Offset{ float(width->Peek() - density.RoundToPx(indicatorWidth)) / 2, density.ToPx(IndicatorVerticalOffset) };
            });
        }
    }

    NavigationItemColors NavigationItemColors::Copy(Color a, Color b, Color c, Color d, Color e, Color f, Color g, Color h) const {
        return { Or(a, selectedIconColor),   Or(b, selectedTextColorTopIconPosition), Or(c, selectedTextColorStartIconPosition),
                 Or(d, selectedIndicatorColor), Or(e, unselectedIconColor),           Or(f, unselectedTextColor),
                 Or(g, disabledIconColor),   Or(h, disabledTextColor) };
    }
    Color NavigationItemColors::IconColor(bool selected, bool enabled) const {
        return !enabled ? disabledIconColor : selected ? selectedIconColor : unselectedIconColor;
    }
    Color NavigationItemColors::TextColor(bool selected, bool enabled, bool isIconPositionTop) const {
        if (!enabled) return disabledTextColor;
        if (selected) return isIconPositionTop ? selectedTextColorTopIconPosition : selectedTextColorStartIconPosition;
        return unselectedTextColor;
    }

    namespace internal {

        PaddingValues DynamicPaddingValues::Value() const {
            // Lerped per side, as calculate*Padding: the relative sides when both are relative.
            return { LerpDp(collapsed.start, expanded.start, progress), LerpDp(collapsed.top, expanded.top, progress),
                     LerpDp(collapsed.end, expanded.end, progress), LerpDp(collapsed.bottom, expanded.bottom, progress),
                     collapsed.rtlAware && expanded.rtlAware };
        }

        void NavigationItem(const NavigationItemParams& p) {
            auto& c = *rt::Composition::Current();
            const auto source = p.interactionSource ? p.interactionSource : RememberInteractionSource();
            const Color iconColor = p.iconColor, textColor = p.textColor;
            const auto icon = p.icon, label = p.label;
            const text::TextStyle labelStyle = p.labelTextStyle;
            const auto styledIcon = [=] { rt::Composition::Current()->Provide(LocalContentColor, iconColor, icon); };
            const auto styledLabel = [=] { ProvideContentColorTextStyle(textColor, labelStyle, label); };
            const auto itemWidth = c.Remember<std::shared_ptr<rt::State<int>>>([] { return std::make_shared<rt::State<int>>(0); });
            Box(ItemModifier(p.modifier, p.selected, p.onClick, p.enabled, source, itemWidth), Alignment::Center(), true, [=] {
                const auto progress = AnimatedProgress(p.selected, Value(MotionSchemeKeyTokens::DefaultSpatial));
                const bool top = p.iconPosition == NavigationItemIconPosition::Top;
                const auto rippleSource = top ? PillSource(source, itemWidth, p.indicatorWidth) : source;
                const bool hasLabel = bool(label);
                std::shared_ptr<ui::MeasurePolicy> policy;
                if (!hasLabel || top)
                    policy = std::make_shared<TopIconOrIconOnlyPolicy>(hasLabel, progress, p.indicatorPadding, p.indicatorToLabelVerticalPadding,
                                                                       p.topIconItemPadding, true);
                else
                    policy = std::make_shared<StartIconPolicy>(progress, p.indicatorPadding, p.startIconToLabelHorizontalPadding, p.startIconItemPadding);
                const ShapePtr shape = p.indicatorShape;
                const Color indicatorColor = p.indicatorColor;
                foundation::Layout(BadgeBounds({}), policy, [=] {
                    IndicatorRipple(rippleSource, shape);
                    Indicator(indicatorColor, shape, progress);
                    Box({}, Alignment::TopStart(), false, styledIcon);
                    if (hasLabel) Box({}, Alignment::TopStart(), false, styledLabel);
                });
            });
        }

        void AnimatedNavigationItem(const AnimatedNavigationItemParams& p) {
            auto& c = *rt::Composition::Current();
            const auto source = p.interactionSource ? p.interactionSource : RememberInteractionSource();
            const Color iconColor = p.iconColor;
            const auto icon = p.icon;
            const auto styledIcon = [=] { rt::Composition::Current()->Provide(LocalContentColor, iconColor, icon); };
            const auto itemWidth = c.Remember<std::shared_ptr<rt::State<int>>>([] { return std::make_shared<rt::State<int>>(0); });
            Box(ItemModifier(p.modifier, p.selected, p.onClick, p.enabled, source, itemWidth), Alignment::Center(), true, [=] {
                auto& cc = *rt::Composition::Current();
                const bool top = p.iconPosition == NavigationItemIconPosition::Top;
                const auto progress = AnimatedProgress(p.selected, Value(MotionSchemeKeyTokens::DefaultSpatial), rt::Key(1));
                const auto positionProgress = AnimatedProgress(!top, Value(MotionSchemeKeyTokens::DefaultSpatial), rt::Key(2));
                // remember { derivedStateOf { ... } } keeps the position it was first composed with.
                const bool topStyleAllowed = cc.Remember<bool>([top] { return top; });
                const auto topStyle = cc.Remember<std::shared_ptr<rt::State<bool>>>([top] { return std::make_shared<rt::State<bool>>(top); });
                const text::TextStyle topLabelStyle = p.topIconLabelTextStyle, startLabelStyle = p.startIconLabelTextStyle;
                const Color textColor = p.textColor;
                const auto label = p.label;
                const auto styledLabel = [=] {
                    const Color color = AnimateColorAsState(textColor, Value(MotionSchemeKeyTokens::DefaultEffects));
                    ProvideContentColorTextStyle(color, topStyle->Get() ? topLabelStyle : startLabelStyle, label);
                };
                const auto rippleSource = top ? PillSource(source, itemWidth, p.topIconIndicatorWidth) : source;
                const bool hasLabel = bool(label);
                std::shared_ptr<ui::MeasurePolicy> policy;
                if (hasLabel)
                    policy = std::make_shared<AnimatedPolicy>(p.iconPosition, positionProgress, progress, p.indicatorPadding,
                                                              p.topIconIndicatorToLabelVerticalPadding, p.startIconToLabelHorizontalPadding, p.contentPadding,
                                                              topStyleAllowed, topStyle);
                else   // No label: a circular indicator.
                    policy = std::make_shared<TopIconOrIconOnlyPolicy>(false, progress, p.noLabelIndicatorPadding, Dp(0), p.contentPadding, false);
                const ShapePtr shape = p.indicatorShape;
                const Color indicatorColor = p.indicatorColor;
                foundation::Layout(BadgeBounds({}), policy, [=] {
                    IndicatorRipple(rippleSource, shape);
                    Indicator(indicatorColor, shape, progress);
                    Box({}, Alignment::TopStart(), false, styledIcon);
                    if (hasLabel)
                        Box(GraphicsLayer(ui::Modifier{},
                                          [positionProgress](GraphicsLayerParams& l) {
                                              const float t = Progress(positionProgress) - 0.5f;
                                              l.alpha = 4 * t * t;
                                          }),
                            Alignment::TopStart(), false, styledLabel);
                });
            });
        }

    }

}
