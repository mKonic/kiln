#include "kiln/foundation/FrameRate.h"

namespace kiln::foundation {

    namespace {
        struct FrameRateParams {
            float value;
            bool operator==(const FrameRateParams& o) const { return value == o.value || (value != value && o.value != o.value); }
        };
        class FrameRateNode final : public ui::ModifierNode, public ui::DrawModifier {
        public:
            FrameRateParams params;
            explicit FrameRateNode(FrameRateParams p) : params(p) {}
            void OnParamsChanged() { InvalidateDraw(); }
            ui::DrawModifier* AsDraw() override { return this; }
            void Draw(ui::ContentDrawScope& scope) override {
                if (Owner() && Owner()->GetOwner()) Owner()->GetOwner()->VoteFrameRate(params.value);
                scope.DrawContent();
            }
        };
    }

    ui::Modifier PreferredFrameRate(ui::Modifier m, float frameRate) {
        return m.With<FrameRateNode>(FrameRateParams{ std::clamp(frameRate, 0.0f, 360.0f) });
    }
    ui::Modifier PreferredFrameRate(ui::Modifier m, ui::FrameRateCategory category) {
        return m.With<FrameRateNode>(FrameRateParams{ category.value });
    }

}
