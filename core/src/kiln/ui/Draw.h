#pragma once

#include <array>

// Drawing during the draw pass (spec 03 §ui-graphics, DrawScope). M2 draws straight into the
// frame's QuadRenderer with an accumulated translation; retained layers come with GraphicsLayer.

#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Image.h"
#include "kiln/graphics/Path.h"
#include "kiln/graphics/Vertices.h"
#include "kiln/ui/Geometry.h"

#include <functional>
#include <memory>
#include <vector>

namespace kiln { class QuadRenderer; struct ParagraphData; class AtlasPage; class PathCache; class GradientRamps; class ImageCache; class FilterTable; struct Rgba; struct RectF; }
namespace kiln::gfx { class Path; struct Stroke; struct Brush; }

namespace kiln::ui {

    struct DrawTarget {
        QuadRenderer* quads = nullptr;
        std::function<const AtlasPage*(std::uint32_t)> page;
        PathCache* paths = nullptr;
        GradientRamps* gradients = nullptr;
        ImageCache* images = nullptr;
        FilterTable* filters = nullptr;
        float viewportWidth = 0, viewportHeight = 0;
    };

    // Corner radii of a clip: top-left, top-right, bottom-right, bottom-left.
    using ClipRadii = std::array<float, 4>;
    // The intersection of two rounded rects in one space when it is itself a rounded rect (each
    // corner belongs to one of them, or is square where their straight edges cross).
    bool IntersectRoundedRects(const Rect& a, const ClipRadii& ra, const Rect& b, const ClipRadii& rb, Rect& out, ClipRadii& radii);

    class DrawScope : public Density {
    public:
        DrawScope(Density d, LayoutDirection dir, DrawTarget target) : Density(d), m_Dir(dir), m_Target(target) {}

        Size GetSize() const { return m_Size; }
        LayoutDirection Direction() const { return m_Dir; }

        void DrawRect(Color color, Offset topLeft, Size size);
        void DrawRoundRect(Color color, Offset topLeft, Size size, float cornerRadius);
        void DrawParagraph(const ParagraphData& paragraph, Offset topLeft, Color color);
        // Fills the path, or strokes it when `stroke` is given (DrawScope.drawPath).
        void DrawPath(const gfx::Path& path, Color color, const gfx::Stroke* stroke = nullptr);
        // drawImage(image, srcOffset, srcSize, dstOffset, dstSize, alpha): src in image pixels.
        void DrawImage(const gfx::ImageBitmap& image, Rect src, Offset dstOffset, Size dstSize, float alpha = 1,
                       const gfx::ColorFilter* colorFilter = nullptr, gfx::FilterQuality quality = gfx::FilterQuality::Low);
        // Brush overloads; `alpha` multiplies the brush (gradient geometry is in scope coordinates).
        void DrawRect(const gfx::Brush& brush, Offset topLeft, Size size, float alpha = 1);
        void DrawRoundRect(const gfx::Brush& brush, Offset topLeft, Size size, float cornerRadius, float alpha = 1);
        void DrawPath(const gfx::Path& path, const gfx::Brush& brush, const gfx::Stroke* stroke = nullptr, float alpha = 1,
                      const gfx::ColorFilter* colorFilter = nullptr);

        // drawVertices: triangles with per-vertex colours (or `color` when the vertices have
        // none), under the current transform and clip.
        void DrawVertices(const gfx::Vertices& vertices, Color color = Color(0xff000000), float alpha = 1);

        // Transform (affine, local -> device). Each call composes onto the current one.
        void Translate(Offset o);
        // Scales about `pivot` (local coordinates).
        void Scale(float sx, float sy, Offset pivot);
        // Rotates clockwise (y-down) by `degrees` about `pivot`.
        void Rotate(float degrees, Offset pivot);
        void Concat(const gfx::Matrix& m);
        const gfx::Matrix& CurrentTransform() const { return m_State.m; }
        void MultiplyAlpha(float a) { m_State.alpha *= a; }
        // Blend mode of the draws that follow, until the Restore (DrawScope's blendMode argument).
        // Modes other than SrcOver read what is drawn below (a copy of it), so they cost a pass.
        void SetBlendMode(gfx::BlendMode mode) { m_State.blend = mode; }
        // Intersects the clip with a local rect, rounded by `radius` (local px). Clips that
        // combine into one rounded rect (per-corner radii) stay analytic; others (a rect cutting
        // into an outer rounded corner, clips in different rotations) clip through a masked layer
        // that ends with the Restore.
        void ClipRect(Rect local, float radius = 0);
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
        void Save() { m_Stack.push_back(m_State); }
        void Restore();
        // Ends the layers that clips left open (the end of a frame).
        void Finish();
        void SetSize(Size s) { m_Size = s; }

    private:
        struct State {
            gfx::Matrix m;
            float alpha = 1;
            bool clip = false;
            Rect clipRect;             // in clip space (device px when clipInverse is identity)
            ClipRadii clipRadii{};
            gfx::Matrix clipInverse;   // device -> clip space
            int clipLayers = 0;        // masked layers opened by clips
            gfx::BlendMode blend = gfx::BlendMode::SrcOver;
        };
        void ApplyBlend(gfx::BlendMode mode);
        void BeginLayerImpl(Rect bounds, float alpha, const gfx::Path* clipPath, bool forClip, gfx::BlendMode blend = gfx::BlendMode::SrcOver,
                            const gfx::ColorFilter* colorFilter = nullptr);
        void EndLayerPlain(float stretchX, float stretchY);
        void EndClipLayers() { while (!m_Layers.empty() && m_Layers.back().forClip) EndLayerPlain(0, 0); }
        bool AxisAligned() const { return m_State.m.b == 0 && m_State.m.c == 0; }
        void ApplyTransform();
        void ApplyClip();
        // Paint for a gradient brush; false for a solid one (its colour is returned in `solid`).
        bool MakePaint(const gfx::Brush& brush, float alpha, void* paint, Color& solid, float& outAlpha);
        void PathCoverage(const gfx::Path& path, const gfx::Stroke* stroke, kiln::Rgba tint, const void* paint, int filterRow = -1);
        int FilterRow(const gfx::ColorFilter* f);
        // The cached coverage mask of a path under the current transform; device origin in ox/oy.
        const void* PathMask(const gfx::Path& path, const gfx::Stroke* stroke, float& ox, float& oy);
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
    };

    class ContentDrawScope : public DrawScope {
    public:
        using DrawScope::DrawScope;
        void DrawContent() { if (m_Content) m_Content(); }
        void SetContent(std::function<void()> c) { m_Content = std::move(c); }
    private:
        std::function<void()> m_Content;
    };

}
