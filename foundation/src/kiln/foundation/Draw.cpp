#include "kiln/foundation/Draw.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"

namespace kiln::foundation {

    namespace {
        // Lambdas are never equal (DrawBehindElement compares by identity), so every
        // recomposition updates the node and redraws it.
        struct BehindParams {
            kiln::Function<void(DrawScope&)> fn;
            bool operator==(const BehindParams&) const { return false; }
        };
        struct BehindNode final : ui::ModifierNode, ui::DrawModifier {
            BehindParams params;
            explicit BehindNode(BehindParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            ui::DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                if (params.fn) params.fn(s);
                s.DrawContent();
            }
        };

        struct ContentParams {
            kiln::Function<void(ContentDrawScope&)> fn;
            bool operator==(const ContentParams&) const { return false; }
        };
        struct ContentNode final : ui::ModifierNode, ui::DrawModifier {
            ContentParams params;
            explicit ContentNode(ContentParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateDraw(); }
            ui::DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override {
                if (params.fn) params.fn(s); else s.DrawContent();
            }
        };
    }

    Modifier DrawBehind(Modifier m, kiln::Function<void(DrawScope&)> onDraw) {
        return m.With<BehindNode>(BehindParams{ std::move(onDraw) });
    }
    Modifier DrawWithContent(Modifier m, kiln::Function<void(ContentDrawScope&)> onDraw) {
        return m.With<ContentNode>(ContentParams{ std::move(onDraw) });
    }
    void Canvas(const Modifier& modifier, kiln::Function<void(DrawScope&)> onDraw) {
        Spacer(DrawBehind(modifier, std::move(onDraw)));
    }


    gfx::ImageBitmap DrawToImageBitmap(const ui::LayoutCoordinates& coordinates) {
        auto* node = const_cast<ui::LayoutNode*>(coordinates.node);
        if (!node || !node->GetOwner()) return {};
        const IntSize size = node->Size();
        if (size.width <= 0 || size.height <= 0) return {};
        const std::vector<std::uint8_t> pixels = node->GetOwner()->RenderToPixels(size.width, size.height, [node](DrawScope& d) {
            node->Draw(static_cast<ContentDrawScope&>(d));
        });
        if (pixels.empty()) return {};
        return gfx::ImageBitmap::FromPixels(size.width, size.height, pixels, true);
    }

}
