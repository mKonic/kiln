#pragma once

// Draw modifiers (drawBehind, drawWithContent) and Canvas. The lambda runs in the draw phase,
// so state it reads invalidates only drawing; a new lambda from recomposition redraws too.

#include "kiln/graphics/Path.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/LayoutNode.h"

#include <functional>

namespace kiln::foundation {

    using ui::ContentDrawScope;
    using ui::DrawScope;
    using ui::Modifier;

    Modifier DrawBehind(Modifier m, std::function<void(DrawScope&)> onDraw);
    // Must call scope.DrawContent() to draw what follows the modifier.
    Modifier DrawWithContent(Modifier m, std::function<void(ContentDrawScope&)> onDraw);
    // Spacer(modifier.drawBehind(onDraw)).
    void Canvas(const Modifier& modifier, std::function<void(DrawScope&)> onDraw);

}
