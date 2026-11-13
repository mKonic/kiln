#include "kiln/material3/IconButton.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/ChildParentSemantics.h"
#include "kiln/material3/internal/InteractionShape.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

namespace kiln::material3 {

    namespace {
        ui::Color Take(ui::Color value, ui::Color fallback) { return value.Specified() ? value : fallback; }
        enum class Variant { Standard, Filled, Tonal, Outlined };
        rt::Composition& Comp() { return *rt::Composition::Current(); }

        auto Source(std::shared_ptr<foundation::MutableInteractionSource> source) {
            if (!source) source = foundation::RememberInteractionSource();
            return source;
        }

        foundation::ShapePtr ShapeFor(const IconToggleButtonShapes& shapes, bool pressed, bool checked) {
            return internal::ShapeByInteraction(shapes.shape, shapes.pressedShape, shapes.checkedShape, pressed, checked,
                                                 Value(tokens::MotionSchemeKeyTokens::DefaultEffects));
        }

        IconButtonColors Colors(Variant variant) {
            switch (variant) {
                case Variant::Filled: return IconButtonDefaults::filledIconButtonColors();
                case Variant::Tonal: return IconButtonDefaults::filledTonalIconButtonColors();
                case Variant::Outlined: return IconButtonDefaults::outlinedIconButtonColors();
                default: return IconButtonDefaults::iconButtonColors();
            }
        }
        IconToggleButtonColors ToggleColors(Variant variant, bool expressive) {
            switch (variant) {
                case Variant::Filled: return IconButtonDefaults::filledIconToggleButtonColors();
                case Variant::Tonal: return IconButtonDefaults::filledTonalIconToggleButtonColors();
                case Variant::Outlined: return expressive ? IconButtonDefaults::outlinedIconToggleButtonVibrantColors()
                                                          : IconButtonDefaults::outlinedIconToggleButtonColors();
                default: return expressive ? IconButtonDefaults::iconToggleButtonVibrantColors() : IconButtonDefaults::iconToggleButtonColors();
            }
        }
        ui::Modifier Role(ui::Modifier m, ui::Role role) {
            return foundation::Semantics(std::move(m), [role](ui::SemanticsConfiguration& c) { c.role = role; });
        }
        void ContentBox(kiln::Function<void()> content) {
            const ui::DpSize size = IconButtonDefaults::smallContainerSize();
            foundation::Box(foundation::SizeMod({}, size.width, size.height), ui::Alignment::Center(), false, std::move(content));
        }

        void ButtonImpl(kiln::Function<void()> onClick, const ui::Modifier& modifier, const IconButtonOptions& options,
                        kiln::Function<void()> content, Variant variant) {
            auto source = Source(options.interactionSource);
            const auto colors = options.colors ? *options.colors : Colors(variant);
            foundation::ShapePtr shape = options.shape ? options.shape : IconButtonDefaults::standardShape();
            if (options.shapes) {
                const auto& s = *options.shapes;
                shape = ShapeFor({ s.shape, s.pressedShape, s.shape }, source->IsPressed(), false);
            }
            const ui::Color container = colors.ContainerColor(options.enabled), foreground = colors.ContentColor(options.enabled);
            if (variant == Variant::Standard) {
                const ui::DpSize size = IconButtonDefaults::smallContainerSize();
                auto m = foundation::SizeMod(MinimumInteractiveComponentSize(modifier), size.width, size.height);
                m = foundation::Background(foundation::Clip(std::move(m), shape), container, shape);
                foundation::ClickableOptions click;
                click.enabled = options.enabled;
                click.role = ui::Role::Button;
                click.interactionSource = source;
                click.indication = Ripple({ .focusRingShape = shape });
                m = internal::ChildSemantics(foundation::Clickable(std::move(m), std::move(onClick), click));
                foundation::Box(m, ui::Alignment::Center(), false, [foreground, content = std::move(content)] {
                    Comp().Provide(LocalContentColor, foreground, content);
                });
                return;
            }
            SurfaceOptions surface;
            surface.shape = shape;
            surface.color = container;
            surface.contentColor = foreground;
            if (options.border) surface.border = *options.border;
            else if (variant == Variant::Outlined) surface.border = IconButtonDefaults::outlinedIconButtonBorder(options.enabled);
            Surface(std::move(onClick), Role(modifier, ui::Role::Button), surface,
                    [content = std::move(content)] { ContentBox(content); }, options.enabled, source);
        }

        void ToggleImpl(bool checked, kiln::Function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                        const IconToggleButtonOptions& options, kiln::Function<void()> content, Variant variant) {
            auto source = Source(options.interactionSource);
            const auto colors = options.colors ? *options.colors : ToggleColors(variant, options.shapes.has_value());
            foundation::ShapePtr shape = options.shape ? options.shape : IconButtonDefaults::standardShape();
            if (options.shapes) shape = ShapeFor(*options.shapes, source->IsPressed(), checked);
            const ui::Color container = colors.ContainerColor(options.enabled, checked), foreground = colors.ContentColor(options.enabled, checked);
            if (variant == Variant::Standard) {
                const ui::DpSize size = IconButtonDefaults::smallContainerSize();
                auto m = foundation::SizeMod(MinimumInteractiveComponentSize(modifier), size.width, size.height);
                m = foundation::Background(foundation::Clip(std::move(m), shape), container);
                foundation::ClickableOptions toggle;
                toggle.enabled = options.enabled;
                toggle.role = ui::Role::Checkbox;
                toggle.interactionSource = source;
                toggle.indication = Ripple({ .focusRingShape = shape });
                m = foundation::Toggleable(std::move(m), checked, std::move(onCheckedChange), toggle);
                foundation::Box(m, ui::Alignment::Center(), false, [foreground, content = std::move(content)] {
                    Comp().Provide(LocalContentColor, foreground, content);
                });
                return;
            }
            SurfaceOptions surface;
            surface.shape = shape;
            surface.color = container;
            surface.contentColor = foreground;
            if (options.border) surface.border = *options.border;
            else if (variant == Variant::Outlined) surface.border = options.shapes
                ? IconButtonDefaults::outlinedIconToggleButtonVibrantBorder(options.enabled, checked)
                : IconButtonDefaults::outlinedIconToggleButtonBorder(options.enabled, checked);
            Surface(checked, std::move(onCheckedChange), Role(modifier, ui::Role::Checkbox), surface,
                    [content = std::move(content)] { ContentBox(content); }, options.enabled, source);
        }
    }

    IconButtonColors IconButtonColors::Copy(ui::Color container, ui::Color content, ui::Color disabledContainer, ui::Color disabledContent) const {
        return { Take(container, containerColor), Take(content, contentColor), Take(disabledContainer, disabledContainerColor),
                 Take(disabledContent, disabledContentColor) };
    }
    IconToggleButtonColors IconToggleButtonColors::Copy(ui::Color container, ui::Color content, ui::Color disabledContainer, ui::Color disabledContent,
                                                      ui::Color checkedContainer, ui::Color checkedContent) const {
        return { Take(container, containerColor), Take(content, contentColor), Take(disabledContainer, disabledContainerColor),
                 Take(disabledContent, disabledContentColor), Take(checkedContainer, checkedContainerColor), Take(checkedContent, checkedContentColor) };
    }

    void IconButton(kiln::Function<void()> click, const ui::Modifier& m, const IconButtonOptions& o, kiln::Function<void()> content) {
        ButtonImpl(std::move(click), m, o, std::move(content), Variant::Standard);
    }
    void FilledIconButton(kiln::Function<void()> click, const ui::Modifier& m, const IconButtonOptions& o, kiln::Function<void()> content) {
        ButtonImpl(std::move(click), m, o, std::move(content), Variant::Filled);
    }
    void FilledTonalIconButton(kiln::Function<void()> click, const ui::Modifier& m, const IconButtonOptions& o, kiln::Function<void()> content) {
        ButtonImpl(std::move(click), m, o, std::move(content), Variant::Tonal);
    }
    void OutlinedIconButton(kiln::Function<void()> click, const ui::Modifier& m, const IconButtonOptions& o, kiln::Function<void()> content) {
        ButtonImpl(std::move(click), m, o, std::move(content), Variant::Outlined);
    }
    void IconToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const IconToggleButtonOptions& o, kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Standard);
    }
    void FilledIconToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const IconToggleButtonOptions& o, kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Filled);
    }
    void FilledTonalIconToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const IconToggleButtonOptions& o,
                                     kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Tonal);
    }
    void OutlinedIconToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const IconToggleButtonOptions& o,
                                  kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Outlined);
    }

}
