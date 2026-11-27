#pragma once

// Canvas (androidx.compose.ui.graphics.Canvas): the imperative drawing interface, with each call
// taking a gfx::Paint. Here it draws through the DrawScope it wraps (drawIntoCanvas), so its
// transforms and clips are the scope's until the matching Restore.
//
//   ui::DrawIntoCanvas(scope, [](ui::Canvas& canvas) {
//       gfx::Paint paint{ .color = ui::Color(0xFF6750A4), .strokeWidth = 4, .style = gfx::PaintingStyle::Stroke };
//       canvas.DrawCircle({ 50, 50 }, 40, paint);
//   });

#include "kiln/graphics/Matrix4.h"
#include "kiln/graphics/Paint.h"
#include "kiln/graphics/Vertices.h"
#include "kiln/ui/Draw.h"

#include <span>
#include <vector>

namespace kiln::ui {

    class Canvas {
    public:
        explicit Canvas(DrawScope& scope) : m_Scope(scope) {}
        ~Canvas();   // restores what is still saved
        Canvas(const Canvas&) = delete;
        Canvas& operator=(const Canvas&) = delete;

        DrawScope& Scope() { return m_Scope; }

        void Save();
        // saveLayer: until the matching Restore, draws go to an offscreen layer clipped to
        // `bounds`, composited with the paint's alpha, blend mode and colour filter.
        void SaveLayer(Rect bounds, const gfx::Paint& paint);
        void Restore();
        void WithSave(const kiln::Function<void()>& block) { Save(); block(); Restore(); }
        void WithSaveLayer(Rect bounds, const gfx::Paint& paint, const kiln::Function<void()>& block) { SaveLayer(bounds, paint); block(); Restore(); }

        void Translate(float dx, float dy) { m_Scope.Translate({ dx, dy }); }
        void Scale(float sx, float sy) { m_Scope.Scale(sx, sy, {}); }
        void Scale(float sx, float sy, float pivotX, float pivotY) { m_Scope.Scale(sx, sy, { pivotX, pivotY }); }
        void Rotate(float degrees) { m_Scope.Rotate(degrees, {}); }
        void Rotate(float degrees, float pivotX, float pivotY) { m_Scope.Rotate(degrees, { pivotX, pivotY }); }
        void RotateRad(float radians, float pivotX = 0, float pivotY = 0) { Rotate(radians * 57.29577951308232f, pivotX, pivotY); }
        void Skew(float sx, float sy) { m_Scope.Skew(sx, sy); }
        void SkewRad(float sxRad, float syRad) { Skew(std::tan(sxRad), std::tan(syRad)); }
        // concat: the matrix's 2D part (z and perspective are not drawn by a canvas either).
        void Concat(const gfx::Matrix4& m) { m_Scope.Concat(m.ToAffine()); }

        void ClipRect(Rect rect, ClipOp op = ClipOp::Intersect) { m_Scope.ClipRect(rect, op); }
        void ClipRect(float left, float top, float right, float bottom, ClipOp op = ClipOp::Intersect) { ClipRect({ left, top, right, bottom }, op); }
        void ClipPath(const gfx::Path& path, ClipOp op = ClipOp::Intersect) { m_Scope.ClipPath(path, op); }

        void DrawLine(Offset p1, Offset p2, const gfx::Paint& paint);
        void DrawRect(Rect rect, const gfx::Paint& paint);
        void DrawRect(float left, float top, float right, float bottom, const gfx::Paint& paint) { DrawRect({ left, top, right, bottom }, paint); }
        void DrawRoundRect(float left, float top, float right, float bottom, float radiusX, float radiusY, const gfx::Paint& paint);
        void DrawOval(Rect rect, const gfx::Paint& paint);
        void DrawOval(float left, float top, float right, float bottom, const gfx::Paint& paint) { DrawOval({ left, top, right, bottom }, paint); }
        void DrawCircle(Offset center, float radius, const gfx::Paint& paint);
        // Degrees clockwise from 3 o'clock.
        void DrawArc(Rect rect, float startAngle, float sweepAngle, bool useCenter, const gfx::Paint& paint);
        void DrawArc(float left, float top, float right, float bottom, float startAngle, float sweepAngle, bool useCenter, const gfx::Paint& paint) {
            DrawArc({ left, top, right, bottom }, startAngle, sweepAngle, useCenter, paint);
        }
        void DrawArcRad(Rect rect, float startAngleRad, float sweepAngleRad, bool useCenter, const gfx::Paint& paint) {
            DrawArc(rect, startAngleRad * 57.29577951308232f, sweepAngleRad * 57.29577951308232f, useCenter, paint);
        }
        void DrawPath(const gfx::Path& path, const gfx::Paint& paint);
        void DrawImage(const gfx::ImageBitmap& image, Offset topLeft, const gfx::Paint& paint);
        // A source region drawn into a destination one (sizes unset: the whole image, its size).
        void DrawImageRect(const gfx::ImageBitmap& image, IntOffset srcOffset, IntSize srcSize, IntOffset dstOffset, IntSize dstSize,
                           const gfx::Paint& paint);
        void DrawImageRect(const gfx::ImageBitmap& image, const gfx::Paint& paint) {
            const IntSize s{ image.Width(), image.Height() };
            DrawImageRect(image, {}, s, {}, s, paint);
        }
        void DrawPoints(PointMode mode, const std::vector<Offset>& points, const gfx::Paint& paint);
        // drawRawPoints: x, y pairs.
        void DrawRawPoints(PointMode mode, std::span<const float> points, const gfx::Paint& paint);
        void DrawVertices(const gfx::Vertices& vertices, gfx::BlendMode blendMode, const gfx::Paint& paint);

    private:
        DrawStyle StyleOf(const gfx::Paint& paint) const;
        const gfx::ColorFilter* FilterOf(const gfx::Paint& paint) const { return paint.colorFilter ? &*paint.colorFilter : nullptr; }
        DrawScope& m_Scope;
        std::vector<bool> m_Saves;   // true: a layer
    };

    // drawIntoCanvas: the scope as a Canvas, for the block.
    inline void DrawIntoCanvas(DrawScope& scope, const kiln::Function<void(Canvas&)>& block) {
        Canvas canvas(scope);
        block(canvas);
    }

}
