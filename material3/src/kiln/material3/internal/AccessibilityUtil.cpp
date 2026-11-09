#include "kiln/material3/internal/AccessibilityUtil.h"

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"

namespace kiln::material3::internal {
    namespace {
        struct Params { bool vertical; bool operator==(const Params&) const = default; };
        struct NegativePaddingNode final : ui::ModifierNode, ui::LayoutModifier {
            Params params;
            explicit NegativePaddingNode(Params p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& child, const ui::Constraints& constraints) override {
                const int padding = s.RoundToPx(ui::Dp(10));
                const int x = params.vertical ? 0 : padding, y = params.vertical ? padding : 0;
                auto* p = &child.Measure(constraints.Offset(x * 2, y * 2));
                return s.Layout(p->Width() - x * 2, p->Height() - y * 2, [=](ui::PlacementScope& scope) { scope.Place(*p, -x, -y); });
            }
        };
    }
    ui::Modifier IncreaseHorizontalSemanticsBounds(ui::Modifier m) {
        m = m.With<NegativePaddingNode>(Params{ false });
        m = foundation::Semantics(m, [](ui::SemanticsConfiguration&) {}, true);
        return foundation::Padding(m, ui::Dp(10), ui::Dp(0));
    }
    ui::Modifier IncreaseVerticalSemanticsBounds(ui::Modifier m) {
        m = m.With<NegativePaddingNode>(Params{ true });
        m = foundation::Semantics(m, [](ui::SemanticsConfiguration&) {}, true);
        return foundation::Padding(m, ui::Dp(0), ui::Dp(10));
    }
}
