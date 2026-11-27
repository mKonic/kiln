// Compose's Canvas calls (AndroidCanvas.android.kt) onto DrawScope.
#include "kiln/ui/Canvas.h"

namespace kiln::ui {

    Canvas::~Canvas() {
        while (!m_Saves.empty()) Restore();
    }

    void Canvas::Save() {
        m_Scope.Save();
        m_Saves.push_back(false);
    }

    void Canvas::SaveLayer(Rect bounds, const gfx::Paint& paint) {
        m_Scope.Save();
        m_Scope.BeginLayer(bounds, paint.alpha, nullptr, paint.blendMode, FilterOf(paint));
        m_Saves.push_back(true);
    }

    void Canvas::Restore() {
        if (m_Saves.empty()) return;
        if (m_Saves.back()) m_Scope.EndLayer();
        m_Saves.pop_back();
        m_Scope.Restore();
    }

    DrawStyle Canvas::StyleOf(const gfx::Paint& paint) const {
        return paint.style == gfx::PaintingStyle::Stroke ? DrawStyle::Stroke(paint.AsStroke()) : DrawStyle::Fill();
    }

    void Canvas::DrawLine(Offset p1, Offset p2, const gfx::Paint& paint) {
        m_Scope.DrawLine(paint.AsBrush(), p1, p2, paint.strokeWidth, paint.strokeCap, paint.pathEffect, paint.alpha, FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawRect(Rect rect, const gfx::Paint& paint) {
        m_Scope.DrawRect(paint.AsBrush(), { rect.left, rect.top }, rect.GetSize(), paint.alpha, StyleOf(paint), FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawRoundRect(float left, float top, float right, float bottom, float radiusX, float radiusY, const gfx::Paint& paint) {
        m_Scope.DrawRoundRect(paint.AsBrush(), { left, top }, Size{ right - left, bottom - top }, { radiusX, radiusY }, paint.alpha,
                              StyleOf(paint), FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawOval(Rect rect, const gfx::Paint& paint) {
        m_Scope.DrawOval(paint.AsBrush(), { rect.left, rect.top }, rect.GetSize(), paint.alpha, StyleOf(paint), FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawCircle(Offset center, float radius, const gfx::Paint& paint) {
        m_Scope.DrawCircle(paint.AsBrush(), radius, center, paint.alpha, StyleOf(paint), FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawArc(Rect rect, float startAngle, float sweepAngle, bool useCenter, const gfx::Paint& paint) {
        m_Scope.DrawArc(paint.AsBrush(), startAngle, sweepAngle, useCenter, { rect.left, rect.top }, rect.GetSize(), paint.alpha,
                        StyleOf(paint), FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawPath(const gfx::Path& path, const gfx::Paint& paint) {
        m_Scope.DrawPath(path, paint.AsBrush(), paint.alpha, StyleOf(paint), FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawImage(const gfx::ImageBitmap& image, Offset topLeft, const gfx::Paint& paint) {
        const float w = float(image.Width()), h = float(image.Height());
        m_Scope.DrawImage(image, Rect{ 0, 0, w, h }, topLeft, Size{ w, h }, paint.alpha, FilterOf(paint), paint.filterQuality);
    }

    void Canvas::DrawImageRect(const gfx::ImageBitmap& image, IntOffset srcOffset, IntSize srcSize, IntOffset dstOffset, IntSize dstSize,
                               const gfx::Paint& paint) {
        const Rect src{ float(srcOffset.x), float(srcOffset.y), float(srcOffset.x + srcSize.width), float(srcOffset.y + srcSize.height) };
        m_Scope.DrawImage(image, src, { float(dstOffset.x), float(dstOffset.y) }, Size{ float(dstSize.width), float(dstSize.height) },
                          paint.alpha, FilterOf(paint), paint.filterQuality);
    }

    void Canvas::DrawPoints(PointMode mode, const std::vector<Offset>& points, const gfx::Paint& paint) {
        m_Scope.DrawPoints(points, mode, paint.AsBrush(), paint.strokeWidth, paint.strokeCap, paint.pathEffect, paint.alpha,
                           FilterOf(paint), paint.blendMode);
    }

    void Canvas::DrawRawPoints(PointMode mode, std::span<const float> points, const gfx::Paint& paint) {
        std::vector<Offset> offsets;
        offsets.reserve(points.size() / 2);
        for (std::size_t i = 0; i + 1 < points.size(); i += 2) offsets.push_back({ points[i], points[i + 1] });
        DrawPoints(mode, offsets, paint);
    }

    void Canvas::DrawVertices(const gfx::Vertices& vertices, gfx::BlendMode blendMode, const gfx::Paint& paint) {
        m_Scope.DrawVertices(vertices, paint.AsBrush(), paint.alpha, FilterOf(paint), blendMode);
    }

}
