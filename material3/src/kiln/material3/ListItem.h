#pragma once

// ListItem.kt / ListItemDefaults.kt: classic lists and the updated, interactive and
// segmented Expressive lists. Slot content owns its sizing (icons, avatars and controls).

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"

#include <optional>

namespace kiln::material3 {
    using foundation::ShapePtr;
    using ui::Color;
    using ui::Dp;

    struct ListItemColors {
        Color containerColor = Color::Unspecified(), contentColor = Color::Unspecified();
        Color leadingContentColor = Color::Unspecified(), trailingContentColor = Color::Unspecified();
        Color overlineContentColor = Color::Unspecified(), supportingContentColor = Color::Unspecified();
        Color disabledContainerColor = Color::Unspecified(), disabledContentColor = Color::Unspecified();
        Color disabledLeadingContentColor = Color::Unspecified(), disabledTrailingContentColor = Color::Unspecified();
        Color disabledOverlineContentColor = Color::Unspecified(), disabledSupportingContentColor = Color::Unspecified();
        Color selectedContainerColor = Color::Unspecified(), selectedContentColor = Color::Unspecified();
        Color selectedLeadingContentColor = Color::Unspecified(), selectedTrailingContentColor = Color::Unspecified();
        Color selectedOverlineContentColor = Color::Unspecified(), selectedSupportingContentColor = Color::Unspecified();
        Color draggedContainerColor = Color::Unspecified(), draggedContentColor = Color::Unspecified();
        Color draggedLeadingContentColor = Color::Unspecified(), draggedTrailingContentColor = Color::Unspecified();
        Color draggedOverlineContentColor = Color::Unspecified(), draggedSupportingContentColor = Color::Unspecified();
        ListItemColors Copy(const ListItemColors& overrides) const;
        Color ContainerColor(bool enabled, bool selected, bool dragged) const;
        Color ContentColor(bool enabled, bool selected, bool dragged) const;
        Color LeadingContentColor(bool enabled, bool selected, bool dragged) const;
        Color TrailingContentColor(bool enabled, bool selected, bool dragged) const;
        Color OverlineContentColor(bool enabled, bool selected, bool dragged) const;
        Color SupportingContentColor(bool enabled, bool selected, bool dragged) const;
        bool operator==(const ListItemColors&) const = default;
    };

    struct ListItemShapes {
        ShapePtr shape, selectedShape, pressedShape, focusedShape, hoveredShape, draggedShape;
        ListItemShapes Copy(ShapePtr shape = {}, ShapePtr selected = {}, ShapePtr pressed = {},
                            ShapePtr focused = {}, ShapePtr hovered = {}, ShapePtr dragged = {}) const;
        ShapePtr ShapeForInteraction(bool selected, bool pressed, bool focused, bool hovered, bool dragged) const;
        bool operator==(const ListItemShapes&) const;
    };
    struct ListItemElevation {
        Dp elevation, draggedElevation;
        bool operator==(const ListItemElevation&) const = default;
    };

    using ListItemVerticalAlignment = kiln::Function<int(int size, int space)>;
    struct ListItemOptions {
        bool enabled = true;
        kiln::Function<void()> leadingContent, trailingContent, overlineContent, supportingContent;
        ListItemVerticalAlignment verticalAlignment; // empty: adaptive center/top alignment
        std::optional<ListItemShapes> shapes;
        std::optional<ListItemColors> colors;
        std::optional<ListItemElevation> elevation;
        std::optional<foundation::PaddingValues> contentPadding;
        kiln::Function<void()> onLongClick;
        std::string onLongClickLabel;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    struct LegacyListItemOptions {
        kiln::Function<void()> overlineContent, supportingContent, leadingContent, trailingContent;
        std::optional<ListItemColors> colors;
        Dp tonalElevation{0}, shadowElevation{0};
    };

    // Classic one/two/three-line layout. It always uses enabled, unselected colours.
    void ListItem(kiln::Function<void()> headline, const ui::Modifier& modifier = {}, const LegacyListItemOptions& options = {});
    // Updated noninteractive, clickable, single-selection, and multi-selection forms.
    void ListItem(const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);
    void ListItem(kiln::Function<void()> onClick, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);
    void ListItem(bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);
    void ListItem(bool checked, kiln::Function<void(bool)> onCheckedChange, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);

    void SegmentedListItem(const ListItemShapes& shapes, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);
    void SegmentedListItem(kiln::Function<void()> onClick, const ListItemShapes& shapes, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);
    void SegmentedListItem(bool selected, kiln::Function<void()> onClick, const ListItemShapes& shapes, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);
    void SegmentedListItem(bool checked, kiln::Function<void(bool)> onCheckedChange, const ListItemShapes& shapes, const ui::Modifier& modifier, const ListItemOptions& options, kiln::Function<void()> content);

    namespace ListItemDefaults {
        inline constexpr Dp Elevation{0}, SegmentedGap{2};
        ShapePtr shape();
        Color containerColor();
        Color contentColor();
        foundation::PaddingValues contentPadding();
        ListItemVerticalAlignment verticalAlignment();
        ListItemColors colors();
        ListItemColors colors(const ListItemColors& overrides);
        ListItemColors segmentedColors();
        ListItemColors segmentedColors(const ListItemColors& overrides);
        ListItemShapes shapes();
        ListItemShapes shapes(ShapePtr shape, ShapePtr selected = {}, ShapePtr pressed = {},
                              ShapePtr focused = {}, ShapePtr hovered = {}, ShapePtr dragged = {});
        ListItemShapes segmentedShapes(int index, int count);
        ListItemShapes segmentedShapes(int index, int count, const ListItemShapes& defaultShapes);
        ListItemElevation elevation(Dp elevation = Dp(0), Dp dragged = Dp(8));
    }
}
