#include "kiln/material3/Button.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/Helpers.h"
#include "kiln/material3/internal/PrecisionPointer.h"
#include "kiln/material3/tokens/BaselineButtonTokens.h"
#include "kiln/material3/tokens/ButtonLargeTokens.h"
#include "kiln/material3/tokens/ButtonMediumTokens.h"
#include "kiln/material3/tokens/ButtonSmallTokens.h"
#include "kiln/material3/tokens/ButtonXLargeTokens.h"
#include "kiln/material3/tokens/ButtonXSmallTokens.h"
#include "kiln/material3/tokens/ElevatedButtonTokens.h"
#include "kiln/material3/tokens/FilledButtonTokens.h"
#include "kiln/material3/tokens/FilledTonalButtonTokens.h"
#include "kiln/material3/tokens/OutlinedButtonTokens.h"
#include "kiln/material3/tokens/TextButtonTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

namespace kiln::material3 {

    using namespace tokens;
    using foundation::Interaction;
    using foundation::ShapePtr;

    namespace {
        rt::Composition& Comp() { return *rt::Composition::Current(); }
        Color Take(Color c, Color fallback) { return c.Specified() ? c : fallback; }
        Dp Mid(Dp a, Dp b) { return Dp((a.value + b.value) / 2); }
    }

    ButtonColors ButtonColors::Copy(Color container, Color content, Color disabledContainer, Color disabledContent) const {
        return { Take(container, containerColor), Take(content, contentColor), Take(disabledContainer, disabledContainerColor),
                 Take(disabledContent, disabledContentColor) };
    }

    // --- elevation -------------------------------------------------------------------------------

    namespace {
        struct Entry { Interaction::Kind kind; std::uint64_t id; bool operator==(const Entry&) const = default; };
        using Entries = std::vector<Entry>;

        rt::Task<> AnimateElevationTo(std::shared_ptr<anim::Animatable> a, ButtonElevation e, bool enabled, Dp target,
                                      std::optional<Interaction::Kind> interaction) {
            if (a->Target()[0] == target.value) co_return;
            if (!enabled) {
                a->SnapTo(target.value);
                co_return;
            }
            // The interaction the previous target belonged to.
            const float previous = a->Target()[0];
            std::optional<Interaction::Kind> from;
            if (previous == e.pressedElevation.value) from = Interaction::Kind::Press;
            else if (previous == e.hoveredElevation.value) from = Interaction::Kind::Hover;
            else if (previous == e.focusedElevation.value) from = Interaction::Kind::Focus;
            co_await internal::AnimateElevation(*a, target, from, interaction);
        }
    }

    Dp ShadowElevation(const ButtonElevation& e, bool enabled, const std::shared_ptr<foundation::MutableInteractionSource>& source) {
        auto& c = Comp();
        auto& interactions = c.RememberState(Entries{});
        auto* state = &interactions;
        c.DisposableEffect(rt::Keys(std::hash<const void*>{}(source.get())), [source, state]() -> std::function<void()> {
            if (!source) return {};
            const std::uint64_t token = source->Subscribe([state](const Interaction& i) {
                if (i.kind != Interaction::Kind::Press && i.kind != Interaction::Kind::Hover && i.kind != Interaction::Kind::Focus) return;
                Entries list = state->Peek();
                if (i.phase == Interaction::Phase::Start) list.push_back({ i.kind, i.id });
                else std::erase_if(list, [&](const Entry& x) { return x.id == i.id; });
                state->Set(std::move(list));
            });
            return [source, token] { source->Unsubscribe(token); };
        });
        const Entries& list = interactions.Get();
        const std::optional<Interaction::Kind> interaction = list.empty() ? std::nullopt : std::optional(list.back().kind);
        Dp target = e.defaultElevation;
        if (!enabled) target = e.disabledElevation;
        else if (interaction == Interaction::Kind::Press) target = e.pressedElevation;
        else if (interaction == Interaction::Kind::Hover) target = e.hoveredElevation;
        else if (interaction == Interaction::Kind::Focus) target = e.focusedElevation;
        auto& animatable = c.Remember<std::shared_ptr<anim::Animatable>>([&] { return std::make_shared<anim::Animatable>(target.value); });
        auto a = animatable;
        c.LaunchedEffect(rt::Keys(target.value), [a, e, enabled, target, interaction] { return AnimateElevationTo(a, e, enabled, target, interaction); });
        return Dp(animatable->Float());
    }

    // --- shapes ----------------------------------------------------------------------------------

    namespace internal {
        ShapePtr ShapeByInteraction(const ButtonShapes& shapes, bool pressed, anim::SpecPtr spec) {
            const ShapePtr& shape = pressed ? shapes.pressedShape : shapes.shape;
            auto rest = std::dynamic_pointer_cast<const foundation::CornerBasedShape>(shapes.shape);
            auto press = std::dynamic_pointer_cast<const foundation::CornerBasedShape>(shapes.pressedShape);
            if (!rest || !press) return shape;
            // key(shapes): a new pair of shapes starts a new animation.
            const std::size_t key = std::hash<const void*>{}(shapes.shape.get()) * 31 + std::hash<const void*>{}(shapes.pressedShape.get());
            return RememberAnimatedShape(std::dynamic_pointer_cast<const foundation::CornerBasedShape>(shape), spec, key);
        }
    }

    // --- Button ----------------------------------------------------------------------------------

    namespace {
        struct Variant {
            ButtonColors (*colors)();
            std::optional<ButtonElevation> (*elevation)();
            ShapePtr (*shape)();
            std::optional<foundation::BorderStroke> (*border)(bool enabled);
            PaddingValues padding;
        };

        void ButtonImpl(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& o, const Variant& v,
                        std::function<void()> content) {
            auto& c = Comp();
            auto source = o.interactionSource;
            if (!source) source = c.Remember<std::shared_ptr<foundation::MutableInteractionSource>>(
                [] { return std::make_shared<foundation::MutableInteractionSource>(); });
            const ButtonColors colors = o.colors ? *o.colors : v.colors();
            const std::optional<ButtonElevation> elevation = o.elevation ? *o.elevation : v.elevation();
            const std::optional<foundation::BorderStroke> border = o.border ? *o.border : (v.border ? v.border(o.enabled) : std::nullopt);
            const Color containerColor = colors.ContainerColor(o.enabled), contentColor = colors.ContentColor(o.enabled);
            const Dp shadow = elevation ? ShadowElevation(*elevation, o.enabled, source) : Dp(0);
            ShapePtr shape;
            PaddingValues padding;
            if (o.shapes) {
                // The Expressive form: the shape follows the press with DefaultEffects.
                const bool pressed = source->IsPressed();
                shape = internal::ShapeByInteraction(*o.shapes, pressed, Value(MotionSchemeKeyTokens::DefaultEffects));
                padding = o.contentPadding ? *o.contentPadding : ButtonDefaults::contentPaddingFor(ButtonDefaults::MinHeight());
            } else {
                shape = o.shape ? o.shape : v.shape();
                padding = o.contentPadding ? *o.contentPadding : v.padding;
            }
            SurfaceOptions s;
            s.shape = shape;
            s.color = containerColor;
            s.contentColor = contentColor;
            s.shadowElevation = shadow;
            s.border = border;
            const ui::Modifier m = foundation::Semantics(modifier, [](ui::SemanticsConfiguration& sc) { sc.role = ui::Role::Button; });
            const text::TextStyle label = MaterialTheme::typography().labelLarge;
            Surface(std::move(onClick), m, s, [contentColor, label, padding, content = std::move(content)] {
                internal::ProvideContentColorTextStyle(contentColor, label, [padding, content] {
                    foundation::Row(foundation::Padding(foundation::DefaultMinSize({}, ButtonDefaults::MinWidth, ButtonDefaults::MinHeight()), padding),
                                    foundation::Arrangement::Center(), foundation::AlignmentV::CenterVertically(), content);
                });
            }, o.enabled, source);
        }

        std::optional<ButtonElevation> FilledElevation() { return ButtonDefaults::buttonElevation(); }
        std::optional<ButtonElevation> ElevatedElevation() { return ButtonDefaults::elevatedButtonElevation(); }
        std::optional<ButtonElevation> TonalElevation() { return ButtonDefaults::filledTonalButtonElevation(); }
        std::optional<ButtonElevation> NoElevation() { return std::nullopt; }
        std::optional<foundation::BorderStroke> OutlinedBorder(bool enabled) { return ButtonDefaults::outlinedButtonBorder(enabled); }
    }

    void Button(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& o, std::function<void()> content) {
        ButtonImpl(std::move(onClick), modifier, o, { ButtonDefaults::buttonColors, FilledElevation, ButtonDefaults::shape, nullptr, ButtonDefaults::ContentPadding },
                   std::move(content));
    }
    void ElevatedButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& o, std::function<void()> content) {
        ButtonImpl(std::move(onClick), modifier, o,
                   { ButtonDefaults::elevatedButtonColors, ElevatedElevation, ButtonDefaults::elevatedShape, nullptr, ButtonDefaults::ContentPadding },
                   std::move(content));
    }
    void FilledTonalButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& o, std::function<void()> content) {
        ButtonImpl(std::move(onClick), modifier, o,
                   { ButtonDefaults::filledTonalButtonColors, TonalElevation, ButtonDefaults::filledTonalShape, nullptr, ButtonDefaults::ContentPadding },
                   std::move(content));
    }
    void OutlinedButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& o, std::function<void()> content) {
        ButtonImpl(std::move(onClick), modifier, o,
                   { ButtonDefaults::outlinedButtonColors, NoElevation, ButtonDefaults::outlinedShape, OutlinedBorder, ButtonDefaults::ContentPadding },
                   std::move(content));
    }
    void TextButton(std::function<void()> onClick, const ui::Modifier& modifier, const ButtonOptions& o, std::function<void()> content) {
        ButtonImpl(std::move(onClick), modifier, o,
                   { ButtonDefaults::textButtonColors, NoElevation, ButtonDefaults::textShape, nullptr, ButtonDefaults::TextButtonContentPadding },
                   std::move(content));
    }

    // --- ButtonDefaults --------------------------------------------------------------------------

    namespace ButtonDefaults {
        namespace {
            const Dp ButtonVerticalPadding{ 8 }, TextButtonHorizontalPadding{ 12 };
            bool PrecisionPointer() { return internal::ShouldUsePrecisionPointerComponentSizing(); }
            Dp SmallVerticalPadding() { return PrecisionPointer() ? Dp(8) : Dp(10); }
            Dp IconSmallHorizontalPadding() { return PrecisionPointer() ? Dp(12) : ButtonSmallTokens::LeadingSpace; }
            PaddingValues SmallPadding(bool start, bool end) {
                return { start ? IconSmallHorizontalPadding() : ButtonSmallTokens::LeadingSpace, SmallVerticalPadding(),
                         end ? IconSmallHorizontalPadding() : ButtonSmallTokens::TrailingSpace, SmallVerticalPadding() };
            }
            PaddingValues MediumPadding(bool, bool) {
                return { ButtonMediumTokens::LeadingSpace, Dp(16), ButtonMediumTokens::TrailingSpace, Dp(16) };
            }
            PaddingValues LargePadding(bool, bool) {
                return { ButtonLargeTokens::LeadingSpace, Dp(32), ButtonLargeTokens::TrailingSpace, Dp(32) };
            }
        }

        const PaddingValues ContentPadding{ BaselineButtonTokens::LeadingSpace, ButtonVerticalPadding, BaselineButtonTokens::TrailingSpace, ButtonVerticalPadding };
        const PaddingValues ButtonWithIconContentPadding{ Dp(16), ButtonVerticalPadding, BaselineButtonTokens::TrailingSpace, ButtonVerticalPadding };
        const PaddingValues TextButtonContentPadding{ TextButtonHorizontalPadding, ButtonVerticalPadding, TextButtonHorizontalPadding, ButtonVerticalPadding };
        const PaddingValues TextButtonWithIconContentPadding{ TextButtonHorizontalPadding, ButtonVerticalPadding, Dp(16), ButtonVerticalPadding };
        PaddingValues SmallContentPadding() { return SmallPadding(false, false); }
        PaddingValues ExtraSmallContentPadding() { return { Dp(12), Dp(6), Dp(12), Dp(6) }; }
        PaddingValues MediumContentPadding() { return MediumPadding(false, false); }
        PaddingValues LargeContentPadding() { return LargePadding(false, false); }
        PaddingValues ExtraLargeContentPadding() { return { ButtonXLargeTokens::LeadingSpace, Dp(48), ButtonXLargeTokens::TrailingSpace, Dp(48) }; }

        Dp MinHeight() { return PrecisionPointer() ? Dp(36) : ButtonSmallTokens::ContainerHeight; }
        const Dp ExtraSmallContainerHeight = ButtonXSmallTokens::ContainerHeight, MediumContainerHeight = ButtonMediumTokens::ContainerHeight,
                 LargeContainerHeight = ButtonLargeTokens::ContainerHeight, ExtraLargeContainerHeight = ButtonXLargeTokens::ContainerHeight;
        const Dp ExtraSmallIconSize = ButtonXSmallTokens::IconSize, SmallIconSize = ButtonSmallTokens::IconSize, MediumIconSize = ButtonMediumTokens::IconSize,
                 LargeIconSize = ButtonLargeTokens::IconSize, ExtraLargeIconSize = ButtonXLargeTokens::IconSize;
        const Dp IconSpacing = ButtonSmallTokens::IconLabelSpace, ExtraSmallIconSpacing{ 4 }, MediumIconSpacing = ButtonMediumTokens::IconLabelSpace,
                 LargeIconSpacing = ButtonLargeTokens::IconLabelSpace, ExtraLargeIconSpacing = ButtonXLargeTokens::IconLabelSpace;

        ShapePtr squareShape() { return Value(ButtonSmallTokens::ContainerShapeSquare); }
        ShapePtr pressedShape() { return Value(ButtonSmallTokens::PressedContainerShape); }
        ShapePtr extraSmallPressedShape() { return Value(ButtonXSmallTokens::PressedContainerShape); }
        ShapePtr mediumPressedShape() { return Value(ButtonMediumTokens::PressedContainerShape); }
        ShapePtr largePressedShape() { return Value(ButtonLargeTokens::PressedContainerShape); }
        ShapePtr extraLargePressedShape() { return Value(ButtonXLargeTokens::PressedContainerShape); }
        ShapePtr shape() { return Value(ButtonSmallTokens::ContainerShapeRound); }
        ShapePtr elevatedShape() { return shape(); }
        ShapePtr filledTonalShape() { return shape(); }
        ShapePtr outlinedShape() { return shape(); }
        ShapePtr textShape() { return shape(); }
        ButtonShapes shapes(ShapePtr s, ShapePtr pressed) {
            const ButtonShapes defaults{ FromToken(MaterialTheme::shapes(), ButtonSmallTokens::ContainerShapeRound),
                                         FromToken(MaterialTheme::shapes(), ButtonSmallTokens::PressedContainerShape) };
            return defaults.Copy(std::move(s), std::move(pressed));
        }

        ButtonColors buttonColors() {
            const ColorScheme& cs = MaterialTheme::colorScheme();
            return { FromToken(cs, FilledButtonTokens::ContainerColor), FromToken(cs, FilledButtonTokens::LabelTextColor),
                     FromToken(cs, FilledButtonTokens::DisabledContainerColor).WithAlpha(FilledButtonTokens::DisabledContainerOpacity),
                     FromToken(cs, FilledButtonTokens::DisabledLabelTextColor).WithAlpha(FilledButtonTokens::DisabledLabelTextOpacity) };
        }
        ButtonColors elevatedButtonColors() {
            const ColorScheme& cs = MaterialTheme::colorScheme();
            return { FromToken(cs, ElevatedButtonTokens::ContainerColor), FromToken(cs, ElevatedButtonTokens::LabelTextColor),
                     FromToken(cs, ElevatedButtonTokens::DisabledContainerColor).WithAlpha(ElevatedButtonTokens::DisabledContainerOpacity),
                     FromToken(cs, ElevatedButtonTokens::DisabledLabelTextColor).WithAlpha(ElevatedButtonTokens::DisabledLabelTextOpacity) };
        }
        ButtonColors filledTonalButtonColors() {
            const ColorScheme& cs = MaterialTheme::colorScheme();
            return { FromToken(cs, FilledTonalButtonTokens::ContainerColor), FromToken(cs, FilledTonalButtonTokens::LabelTextColor),
                     FromToken(cs, FilledTonalButtonTokens::DisabledContainerColor).WithAlpha(FilledTonalButtonTokens::DisabledContainerOpacity),
                     FromToken(cs, FilledTonalButtonTokens::DisabledLabelTextColor).WithAlpha(FilledTonalButtonTokens::DisabledLabelTextOpacity) };
        }
        ButtonColors outlinedButtonColors() {
            const ColorScheme& cs = MaterialTheme::colorScheme();
            return { Color::Transparent(), FromToken(cs, OutlinedButtonTokens::LabelTextColor), Color::Transparent(),
                     FromToken(cs, OutlinedButtonTokens::DisabledLabelTextColor).WithAlpha(OutlinedButtonTokens::DisabledLabelTextOpacity) };
        }
        ButtonColors textButtonColors() {
            const ColorScheme& cs = MaterialTheme::colorScheme();
            return { Color::Transparent(), FromToken(cs, ColorSchemeKeyTokens::Primary), Color::Transparent(),
                     FromToken(cs, TextButtonTokens::DisabledLabelColor).WithAlpha(TextButtonTokens::DisabledLabelOpacity) };
        }

        ButtonColors buttonColors(Color containerColor, Color contentColor, Color disabledContainerColor, Color disabledContentColor) {
            return buttonColors().Copy(containerColor, contentColor, disabledContainerColor, disabledContentColor);
        }
        ButtonColors elevatedButtonColors(Color containerColor, Color contentColor, Color disabledContainerColor, Color disabledContentColor) {
            return elevatedButtonColors().Copy(containerColor, contentColor, disabledContainerColor, disabledContentColor);
        }
        ButtonColors filledTonalButtonColors(Color containerColor, Color contentColor, Color disabledContainerColor, Color disabledContentColor) {
            return filledTonalButtonColors().Copy(containerColor, contentColor, disabledContainerColor, disabledContentColor);
        }
        ButtonColors outlinedButtonColors(Color containerColor, Color contentColor, Color disabledContainerColor, Color disabledContentColor) {
            return outlinedButtonColors().Copy(containerColor, contentColor, disabledContainerColor, disabledContentColor);
        }
        ButtonColors textButtonColors(Color containerColor, Color contentColor, Color disabledContainerColor, Color disabledContentColor) {
            return textButtonColors().Copy(containerColor, contentColor, disabledContainerColor, disabledContentColor);
        }

        ButtonElevation buttonElevation() {
            return { FilledButtonTokens::ContainerElevation, FilledButtonTokens::PressedContainerElevation, FilledButtonTokens::FocusedContainerElevation,
                     FilledButtonTokens::HoveredContainerElevation, FilledButtonTokens::DisabledContainerElevation };
        }
        ButtonElevation elevatedButtonElevation() {
            return { ElevatedButtonTokens::ContainerElevation, ElevatedButtonTokens::PressedContainerElevation, ElevatedButtonTokens::FocusedContainerElevation,
                     ElevatedButtonTokens::HoveredContainerElevation, ElevatedButtonTokens::DisabledContainerElevation };
        }
        ButtonElevation filledTonalButtonElevation() {
            return { FilledTonalButtonTokens::ContainerElevation, FilledTonalButtonTokens::PressedContainerElevation,
                     FilledTonalButtonTokens::FocusContainerElevation, FilledTonalButtonTokens::HoverContainerElevation, Dp(0) };
        }
        foundation::BorderStroke outlinedButtonBorder(bool enabled) {
            const Color c = Value(OutlinedButtonTokens::OutlineColor);
            return { ButtonSmallTokens::OutlinedOutlineWidth, enabled ? c : c.WithAlpha(OutlinedButtonTokens::DisabledContainerOpacity) };
        }

        ButtonShapes shapesFor(Dp h) {
            const Dp xs = ExtraSmallContainerHeight, s = MinHeight(), m = MediumContainerHeight, l = LargeContainerHeight, xl = ExtraLargeContainerHeight;
            if (h.value <= Mid(xs, s).value) return shapes(shape(), extraSmallPressedShape());
            if (h.value <= Mid(s, m).value) return shapes();
            if (h.value <= Mid(m, l).value) return shapes(shape(), mediumPressedShape());
            if (h.value <= Mid(l, xl).value) return shapes(shape(), largePressedShape());
            return shapes(shape(), extraLargePressedShape());
        }
        PaddingValues contentPaddingFor(Dp h, bool start, bool end) {
            if (h.value < MinHeight().value) return ExtraSmallContentPadding();
            if (h.value < MediumContainerHeight.value) return SmallPadding(start, end);
            if (h.value < LargeContainerHeight.value) return MediumPadding(start, end);
            if (h.value < ExtraLargeContainerHeight.value) return LargePadding(start, end);
            return ExtraLargeContentPadding();
        }
        Dp iconSizeFor(Dp h) {
            if (h.value < MinHeight().value) return ExtraSmallIconSize;
            if (h.value < MediumContainerHeight.value) return SmallIconSize;
            if (h.value < LargeContainerHeight.value) return MediumIconSize;
            if (h.value < ExtraLargeContainerHeight.value) return LargeIconSize;
            return ExtraLargeIconSize;
        }
        Dp iconSpacingFor(Dp h) {
            if (h.value < MinHeight().value) return ExtraSmallIconSpacing;
            if (h.value < MediumContainerHeight.value) return IconSpacing;
            if (h.value < LargeContainerHeight.value) return MediumIconSpacing;
            if (h.value < ExtraLargeContainerHeight.value) return LargeIconSpacing;
            return ExtraLargeIconSpacing;
        }
        text::TextStyle textStyleFor(Dp h) {
            const Typography& t = MaterialTheme::typography();
            if (h.value < MediumContainerHeight.value) return t.labelLarge;
            if (h.value < LargeContainerHeight.value) return t.titleMedium;
            if (h.value < ExtraLargeContainerHeight.value) return t.headlineSmall;
            return t.headlineLarge;
        }
    }

}
