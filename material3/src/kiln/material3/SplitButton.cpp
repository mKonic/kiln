#include "kiln/material3/SplitButton.h"

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/InteractionShape.h"
#include "kiln/material3/tokens/ButtonSmallTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/SplitButtonLargeTokens.h"
#include "kiln/material3/tokens/SplitButtonMediumTokens.h"
#include "kiln/material3/tokens/SplitButtonSmallTokens.h"
#include "kiln/material3/tokens/SplitButtonXLargeTokens.h"
#include "kiln/material3/tokens/SplitButtonXSmallTokens.h"
#include "kiln/material3/tokens/StateTokens.h"

#include <array>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using namespace tokens;

    namespace {
        using Content = std::function<void()>;
        constexpr Dp ButtonMinWidth{ 48 };
        enum class Variant { Filled, Tonal, Outlined, Elevated };

        // The size bucket of the shapes (the midpoints between the button heights).
        int ShapeBucket(Dp h) {
            const float xs = ButtonDefaults::ExtraSmallContainerHeight.value, s = ButtonDefaults::MinHeight().value,
                        m = ButtonDefaults::MediumContainerHeight.value, l = ButtonDefaults::LargeContainerHeight.value,
                        xl = ButtonDefaults::ExtraLargeContainerHeight.value;
            return h.value <= (xs + s) / 2 ? 0 : h.value <= (s + m) / 2 ? 1 : h.value <= (m + l) / 2 ? 2 : h.value <= (l + xl) / 2 ? 3 : 4;
        }
        int SizeBucket(Dp h, Dp small) {
            using namespace SplitButtonDefaults;
            return h.value < small.value ? 0 : h.value < MediumContainerHeight.value ? 1 : h.value < LargeContainerHeight.value ? 2
                 : h.value < ExtraLargeContainerHeight.value ? 3 : 4;
        }
        // One instance per size, so the morph state keyed on the shapes survives recomposition.
        const std::array<SplitButtonShapes, 5>& ShapeSets(bool leading) {
            using namespace SplitButtonDefaults;
            static const auto make = [](bool lead) {
                const CornerSize inner[] = { ExtraSmallInnerCornerSize, SmallInnerCornerSize, MediumInnerCornerSize, LargeInnerCornerSize,
                                             ExtraLargeInnerCornerSize };
                const CornerSize pressed[] = { ExtraSmallInnerCornerSizePressed, SmallInnerCornerSizePressed, MediumInnerCornerSizePressed,
                                               LargeInnerCornerSizePressed, ExtraLargeInnerCornerSizePressed };
                const CornerSize outer = OuterCornerSize();
                const auto shape = [&](CornerSize in) { return lead ? RoundedCorner(outer, in, in, outer) : RoundedCorner(in, outer, outer, in); };
                std::array<SplitButtonShapes, 5> result;
                for (int i = 0; i < 5; ++i) result[i] = { shape(inner[i]), shape(pressed[i]), lead ? nullptr : CircleShape() };
                return result;
            };
            static const auto leadingShapes = make(true), trailingShapes = make(false);
            return leading ? leadingShapes : trailingShapes;
        }
        ButtonColors Colors(Variant v) {
            switch (v) {
                case Variant::Tonal: return ButtonDefaults::filledTonalButtonColors();
                case Variant::Outlined: return ButtonDefaults::outlinedButtonColors();
                case Variant::Elevated: return ButtonDefaults::elevatedButtonColors();
                default: return ButtonDefaults::buttonColors();
            }
        }
        std::optional<ButtonElevation> Elevation(Variant v) {
            switch (v) {
                case Variant::Tonal: return ButtonDefaults::filledTonalButtonElevation();
                case Variant::Outlined: return std::nullopt;
                case Variant::Elevated: return ButtonDefaults::elevatedButtonElevation();
                default: return ButtonDefaults::buttonElevation();
            }
        }

        struct Action {
            Content onClick;                          // the plain form
            std::optional<bool> checked;              // the toggleable trailing form
            std::function<void(bool)> onCheckedChange;
        };
        void Impl(bool leading, Action action, const Modifier& modifier, const SplitButtonOptions& o, Content content, Variant variant) {
            using namespace SplitButtonDefaults;
            auto& c = *rt::Composition::Current();
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const bool checked = action.checked.value_or(false);
            const auto shapes = o.shapes ? *o.shapes : leading ? leadingButtonShapesFor(SmallContainerHeight) : trailingButtonShapesFor(SmallContainerHeight);
            const ShapePtr shape = internal::ShapeByInteraction(shapes.shape, shapes.pressedShape ? shapes.pressedShape : shapes.shape,
                                                                shapes.checkedShape ? shapes.checkedShape : shapes.shape, source->IsPressed(), checked,
                                                                Value(MotionSchemeKeyTokens::DefaultEffects));
            const auto colors = o.colors ? *o.colors : Colors(variant);
            const auto elevation = o.elevation ? *o.elevation : Elevation(variant);
            const Color contentColor = colors.ContentColor(o.enabled);
            SurfaceOptions surface;
            surface.shape = shape;
            surface.color = colors.ContainerColor(o.enabled);
            surface.contentColor = contentColor;
            surface.shadowElevation = elevation ? ShadowElevation(*elevation, o.enabled, source) : Dp(0);
            surface.border = o.border ? *o.border : variant == Variant::Outlined ? std::optional(ButtonDefaults::outlinedButtonBorder(o.enabled)) : std::nullopt;
            const auto padding = o.contentPadding ? *o.contentPadding
                : leading ? leadingButtonContentPaddingFor(SmallContainerHeight) : trailingButtonContentPaddingFor(SmallContainerHeight);

            Modifier row = DefaultMinSize({}, ButtonMinWidth, SmallContainerHeight);
            if (!leading) {
                const bool swap = !padding.rtlAware && c.Read(LocalLayoutDirection) == LayoutDirection::Rtl;
                const Dp start = swap ? padding.end : padding.start, end = swap ? padding.start : padding.end;
                if (auto optical = std::dynamic_pointer_cast<const internal::ShapeWithHorizontalCenterOptically>(shape))
                    row = internal::HorizontalCenterOptically(std::move(row), std::move(optical), start, end);
                else if (auto corners = std::dynamic_pointer_cast<const CornerBasedShape>(shape))
                    row = internal::HorizontalCenterOptically(std::move(row), std::move(corners), start, end);
            }
            row = Padding(std::move(row), padding);
            const auto body = [contentColor, row, content = std::move(content)] {
                internal::ProvideContentColorTextStyle(contentColor, MaterialTheme::typography().labelLarge, [row, content] {
                    Row(row, Arrangement::Center(), AlignmentV::CenterVertically(), content);
                });
            };
            Modifier m = modifier;
            // A checked trailing button keeps the pressed state layer over its container.
            if (action.checked) m = DrawWithContent(std::move(m), [checked, shape, contentColor](ContentDrawScope& s) {
                s.DrawContent();
                if (checked) DrawOutline(s, shape->CreateOutline(s.GetSize(), s.Direction(), s),
                                         contentColor.WithAlpha(contentColor.Alpha() * StateTokens::PressedStateLayerOpacity));
            });
            m = Semantics(std::move(m), [](SemanticsConfiguration& config) { config.role = Role::Button; });
            if (action.checked) Surface(checked, std::move(action.onCheckedChange), m, surface, body, o.enabled, source);
            else Surface(std::move(action.onClick), m, surface, body, o.enabled, source);
        }
    }

    namespace SplitButtonDefaults {
        const Dp LeadingIconSize = ButtonSmallTokens::IconSize, TrailingIconSize = SplitButtonSmallTokens::TrailingIconSize,
                 Spacing = SplitButtonSmallTokens::BetweenSpace;
        const CornerSize ExtraSmallInnerCornerSize = SplitButtonXSmallTokens::InnerCornerCornerSize,
                         SmallInnerCornerSize = SplitButtonSmallTokens::InnerCornerCornerSize,
                         MediumInnerCornerSize = SplitButtonMediumTokens::InnerCornerCornerSize,
                         LargeInnerCornerSize = SplitButtonLargeTokens::InnerCornerCornerSize,
                         ExtraLargeInnerCornerSize = SplitButtonXLargeTokens::InnerCornerCornerSize;
        const CornerSize ExtraSmallInnerCornerSizePressed = SplitButtonXSmallTokens::InnerPressedCornerCornerSize,
                         SmallInnerCornerSizePressed = SplitButtonSmallTokens::InnerPressedCornerCornerSize,
                         MediumInnerCornerSizePressed = SplitButtonMediumTokens::InnerPressedCornerCornerSize,
                         LargeInnerCornerSizePressed = SplitButtonLargeTokens::InnerPressedCornerCornerSize,
                         ExtraLargeInnerCornerSizePressed = SplitButtonXLargeTokens::InnerPressedCornerCornerSize;
        CornerSize OuterCornerSize() { return ShapeDefaults::CornerFull(); }
#define KILN_SPLIT_PADDING(tokens, side) PaddingValues{ tokens::side##ButtonLeadingSpace, Dp(0), tokens::side##ButtonTrailingSpace, Dp(0) }
        const PaddingValues ExtraSmallLeadingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonXSmallTokens, Leading),
                            SmallLeadingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonSmallTokens, Leading),
                            MediumLeadingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonMediumTokens, Leading),
                            LargeLeadingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonLargeTokens, Leading),
                            ExtraLargeLeadingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonXLargeTokens, Leading);
        const PaddingValues ExtraSmallTrailingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonXSmallTokens, Trailing),
                            SmallTrailingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonSmallTokens, Trailing),
                            MediumTrailingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonMediumTokens, Trailing),
                            LargeTrailingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonLargeTokens, Trailing),
                            ExtraLargeTrailingButtonContentPadding = KILN_SPLIT_PADDING(SplitButtonXLargeTokens, Trailing);
#undef KILN_SPLIT_PADDING
        const Dp ExtraSmallTrailingButtonIconSize = SplitButtonXSmallTokens::TrailingIconSize,
                 SmallTrailingButtonIconSize = SplitButtonSmallTokens::TrailingIconSize,
                 MediumTrailingButtonIconSize = SplitButtonMediumTokens::TrailingIconSize,
                 LargeTrailingButtonIconSize = SplitButtonLargeTokens::TrailingIconSize,
                 ExtraLargeTrailingButtonIconSize = SplitButtonXLargeTokens::TrailingIconSize;
        const Dp ExtraSmallContainerHeight = SplitButtonXSmallTokens::ContainerHeight, SmallContainerHeight = SplitButtonSmallTokens::ContainerHeight,
                 MediumContainerHeight = SplitButtonMediumTokens::ContainerHeight, LargeContainerHeight = SplitButtonLargeTokens::ContainerHeight,
                 ExtraLargeContainerHeight = SplitButtonXLargeTokens::ContainerHeight;

        SplitButtonShapes leadingButtonShapesFor(Dp h) { return ShapeSets(true)[ShapeBucket(h)]; }
        SplitButtonShapes trailingButtonShapesFor(Dp h) { return ShapeSets(false)[ShapeBucket(h)]; }
        PaddingValues leadingButtonContentPaddingFor(Dp h) {
            const PaddingValues* values[] = { &ExtraSmallLeadingButtonContentPadding, &SmallLeadingButtonContentPadding, &MediumLeadingButtonContentPadding,
                                              &LargeLeadingButtonContentPadding, &ExtraLargeLeadingButtonContentPadding };
            return *values[SizeBucket(h, SmallContainerHeight)];
        }
        PaddingValues trailingButtonContentPaddingFor(Dp h) {
            const PaddingValues* values[] = { &ExtraSmallTrailingButtonContentPadding, &SmallTrailingButtonContentPadding,
                                              &MediumTrailingButtonContentPadding, &LargeTrailingButtonContentPadding,
                                              &ExtraLargeTrailingButtonContentPadding };
            return *values[SizeBucket(h, SmallContainerHeight)];
        }
        Dp leadingButtonIconSizeFor(Dp h) { return ButtonDefaults::iconSizeFor(h); }
        Dp trailingButtonIconSizeFor(Dp h) {
            const Dp values[] = { ExtraSmallTrailingButtonIconSize, SmallTrailingButtonIconSize, MediumTrailingButtonIconSize, LargeTrailingButtonIconSize,
                                  ExtraLargeTrailingButtonIconSize };
            const float s = ButtonDefaults::MinHeight().value, m = ButtonDefaults::MediumContainerHeight.value,
                        l = ButtonDefaults::LargeContainerHeight.value, xl = ButtonDefaults::ExtraLargeContainerHeight.value;
            return values[h.value < s ? 0 : h.value < m ? 1 : h.value < l ? 2 : h.value < xl ? 3 : 4];
        }

        void LeadingButton(Content onClick, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(true, { std::move(onClick) }, m, o, std::move(content), Variant::Filled);
        }
        void TrailingButton(Content onClick, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(false, { std::move(onClick) }, m, o, std::move(content), Variant::Filled);
        }
        void TrailingButton(bool checked, std::function<void(bool)> change, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(false, { {}, checked, std::move(change) }, m, o, std::move(content), Variant::Filled);
        }
        void TonalLeadingButton(Content onClick, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(true, { std::move(onClick) }, m, o, std::move(content), Variant::Tonal);
        }
        void TonalTrailingButton(bool checked, std::function<void(bool)> change, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(false, { {}, checked, std::move(change) }, m, o, std::move(content), Variant::Tonal);
        }
        void OutlinedLeadingButton(Content onClick, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(true, { std::move(onClick) }, m, o, std::move(content), Variant::Outlined);
        }
        void OutlinedTrailingButton(bool checked, std::function<void(bool)> change, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(false, { {}, checked, std::move(change) }, m, o, std::move(content), Variant::Outlined);
        }
        void ElevatedLeadingButton(Content onClick, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(true, { std::move(onClick) }, m, o, std::move(content), Variant::Elevated);
        }
        void ElevatedTrailingButton(bool checked, std::function<void(bool)> change, const Modifier& m, const SplitButtonOptions& o, Content content) {
            Impl(false, { {}, checked, std::move(change) }, m, o, std::move(content), Variant::Elevated);
        }
    }

    void SplitButtonLayout(Content leadingButton, Content trailingButton, const Modifier& modifier, Dp spacing) {
        const auto policy = std::make_shared<LambdaMeasurePolicy>([spacing](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
            Measurable& leading = *children[0];
            Measurable& trailing = *children[1];
            const int space = s.RoundToPx(spacing);
            // The trailing button's wish gives the width left for the leading one.
            const int trailingWidth = trailing.MaxIntrinsicWidth(c.maxHeight);
            const int leadingWidth = c.maxWidth == Constraints::Infinity ? c.maxWidth : std::max(0, c.maxWidth - (trailingWidth + space));
            const int height = c.ConstrainHeight(std::max(leading.MaxIntrinsicHeight(leadingWidth), trailing.MaxIntrinsicHeight(trailingWidth)));
            // Trailing first, so it has priority for width; both get the same height.
            Constraints forTrailing = c;
            forTrailing.minWidth = 0; forTrailing.minHeight = forTrailing.maxHeight = height;
            Placeable* t = &trailing.Measure(forTrailing);
            Constraints forLeading = c.Offset(-(t->Width() + space), 0);
            forLeading.minWidth = 0; forLeading.minHeight = forLeading.maxHeight = height;
            Placeable* l = &leading.Measure(forLeading);
            const int width = c.ConstrainWidth(l->Width() + t->Width() + space);
            const int laidOut = c.ConstrainHeight(std::max(l->Height(), t->Height()));
            return s.Layout(width, laidOut, [l, t, space, laidOut](PlacementScope& place) {
                place.PlaceRelative(*l, 0, (laidOut - l->Height()) / 2);
                place.PlaceRelative(*t, l->Width() + space, (laidOut - t->Height()) / 2);
            });
        });
        Layout(MinimumInteractiveComponentSize(modifier), policy, [leadingButton = std::move(leadingButton), trailingButton = std::move(trailingButton)] {
            // The minimum touch size is enforced on the whole layout instead of on each button.
            rt::Composition::Current()->Provide(LocalMinimumInteractiveComponentSize, Dp(0), [=] {
                Box({}, Alignment::Center(), true, leadingButton);
                Box({}, Alignment::Center(), true, trailingButton);
            });
        });
    }

}
