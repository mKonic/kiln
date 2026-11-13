#include "kiln/material3/WavyProgressIndicator.h"
#include <kiln/Function.h>

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/internal/AccessibilityUtil.h"
#include "kiln/material3/internal/ProgressIndicator.h"
#include "kiln/material3/internal/WavyProgress.h"
#include "kiln/material3/tokens/MotionTokens.h"
#include "kiln/material3/tokens/ProgressIndicatorTokens.h"

#include <array>
#include <cmath>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    namespace {
        struct Params {
            bool circular;
            kiln::Function<float()> progress;
            kiln::Function<float(float)> amplitude;
            Color color, track;
            gfx::Stroke stroke, trackStroke;
            Dp gap, stop, wavelength, speed, indeterminateWavelength, indeterminateSpeed;
            bool operator==(const Params&) const { return false; } // refresh callbacks on recomposition
        };
        struct WavyNode final : ModifierNode, DrawModifier {
            Params params;
            internal::WavyProgressState motion;
            internal::LinearWavyDrawingCache linear;
            internal::CircularWavyDrawingCache circular;
            std::unique_ptr<anim::Animatable> amplitude;
            rt::Job amplitudeJob, clockJob;
            float targetAmplitude = -1, waveOffset = 0;
            Size size;
            bool attached = false;
            explicit WavyNode(Params p) : params(std::move(p)) { motion.circular = params.circular; }
            DrawModifier* AsDraw() override { return this; }
            void OnAttach() override {
                attached = true;
                if (!std::isfinite(params.progress())) motion.StartIndeterminate();
                InvalidateDraw();
            }
            void OnDetach() override {
                attached = false;
                clockJob.Cancel(); amplitudeJob.Cancel(); amplitude.reset(); targetAmplitude = -1;
                motion = {}; motion.circular = params.circular;
            }
            void OnParamsChanged() { InvalidateDraw(); }
            float DeterminateProgress() const { const float v = params.progress(); return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0; }
            float TargetAmplitude() const {
                const float value = params.amplitude(motion.indeterminate ? NAN : DeterminateProgress());
                return std::isnan(value) ? 1 : std::clamp(value, 0.0f, 1.0f);
            }
            void UpdateAmplitude() {
                const float target = TargetAmplitude();
                if (!amplitude) { amplitude = std::make_unique<anim::Animatable>(target); targetAmplitude = target; }
                if (target == targetAmplitude || !attached) return;
                targetAmplitude = target;
                amplitudeJob.Cancel();
                const auto easing = amplitude->Float() < target ? tokens::MotionTokens::EasingStandardCubicBezier : tokens::MotionTokens::EasingEmphasizedAccelerateCubicBezier;
                amplitudeJob = rt::Launch(rt::Dispatcher::Main(), AnimateAmplitude(target, anim::Tween(500, 0, easing.ToEasing())));
            }
            rt::Task<> AnimateAmplitude(float target, anim::SpecPtr spec) { co_await amplitude->AnimateTo(target, spec); }
            Dp Wavelength() const { return !params.circular && motion.indeterminate ? params.indeterminateWavelength : params.wavelength; }
            Dp Speed() const { return !params.circular && motion.indeterminate ? params.indeterminateSpeed : params.speed; }
            bool WaveMoves() const { return Speed().value > 0 && Wavelength().value > 0 && (!params.circular || targetAmplitude > 0 || (amplitude && amplitude->Float() > 0)); }
            bool NeedsClock() const { return motion.indeterminate || motion.handoff || WaveMoves(); }
            rt::Task<> RunClock() {
                std::int64_t previous = -1;
                while (attached && NeedsClock()) {
                    const auto now = co_await rt::NextFrame();
                    const float scale = anim::DurationScale();
                    if (scale <= 0) { motion.cycleMillis = 0; InvalidateDraw(); co_return; }
                    const float millis = previous < 0 ? 0 : float(now - previous) / 1'000'000 / scale;
                    previous = now;
                    const bool wasIndeterminate = motion.indeterminate;
                    motion.Step(millis, params.progress());
                    if (wasIndeterminate && !motion.indeterminate && !params.circular) {
                        targetAmplitude = TargetAmplitude();
                        amplitudeJob.Cancel();
                        if (amplitude) amplitude->SnapTo(targetAmplitude);
                    }
                    if (WaveMoves()) {
                        const int vertices = params.circular ? circular.VertexCount(size, Wavelength().value * density, params.stroke.width) : 1;
                        // Size and stroke are pixels, so use the draw density cached below.
                        const float duration = std::max(50.0f, float(RoundJava(Wavelength().value / Speed().value * 1000 * vertices)));
                        waveOffset = std::fmod(waveOffset + millis / duration, 1.0f);
                    } else if (Speed().value <= 0 || Wavelength().value <= 0) waveOffset = 0;
                    InvalidateDraw();
                }
            }
            float density = 1;
            void Draw(ContentDrawScope& s) override {
                size = s.GetSize(); density = s.density;
                if (!std::isfinite(params.progress()) && !motion.indeterminate) motion.StartIndeterminate();
                const float scale = anim::DurationScale(); // changes redraw a paused indicator
                if (scale <= 0) {
                    clockJob.Cancel(); waveOffset = 0;
                    if (std::isfinite(params.progress())) { motion = {}; motion.circular = params.circular; }
                    else motion.cycleMillis = 0;
                }
                UpdateAmplitude();
                if (scale > 0 && NeedsClock() && !clockJob.Active()) clockJob = rt::Launch(rt::Dispatcher::Main(), RunClock());
                const float amp = amplitude ? amplitude->Float() : 0;
                if (params.circular) {
                    circular.Update(size, s.ToPx(params.wavelength), motion.EndProgress(params.progress()), amp, amp > 0 && params.speed.value > 0 ? waveOffset : 0,
                                    s.ToPx(params.gap), params.stroke, params.trackStroke, params.speed.value > 0);
                    s.Save();
                    s.Rotate(motion.Rotation(), { size.width / 2, size.height / 2 });
                    s.DrawPath(circular.track, params.track, &params.trackStroke);
                    s.DrawPath(circular.progress, params.color, &params.stroke);
                    s.Restore();
                } else {
                    std::array<float, 4> fractions{};
                    if (motion.indeterminate) {
                        const auto v = internal::LinearProgressMotionAt(std::int64_t(RoundJava(motion.cycleMillis)) * 1'000'000);
                        fractions = { v.firstTail, v.firstHead, motion.drawSecondLine ? v.secondTail : 0, motion.drawSecondLine ? v.secondHead : 0 };
                    } else fractions[1] = motion.EndProgress(params.progress());
                    linear.Update(size, s.ToPx(Wavelength()), std::span<const float>(fractions.data(), motion.indeterminate ? 4 : 2), amp, amp > 0 ? waveOffset : 0, s.ToPx(params.gap), params.stroke, params.trackStroke);
                    s.Save();
                    if (s.Direction() == LayoutDirection::Rtl) s.Rotate(180, { size.width / 2, size.height / 2 });
                    s.DrawPath(linear.track, params.track, &params.trackStroke);
                    for (const auto& path : linear.progress) s.DrawPath(path, params.color, &params.stroke);
                    if (!motion.indeterminate) DrawStop(s, fractions[1]);
                    s.Restore();
                }
            }
            void DrawStop(DrawScope& s, float progress) const {
                float stop = std::min(params.trackStroke.width, s.ToPx(params.stop) * motion.handoffFraction);
                const float offset = stop == params.trackStroke.width ? 0 : params.trackStroke.width / 4;
                float x = size.width - stop - offset;
                const float progressX = size.width * progress + linear.capWidth;
                if (x <= progressX) { stop = std::max(0.0f, stop - (progressX - x)); x = progressX; }
                if (stop <= 0) return;
                const Offset origin{ x, (size.height - stop) / 2 };
                if (params.trackStroke.cap == gfx::StrokeCap::Round) s.DrawRoundRect(params.color, origin, { stop, stop }, stop / 2);
                else s.DrawRect(params.color, origin, { stop, stop });
            }
        };
        Modifier ProgressSemantics(Modifier m, const kiln::Function<float()>& progress) {
            return Semantics(m, [progress](SemanticsConfiguration& config) {
                const float value = progress();
                config.progressBarRangeInfo = std::isfinite(value) ? ProgressBarRangeInfo{ std::clamp(value, 0.0f, 1.0f), 0, 1 } : ProgressBarRangeInfo::Indeterminate();
            }, true);
        }
        Params LinearParams(kiln::Function<float()> progress, const LinearWavyProgressIndicatorOptions& o, bool indeterminate) {
            const auto wavelength = o.wavelength.value_or(indeterminate ? WavyProgressIndicatorDefaults::LinearIndeterminateWavelength : WavyProgressIndicatorDefaults::LinearDeterminateWavelength);
            return { false, std::move(progress), o.amplitude ? o.amplitude : kiln::Function<float(float)>(indeterminate ? [](float) { return 1.0f; } : WavyProgressIndicatorDefaults::indicatorAmplitude),
                o.color.Specified() ? o.color : WavyProgressIndicatorDefaults::indicatorColor(), o.trackColor.Specified() ? o.trackColor : WavyProgressIndicatorDefaults::trackColor(),
                o.stroke.value_or(WavyProgressIndicatorDefaults::linearIndicatorStroke()), o.trackStroke.value_or(WavyProgressIndicatorDefaults::linearTrackStroke()), o.gapSize, indeterminate ? Dp(0) : o.stopSize,
                wavelength, o.waveSpeed.value_or(wavelength), indeterminate ? wavelength : WavyProgressIndicatorDefaults::LinearIndeterminateWavelength,
                indeterminate ? o.waveSpeed.value_or(wavelength) : WavyProgressIndicatorDefaults::LinearIndeterminateWavelength };
        }
        Params CircularParams(kiln::Function<float()> progress, const CircularWavyProgressIndicatorOptions& o, bool indeterminate) {
            return { true, std::move(progress), o.amplitude ? o.amplitude : kiln::Function<float(float)>(indeterminate ? [](float) { return 1.0f; } : WavyProgressIndicatorDefaults::indicatorAmplitude),
                o.color.Specified() ? o.color : WavyProgressIndicatorDefaults::indicatorColor(), o.trackColor.Specified() ? o.trackColor : WavyProgressIndicatorDefaults::trackColor(),
                o.stroke.value_or(WavyProgressIndicatorDefaults::circularIndicatorStroke()), o.trackStroke.value_or(WavyProgressIndicatorDefaults::circularTrackStroke()), o.gapSize, Dp(0), o.wavelength, o.waveSpeed.value_or(o.wavelength), o.wavelength, o.waveSpeed.value_or(o.wavelength) };
        }
    }
    void LinearWavyProgressIndicator(kiln::Function<float()> progress, const Modifier& modifier, const LinearWavyProgressIndicatorOptions& o) {
        auto m = ClipToBounds(SizeMod(ProgressSemantics(internal::IncreaseVerticalSemanticsBounds(modifier), progress), WavyProgressIndicatorDefaults::LinearContainerWidth, WavyProgressIndicatorDefaults::LinearContainerHeight));
        Spacer(m.With<WavyNode>(LinearParams(std::move(progress), o, false)));
    }
    void LinearWavyProgressIndicator(const Modifier& modifier, const LinearWavyProgressIndicatorOptions& o) {
        const kiln::Function<float()> progress = [] { return NAN; };
        auto m = ClipToBounds(SizeMod(ProgressSemantics(internal::IncreaseVerticalSemanticsBounds(modifier), progress), WavyProgressIndicatorDefaults::LinearContainerWidth, WavyProgressIndicatorDefaults::LinearContainerHeight));
        Spacer(m.With<WavyNode>(LinearParams(progress, o, true)));
    }
    void CircularWavyProgressIndicator(kiln::Function<float()> progress, const Modifier& modifier, const CircularWavyProgressIndicatorOptions& o) {
        auto m = SizeMod(modifier, WavyProgressIndicatorDefaults::CircularContainerSize, WavyProgressIndicatorDefaults::CircularContainerSize);
        m = m.With<WavyNode>(CircularParams(progress, o, false));
        Spacer(ProgressSemantics(m, progress));
    }
    void CircularWavyProgressIndicator(const Modifier& modifier, const CircularWavyProgressIndicatorOptions& o) {
        const kiln::Function<float()> progress = [] { return NAN; };
        Box(ProgressSemantics(SizeMod(modifier, WavyProgressIndicatorDefaults::CircularContainerSize, WavyProgressIndicatorDefaults::CircularContainerSize), progress), Alignment::TopStart(), false, [=] {
            Spacer(FillMax({}, Axis::Both).With<WavyNode>(CircularParams(progress, o, true)));
        });
    }
    namespace WavyProgressIndicatorDefaults {
        Color indicatorColor() { return Value(tokens::ProgressIndicatorTokens::ActiveIndicatorColor); }
        Color trackColor() { return Value(tokens::ProgressIndicatorTokens::TrackColor); }
        gfx::Stroke linearIndicatorStroke() { return { rt::Composition::Current()->Read(ui::LocalDensity).ToPx(Dp(4)), 4, gfx::StrokeCap::Round }; }
        gfx::Stroke linearTrackStroke() { return linearIndicatorStroke(); }
        gfx::Stroke circularIndicatorStroke() { return linearIndicatorStroke(); }
        gfx::Stroke circularTrackStroke() { return linearIndicatorStroke(); }
        float indicatorAmplitude(float progress) { return progress <= 0.1f || progress >= 0.95f ? 0 : 1; }
        anim::SpecPtr progressAnimationSpec() { return anim::Tween(500, 0, anim::Linear()); }
    }
}
