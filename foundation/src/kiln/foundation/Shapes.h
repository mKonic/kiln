#pragma once

// Corner-based shapes (foundation/shape): CornerSize, RoundedCornerShape, CutCornerShape and
// their absolute (non-mirroring) forms, CircleShape, RectangleShape; outline drawing helpers.

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/Shape.h"
#include "kiln/graphics/shapes/Morph.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/Geometry.h"

#include <memory>

namespace kiln::foundation {

    using ui::Dp;
    using ui::Size;

    // CornerSize: dp + px + percent of the shape's min dimension. Every Compose CornerSize is one
    // of the three, and toPx is linear in them, so lerp is componentwise.
    struct CornerSize {
        float dp = 0, px = 0, percent = 0;
        static CornerSize Of(Dp d) { return { d.value, 0, 0 }; }
        static CornerSize Px(float p) { return { 0, p, 0 }; }
        static CornerSize Percent(float p) { return { 0, 0, p }; }
        float ToPx(Size shape, const ui::Density& d) const { return d.ToPx(Dp(dp)) + px + shape.MinDimension() * percent / 100.0f; }
        static CornerSize Lerp(const CornerSize& a, const CornerSize& b, float t) {
            return { a.dp + (b.dp - a.dp) * t, a.px + (b.px - a.px) * t, a.percent + (b.percent - a.percent) * t };
        }
        bool operator==(const CornerSize&) const = default;
    };

    class CornerBasedShape : public gfx::Shape {
    public:
        CornerBasedShape(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) : topStart(ts), topEnd(te), bottomEnd(be), bottomStart(bs) {}
        // CornerBasedShape.createOutline: start corners and end corners each scaled down
        // together to fit the min dimension.
        gfx::Outline CreateOutline(Size size, ui::LayoutDirection dir, const ui::Density& d) const final;
        virtual gfx::Outline CreateOutline(Size size, float ts, float te, float be, float bs, ui::LayoutDirection dir) const = 0;
        // CornerBasedShape.copy: the same kind of shape with other corners.
        virtual std::shared_ptr<const CornerBasedShape> Copy(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) const = 0;
        CornerSize topStart, topEnd, bottomEnd, bottomStart;
    };

    class RoundedCornerShape final : public CornerBasedShape {
    public:
        using CornerBasedShape::CornerBasedShape;
        bool absolute = false;   // AbsoluteRoundedCornerShape: no RTL mirroring
        gfx::Outline CreateOutline(Size size, float ts, float te, float be, float bs, ui::LayoutDirection dir) const override;
        bool Equals(const gfx::Shape& o) const override;
        std::shared_ptr<const CornerBasedShape> Copy(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) const override;
    };

    class CutCornerShape final : public CornerBasedShape {
    public:
        using CornerBasedShape::CornerBasedShape;
        bool absolute = false;
        gfx::Outline CreateOutline(Size size, float ts, float te, float be, float bs, ui::LayoutDirection dir) const override;
        bool Equals(const gfx::Shape& o) const override;
        std::shared_ptr<const CornerBasedShape> Copy(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) const override;
    };

    class RectangleShapeImpl final : public gfx::Shape {
    public:
        gfx::Outline CreateOutline(Size size, ui::LayoutDirection, const ui::Density&) const override { return { ui::Rect{ 0, 0, size.width, size.height } }; }
        bool Equals(const gfx::Shape& o) const override { return dynamic_cast<const RectangleShapeImpl*>(&o) != nullptr; }
    };

    using ShapePtr = std::shared_ptr<const gfx::Shape>;
    ShapePtr RoundedCorner(CornerSize all);
    ShapePtr RoundedCorner(CornerSize topStart, CornerSize topEnd, CornerSize bottomEnd, CornerSize bottomStart);
    inline ShapePtr RoundedCorner(Dp all) { return RoundedCorner(CornerSize::Of(all)); }
    inline ShapePtr RoundedCornerPercent(float percent) { return RoundedCorner(CornerSize::Percent(percent)); }
    ShapePtr AbsoluteRoundedCorner(CornerSize topLeft, CornerSize topRight, CornerSize bottomRight, CornerSize bottomLeft);
    ShapePtr CutCorner(CornerSize all);
    ShapePtr CutCorner(CornerSize topStart, CornerSize topEnd, CornerSize bottomEnd, CornerSize bottomStart);
    inline ShapePtr CutCorner(Dp all) { return CutCorner(CornerSize::Of(all)); }
    ShapePtr AbsoluteCutCorner(CornerSize topLeft, CornerSize topRight, CornerSize bottomRight, CornerSize bottomLeft);
    const ShapePtr& CircleShape();      // RoundedCornerShape(50%)
    const ShapePtr& RectangleShape();
    // Same kind of corner-based shape: corners interpolated; otherwise the nearer one by t.
    ShapePtr Lerp(const ShapePtr& a, const ShapePtr& b, float t);

    // RoundedPolygon / Morph to Path (material3 internal/ShapeUtil.kt pathFromCubics): the path is
    // rotated about the origin so its first cubic starts at `startAngle` degrees (270 = 12
    // o'clock); repeatPath draws the outline twice (for stroked progress animations).
    gfx::Path PolygonToPath(const gfx::shapes::RoundedPolygon& polygon, int startAngle = 270, bool repeatPath = false, bool closePath = true);
    gfx::Path MorphToPath(const gfx::shapes::Morph& morph, float progress, int startAngle = 270, bool repeatPath = false,
                          bool closePath = true, float pivotX = 0, float pivotY = 0);
    // RoundedPolygon.toShape (MaterialShapes.kt): a normalized polygon scaled to the size and
    // centred on it.
    ShapePtr ToShape(const gfx::shapes::RoundedPolygon& polygon, int startAngle = 0);

    // drawOutline: rectangles and uniform circular rounded rects take the SDF fast path.
    void DrawOutline(ui::DrawScope& s, const gfx::Outline& outline, ui::Color color);
    void DrawOutline(ui::DrawScope& s, const gfx::Outline& outline, const gfx::Brush& brush, float alpha = 1);

}
