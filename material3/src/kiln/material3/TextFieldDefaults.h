#pragma once

// TextFieldDefaults / OutlinedTextFieldDefaults (TextFieldDefaults.kt): shapes, sizes, paddings,
// colours and the containers (a filled background with an indicator line, or an outline).
// The colours are a plain struct: change its fields for custom colours (Compose's colors(...)
// arguments).

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/TextFieldColors.h"

#include <functional>
#include <memory>

namespace kiln::material3 {

    namespace TextFieldDefaults {
        foundation::ShapePtr shape();
        foundation::ShapePtr roundedShape();
        inline constexpr ui::Dp MinHeight{ 56 }, MinWidth{ 280 };
        inline constexpr ui::Dp UnfocusedIndicatorThickness{ 1 }, FocusedIndicatorThickness{ 2 };
        foundation::PaddingValues contentPaddingWithLabel(ui::Dp start = ui::Dp(16), ui::Dp end = ui::Dp(16), ui::Dp top = ui::Dp(8),
                                                          ui::Dp bottom = ui::Dp(8));
        foundation::PaddingValues contentPaddingWithoutLabel(ui::Dp start = ui::Dp(16), ui::Dp top = ui::Dp(16), ui::Dp end = ui::Dp(16),
                                                             ui::Dp bottom = ui::Dp(16));
        // The filled colours, with the current text selection colours.
        TextFieldColors colors();
        // The Expressive tonal colours (surface-container fill, no indicator); for Inside labels.
        TextFieldColors tonalColors();
        // The container: the background (animating between states) and the indicator line.
        void Container(bool enabled, bool isError, std::shared_ptr<foundation::MutableInteractionSource> interactionSource,
                       const ui::Modifier& modifier = {}, std::optional<TextFieldColors> colors = std::nullopt, foundation::ShapePtr shape = nullptr,
                       ui::Dp focusedIndicatorLineThickness = FocusedIndicatorThickness,
                       ui::Dp unfocusedIndicatorLineThickness = UnfocusedIndicatorThickness);
        // Modifier.indicatorLine: a line along the bottom, thicker and coloured while focused,
        // clipped to the field's shape. Null colours or shape: the theme's.
        ui::Modifier IndicatorLine(ui::Modifier m, bool enabled, bool isError, std::shared_ptr<foundation::MutableInteractionSource> interactionSource,
                                   std::optional<TextFieldColors> colors = std::nullopt, foundation::ShapePtr textFieldShape = nullptr,
                                   ui::Dp focusedIndicatorLineThickness = FocusedIndicatorThickness,
                                   ui::Dp unfocusedIndicatorLineThickness = UnfocusedIndicatorThickness);
    }

    namespace OutlinedTextFieldDefaults {
        foundation::ShapePtr shape();
        foundation::ShapePtr roundedShape();
        inline constexpr ui::Dp MinHeight{ 56 }, MinWidth{ 280 };
        inline constexpr ui::Dp UnfocusedBorderThickness{ 1 }, FocusedBorderThickness{ 2 };
        foundation::PaddingValues contentPaddingWithLabel(ui::Dp start = ui::Dp(16), ui::Dp top = ui::Dp(8), ui::Dp end = ui::Dp(16),
                                                          ui::Dp bottom = ui::Dp(8));
        foundation::PaddingValues contentPaddingWithoutLabel(ui::Dp start = ui::Dp(16), ui::Dp top = ui::Dp(16), ui::Dp end = ui::Dp(16),
                                                             ui::Dp bottom = ui::Dp(16));
        TextFieldColors colors();
        // The Expressive tonal colours (outline-variant border); for Cutout labels.
        TextFieldColors tonalColors();
        // The container: the outline (animating its colour and thickness) over the background.
        void Container(bool enabled, bool isError, std::shared_ptr<foundation::MutableInteractionSource> interactionSource,
                       const ui::Modifier& modifier = {}, std::optional<TextFieldColors> colors = std::nullopt, foundation::ShapePtr shape = nullptr,
                       ui::Dp focusedBorderThickness = FocusedBorderThickness, ui::Dp unfocusedBorderThickness = UnfocusedBorderThickness);
    }

    namespace internal {
        // textFieldBackground: the shape filled with the colour read while drawing.
        ui::Modifier TextFieldBackground(ui::Modifier m, kiln::Function<ui::Color()> color, foundation::ShapePtr shape);
        // outlineCutout: draws what it wraps except a gap for the label in the top edge.
        ui::Modifier OutlineCutout(ui::Modifier m, kiln::Function<ui::Size()> labelSize, ui::AlignmentH alignment, foundation::PaddingValues padding);
        // supportingTextPadding
        foundation::PaddingValues SupportingTextPadding();
        // The default content padding: with a label only for an Inside label.
        foundation::PaddingValues DefaultContentPadding(bool hasLabel, const TextFieldLabelPosition& position);
    }

}
