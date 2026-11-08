#include "kiln/material3/FloatingActionButton.h"

#include "kiln/animation/Animatable.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/ui/LayoutNode.h"

namespace kiln::material3 {
    namespace {
        struct VisibilityParams {
            bool visible;
            ui::Alignment alignment;
            float targetScale;
            anim::SpecPtr scaleSpec, alphaSpec;
            bool operator==(const VisibilityParams&) const = default;
        };
        rt::Task<> Animate(anim::Animatable& value, float target, anim::SpecPtr spec) {
            co_await value.AnimateTo(target, std::move(spec));
        }
        struct VisibilityNode final : ui::ModifierNode, ui::LayoutModifier, ui::DrawModifier {
            VisibilityParams params;
            anim::Animatable scale, alpha;
            rt::Job scaleJob, alphaJob;
            explicit VisibilityNode(VisibilityParams p) : params(p), scale(p.visible ? 1.0f : 0.0f), alpha(p.visible ? 1.0f : 0.0f) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::DrawModifier* AsDraw() override { return this; }
            void OnAttach() override { Update(); }
            void OnDetach() override { scaleJob.Cancel(); alphaJob.Cancel(); }
            void OnParamsChanged() { if (Owner() && Owner()->GetOwner()) Update(); InvalidateMeasure(); InvalidateDraw(); }
            void Update() {
                // Modifier updates run while composition is applying. Animation values belong
                // to measure/draw, not to the surrounding recompose scope.
                rt::Observation::Untracked([&] {
                    scaleJob.Cancel(); alphaJob.Cancel();
                    const float target = params.visible ? 1.0f : 0.0f;
                    if (scale.Float() != target) scaleJob = rt::Launch(rt::Dispatcher::Main(), Animate(scale, target, params.scaleSpec));
                    if (alpha.Float() != target) alphaJob = rt::Launch(rt::Dispatcher::Main(), Animate(alpha, target, params.alphaSpec));
                });
            }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                if (alpha.Float() == 0) return s.Layout(0, 0, [](ui::PlacementScope&) {});
                auto& p = m.Measure(c);
                return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& ps) { ps.Place(p, 0, 0); });
            }
            void Draw(ui::ContentDrawScope& s) override {
                const auto size = s.GetSize();
                const auto pivot = params.alignment.Align({ 1, 1 }, { int(size.width), int(size.height) }, s.Direction());
                const float factor = params.targetScale + (1 - params.targetScale) * scale.Float();
                const float inset = float(int(s.ToPx(ui::Dp(16))));
                s.Save();
                s.BeginLayer({ -inset, -inset, size.width + inset, size.height + inset }, std::clamp(alpha.Float(), 0.0f, 1.0f));
                s.DrawContent();
                // Scale the recorded layer, including its shadow, rather than scaling the
                // caster before the shadow is generated. The alignment pivot is node-local.
                const float transform[9] = { factor, 0, float(pivot.x) * (1 - factor),
                                             0, factor, float(pivot.y) * (1 - factor), 0, 0, 1 };
                s.EndLayerProjective(transform);
                s.Restore();
            }
        };
    }

    ui::Modifier AnimateFloatingActionButton(ui::Modifier m, bool visible, ui::Alignment alignment, float targetScale,
                                             anim::SpecPtr scaleAnimationSpec, anim::SpecPtr alphaAnimationSpec) {
        const auto motion = rt::Composition::Current() ? MaterialTheme::motionScheme() : MotionScheme::Standard();
        if (!scaleAnimationSpec) scaleAnimationSpec = motion->FastSpatialSpec();
        if (!alphaAnimationSpec) alphaAnimationSpec = motion->FastEffectsSpec();
        return m.With<VisibilityNode>(VisibilityParams{ visible, alignment, targetScale, std::move(scaleAnimationSpec), std::move(alphaAnimationSpec) });
    }
}
