#include "kiln/material3/NavigationBar.h"
#include <kiln/Function.h>

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Scaffold.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/AnimatedProgress.h"
#include "kiln/material3/internal/MappedInteractionSource.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
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
        constexpr Dp NavigationBarHeight = NavigationBarTokens::TallContainerHeight;
        constexpr Dp IndicatorHorizontalPadding{ (NavigationBarVerticalItemTokens::ActiveIndicatorWidth.value - NavigationBarVerticalItemTokens::IconSize.value) / 2 };
        constexpr Dp IndicatorVerticalPadding = internal::NavigationBarIndicatorVerticalPadding;
        constexpr Dp IndicatorVerticalOffset{ 12 };

        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }

        // NavigationBarItemLayout: the children are the indicator ripple, the indicator, the icon
        // and the label (if any), in that order.
        class ItemPolicy final : public ui::MeasurePolicy {
        public:
            ItemPolicy(bool hasLabel, bool alwaysShowLabel, std::shared_ptr<anim::Animatable> size)
                : m_HasLabel(hasLabel), m_AlwaysShowLabel(alwaysShowLabel), m_Size(std::move(size)) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                // Not below 0: bouncy springs overshoot.
                const float progress = std::max(0.0f, m_Size->Float());
                const ui::Constraints loose = c.CopyMaxDimensions();
                ui::Placeable& icon = children[2]->Measure(loose);
                const int totalIndicatorWidth = icon.Width() + s.RoundToPx(Dp(IndicatorHorizontalPadding.value * 2));
                const int animatedIndicatorWidth = ui::RoundJava(float(totalIndicatorWidth) * progress);
                const int indicatorHeight = icon.Height() + s.RoundToPx(Dp(IndicatorVerticalPadding.value * 2));
                ui::Placeable& ripple = children[0]->Measure(ui::Constraints::Fixed(totalIndicatorWidth, indicatorHeight));
                ui::Placeable& indicator = children[1]->Measure(ui::Constraints::Fixed(animatedIndicatorWidth, indicatorHeight));
                if (!m_HasLabel) return PlaceIcon(s, icon, ripple, indicator, c);
                ui::Placeable& label = children[3]->Measure(loose);
                return PlaceLabelAndIcon(s, label, icon, ripple, indicator, c, progress);
            }

        private:
            int ContainerWidth(ui::MeasureScope& s, const ui::Placeable& icon, const ui::Constraints& c) const {
                return c.maxWidth == ui::Constraints::Infinity ? icon.Width() + s.RoundToPx(internal::NavigationBarItemToIconMinimumPadding) * 2 : c.maxWidth;
            }
            // Everything centred.
            ui::MeasureResult PlaceIcon(ui::MeasureScope& s, ui::Placeable& icon, ui::Placeable& ripple, ui::Placeable& indicator,
                                        const ui::Constraints& c) const {
                const int width = ContainerWidth(s, icon, c);
                const int height = std::clamp(s.RoundToPx(NavigationBarHeight), c.minHeight, c.maxHeight);
                return s.Layout(width, height, [&, width, height](ui::PlacementScope& p) {
                    p.PlaceRelative(indicator, (width - indicator.Width()) / 2, (height - indicator.Height()) / 2);
                    p.PlaceRelative(icon, (width - icon.Width()) / 2, (height - icon.Height()) / 2);
                    p.PlaceRelative(ripple, (width - ripple.Width()) / 2, (height - ripple.Height()) / 2);
                });
            }
            // Icon and label together in the middle when selected or always labelled; otherwise
            // the icon moves toward the centre as the selection progress falls, and the label hides.
            ui::MeasureResult PlaceLabelAndIcon(ui::MeasureScope& s, ui::Placeable& label, ui::Placeable& icon, ui::Placeable& ripple,
                                                ui::Placeable& indicator, const ui::Constraints& c, float progress) const {
                const float vPad = s.ToPx(IndicatorVerticalPadding), toLabel = s.ToPx(internal::NavigationBarIndicatorToLabelPadding);
                const float contentHeight = float(icon.Height()) + vPad + toLabel + float(label.Height());
                const float contentVerticalPadding = std::max((float(c.minHeight) - contentHeight) / 2, vPad);
                const float height = contentHeight + contentVerticalPadding * 2;
                const float selectedIconY = contentVerticalPadding;
                const float unselectedIconY = m_AlwaysShowLabel ? selectedIconY : (height - float(icon.Height())) / 2;
                const float offset = (unselectedIconY - selectedIconY) * (1 - progress);
                const float labelY = selectedIconY + float(icon.Height()) + vPad + toLabel;
                const int width = ContainerWidth(s, icon, c);
                const int labelX = (width - label.Width()) / 2, iconX = (width - icon.Width()) / 2, rippleX = (width - ripple.Width()) / 2;
                const float rippleY = selectedIconY - vPad;
                const int indicatorY = ui::RoundJava(selectedIconY) - s.RoundToPx(IndicatorVerticalPadding);
                const bool showLabel = m_AlwaysShowLabel || progress != 0;
                return s.Layout(width, ui::RoundJava(height), [&, width, offset, labelX, iconX, rippleX, rippleY, indicatorY, labelY, selectedIconY,
                                                              showLabel](ui::PlacementScope& p) {
                    p.PlaceRelative(indicator, (width - indicator.Width()) / 2, ui::RoundJava(float(indicatorY) + offset));
                    if (showLabel) p.PlaceRelative(label, labelX, ui::RoundJava(labelY + offset));
                    p.PlaceRelative(icon, iconX, ui::RoundJava(selectedIconY + offset));
                    p.PlaceRelative(ripple, rippleX, ui::RoundJava(rippleY + offset));
                });
            }
            bool m_HasLabel, m_AlwaysShowLabel;
            std::shared_ptr<anim::Animatable> m_Size;
        };
    }

    NavigationBarItemColors NavigationBarItemColors::Copy(Color a, Color b, Color c, Color d, Color e, Color f, Color g) const {
        return { Or(a, selectedIconColor), Or(b, selectedTextColor), Or(c, selectedIndicatorColor), Or(d, unselectedIconColor),
                 Or(e, unselectedTextColor), Or(f, disabledIconColor), Or(g, disabledTextColor) };
    }
    Color NavigationBarItemColors::IconColor(bool selected, bool enabled) const {
        return !enabled ? disabledIconColor : selected ? selectedIconColor : unselectedIconColor;
    }
    Color NavigationBarItemColors::TextColor(bool selected, bool enabled) const {
        return !enabled ? disabledTextColor : selected ? selectedTextColor : unselectedTextColor;
    }

    namespace NavigationBarDefaults {
        Color containerColor() { return Value(NavigationBarTokens::ContainerColor); }
        WindowInsets windowInsets() { return ScaffoldDefaults::contentWindowInsets().Only(WindowInsetsSides::Horizontal() + WindowInsetsSides::Bottom()); }
    }

    namespace NavigationBarItemDefaults {
        NavigationBarItemColors colors() {
            const Color inactiveIcon = Value(NavigationBarTokens::ItemInactiveIconColor), inactiveLabel = Value(NavigationBarTokens::ItemInactiveLabelTextColor);
            return { Value(NavigationBarTokens::ItemActiveIconColor), Value(NavigationBarTokens::ItemActiveLabelTextColor),
                     Value(NavigationBarTokens::ItemActiveIndicatorColor), inactiveIcon, inactiveLabel,
                     inactiveIcon.WithAlpha(DisabledAlpha), inactiveLabel.WithAlpha(DisabledAlpha) };
        }
        NavigationBarItemColors colors(Color selectedIconColor, Color selectedTextColor, Color indicatorColor, Color unselectedIconColor,
                                       Color unselectedTextColor, Color disabledIconColor, Color disabledTextColor) {
            // The disabled colours default to the given unselected ones, faded.
            const NavigationBarItemColors base = colors();
            const Color unselectedIcon = Or(unselectedIconColor, base.unselectedIconColor), unselectedText = Or(unselectedTextColor, base.unselectedTextColor);
            return base.Copy(selectedIconColor, selectedTextColor, indicatorColor, unselectedIcon, unselectedText,
                             Or(disabledIconColor, unselectedIcon.WithAlpha(DisabledAlpha)), Or(disabledTextColor, unselectedText.WithAlpha(DisabledAlpha)));
        }
    }

    void NavigationBar(const ui::Modifier& modifier, const NavigationBarOptions& o, kiln::Function<void()> content) {
        const Color container = o.containerColor.value_or(NavigationBarDefaults::containerColor());
        SurfaceOptions surface;
        surface.color = container;
        surface.contentColor = o.contentColor.value_or(ContentColorFor(container));
        surface.tonalElevation = o.tonalElevation;
        const WindowInsets insets = o.windowInsets.value_or(NavigationBarDefaults::windowInsets());
        Surface(modifier, surface, [insets, content = std::move(content)] {
            Row(SelectableGroup(DefaultMinSize(WindowInsetsPadding(FillMax({}, Axis::Horizontal), insets), Dp::Unspecified(), NavigationBarHeight)),
                Arrangement::SpacedBy(internal::NavigationBarItemHorizontalPadding), AlignmentV::CenterVertically(), content);
        });
    }

    void NavigationBarItem(bool selected, kiln::Function<void()> onClick, kiln::Function<void()> icon, const ui::Modifier& modifier,
                           const NavigationBarItemOptions& o) {
        auto& c = *rt::Composition::Current();
        const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
        const NavigationBarItemColors colors = o.colors.value_or(NavigationBarItemDefaults::colors());
        const bool enabled = o.enabled, hasLabel = bool(o.label), alwaysShowLabel = o.alwaysShowLabel;
        const anim::SpecPtr colorSpec = Value(MotionSchemeKeyTokens::DefaultEffects);

        const auto styledIcon = [=] {
            const Color iconColor = AnimateColorAsState(colors.IconColor(selected, enabled), colorSpec);
            // With a label shown, accessibility services need not repeat the icon's description.
            const bool clearSemantics = hasLabel && (alwaysShowLabel || selected);
            Box(clearSemantics ? ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {}) : ui::Modifier{}, Alignment::TopStart(), false,
                [&] { rt::Composition::Current()->Provide(LocalContentColor, iconColor, icon); });
        };
        const auto label = o.label;
        const auto styledLabel = [=] {
            const Color textColor = AnimateColorAsState(colors.TextColor(selected, enabled), colorSpec);
            internal::ProvideContentColorTextStyle(textColor, Value(NavigationBarTokens::LabelTextFont), label);
        };

        auto& itemWidth = c.Remember<std::shared_ptr<rt::State<int>>>([] { return std::make_shared<rt::State<int>>(0); });
        const auto width = itemWidth;
        ClickableOptions click;
        click.enabled = enabled;
        click.role = ui::Role::Tab;
        click.interactionSource = source;
        click.indication = nullptr;
        click.useLocalIndication = false;
        ui::Modifier m = Selectable(modifier, selected, std::move(onClick), click);
        m = DefaultMinSize(std::move(m), Dp::Unspecified(), NavigationBarHeight);
        m = Weight(std::move(m), 1);
        m = OnSizeChanged(std::move(m), [width](ui::IntSize size) { width->Set(size.width); });
        Box(m, Alignment::Center(), true, [=] {
            auto& cc = *rt::Composition::Current();
            const auto alpha = internal::AnimatedProgress(selected, Value(MotionSchemeKeyTokens::DefaultEffects), rt::Key(1));
            const auto size = internal::AnimatedProgress(selected, Value(MotionSchemeKeyTokens::FastSpatial), rt::Key(2));
            // The whole item is selectable, but only the pill shows the ripple: presses are
            // mapped into the pill's coordinates.
            const ui::Density density = cc.Read(ui::LocalDensity);
            const auto mapped = internal::RememberMappedInteractionSource(source, [width, density] {
                const int indicatorWidth = density.RoundToPx(NavigationBarVerticalItemTokens::ActiveIndicatorWidth);
                return ui::Offset{ float(width->Peek() - indicatorWidth) / 2, density.ToPx(IndicatorVerticalOffset) };
            });
            const ShapePtr shape = Value(NavigationBarTokens::ItemActiveIndicatorShape);
            const Color indicatorColor = colors.IndicatorColor();
            foundation::Layout(internal::BadgeBounds({}), std::make_shared<ItemPolicy>(hasLabel, alwaysShowLabel, size), [=] {
                // The ripple is separate from the indicator, whose width animates.
                RippleOptions ripple;
                ripple.focusRingShape = shape;
                Box(Ripple(ripple)->Create(Clip(ui::Modifier{}, shape), mapped), Alignment::TopStart(), false, {});
                Box(Background(GraphicsLayer(ui::Modifier{}, [alpha](GraphicsLayerParams& p) { p.alpha = alpha->Float(); }), indicatorColor, shape),
                    Alignment::TopStart(), false, {});
                Box({}, Alignment::TopStart(), false, styledIcon);
                if (hasLabel)
                    Box(GraphicsLayer(ui::Modifier{}, [alpha, alwaysShowLabel](GraphicsLayerParams& p) { p.alpha = alwaysShowLabel ? 1.0f : alpha->Float(); }),
                        Alignment::TopStart(), false, styledLabel);
            });
        });
    }

}
