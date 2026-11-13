#pragma once

#include <array>
#include <kiln/Function.h>

// Drawing during the draw pass (spec 03 §ui-graphics, DrawScope). M2 draws straight into the
// frame's QuadRenderer with an accumulated translation; retained layers come with GraphicsLayer.

#include "kiln/graphics/Brush.h"
#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Image.h"
#include "kiln/graphics/Path.h"
#include "kiln/graphics/PathShadow.h"
#include "kiln/graphics/Shadow.h"
#include "kiln/graphics/Shape.h"
#include "kiln/graphics/Vertices.h"
#include "kiln/ui/Geometry.h"

#include <functional>
#include <span>
#include <memory>
#include <vector>

namespace kiln { class QuadRenderer; struct ParagraphData; class AtlasPage; class PathCache; class GradientRamps; class ImageCache; class FilterTable; struct Rgba; struct RectF; }
namespace kiln::gfx { class Path; struct Stroke; struct Brush; }

namespace kiln::ui {

    struct DrawTarget {
        QuadRenderer* quads = nullptr;
        kiln::Function<const AtlasPage*(std::uint32_t)> page;
        PathCache* paths = nullptr;
        GradientRamps* gradients = nullptr;
        ImageCache* images = nullptr;
        FilterTable* filters = nullptr;
        float viewportWidth = 0, viewportHeight = 0;
        gfx::ShadowLight shadowLight;   // the window's light for elevation shadows
    };

    // Corner radii of a clip: top-left, top-right, bottom-right, bottom-left.
    using ClipRadii = std::array<float, 4>;
    // The intersection of two rounded rects in one space when it is itself a rounded rect (each
    // corner belongs to one of them, or is square where their straight edges cross).
    bool IntersectRoundedRects(const Rect& a, const ClipRadii& ra, const Rect& b, const ClipRadii& rb, Rect& out, ClipRadii& radii);

    // DrawStyle: fill the shape, or stroke its outline (Fill / Stroke).
    struct DrawStyle {
        std::optional<gfx::Stroke> stroke;
        static DrawStyle Fill() { return {}; }
        static DrawStyle Stroke(gfx::Stroke s) { return { std::move(s) }; }
        bool operator==(const DrawStyle&) const = default;
    };
    // DefaultAlpha (Paint.kt): fully opaque.
    inline constexpr float DefaultAlpha = 1.0f;

    // ClipOp: keep what is inside the clip (Intersect) or what is outside it (Difference).
    enum class ClipOp : std::uint8_t { Difference, Intersect };
    // PointMode (drawPoints).
    enum class PointMode : std::uint8_t { Points, Lines, Polygon };

    class DrawScope;
    // A display list (a RenderNode's recording): commands replayed onto a scope, in the
    // coordinates of where the recording began.
    using DisplayList = std::vector<kiln::Function<void(DrawScope&)>>;

    class DrawScope : public Density {
    public:
        DrawScope(Density d, LayoutDirection dir, DrawTarget target) : Density(d), m_Dir(dir), m_Target(target) {}

        Size GetSize() const { return m_Size; }
        LayoutDirection Direction() const { return m_Dir; }
        // Each layout node draws with its own direction (a subtree can differ from the window).
        void SetDirection(LayoutDirection dir) { m_Dir = dir; }

        void DrawRect(Color color, Offset topLeft, Size size);
        void DrawRoundRect(Color color, Offset topLeft, Size size, float cornerRadius);
        void DrawParagraph(const ParagraphData& paragraph, Offset topLeft, Color color);
        // Per-run colours (GlyphQuad::run); a run without one, or unspecified, uses `color`.
        void DrawParagraph(const ParagraphData& paragraph, Offset topLeft, Color color, std::span<const Color> runColors);
        // Fills the path, or strokes it when `stroke` is given (DrawScope.drawPath).
        void DrawPath(const gfx::Path& path, Color color, const gfx::Stroke* stroke = nullptr);
        // drawImage(image, srcOffset, srcSize, dstOffset, dstSize, alpha): src in image pixels.
        void DrawImage(const gfx::ImageBitmap& image, Rect src, Offset dstOffset, Size dstSize, float alpha = 1,
                       const gfx::ColorFilter* colorFilter = nullptr, gfx::FilterQuality quality = gfx::FilterQuality::Low);
        // Bitmap shader alpha (R) and independent geometry coverage (G), filled with a brush.
        // An embedded external surface's frame (ExternalFrame::page) filling the rect; an
        // opaque one ignores the frame's alpha.
        void DrawExternalFrame(const AtlasPage& page, Offset dst, Size size, float alpha = 1, bool opaque = true);
        void DrawMaskShader(const gfx::ImageBitmap& mask, const gfx::Brush& brush, Offset origin, float alpha = 1,
                            const gfx::ColorFilter* colorFilter = nullptr);
        // The platform press ripple (RippleDrawable's patterned style, drawn by HWUI's
        // AnimatedRippleDrawable): RippleShader over an anti-aliased circle of `radius` around
        // `origin`. maxRadius is the shader's in_maxRadius, resolution its in_resolution;
        // turbulencePhase drives the noise and turbulence uniforms as HWUI derives them.
        struct Ripple {
            Offset origin, touch;
            float radius = 0, progress = 0, maxRadius = 0;
            Size resolution{ 1, 1 };
            float turbulencePhase = 0;
            Color color, sparkleColor{ 0x8dffffffu };
        };
        void DrawRipple(const Ripple& ripple);
        // illuminate's light shader over `rect` (local): `uniforms` is whole rows of 24 floats in
        // the layout quad_body.glsl's lightColor reads (the alpha slot is filled here).
        void DrawLighting(Rect rect, std::vector<float> uniforms);
        // Brush overloads; `alpha` multiplies the brush (gradient geometry is in scope coordinates).
        void DrawRect(const gfx::Brush& brush, Offset topLeft, Size size, float alpha = 1);
        void DrawRoundRect(const gfx::Brush& brush, Offset topLeft, Size size, float cornerRadius, float alpha = 1);
        // A rounded rect with its own radius per corner (a drawer's or sheet's shape): filled
        // through the analytic rounded clip when its corners are circular and the clip combines
        // with it, else as a path.
        void DrawRoundRect(const gfx::Brush& brush, const gfx::RoundRect& rr, float alpha = 1);
        void DrawPath(const gfx::Path& path, const gfx::Brush& brush, const gfx::Stroke* stroke = nullptr, float alpha = 1,
                      const gfx::ColorFilter* colorFilter = nullptr);

        // Canvas.drawVertices as Android draws it (hwui's JNI over SkCanvas::drawVertices), under
        // the current transform and clip: with texture coordinates, the paint's shader at them
        // modulated by the vertex colours; without, the vertex colours (else the paint colour,
        // opaque black for a shader brush). Then alpha, the colour filter and the blend mode.
        // Composite brushes draw as a shader-less paint.
        void DrawVertices(const gfx::Vertices& vertices, const gfx::Brush& paint, float alpha = 1, const gfx::ColorFilter* colorFilter = nullptr,
                          gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawVertices(const gfx::Vertices& vertices, Color color = Color(0xff000000), float alpha = 1);

        // An elevation shadow of `outline` (local) raised `elevation` px under the window's light
        // (HWUI EndReorderBarrierDrawable::drawShadow through SkShadowUtils): ambient and spot
        // shadows tinted by the colours and the theme's alphas, scaled by `casterAlpha` (a
        // translucent caster also shadows the area under itself).
        // `projection` adds a 3D layer's local homography and tilted z plane, before the
        // current affine transform. Without it the caster is flat at `elevation`.
        void DrawShadow(const gfx::Outline& outline, float elevation, Color ambientColor, Color spotColor, float casterAlpha = 1,
                        const gfx::ShadowProjection* projection = nullptr);

        // --- DrawScope's shape calls (DrawScope.kt): a brush (or colour), the shape, then alpha,
        // style (fill or stroke), colour filter and blend mode. A stroke of width 0 is a hairline.
        void DrawLine(const gfx::Brush& brush, Offset start, Offset end, float strokeWidth = 0, gfx::StrokeCap cap = gfx::StrokeCap::Butt,
                      std::shared_ptr<const gfx::PathEffect> pathEffect = nullptr, float alpha = 1, const gfx::ColorFilter* colorFilter = nullptr,
                      gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawLine(Color color, Offset start, Offset end, float strokeWidth = 0, gfx::StrokeCap cap = gfx::StrokeCap::Butt,
                      std::shared_ptr<const gfx::PathEffect> pathEffect = nullptr, float alpha = 1, const gfx::ColorFilter* colorFilter = nullptr,
                      gfx::BlendMode blendMode = gfx::BlendMode::SrcOver) {
            DrawLine(gfx::Brush::SolidColor(color), start, end, strokeWidth, cap, std::move(pathEffect), alpha, colorFilter, blendMode);
        }
        // radius < 0: half the smaller side; center unset: the scope's centre.
        void DrawCircle(const gfx::Brush& brush, float radius = -1, std::optional<Offset> center = std::nullopt, float alpha = 1,
                        const DrawStyle& style = DrawStyle::Fill(), const gfx::ColorFilter* colorFilter = nullptr,
                        gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawCircle(Color color, float radius = -1, std::optional<Offset> center = std::nullopt, float alpha = 1,
                        const DrawStyle& style = DrawStyle::Fill(), const gfx::ColorFilter* colorFilter = nullptr,
                        gfx::BlendMode blendMode = gfx::BlendMode::SrcOver) {
            DrawCircle(gfx::Brush::SolidColor(color), radius, center, alpha, style, colorFilter, blendMode);
        }
        // size unset: the rest of the scope from topLeft.
        void DrawOval(const gfx::Brush& brush, Offset topLeft = {}, std::optional<Size> size = std::nullopt, float alpha = 1,
                      const DrawStyle& style = DrawStyle::Fill(), const gfx::ColorFilter* colorFilter = nullptr,
                      gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawOval(Color color, Offset topLeft = {}, std::optional<Size> size = std::nullopt, float alpha = 1,
                      const DrawStyle& style = DrawStyle::Fill(), const gfx::ColorFilter* colorFilter = nullptr,
                      gfx::BlendMode blendMode = gfx::BlendMode::SrcOver) {
            DrawOval(gfx::Brush::SolidColor(color), topLeft, size, alpha, style, colorFilter, blendMode);
        }
        // Degrees clockwise from 3 o'clock; useCenter closes the arc through the oval's centre.
        void DrawArc(const gfx::Brush& brush, float startAngle, float sweepAngle, bool useCenter, Offset topLeft = {},
                     std::optional<Size> size = std::nullopt, float alpha = 1, const DrawStyle& style = DrawStyle::Fill(),
                     const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawArc(Color color, float startAngle, float sweepAngle, bool useCenter, Offset topLeft = {},
                     std::optional<Size> size = std::nullopt, float alpha = 1, const DrawStyle& style = DrawStyle::Fill(),
                     const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver) {
            DrawArc(gfx::Brush::SolidColor(color), startAngle, sweepAngle, useCenter, topLeft, size, alpha, style, colorFilter, blendMode);
        }
        // Points: a dot per point (round caps make it round); Lines: each pair a segment;
        // Polygon: one line through them all.
        void DrawPoints(const std::vector<Offset>& points, PointMode pointMode, const gfx::Brush& brush, float strokeWidth = 0,
                        gfx::StrokeCap cap = gfx::StrokeCap::Butt, std::shared_ptr<const gfx::PathEffect> pathEffect = nullptr, float alpha = 1,
                        const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawPoints(const std::vector<Offset>& points, PointMode pointMode, Color color, float strokeWidth = 0,
                        gfx::StrokeCap cap = gfx::StrokeCap::Butt, std::shared_ptr<const gfx::PathEffect> pathEffect = nullptr, float alpha = 1,
                        const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver) {
            DrawPoints(points, pointMode, gfx::Brush::SolidColor(color), strokeWidth, cap, std::move(pathEffect), alpha, colorFilter, blendMode);
        }
        // drawRect / drawRoundRect / drawPath with a style (cornerRadius: x / y radii).
        void DrawRect(const gfx::Brush& brush, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                      const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawRoundRect(const gfx::Brush& brush, Offset topLeft, std::optional<Size> size, Offset cornerRadius, float alpha,
                           const DrawStyle& style, const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        void DrawPath(const gfx::Path& path, const gfx::Brush& brush, float alpha, const DrawStyle& style,
                      const gfx::ColorFilter* colorFilter = nullptr, gfx::BlendMode blendMode = gfx::BlendMode::SrcOver);
        // drawImage(image, topLeft): the whole image at its size.
        void DrawImage(const gfx::ImageBitmap& image, Offset topLeft = {}, float alpha = 1);
        // inset / clipPath / withTransform / rotateRad.
        void Inset(float left, float top, float right, float bottom, const kiln::Function<void(DrawScope&)>& draw) {
            WithTransform([&](DrawScope& d) { d.Translate({ left, top }); d.m_Size = { d.m_Size.width - left - right, d.m_Size.height - top - bottom }; }, draw);
        }
        // Clips to a path through a masked layer, until the Restore: inside it (Intersect) or
        // outside it (Difference).
        void ClipPath(const gfx::Path& path, ClipOp op = ClipOp::Intersect);
        // clipRect(clipOp = Difference): everything but the rect.
        void ClipRect(Rect local, ClipOp op) {
            if (op == ClipOp::Intersect) { ClipRect(local); return; }
            gfx::Path p;
            p.AddRect(local);
            ClipPath(p, op);
        }
        void RotateRad(float radians, Offset pivot) { Rotate(radians * 57.29577951308232f, pivot); }
        void Skew(float sx, float sy) { Concat(gfx::Matrix{ 1, sy, sx, 1, 0, 0 }); }
        void WithTransform(const kiln::Function<void(DrawScope&)>& transform, const kiln::Function<void(DrawScope&)>& draw) {
            Save();
            const Size size = m_Size;
            transform(*this);
            draw(*this);
            m_Size = size;
            Restore();
        }

        // Transform (affine, local -> device). Each call composes onto the current one.
        void Translate(Offset o);
        // Scales about `pivot` (local coordinates).
        void Scale(float sx, float sy, Offset pivot);
        // Rotates clockwise (y-down) by `degrees` about `pivot`.
        void Rotate(float degrees, Offset pivot);
        void Concat(const gfx::Matrix& m);
        const gfx::Matrix& CurrentTransform() const { return m_State.m; }
        void MultiplyAlpha(float a) {
            if (m_Recording) m_Recording->push_back([a](DrawScope& d) { d.MultiplyAlpha(a); });
            m_State.alpha *= a;
        }
        // Blend mode of the draws that follow, until the Restore (DrawScope's blendMode argument).
        // Modes other than SrcOver read what is drawn below (a copy of it), so they cost a pass.
        void SetBlendMode(gfx::BlendMode mode) {
            if (m_Recording) m_Recording->push_back([mode](DrawScope& d) { d.SetBlendMode(mode); });
            m_State.blend = mode;
        }
        // Intersects the clip with a local rect, rounded by `radius` (local px). Clips that
        // combine into one rounded rect (per-corner radii) stay analytic; others (a rect cutting
        // into an outer rounded corner, clips in different rotations) clip through a masked layer
        // that ends with the Restore.
        void ClipRect(Rect local, float radius = 0);
        // The same with a radius per corner (circular corners stay analytic; elliptical ones clip
        // through a masked layer).
        void ClipRoundRect(const gfx::RoundRect& rr);
        // Offscreen layer (saveLayerAlpha): content drawn until EndLayer composites as one image
        // with `alpha`, clipped to `bounds` (local). Inherited alpha applies at compositing.
        // With `clipPath` (local), the layer is masked to it (anti-aliased clip by path).
        // The layer composites with `blend` and through `colorFilter` (graphicsLayer.blendMode /
        // colorFilter); its content starts in SrcOver.
        void BeginLayer(Rect bounds, float alpha, const gfx::Path* clipPath = nullptr, gfx::BlendMode blend = gfx::BlendMode::SrcOver,
                        const gfx::ColorFilter* colorFilter = nullptr);
        void EndLayer(float stretchX = 0, float stretchY = 0);   // with a stretch overscroll
        // Composites the layer through a projective transform `h` (row-major 3x3, local
        // coordinates of the BeginLayer call -> local), for 3D-rotated graphics layers.
        void EndLayerProjective(const float h[9]);
        // Ends a layer through render effects: Gaussian blurs in order (RenderEffect.createBlurEffect,
        // sigmas in local units; outside the image the edge repeats, or with `decal` is
        // transparent), then composited moved by `offset` (createOffsetEffect, local units).
        struct LayerBlur { float sigmaX = 0, sigmaY = 0; bool decal = false; };
        void EndLayerEffects(const std::vector<LayerBlur>& blurs, Offset offset = {});
        // Ends a layer through a progressive blur (Modifier.blur(BlurRadiusSpec)): the radius at
        // each point is maxRadius (local px) times the alpha of `intensity` there (a linear or
        // radial gradient in local coordinates).
        void EndLayerProgressiveBlur(float maxRadius, const gfx::Brush& intensity, bool decal);
        void Save() {
            if (m_Recording) m_Recording->push_back([](DrawScope& d) { d.Save(); });
            m_Stack.push_back(m_State);
        }
        void Restore();
        // Ends the layers that clips left open (the end of a frame).
        void Finish();
        void SetSize(Size s) { m_Size = s; }

        // Recording (GraphicsLayer.record): until EndRecording, draws and state changes append to
        // `list` instead of rasterizing, relative to a fresh state (identity transform, no clip,
        // full alpha). Recordings nest.
        void BeginRecording(DisplayList& list);
        void EndRecording();
        bool IsRecording() const { return m_Recording != nullptr; }
        // Appends a command to the recording (or runs it now when not recording).
        void Record(kiln::Function<void(DrawScope&)> op);
        // Replays a recording under the current transform, clip and alpha.
        void Replay(const DisplayList& list);
        // A command that draws live content (a node's children): recorded like Record, and also
        // handed to the collector a discarded recording set, which records that content again
        // without drawing it (an invisible layer keeps the layers inside it current).
        void RecordLive(kiln::Function<void(DrawScope&)> op) {
            if (m_LiveCollector) m_LiveCollector->push_back(op);
            Record(std::move(op));
        }
        // Collects RecordLive commands until set back (returns the previous collector).
        DisplayList* CollectLive(DisplayList* collector) { std::swap(m_LiveCollector, collector); return collector; }
        // The local bounds of what `list` would draw here (after transforms and clips, with the
        // layers it opens counted at their bounds), drawing nothing. Unset when nothing measures
        // it (a headless scope, or one recording); an empty rect when it draws nothing.
        std::optional<Rect> MeasureInk(const DisplayList& list);
        // A record-only scope: layout nodes record their drawing but draw nothing.
        void SetRecordOnly(bool on) { m_RecordOnly = on; }
        bool RecordOnly() const { return m_RecordOnly; }

    private:
        struct State {
            gfx::Matrix m;
            float alpha = 1;
            bool clip = false;
            Rect clipRect;             // in clip space (device px when clipInverse is identity)
            ClipRadii clipRadii{};
            gfx::Matrix clipInverse;   // device -> clip space
            // A plain device rect that clips as well (a rect cutting the rounded clip, say).
            Rect clipOuter{ -1e9f, -1e9f, 1e9f, 1e9f };
            int clipLayers = 0;        // masked layers opened by clips
            gfx::BlendMode blend = gfx::BlendMode::SrcOver;
        };
        void ApplyBlend(gfx::BlendMode mode);
        // Image, composite and shader brushes: the shape drawn as coverage into a layer, then the
        // brush painted over it (SrcIn) across `bounds` (local).
        void FillLayered(const kiln::Function<void()>& coverage, Rect bounds, const gfx::Brush& brush, float alpha, const gfx::ColorFilter* filter);
        void PaintBrush(Rect bounds, const gfx::Brush& brush);
        void PaintImageTiles(Rect bounds, const gfx::Brush& brush);
        void BeginLayerImpl(Rect bounds, float alpha, const gfx::Path* clipPath, bool forClip, gfx::BlendMode blend = gfx::BlendMode::SrcOver,
                            const gfx::ColorFilter* colorFilter = nullptr);
        void EndLayerPlain(float stretchX, float stretchY);
        void EndClipLayers() { while (!m_Layers.empty() && m_Layers.back().forClip) EndLayerPlain(0, 0); }
        bool AxisAligned() const { return m_State.m.b == 0 && m_State.m.c == 0; }
        void ClipRounded(Rect local, const ClipRadii& localRadii);
        void ApplyTransform();
        void ApplyClip();
        // Paint for a gradient brush; false for a solid one (its colour is returned in `solid`).
        bool MakePaint(const gfx::Brush& brush, float alpha, void* paint, Color& solid, float& outAlpha);
        void PathCoverage(const gfx::Path& path, const gfx::Stroke* stroke, kiln::Rgba tint, const void* paint, int filterRow = -1);
        int FilterRow(const gfx::ColorFilter* f);
        // The cached coverage mask of a path under the current transform; device origin in ox/oy.
        const void* PathMask(const gfx::Path& path, const gfx::Stroke* stroke, float& ox, float& oy, const Rect* deviceVisible = nullptr);
        void DrawBlurredShadowPath(const gfx::Path& outline, float sigma, Offset shift, Color color);
        LayoutDirection m_Dir;
        DrawTarget m_Target;
        State m_State;
        std::vector<State> m_Stack;
        struct Layer { State saved; float alpha; Rect device; bool active; std::shared_ptr<gfx::Path> clip; gfx::Matrix clipMatrix; bool forClip = false;
                       gfx::BlendMode blend = gfx::BlendMode::SrcOver; std::shared_ptr<gfx::ColorFilter> filter; };
        std::vector<Layer> m_Layers;
        void CompositeLayer(const Layer& l, const kiln::RectF& rect, const kiln::RectF& uv, const kiln::AtlasPage& page);
        void MaskLayer(const Layer& l);
        Size m_Size;
        DisplayList* m_Recording = nullptr;
        DisplayList* m_LiveCollector = nullptr;
        bool m_RecordOnly = false;
        struct Recording { DisplayList* list; State state; std::vector<State> stack; };
        std::vector<Recording> m_Recordings;
    };

    class ContentDrawScope : public DrawScope {
    public:
        using DrawScope::DrawScope;
        // The content sets the scope's size as it draws; this scope's own size comes back after.
        void DrawContent() {
            if (!m_Content) return;
            const Size size = GetSize();
            m_Content();
            SetSize(size);
        }
        void SetContent(kiln::Function<void()> c) { m_Content = std::move(c); }
        // Sets the content and returns the one it replaces (to put back after).
        kiln::Function<void()> SwapContent(kiln::Function<void()> c) { std::swap(m_Content, c); return c; }
    private:
        kiln::Function<void()> m_Content;
    };

}
