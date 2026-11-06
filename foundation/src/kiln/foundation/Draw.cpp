#include "kiln/foundation/Draw.h"

#include "kiln/foundation/Compose.h"

namespace kiln::foundation {

    namespace {
        // Lambdas are never equal (DrawBehindElement compares by identity), so every
        // recomposition updates the node and redraws it.
        struct BehindParams {
            std::function<void(DrawScope&)> fn;
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
            std::function<void(ContentDrawScope&)> fn;
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

    Modifier DrawBehind(Modifier m, std::function<void(DrawScope&)> onDraw) {
        return m.With<BehindNode>(BehindParams{ std::move(onDraw) });
    }
    Modifier DrawWithContent(Modifier m, std::function<void(ContentDrawScope&)> onDraw) {
        return m.With<ContentNode>(ContentParams{ std::move(onDraw) });
    }
    void Canvas(const Modifier& modifier, std::function<void(DrawScope&)> onDraw) {
        Spacer(DrawBehind(modifier, std::move(onDraw)));
    }

}
