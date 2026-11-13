#pragma once

// The style modifiers (foundation style/Placement.kt, Appearance.kt,
// StyleGraphicsLayerModifier.kt, StylePadding.kt):
//   - Placement { }: insets from the sides (left / right / top / bottom; one of a pair anchors
//     the element to that side), min / max sizes and a width / height (a distance or a fraction
//     of the incoming maximum), applied while measuring;
//   - Appearance { }: background, content, foreground, border and painted shadows in a shape;
//   - GraphicsLayer(layerRequired, block): a layer only while `layerRequired` says so;
//   - Style* (resolver): each of those, and content / external / border padding, from the
//     predefined style properties a StyleResolver resolved.
// Blocks read state observably: a change measures or draws again without recomposing.

#include "kiln/foundation/Layouts.h"
#include <kiln/Function.h>
#include "kiln/foundation/style/StyleResolver.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    // PlacementModifierScope: unspecified sides and sizes leave the constraints alone.
    struct PlacementModifierScope {
        ui::Dp left = ui::Dp::Unspecified(), right = ui::Dp::Unspecified(), top = ui::Dp::Unspecified(), bottom = ui::Dp::Unspecified();
        ui::Dp minWidth = ui::Dp::Unspecified(), maxWidth = ui::Dp::Unspecified(), minHeight = ui::Dp::Unspecified(),
               maxHeight = ui::Dp::Unspecified();
        Breadth width, height;
    };
    Modifier Placement(Modifier m, kiln::Function<void(PlacementModifierScope&)> block);

    // AppearanceScope: what is drawn, in `shape` (the element's semantics shape too).
    struct AppearanceScope {
        Fill background, foreground;
        ui::Dp borderWidth = ui::Dp::Unspecified();
        Fill border;
        Shadows dropShadow, innerShadow;
        ShapePtr shape = RectangleShape();
    };
    Modifier Appearance(Modifier m, kiln::Function<void(AppearanceScope&)> block);

    // graphicsLayer(layerRequired, block): the block's layer while `layerRequired` returns true
    // (or is null), else no layer.
    Modifier GraphicsLayer(Modifier m, kiln::Function<bool()> layerRequired, kiln::Function<void(GraphicsLayerParams&)> block);

    Modifier StylePlacement(Modifier m, std::shared_ptr<StyleResolver> resolver);
    Modifier StyleAppearance(Modifier m, std::shared_ptr<StyleResolver> resolver);
    Modifier StyleGraphicsLayer(Modifier m, std::shared_ptr<StyleResolver> resolver);
    Modifier StyleContentPadding(Modifier m, std::shared_ptr<StyleResolver> resolver);
    Modifier StyleExternalPadding(Modifier m, std::shared_ptr<StyleResolver> resolver);
    // The border width as padding (rounded up to whole pixels; a hairline is one pixel).
    Modifier StyleBorderPadding(Modifier m, std::shared_ptr<StyleResolver> resolver);

}
