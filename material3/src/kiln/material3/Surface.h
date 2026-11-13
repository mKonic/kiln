#pragma once

// material3 Surface.kt: a container with a shape, a colour (tonally elevated when it is the
// theme's surface), an optional border and elevation shadow, clipping its content to the shape.
// It provides the content colour and the absolute tonal elevation, is a traversal group, and
// keeps touches from reaching what is behind it. The clickable, selectable and toggleable forms
// add the ripple and the minimum touch size.

#include "kiln/foundation/Border.h"
#include <kiln/Function.h>
#include "kiln/foundation/Interaction.h"

#include <functional>
#include <optional>

namespace kiln::material3 {

    // LocalAbsoluteTonalElevation: the sum of the tonal elevations of the surfaces around.
    inline rt::Local<ui::Dp> LocalAbsoluteTonalElevation{ ui::Dp(0) };

    struct SurfaceOptions {
        foundation::ShapePtr shape;                  // null: RectangleShape
        std::optional<ui::Color> color;              // unset: colorScheme.surface
        std::optional<ui::Color> contentColor;       // unset: contentColorFor(color)
        ui::Dp tonalElevation{ 0 }, shadowElevation{ 0 };
        std::optional<foundation::BorderStroke> border;
    };

    void Surface(const ui::Modifier& modifier, const SurfaceOptions& options, kiln::Function<void()> content);
    // Surface(onClick, ...)
    void Surface(kiln::Function<void()> onClick, const ui::Modifier& modifier, const SurfaceOptions& options, kiln::Function<void()> content,
                 bool enabled = true, std::shared_ptr<foundation::MutableInteractionSource> interactionSource = nullptr);
    // Surface(selected, onClick, ...)
    void Surface(bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier, const SurfaceOptions& options,
                 kiln::Function<void()> content, bool enabled = true,
                 std::shared_ptr<foundation::MutableInteractionSource> interactionSource = nullptr);
    // Surface(checked, onCheckedChange, ...)
    void Surface(bool checked, kiln::Function<void(bool)> onCheckedChange, const ui::Modifier& modifier, const SurfaceOptions& options,
                 kiln::Function<void()> content, bool enabled = true,
                 std::shared_ptr<foundation::MutableInteractionSource> interactionSource = nullptr);

    namespace internal {
        // The internal Surface(backgroundColor: ColorProducer): the colour read at draw time,
        // tonally elevated when it is the surface colour; no shape, border or content colour.
        void Surface(kiln::Function<ui::Color()> backgroundColor, const ui::Modifier& modifier, ui::Dp tonalElevation, ui::Dp shadowElevation,
                     kiln::Function<void()> content);
    }

}
