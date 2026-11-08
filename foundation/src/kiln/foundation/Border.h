#pragma once

// border (foundation Border.kt, BorderRenderer.kt, BorderStroke.kt): a border drawn inside the
// shape's outline over the content, its width rounded up to whole pixels (Dp.Hairline is one
// pixel) and at most half the outline's smaller side; wider than that, the outline is filled.

#include "kiln/foundation/Shapes.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/graphics/Brush.h"

namespace kiln::foundation {

    using ui::Modifier;

    struct BorderStroke {
        ui::Dp width;
        gfx::Brush brush;
        BorderStroke(ui::Dp w, gfx::Brush b) : width(w), brush(std::move(b)) {}
        BorderStroke(ui::Dp w, ui::Color c) : width(w), brush(gfx::Brush::SolidColor(c)) {}
        bool operator==(const BorderStroke&) const = default;
    };

    // BorderRenderer: draws a border for an outline (the shapes of the outline kinds).
    class BorderRenderer {
    public:
        void DrawBorder(ui::DrawScope& scope, ui::Dp width, const gfx::Brush& brush, const gfx::Outline& outline);
    };

    Modifier Border(Modifier m, const BorderStroke& border, ShapePtr shape = RectangleShape());
    Modifier Border(Modifier m, ui::Dp width, ui::Color color, ShapePtr shape = RectangleShape());
    Modifier Border(Modifier m, ui::Dp width, gfx::Brush brush, ShapePtr shape);

}
