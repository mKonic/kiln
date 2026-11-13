// BackgroundAnnotation.kt's drawBehind: the range's boxes as one outline (with the corner radius,
// spread and line merging), filled, then the border stroked on it.
#include "kiln/text/AnnotatedString.h"
#include "kiln/text/Paragraph.h"
#include "kiln/ui/Draw.h"

namespace kiln::text {

    DrawAnnotation MakeDrawAnnotation(BackgroundAnnotation bg) {
        // The path is kept between draws (allocating one per frame is what Compose avoids too).
        auto path = std::make_shared<gfx::Path>();
        return MakeDrawAnnotation([bg = std::move(bg), path](DrawAnnotationScope& s) {
            const ui::Color fill = bg.colorProducer ? bg.colorProducer() : bg.color;
            const bool hasFill = bg.brush || fill.Specified();
            ui::Color borderColor = ui::Color::Unspecified();
            if (bg.border) borderColor = bg.border->colorProducer ? bg.border->colorProducer() : bg.border->color;
            const bool hasBorder = bg.border && bg.border->width.Specified() && bg.border->width.value >= 0 &&
                                   (bg.border->brush || borderColor.Specified());
            if (!hasFill && !hasBorder) return;

            ui::DrawScope& d = s.drawScope;
            const float r = d.ToPx(bg.cornerRadius);
            s.textLayoutResult.PathFromBoundingBoxes(s.start, s.end, *path, { r, r }, d.ToPx(bg.spread.left), d.ToPx(bg.spread.top),
                                                     d.ToPx(bg.spread.right), d.ToPx(bg.spread.bottom), bg.mergeLines);
            if (path->IsEmpty()) return;
            if (bg.brush) d.DrawPath(*path, *bg.brush, nullptr, bg.alpha);
            else if (fill.Specified()) d.DrawPath(*path, gfx::Brush::SolidColor(fill), nullptr, bg.alpha);
            if (hasBorder) {
                gfx::Stroke stroke;
                stroke.width = d.ToPx(bg.border->width);   // 0: a hairline
                if (bg.border->brush) d.DrawPath(*path, *bg.border->brush, &stroke, bg.alpha);
                else d.DrawPath(*path, gfx::Brush::SolidColor(borderColor), &stroke, bg.alpha);
            }
        });
    }

}
