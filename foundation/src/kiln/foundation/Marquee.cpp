#include "kiln/foundation/Marquee.h"

#include "kiln/animation/Animatable.h"
#include "kiln/animation/Spec.h"
#include "kiln/runtime/SnapshotFlow.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>
#include <optional>

namespace kiln::foundation {

    using namespace ui;

    MarqueeSpacing MarqueeSpacing::Of(Dp spacing) {
        return { std::make_shared<const std::function<int(const Density&, int, int)>>([spacing](const Density& d, int, int) { return d.RoundToPx(spacing); }) };
    }
    MarqueeSpacing MarqueeSpacing::FractionOfContainer(float fraction) {
        return { std::make_shared<const std::function<int(const Density&, int, int)>>([fraction](const Density&, int, int width) { return RoundJava(fraction * float(width)); }) };
    }
    MarqueeSpacing MarqueeDefaults::Spacing() {
        static const MarqueeSpacing spacing = MarqueeSpacing::FractionOfContainer(1.0f / 3.0f);
        return spacing;
    }

    namespace {
        struct MarqueeParams {
            int iterations;
            MarqueeAnimationMode animationMode;
            int delayMillis, initialDelayMillis;
            MarqueeSpacing spacing;
            Dp velocity;
            bool operator==(const MarqueeParams&) const = default;
        };

        // createMarqueeAnimationSpec: a linear tween at `velocity` per iteration, repeated.
        anim::SpecPtr MarqueeSpec(int iterations, float target, int initialDelayMillis, int delayMillis, float pxPerSec) {
            const float pxPerMilli = std::abs(pxPerSec) / 1000.0f;
            const int duration = int(std::ceil(target / pxPerMilli));
            anim::SpecPtr single = anim::Tween(duration, delayMillis, anim::Linear());
            const std::int64_t startOffset = anim::StartOffset(-delayMillis + initialDelayMillis);
            if (iterations == INT_MAX) return anim::InfiniteRepeatable(single, anim::RepeatMode::Restart, startOffset);
            return anim::Repeatable(iterations, single, anim::RepeatMode::Restart, startOffset);
        }

        // MarqueeModifierNode.
        struct MarqueeNode final : ModifierNode, LayoutModifier, DrawModifier, FocusEventModifier {
            MarqueeParams params;
            rt::State<int> contentWidth{ 0 }, containerWidth{ 0 };
            rt::State<bool> hasFocus{ false };
            rt::State<MarqueeAnimationMode> mode;
            rt::State<std::shared_ptr<const MarqueeSpacing>> spacing;
            std::shared_ptr<anim::Animatable> offset = std::make_shared<anim::Animatable>(0.0f);
            float density = 1;
            rt::Job collectJob, animationJob;

            explicit MarqueeNode(MarqueeParams p)
                : params(std::move(p)), mode(params.animationMode), spacing(std::make_shared<const MarqueeSpacing>(params.spacing)) {}
            LayoutModifier* AsLayout() override { return this; }
            DrawModifier* AsDraw() override { return this; }
            FocusEventModifier* AsFocusEvent() override { return this; }

            int SpacingPx() const { return spacing.Get()->Calculate(Density{ density, 1 }, contentWidth.Get(), containerWidth.Get()); }

            void OnAttach() override { RestartAnimation(); }
            void OnDetach() override {
                collectJob = {};
                animationJob = {};
            }
            void OnParamsChanged() {
                spacing.Set(std::make_shared<const MarqueeSpacing>(params.spacing));
                mode.Set(params.animationMode);
                RestartAnimation();
                InvalidateMeasure();
            }
            void OnFocusEvent(FocusState s) override { hasFocus.Set(HasFocus(s)); }

            void RestartAnimation() {
                collectJob = {};
                animationJob = {};
                if (params.iterations <= 0) return;
                // snapshotFlow { the content width with spacing while it should scroll }.collectLatest
                rt::SnapshotFlow<std::optional<float>> flow([this]() -> std::optional<float> {
                    if (contentWidth.Get() <= containerWidth.Get()) return std::nullopt;
                    if (mode.Get() == MarqueeAnimationMode::WhileFocused && !hasFocus.Get()) return std::nullopt;
                    return float(contentWidth.Get() + SpacingPx());
                });
                collectJob = rt::Launch(rt::Dispatcher::Main(), flow.Collect([this](const std::optional<float>& width) {
                    animationJob = {};
                    if (!width) { offset->SnapTo(0.0f); return; }
                    animationJob = rt::Launch(rt::Dispatcher::Main(), Run(offset, *width,
                                                                          MarqueeSpec(params.iterations, *width, params.initialDelayMillis, params.delayMillis, params.velocity.value * density)));
                }));
            }
            static rt::Task<> Run(std::shared_ptr<anim::Animatable> offset, float target, anim::SpecPtr spec) {
                offset->SnapTo(0.0f);
                co_await offset->AnimateTo(target, spec);
                offset->SnapTo(0.0f);
            }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                density = s.density;
                Constraints child = c;
                child.maxWidth = Constraints::Infinity;
                Placeable& p = m.Measure(child);
                const int container = c.ConstrainWidth(p.Width());
                containerWidth.Set(container);
                contentWidth.Set(p.Width());
                return s.Layout(container, p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
            int MinIntrinsicWidth(MeasureScope&, Measurable&, int) override { return 0; }
            int MaxIntrinsicWidth(MeasureScope&, Measurable& m, int height) override { return m.MaxIntrinsicWidth(height); }
            int MinIntrinsicHeight(MeasureScope&, Measurable& m, int) override { return m.MinIntrinsicHeight(Constraints::Infinity); }
            int MaxIntrinsicHeight(MeasureScope&, Measurable& m, int) override { return m.MaxIntrinsicHeight(Constraints::Infinity); }

            void Draw(ContentDrawScope& s) override {
                const float value = offset->Float();   // read: redraws follow the animation
                const int content = contentWidth.Get(), container = containerWidth.Get(), spacingPx = SpacingPx();
                const bool ltr = s.Direction() == LayoutDirection::Ltr;
                float clipWindowOffset;
                if (params.velocity.value > 0) clipWindowOffset = ltr ? value : -value + float(content * 2 + spacingPx - container);
                else clipWindowOffset = ltr ? -value + float(content + spacingPx) : value + float(content - container);
                const bool firstVisible = clipWindowOffset < float(content);
                const bool secondVisible = clipWindowOffset + float(container) > float(content + spacingPx);
                const float secondOffset = float(content + spacingPx);
                s.Save();
                s.ClipRect({ 0, 0, float(container), s.GetSize().height });
                s.Translate({ -clipWindowOffset, 0 });
                if (firstVisible) s.DrawContent();
                if (secondVisible) {
                    s.Save();
                    s.Translate({ secondOffset, 0 });
                    s.DrawContent();
                    s.Restore();
                }
                s.Restore();
            }
        };
    }

    Modifier BasicMarquee(Modifier m, MarqueeOptions o) {
        const int initialDelay = o.initialDelayMillis >= 0 ? o.initialDelayMillis : o.animationMode == MarqueeAnimationMode::Immediately ? o.repeatDelayMillis : 0;
        return m.With<MarqueeNode>(MarqueeParams{ o.iterations, o.animationMode, o.repeatDelayMillis, initialDelay, o.spacing, o.velocity });
    }

}
