#include "kiln/material3/ToggleButton.h"
#include <kiln/Function.h>

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/InteractionShape.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

namespace kiln::material3 {

    namespace {
        ui::Color Take(ui::Color color, ui::Color fallback) { return color.Specified() ? color : fallback; }
        enum class Variant { Filled, Elevated, Tonal, Outlined };
        ToggleButtonColors Colors(Variant variant) {
            switch (variant) {
                case Variant::Elevated: return ElevatedToggleButtonDefaults::colors();
                case Variant::Tonal: return FilledTonalToggleButtonDefaults::colors();
                case Variant::Outlined: return OutlinedToggleButtonDefaults::colors();
                default: return ToggleButtonDefaults::colors();
            }
        }
        std::optional<ToggleButtonElevation> Elevation(Variant variant) {
            switch (variant) {
                case Variant::Elevated: return ElevatedToggleButtonDefaults::elevation();
                case Variant::Tonal: return FilledTonalToggleButtonDefaults::elevation();
                case Variant::Outlined: return std::nullopt;
                default: return ToggleButtonDefaults::elevation();
            }
        }
        void ToggleImpl(bool checked, kiln::Function<void(bool)> onCheckedChange, const ui::Modifier& modifier,
                        const ToggleButtonOptions& options, kiln::Function<void()> content, Variant variant) {
            auto source = options.interactionSource ? options.interactionSource : foundation::RememberInteractionSource();
            const auto colors = options.colors ? *options.colors : Colors(variant);
            const auto elevation = options.elevation ? *options.elevation : Elevation(variant);
            const auto shapes = options.shapes ? *options.shapes : ToggleButtonDefaults::shapesFor(options.buttonSize);
            auto border = options.border ? *options.border : variant == Variant::Outlined
                ? OutlinedToggleButtonDefaults::border(options.enabled, checked) : std::nullopt;
            const auto padding = options.contentPadding ? *options.contentPadding
                : ToggleButtonDefaults::contentPaddingFor(options.buttonSize, bool(options.icon));
            const ui::Dp height = options.buttonSize.height;
            SurfaceOptions surface;
            surface.color = colors.ContainerColor(options.enabled, checked);
            surface.contentColor = colors.ContentColor(options.enabled, checked);
            surface.shadowElevation = elevation ? ShadowElevation(*elevation, options.enabled, source) : ui::Dp(0);
            surface.shape = internal::ShapeByInteraction(shapes.shape, shapes.pressedShape, shapes.checkedShape,
                                                         source->IsPressed(), checked, Value(tokens::MotionSchemeKeyTokens::FastSpatial));
            surface.border = internal::AnimateBorderStrokeAsState(border);
            const auto label = ButtonDefaults::textStyleFor(height);
            const auto iconSize = ButtonDefaults::iconSizeFor(height), iconSpacing = ButtonDefaults::iconSpacingFor(height);
            const ui::Color foreground = *surface.contentColor;
            const auto m = foundation::Semantics(modifier, [](ui::SemanticsConfiguration& c) { c.role = ui::Role::Checkbox; });
            Surface(checked, std::move(onCheckedChange), m, surface,
                    [foreground, label, height, padding, icon = options.icon, iconSize, iconSpacing, content = std::move(content)] {
                internal::ProvideContentColorTextStyle(foreground, label, [=] {
                    foundation::Row(foundation::Padding(foundation::DefaultMinSize({}, ui::Dp::Unspecified(), height), padding),
                                    foundation::Arrangement::Center(), ui::AlignmentV::CenterVertically(), [=] {
                        if (icon) {
                            foundation::Box(foundation::SizeMod({}, iconSize, iconSize), ui::Alignment::Center(), true, icon);
                            foundation::Spacer(foundation::Width({}, iconSpacing));
                        }
                        content();
                    });
                });
            }, options.enabled, source);
        }
    }

    ToggleButtonColors ToggleButtonColors::Copy(ui::Color container, ui::Color content, ui::Color disabledContainer, ui::Color disabledContent,
                                                ui::Color checkedContainer, ui::Color checkedContent) const {
        return { Take(container, containerColor), Take(content, contentColor), Take(disabledContainer, disabledContainerColor),
                 Take(disabledContent, disabledContentColor), Take(checkedContainer, checkedContainerColor), Take(checkedContent, checkedContentColor) };
    }

    namespace internal {
        std::optional<foundation::BorderStroke> AnimateBorderStrokeAsState(const std::optional<foundation::BorderStroke>& border) {
            const auto width = foundation::AnimateDpAsState(border ? border->width : ui::Dp(0), Value(tokens::MotionSchemeKeyTokens::FastSpatial));
            const bool solid = border && border->brush.kind == gfx::Brush::Kind::Solid && !border->brush.colors.empty();
            const auto color = foundation::AnimateColorAsState(solid ? border->brush.colors[0] : ui::Color::Transparent(),
                                                                Value(tokens::MotionSchemeKeyTokens::DefaultEffects));
            if (width.value <= 0) return std::nullopt;
            return border && !solid ? foundation::BorderStroke(width, border->brush) : foundation::BorderStroke(width, color);
        }
    }

    void ToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const ToggleButtonOptions& o, kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Filled);
    }
    void ElevatedToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const ToggleButtonOptions& o, kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Elevated);
    }
    void FilledTonalToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const ToggleButtonOptions& o, kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Tonal);
    }
    void OutlinedToggleButton(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& m, const ToggleButtonOptions& o, kiln::Function<void()> content) {
        ToggleImpl(checked, std::move(change), m, o, std::move(content), Variant::Outlined);
    }

}
