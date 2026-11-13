#include "kiln/foundation/GraphicsLayer.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation::layer {

    namespace {
        // The layer's outline as the shape a graphicsLayer clips to and shadows with.
        struct FixedOutline final : gfx::Shape {
            gfx::Outline outline;
            explicit FixedOutline(gfx::Outline o) : outline(std::move(o)) {}
            gfx::Outline CreateOutline(ui::Size, ui::LayoutDirection, const ui::Density&) const override { return outline; }
        };
        ui::Size Or(ui::Size s, IntSize fallback) { return s.width < 0 ? ui::Size{ float(fallback.width), float(fallback.height) } : s; }
    }

    void GraphicsLayer::Record(const ui::Density& density, ui::LayoutDirection direction, IntSize size, const kiln::Function<void(ui::DrawScope&)>& block) {
        ui::ContentDrawScope scope(density, direction, ui::DrawTarget{});
        Record(scope, size, [&](ui::ContentDrawScope& s) { block(s); });
    }

    void GraphicsLayer::Record(ui::ContentDrawScope& scope, IntSize size, const kiln::Function<void(ui::ContentDrawScope&)>& block) {
        ui::DisplayList list;
        const ui::Size outer = scope.GetSize();
        scope.BeginRecording(list);
        scope.SetSize({ float(size.width), float(size.height) });
        block(scope);
        scope.EndRecording();
        scope.SetSize(outer);
        m_List = std::move(list);
        m_Size = size;
        m_Released = false;
    }

    void GraphicsLayer::SetRectOutline(Offset at, ui::Size size) {
        const ui::Size s = Or(size, m_Size);
        m_Outline = gfx::Outline{ ui::Rect{ at.x, at.y, at.x + s.width, at.y + s.height } };
    }

    void GraphicsLayer::SetRoundRectOutline(Offset at, ui::Size size, float cornerRadius) {
        const ui::Size s = Or(size, m_Size);
        m_Outline = gfx::Outline{ gfx::RoundRect::Uniform({ at.x, at.y, at.x + s.width, at.y + s.height }, cornerRadius) };
    }

    gfx::ImageBitmap GraphicsLayer::ToImageBitmap(const ui::Owner& owner) const {
        if (m_Size.width <= 0 || m_Size.height <= 0 || m_Released) return {};
        auto self = std::const_pointer_cast<GraphicsLayer>(shared_from_this());
        const std::vector<std::uint8_t> pixels = owner.RenderToPixels(m_Size.width, m_Size.height, [self](ui::DrawScope& d) {
            const IntOffset at = self->topLeft;
            self->topLeft = {};
            DrawLayer(d, *self);
            self->topLeft = at;
        });
        if (pixels.empty()) return {};
        return gfx::ImageBitmap::FromPixels(m_Size.width, m_Size.height, pixels, true);
    }

    void DrawLayer(ui::DrawScope& scope, GraphicsLayer& layer) {
        if (layer.m_Released || layer.m_List.empty()) return;
        if (scope.IsRecording()) {
            // By reference: the outer recording draws what this layer holds then.
            scope.Record([l = layer.shared_from_this()](ui::DrawScope& d) { DrawLayer(d, *l); });
            return;
        }
        // Every scope a frame draws with is a ContentDrawScope (the owner makes them).
        auto& s = static_cast<ui::ContentDrawScope&>(scope);
        GraphicsLayerParams p = layer;
        const ui::Size size{ float(layer.m_Size.width), float(layer.m_Size.height) };
        if (layer.pivotOffset && size.width > 0 && size.height > 0) {
            p.originX = layer.pivotOffset->x / size.width;
            p.originY = layer.pivotOffset->y / size.height;
        } else {
            p.originX = p.originY = 0.5f;
        }
        if (layer.m_Outline) p.clipShape = std::make_shared<FixedOutline>(*layer.m_Outline);
        p.size = size;
        const ui::Size outer = s.GetSize();
        s.Save();
        s.Translate({ float(layer.topLeft.x), float(layer.topLeft.y) });
        s.SetSize(size);
        detail::DrawWithLayerParams(s, p, [&s, &layer] { s.Replay(layer.m_List); });
        s.Restore();
        s.SetSize(outer);
    }

    GraphicsContext& GraphicsContext::Default() {
        static GraphicsContext context;
        return context;
    }

    GraphicsLayerPtr RememberGraphicsLayer() {
        auto& c = *rt::Composition::Current();
        GraphicsContext* context = c.Read(LocalGraphicsContext);
        auto& layer = c.Remember<GraphicsLayerPtr>([context] { return context->CreateGraphicsLayer(); });
        GraphicsLayerPtr keep = layer;
        c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(keep.get())), [keep, context] {
            return kiln::Function<void()>([keep, context] { context->ReleaseGraphicsLayer(*keep); });
        });
        return layer;
    }

}
