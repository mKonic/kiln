#pragma once

// DragHandle.kt: VerticalDragHandle, the handle that resizes panes (a pane expansion drag
// handle, for one). It grows while pressed or dragged, with each state's colour and shape.

#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Modifier.h"

#include <memory>
#include <optional>

namespace kiln::material3 {

    struct DragHandleColors {
        ui::Color color, pressedColor, draggedColor;
        bool operator==(const DragHandleColors&) const = default;
    };
    struct DragHandleShapes {
        foundation::ShapePtr shape, pressedShape, draggedShape;
        bool operator==(const DragHandleShapes&) const = default;
    };
    struct DragHandleSizes {
        ui::DpSize size, pressedSize, draggedSize;
        bool operator==(const DragHandleSizes&) const = default;
    };

    namespace VerticalDragHandleDefaults {
        DragHandleColors colors();
        // Unset entries take the defaults.
        DragHandleColors colors(std::optional<ui::Color> color, std::optional<ui::Color> pressedColor = std::nullopt,
                                std::optional<ui::Color> draggedColor = std::nullopt);
        DragHandleShapes shapes();
        DragHandleShapes shapes(foundation::ShapePtr shape, foundation::ShapePtr pressedShape = nullptr, foundation::ShapePtr draggedShape = nullptr);
        DragHandleSizes sizes();
        DragHandleSizes sizes(std::optional<ui::DpSize> size, std::optional<ui::DpSize> pressedSize = std::nullopt,
                              std::optional<ui::DpSize> draggedSize = std::nullopt);
    }

    struct VerticalDragHandleOptions {
        std::optional<DragHandleSizes> sizes;     // unset: VerticalDragHandleDefaults::sizes()
        std::optional<DragHandleColors> colors;   // unset: VerticalDragHandleDefaults::colors()
        std::optional<DragHandleShapes> shapes;   // unset: VerticalDragHandleDefaults::shapes()
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;   // null: remembered
    };
    void VerticalDragHandle(const ui::Modifier& modifier = {}, const VerticalDragHandleOptions& options = {});

}
