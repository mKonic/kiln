#pragma once

// Card.kt: filled, elevated and outlined surfaces with Column content. Only the clickable
// overload reserves a touch target, observes interactions and adds input semantics.

#include "kiln/foundation/Border.h"
#include "kiln/foundation/Interaction.h"

#include <optional>

namespace kiln::material3 {
    struct CardColors {
        ui::Color containerColor, contentColor, disabledContainerColor, disabledContentColor;
        CardColors Copy(ui::Color container = ui::Color::Unspecified(), ui::Color content = ui::Color::Unspecified(),
                        ui::Color disabledContainer = ui::Color::Unspecified(), ui::Color disabledContent = ui::Color::Unspecified()) const;
        ui::Color ContainerColor(bool enabled) const { return enabled ? containerColor : disabledContainerColor; }
        ui::Color ContentColor(bool enabled) const { return enabled ? contentColor : disabledContentColor; }
        bool operator==(const CardColors&) const = default;
    };
    struct CardElevation {
        ui::Dp defaultElevation, pressedElevation, focusedElevation, hoveredElevation, draggedElevation, disabledElevation;
        bool operator==(const CardElevation&) const = default;
    };
    ui::Dp ShadowElevation(const CardElevation& elevation, bool enabled, const std::shared_ptr<foundation::MutableInteractionSource>& source);

    struct CardOptions {
        bool enabled = true;  // the plain overload always uses enabled colors, as in Card.kt
        foundation::ShapePtr shape;
        std::optional<CardColors> colors;
        std::optional<CardElevation> elevation;
        std::optional<std::optional<foundation::BorderStroke>> border;  // unset: variant default; inner nullopt: none
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void Card(const ui::Modifier& modifier, const CardOptions& options, std::function<void()> content);
    void Card(std::function<void()> onClick, const ui::Modifier& modifier, const CardOptions& options, std::function<void()> content);
    void ElevatedCard(const ui::Modifier& modifier, const CardOptions& options, std::function<void()> content);
    void ElevatedCard(std::function<void()> onClick, const ui::Modifier& modifier, const CardOptions& options, std::function<void()> content);
    void OutlinedCard(const ui::Modifier& modifier, const CardOptions& options, std::function<void()> content);
    void OutlinedCard(std::function<void()> onClick, const ui::Modifier& modifier, const CardOptions& options, std::function<void()> content);

    namespace CardDefaults {
        foundation::ShapePtr shape();
        foundation::ShapePtr elevatedShape();
        foundation::ShapePtr outlinedShape();
        CardElevation cardElevation();
        CardElevation elevatedCardElevation();
        CardElevation outlinedCardElevation();
        CardColors cardColors();
        CardColors elevatedCardColors();
        CardColors outlinedCardColors();
        CardColors cardColors(ui::Color container, std::optional<ui::Color> content = {}, ui::Color disabledContainer = ui::Color::Unspecified(), std::optional<ui::Color> disabledContent = {});
        CardColors elevatedCardColors(ui::Color container, std::optional<ui::Color> content = {}, ui::Color disabledContainer = ui::Color::Unspecified(), std::optional<ui::Color> disabledContent = {});
        CardColors outlinedCardColors(ui::Color container, std::optional<ui::Color> content = {}, ui::Color disabledContainer = ui::Color::Unspecified(), std::optional<ui::Color> disabledContent = {});
        foundation::BorderStroke outlinedCardBorder(bool enabled = true);
    }
}
