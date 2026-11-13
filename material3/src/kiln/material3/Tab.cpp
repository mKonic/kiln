#include "kiln/material3/Tab.h"
#include <kiln/Function.h>

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/PrimaryNavigationTabTokens.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    using internal::HorizontalTextPadding;

    namespace {
        constexpr Dp SmallTabHeight = PrimaryNavigationTabTokens::ContainerHeight, LargeTabHeight{ 72 };
        // From the top of the indicator to the (last) text baseline, with an icon.
        constexpr Dp SingleLineTextBaselineWithIcon{ 14 }, DoubleLineTextBaselineWithIcon{ 6 };
        constexpr float IconDistanceFromBaselineSp = 20;   // from the first baseline to the icon's bottom
        constexpr Dp TextDistanceFromLeadingIcon{ 8 };

        // TabTransition: the content colour, faded in (DefaultEffects) or out (FastEffects).
        void TabTransition(Color activeColor, Color inactiveColor, bool selected, kiln::Function<void()> content) {
            const Color color = AnimateColorAsState(selected ? activeColor : inactiveColor,
                                                    Value(selected ? MotionSchemeKeyTokens::DefaultEffects : MotionSchemeKeyTokens::FastEffects));
            rt::Composition::Current()->Provide(LocalContentColor, color, std::move(content));
        }

        ClickableOptions Selection(const TabOptions& o, Color selectedContentColor) {
            // The ripple always takes the selected colour: it shows before the tab is selected.
            RippleOptions ripple;
            ripple.bounded = true;
            ripple.color = selectedContentColor;
            ClickableOptions click;
            click.enabled = o.enabled;
            click.role = ui::Role::Tab;
            click.interactionSource = o.interactionSource;
            click.indication = Ripple(ripple);
            click.useLocalIndication = false;
            return click;
        }

        // TabBaselineLayout: text (padded 16 dp either side) and/or an icon, on their baselines.
        class BaselinePolicy final : public ui::MeasurePolicy {
        public:
            BaselinePolicy(bool hasText, bool hasIcon) : m_HasText(hasText), m_HasIcon(hasIcon) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                std::size_t i = 0;
                // The text measured with a loose height, so it takes only what it needs.
                ui::Placeable* text = m_HasText ? &children[i++]->Measure(ui::Constraints{ c.minWidth, c.maxWidth, 0, c.maxHeight }) : nullptr;
                ui::Placeable* icon = m_HasIcon ? &children[i++]->Measure(c) : nullptr;
                const int tabWidth = std::max(text ? text->Width() : 0, icon ? icon->Width() : 0);
                const int specHeight = s.RoundToPx(text && icon ? LargeTabHeight : SmallTabHeight);
                const int iconDistance = ui::RoundJava(s.SpToPx(IconDistanceFromBaselineSp));
                const int tabHeight = std::max(specHeight, (icon ? icon->Height() : 0) + (text ? text->Height() : 0) + iconDistance);
                const int firstBaseline = text ? text->Get(ui::FirstBaseline) : 0, lastBaseline = text ? text->Get(ui::LastBaseline) : 0;
                const ui::Density d = s;
                return s.Layout(tabWidth, tabHeight, [=](ui::PlacementScope& p) {
                    if (text && icon) {
                        const Dp baselineOffset = firstBaseline == lastBaseline ? SingleLineTextBaselineWithIcon : DoubleLineTextBaselineWithIcon;
                        // From the last baseline to the bottom of the tab.
                        const int textOffset = d.RoundToPx(baselineOffset) + d.RoundToPx(PrimaryNavigationTabTokens::ActiveIndicatorHeight);
                        // From the top of the icon to the top of the text's box.
                        const int iconOffset = icon->Height() + iconDistance - firstBaseline;
                        const int textY = tabHeight - lastBaseline - textOffset;
                        p.PlaceRelative(*text, (tabWidth - text->Width()) / 2, textY);
                        p.PlaceRelative(*icon, (tabWidth - icon->Width()) / 2, textY - iconOffset);
                    } else if (text || icon) {
                        ui::Placeable& only = text ? *text : *icon;
                        p.PlaceRelative(only, 0, (tabHeight - only.Height()) / 2);
                    }
                });
            }

            // Intrinsics without a simulated measure pass (TabBaselineLayoutMeasurePolicy).
            int MinIntrinsicWidth(ui::MeasureScope&, std::span<ui::Measurable* const> children, int height) override {
                return Widths(children, height, false);
            }
            int MaxIntrinsicWidth(ui::MeasureScope&, std::span<ui::Measurable* const> children, int height) override {
                return Widths(children, height, true);
            }
            int MinIntrinsicHeight(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int width) override {
                return Height(s, children, width, false);
            }
            int MaxIntrinsicHeight(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int width) override {
                return Height(s, children, width, true);
            }

        private:
            ui::Measurable* Text(std::span<ui::Measurable* const> c) const { return m_HasText ? c[0] : nullptr; }
            ui::Measurable* Icon(std::span<ui::Measurable* const> c) const { return m_HasIcon ? c[m_HasText ? 1 : 0] : nullptr; }
            int Widths(std::span<ui::Measurable* const> c, int height, bool max) const {
                auto w = [&](ui::Measurable* m) { return m ? (max ? m->MaxIntrinsicWidth(height) : m->MinIntrinsicWidth(height)) : 0; };
                return std::max(w(Text(c)), w(Icon(c)));
            }
            int Height(ui::MeasureScope& s, std::span<ui::Measurable* const> c, int width, bool max) const {
                auto h = [&](ui::Measurable* m) { return m ? (max ? m->MaxIntrinsicHeight(width) : m->MinIntrinsicHeight(width)) : 0; };
                const int specHeight = s.RoundToPx(Text(c) && Icon(c) ? LargeTabHeight : SmallTabHeight);
                return std::max(specHeight, h(Icon(c)) + h(Text(c)) + ui::RoundJava(s.SpToPx(IconDistanceFromBaselineSp)));
            }
            bool m_HasText, m_HasIcon;
        };
    }

    void Tab(bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier, const TabOptions& o, kiln::Function<void()> content) {
        const Color selectedColor = o.selectedContentColor.value_or(rt::Composition::Current()->Read(LocalContentColor));
        const Color unselectedColor = o.unselectedContentColor.value_or(selectedColor);
        const ClickableOptions click = Selection(o, selectedColor);
        TabTransition(selectedColor, unselectedColor, selected, [=] {
            Column(FillMax(Selectable(modifier, selected, onClick, click), Axis::Horizontal), Arrangement::Center(), AlignmentH::CenterHorizontally(), content);
        });
    }

    void Tab(bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier, kiln::Function<void()> text, kiln::Function<void()> icon,
             const TabOptions& o) {
        kiln::Function<void()> styledText;
        if (text) {
            styledText = [text] {
                text::TextStyle style = Value(PrimaryNavigationTabTokens::LabelTextFont);
                style.WithAlign(text::TextAlign::Center);
                ProvideTextStyle(style, text);
            };
        }
        const bool hasText = bool(text), hasIcon = bool(icon);
        Tab(selected, std::move(onClick), internal::BadgeBounds(modifier), o, [=] {
            foundation::Layout({}, std::make_shared<BaselinePolicy>(hasText, hasIcon), [=] {
                if (hasText) Box(Padding({}, HorizontalTextPadding, Dp(0)), Alignment::TopStart(), false, styledText);
                if (hasIcon) Box({}, Alignment::TopStart(), false, icon);
            });
        });
    }

    void LeadingIconTab(bool selected, kiln::Function<void()> onClick, kiln::Function<void()> text, kiln::Function<void()> icon,
                        const ui::Modifier& modifier, const TabOptions& o) {
        const Color selectedColor = o.selectedContentColor.value_or(rt::Composition::Current()->Read(LocalContentColor));
        const Color unselectedColor = o.unselectedContentColor.value_or(selectedColor);
        const ClickableOptions click = Selection(o, selectedColor);
        TabTransition(selectedColor, unselectedColor, selected, [=] {
            ui::Modifier m = Height(modifier, SmallTabHeight);
            m = Selectable(std::move(m), selected, onClick, click);
            m = FillMax(Padding(std::move(m), HorizontalTextPadding, Dp(0)), Axis::Horizontal);
            Row(m, Arrangement::Center(), AlignmentV::CenterVertically(), [=] {
                icon();
                Spacer(SizeIn({}, TextDistanceFromLeadingIcon, Dp::Unspecified(), TextDistanceFromLeadingIcon, Dp::Unspecified(), false));
                text::TextStyle style = Value(PrimaryNavigationTabTokens::LabelTextFont);
                style.WithAlign(text::TextAlign::Center);
                ProvideTextStyle(style, text);
            });
        });
    }

}
