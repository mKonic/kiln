#include "kiln/foundation/Border.h"

#include "kiln/graphics/Path.h"
#include "kiln/graphics/Shape.h"
#include "kiln/ui/Semantics.h"

#include <cmath>
#include <variant>

namespace kiln::foundation {

    using ui::Offset;
    using ui::Rect;
    using ui::Size;

    namespace {
        Offset Shrink(Offset r, float v) { return { std::max(0.0f, r.x - v), std::max(0.0f, r.y - v) }; }
        gfx::Path RoundRectPath(const gfx::RoundRect& rr) {
            gfx::Path p;
            p.AddRoundRect(rr);
            return p;
        }
    }

    void BorderRenderer::DrawBorder(ui::DrawScope& s, ui::Dp width, const gfx::Brush& brush, const gfx::Outline& outline) {
        // Hairline (0 dp) is one pixel; Unspecified draws nothing.
        const float widthPx = !width.Specified() ? 0.0f : width.value == 0 ? 1.0f : std::ceil(s.ToPx(width));
        if (!(widthPx > 0)) return;
        // The outline's own bounds (isBorderMatchesOutlineBoundsFixEnabled).
        const Rect bounds = outline.Bounds();
        if (bounds.Empty()) return;
        const float minDimension = std::min(bounds.Width(), bounds.Height());
        const float strokeWidth = std::max(0.0f, std::min(std::ceil(std::max(widthPx, 0.0f)), std::ceil(minDimension / 2)));
        const float halfStroke = strokeWidth / 2;
        const Offset topLeft{ bounds.left + halfStroke, bounds.top + halfStroke };
        const Size borderSize{ bounds.Width() - strokeWidth, bounds.Height() - strokeWidth };
        const bool fillArea = strokeWidth * 2 > minDimension;
        const gfx::Stroke stroke{ strokeWidth };

        if (const auto* r = std::get_if<Rect>(&outline.value)) {
            if (fillArea) {
                s.DrawRect(brush, { r->left, r->top }, { r->Width(), r->Height() });
            } else {
                gfx::Path p;
                p.AddRect({ topLeft.x, topLeft.y, topLeft.x + borderSize.width, topLeft.y + borderSize.height });
                s.DrawPath(p, brush, &stroke);
            }
            return;
        }
        if (const auto* rr = std::get_if<gfx::RoundRect>(&outline.value)) {
            const bool simple = rr->topLeft.x == rr->topLeft.y && rr->topLeft == rr->topRight && rr->topLeft == rr->bottomRight &&
                                rr->topLeft == rr->bottomLeft;
            if (simple) {
                const Offset corner = rr->topLeft;
                if (fillArea) {
                    s.DrawRoundRect(brush, { bounds.left, bounds.top }, { bounds.Width(), bounds.Height() }, corner.x);
                } else if (corner.x < halfStroke) {
                    // The stroke's inside would have sharp corners: the filled shape with the
                    // inner rect clipped out (ClipOp.Difference).
                    gfx::Path inner;
                    inner.AddRect({ bounds.left + strokeWidth, bounds.top + strokeWidth, bounds.right - strokeWidth, bounds.bottom - strokeWidth });
                    s.DrawPath(gfx::Path::Combine(gfx::PathOperation::Difference, RoundRectPath({ bounds, corner, corner, corner, corner }), inner), brush);
                } else {
                    const Offset r = Shrink(corner, halfStroke);
                    s.DrawPath(RoundRectPath({ { topLeft.x, topLeft.y, topLeft.x + borderSize.width, topLeft.y + borderSize.height }, r, r, r, r }),
                               brush, &stroke);
                }
            } else {
                // setRoundRectPath: the round rect minus the one inset by the stroke.
                gfx::Path p = RoundRectPath(*rr);
                if (!fillArea) {
                    const gfx::RoundRect inset{ { rr->rect.left + strokeWidth, rr->rect.top + strokeWidth, rr->rect.right - strokeWidth, rr->rect.bottom - strokeWidth },
                                                Shrink(rr->topLeft, strokeWidth), Shrink(rr->topRight, strokeWidth),
                                                Shrink(rr->bottomRight, strokeWidth), Shrink(rr->bottomLeft, strokeWidth) };
                    p = gfx::Path::Combine(gfx::PathOperation::Difference, p, RoundRectPath(inset));
                }
                s.DrawPath(p, brush);
            }
            return;
        }
        const gfx::Path& path = std::get<gfx::Path>(outline.value);
        if (fillArea) {
            s.DrawPath(path, brush);
            return;
        }
        // Generic: offscreen over the path bounds, a stroke of twice the width, then everything
        // outside the path cleared (scaled up a pixel to clear the anti-aliased rim).
        gfx::Path boundsPath;
        boundsPath.AddRect(bounds);
        const gfx::Path mask = gfx::Path::Combine(gfx::PathOperation::Difference, boundsPath, path);
        const float w = std::ceil(bounds.Width()), h = std::ceil(bounds.Height());
        s.Save();
        s.BeginLayer({ bounds.left, bounds.top, bounds.left + w, bounds.top + h }, 1);
        const gfx::Stroke doubled{ strokeWidth * 2 };
        s.DrawPath(path, brush, &doubled);
        s.SetBlendMode(gfx::BlendMode::Clear);
        s.Scale((w + 1) / w, (h + 1) / h, { w / 2, h / 2 });
        s.DrawPath(mask, brush);
        s.EndLayer();
        s.Restore();
    }

    namespace {
        struct BorderParams {
            ui::Dp width;
            gfx::Brush brush;
            ShapePtr shape;
            bool operator==(const BorderParams& o) const {
                return width == o.width && brush == o.brush && (shape == o.shape || (shape && o.shape && shape->Equals(*o.shape)));
            }
        };
        struct BorderNode final : ui::ModifierNode, ui::DrawModifier, ui::SemanticsModifier {
            BorderParams params;
            BorderRenderer renderer;
            explicit BorderNode(BorderParams p) : params(std::move(p)) {}
            ui::DrawModifier* AsDraw() override { return this; }
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void OnParamsChanged() { InvalidateDraw(); InvalidateSemantics(); }
            void ApplySemantics(ui::SemanticsConfiguration& c) override { c.shape = params.shape; }
            void Draw(ui::ContentDrawScope& s) override {
                s.DrawContent();
                if (!params.shape) return;
                renderer.DrawBorder(s, params.width, params.brush, params.shape->CreateOutline(s.GetSize(), s.Direction(), s));
            }
        };
    }

    Modifier Border(Modifier m, const BorderStroke& border, ShapePtr shape) { return Border(std::move(m), border.width, border.brush, std::move(shape)); }
    Modifier Border(Modifier m, ui::Dp width, ui::Color color, ShapePtr shape) {
        return Border(std::move(m), width, gfx::Brush::SolidColor(color), std::move(shape));
    }
    Modifier Border(Modifier m, ui::Dp width, gfx::Brush brush, ShapePtr shape) {
        return m.With<BorderNode>(BorderParams{ width, std::move(brush), std::move(shape) });
    }

}
