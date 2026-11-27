#pragma once

// Paint (androidx.compose.ui.graphics.Paint): how a ui::Canvas call fills or strokes. The shader
// is a brush made for a size (Brush::ApplyTo sets it); without one the colour fills.

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Path.h"

#include <memory>
#include <optional>

namespace kiln::gfx {

    // PaintingStyle.
    enum class PaintingStyle : std::uint8_t { Fill, Stroke };

    struct Paint {
        float alpha = 1;
        BlendMode blendMode = BlendMode::SrcOver;
        ui::Color color = ui::Color::Black();
        std::optional<ColorFilter> colorFilter;
        FilterQuality filterQuality = FilterQuality::Low;
        std::shared_ptr<const PathEffect> pathEffect;
        std::optional<Brush> shader;
        StrokeCap strokeCap = StrokeCap::Butt;
        StrokeJoin strokeJoin = StrokeJoin::Miter;
        float strokeMiterLimit = 4;
        float strokeWidth = 0;   // 0: a hairline
        PaintingStyle style = PaintingStyle::Fill;

        // The brush this paint draws with: the shader, else the colour.
        Brush AsBrush() const { return shader ? *shader : Brush::SolidColor(color); }
        Stroke AsStroke() const { return { strokeWidth, strokeMiterLimit, strokeCap, strokeJoin, pathEffect }; }
    };

}
