#pragma once

// A retained drawing layer (androidx.compose.ui.graphics.layer.GraphicsLayer): a recording of
// draw commands plus the properties it is drawn with (position, transform, alpha, clip to an
// outline, shadow, compositing). Recorded once, it can be drawn any number of times, anywhere,
// also after what drew it is gone: CapturedAnimatedVisibility draws an exiting content's last
// frame this way, shared elements draw their content in the transition's overlay.

#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Draw.h"

#include <memory>
#include <optional>

namespace kiln::ui { class Owner; }

namespace kiln::foundation::layer {

    using ui::IntOffset;
    using ui::IntSize;
    using ui::Offset;

    // RenderNode's default camera distance (in its 72 px units).
    inline constexpr float DefaultCameraDistance = 8.0f;

    // The layer's properties are GraphicsLayerParams' (alpha, scale, translation, rotation,
    // camera distance, clip, blend mode, colour filter, render effect, shadow, compositing);
    // the pivot, outline and placement are the layer's own.
    class GraphicsLayer : public GraphicsLayerParams, public std::enable_shared_from_this<GraphicsLayer> {
    public:
        // Where the layer is drawn, relative to the scope drawing it.
        IntOffset topLeft;
        // The pivot of rotation and scale; unset: the centre.
        std::optional<Offset> pivotOffset;

        // record(density, layoutDirection, size, block): replaces the recording with what
        // `block` draws now. The scope overload records in the scope being drawn (so the block
        // can draw that scope's content, as drawWithContent { layer.record { drawContent() } }).
        void Record(const ui::Density& density, ui::LayoutDirection direction, IntSize size, const std::function<void(ui::DrawScope&)>& block);
        void Record(ui::ContentDrawScope& scope, IntSize size, const std::function<void(ui::ContentDrawScope&)>& block);
        IntSize Size() const { return m_Size; }

        // The outline (for the shadow, and the clip when `clip` is set); the rectangle of the
        // layer's size until one is set.
        const std::optional<gfx::Outline>& Outline() const { return m_Outline; }
        void SetOutline(gfx::Outline outline) { m_Outline = std::move(outline); }
        void SetRectOutline(Offset topLeft = {}, ui::Size size = { -1, -1 });
        void SetRoundRectOutline(Offset topLeft = {}, ui::Size size = { -1, -1 }, float cornerRadius = 0);
        void SetPathOutline(gfx::Path path) { m_Outline = gfx::Outline{ std::move(path) }; }

        // The recording drawn into an image of the layer's size (toImageBitmap); blocking.
        gfx::ImageBitmap ToImageBitmap(const ui::Owner& owner) const;

        // release: drops the recording; a released layer draws nothing.
        void Release() { m_List.clear(); m_Released = true; }
        bool IsReleased() const { return m_Released; }

        const ui::DisplayList& DisplayList() const { return m_List; }

    private:
        friend void DrawLayer(ui::DrawScope& scope, GraphicsLayer& layer);
        ui::DisplayList m_List;
        IntSize m_Size;
        std::optional<gfx::Outline> m_Outline;
        bool m_Released = false;
    };
    using GraphicsLayerPtr = std::shared_ptr<GraphicsLayer>;

    // DrawScope.drawLayer(graphicsLayer): draws the layer's recording at its topLeft with its
    // properties. Drawn while recording another layer, it is recorded by reference: the outer
    // recording draws whatever this layer holds when it is drawn.
    void DrawLayer(ui::DrawScope& scope, GraphicsLayer& layer);

    // setOutline(outline).
    inline void SetOutline(GraphicsLayer& layer, gfx::Outline outline) { layer.SetOutline(std::move(outline)); }

    // GraphicsContext: creates and releases layers (LocalGraphicsContext provides one).
    class GraphicsContext {
    public:
        virtual ~GraphicsContext() = default;
        virtual GraphicsLayerPtr CreateGraphicsLayer() { return std::make_shared<GraphicsLayer>(); }
        virtual void ReleaseGraphicsLayer(GraphicsLayer& layer) { layer.Release(); }
        static GraphicsContext& Default();
    };
    inline rt::Local<GraphicsContext*> LocalGraphicsContext{ &GraphicsContext::Default() };

    // rememberGraphicsLayer(): a layer from the current GraphicsContext, released when it
    // leaves the composition.
    GraphicsLayerPtr RememberGraphicsLayer();

}
