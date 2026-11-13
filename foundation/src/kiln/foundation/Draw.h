#pragma once

// Draw modifiers (drawBehind, drawWithContent) and Canvas. The lambda runs in the draw phase,
// so state it reads invalidates only drawing; a new lambda from recomposition redraws too.

#include "kiln/graphics/Image.h"
#include <kiln/Function.h>
#include "kiln/graphics/Path.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/LayoutNode.h"

#include <functional>

namespace kiln::foundation {

    using ui::ContentDrawScope;
    using ui::DrawScope;
    using ui::Modifier;

    Modifier DrawBehind(Modifier m, kiln::Function<void(DrawScope&)> onDraw);
    // Must call scope.DrawContent() to draw what follows the modifier.
    Modifier DrawWithContent(Modifier m, kiln::Function<void(ContentDrawScope&)> onDraw);
    // Spacer(modifier.drawBehind(onDraw)).
    void Canvas(const Modifier& modifier, kiln::Function<void(DrawScope&)> onDraw);
    // The layout at `coordinates` (its whole subtree) drawn into an image off screen
    // (GraphicsLayer.toImageBitmap of a layer recording that layout); blocking, for one-off
    // captures. Empty without a renderer or a size.
    gfx::ImageBitmap DrawToImageBitmap(const ui::LayoutCoordinates& coordinates);

}
