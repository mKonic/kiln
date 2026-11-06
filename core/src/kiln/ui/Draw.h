#pragma once

// Drawing during the draw pass (spec 03 §ui-graphics, DrawScope). M2 draws straight into the
// frame's QuadRenderer with an accumulated translation; retained layers come with GraphicsLayer.

#include "kiln/graphics/Path.h"
#include "kiln/ui/Geometry.h"

#include <functional>
#include <memory>
#include <vector>

namespace kiln { class QuadRenderer; struct ParagraphData; class AtlasPage; class PathCache; class GradientRamps; class ImageCache; class FilterTable; struct Rgba; }
namespace kiln::gfx { class Path; struct Stroke; struct Brush; class ImageBitmap; struct ColorFilter; }

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
                       const gfx::ColorFilter* colorFilter = nullptr);
        // Brush overloads; `alpha` multiplies the brush (gradient geometry is in scope coordinates).
        void DrawRect(const gfx::Brush& brush, Offset topLeft, Size size, float alpha = 1);
        void DrawRoundRect(const gfx::Brush& brush, Offset topLeft, Size size, float cornerRadius, float alpha = 1);
        void DrawPath(const gfx::Path& path, const gfx::Brush& brush, const gfx::Stroke* stroke = nullptr, float alpha = 1,
                      const gfx::ColorFilter* colorFilter = nullptr);

        // Transform (affine, local -> device). Each call composes onto the current one.
        void Translate(Offset o);
        // Scales about `pivot` (local coordinates).
        void Scale(float sx, float sy, Offset pivot);
        // Rotates clockwise (y-down) by `degrees` about `pivot`.
        void Rotate(float degrees, Offset pivot);
        void Concat(const gfx::Matrix& m);
        const gfx::Matrix& CurrentTransform() const { return m_State.m; }
        void MultiplyAlpha(float a) { m_State.alpha *= a; }
        // Intersects the clip with a local rect, rounded by `radius` (local px). Nested rounded
        // clips keep only the innermost rounding; under rotation the innermost clip replaces
        // the outer one.
        void ClipRect(Rect local, float radius = 0);
        // Offscreen layer (saveLayerAlpha): content drawn until EndLayer composites as one image
        // with `alpha`, clipped to `bounds` (local). Inherited alpha applies at compositing.
        // With `clipPath` (local), the layer is masked to it (anti-aliased clip by path).
        void BeginLayer(Rect bounds, float alpha, const gfx::Path* clipPath = nullptr);
        void EndLayer(float stretchX = 0, float stretchY = 0);   // with a stretch overscroll
        void Save() { m_Stack.push_back(m_State); }
        void Restore();
        void SetSize(Size s) { m_Size = s; }

    private:
        struct State {
            gfx::Matrix m;
            float alpha = 1;
            bool clip = false;
            Rect clipRect;             // in clip space (device px when clipInverse is identity)
            float clipRadius = 0;
            gfx::Matrix clipInverse;   // device -> clip space
        };
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
        struct Layer { State saved; float alpha; Rect device; bool active; std::shared_ptr<gfx::Path> clip; gfx::Matrix clipMatrix; };
        std::vector<Layer> m_Layers;
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
