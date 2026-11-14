#pragma once

// painterResource / ImageVector.vectorResource / ImageBitmap.imageResource (ui
// res/PainterResources.android.kt, VectorResources.android.kt, ImageResources.android.kt): an
// app drawable (R::drawable::name) as a painter. A vector drawable becomes an ImageVector
// (graphics/vector/compat/XmlVectorParser.android.kt's reading of vector, group, clip-path and
// path, with gradients); a PNG, JPEG or WebP is decoded. Both are remembered per drawable and
// configuration (night mode and density pick other variants).

#include "kiln/foundation/Image.h"
#include "kiln/foundation/VectorPainter.h"
#include "kiln/graphics/vector/ImageVector.h"
#include "kiln/ui/AppResources.h"

#include <memory>

namespace kiln::foundation {

    std::shared_ptr<Painter> PainterResource(ui::DrawableRes id);
    gfx::vector::ImageVectorPtr VectorResource(ui::DrawableRes id);
    gfx::ImageBitmap ImageResource(ui::DrawableRes id);

    // The ImageVector of resolved vector elements (AppResources' VectorElement format), at the
    // density for "px" sizes. Throws std::invalid_argument for a malformed vector (no viewport).
    gfx::vector::ImageVectorPtr BuildVectorResource(const std::vector<ui::VectorElement>& elements, float density);

}
