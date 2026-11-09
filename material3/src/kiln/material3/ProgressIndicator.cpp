#include "kiln/material3/ProgressIndicator.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/internal/AccessibilityUtil.h"
#include "kiln/material3/internal/ProgressIndicator.h"
#include "kiln/material3/tokens/MotionTokens.h"
#include "kiln/material3/tokens/ProgressIndicatorTokens.h"

#include <cmath>
#include <numbers>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using gfx::StrokeCap;
    namespace {
        float Coerce(float value) { return std::clamp(value, 0.0f, 1.0f); }
        Modifier ProgressSemantics(Modifier m, const std::function<float()>& progress = {}) {
            return Semantics(m, [progress](SemanticsConfiguration& config) {
                const float value = progress ? Coerce(progress()) : 0;
                config.progressBarRangeInfo = progress ? ProgressBarRangeInfo{ std::isnan(value) ? 0 : value, 0, 1 } : ProgressBarRangeInfo::Indeterminate();
            }, true);
        }
        void LinearSegment(DrawScope& s, float start, float end, Color color, float strokeWidth, StrokeCap cap) {
            const auto size = s.GetSize();
            if (!std::isfinite(start) || !std::isfinite(end) || size.width <= 0 || strokeWidth <= 0) return;
            float a = (s.Direction() == LayoutDirection::Ltr ? start : 1 - end) * size.width;
            float b = (s.Direction() == LayoutDirection::Ltr ? end : 1 - start) * size.width;
            const float y = size.height / 2;
            if (cap == StrokeCap::Butt || size.height > size.width) {
                s.DrawRect(color, { std::min(a, b), y - strokeWidth / 2 }, { std::abs(b - a), strokeWidth });
            } else if (std::abs(end - start) > 0) {
                const float half = strokeWidth / 2;
                a = std::clamp(a, half, size.width - half);
                b = std::clamp(b, half, size.width - half);
                const Offset origin{ std::min(a, b) - half, y - half };
                const Size extent{ std::abs(b - a) + strokeWidth, strokeWidth };
                if (cap == StrokeCap::Round) s.DrawRoundRect(color, origin, extent, half);
                else s.DrawRect(color, origin, extent);
            }
        }
        void CircularSegment(DrawScope& s, float start, float sweep, Color color, const gfx::Stroke& stroke) {
            if (!std::isfinite(start) || !std::isfinite(sweep) || sweep == 0 || s.GetSize().width <= stroke.width) return;
            const float inset = stroke.width / 2, end = s.GetSize().width - inset;
            gfx::Path path;
            // drawArc with >=360 degrees draws an oval, without a seam at the caps.
            if (sweep >= 360) path.AddOval({ inset, inset, end, end });
            else path.AddArc({ inset, inset, end, end }, start, sweep);
            s.DrawPath(path, color, &stroke);
        }
        void CircularDrawing(DrawScope& s, float start, float sweep, Color color, Color track, Dp width, Dp gap, StrokeCap cap) {
            const float adjusted = s.ToPx(gap) + ((cap == StrokeCap::Butt || s.GetSize().height > s.GetSize().width) ? 0 : s.ToPx(width));
            const float gapSweep = adjusted / (std::numbers::pi_v<float> * s.GetSize().width) * 360;
            const gfx::Stroke stroke{ s.ToPx(width), 4, cap };
            const float offset = std::min(sweep, gapSweep);
            CircularSegment(s, start + sweep + offset, 360 - sweep - offset * 2, track, stroke);
            CircularSegment(s, start, sweep, color, stroke);
        }
        anim::SpecPtr LineSpec(int delay, int duration) {
            return anim::InfiniteRepeatable(anim::Keyframes(1750, {
                { delay, { 0, 0, 0, 0 }, tokens::MotionTokens::EasingEmphasizedAccelerateCubicBezier.ToEasing() },
                { delay + duration, { 1, 0, 0, 0 } }
            }));
        }
        float At(const anim::SpecPtr& spec, std::int64_t time, float from, float to) {
            return spec->ValueAt(time, { from, 0, 0, 0 }, { to, 0, 0, 0 }, {}, 1)[0];
        }
    }

    namespace internal {
        LinearProgressMotion LinearProgressMotionAt(std::int64_t nanos) {
            static const auto head1 = LineSpec(0, 1000), tail1 = LineSpec(250, 1000), head2 = LineSpec(650, 850), tail2 = LineSpec(900, 850);
            return { At(head1, nanos, 0, 1), At(tail1, nanos, 0, 1), At(head2, nanos, 0, 1), At(tail2, nanos, 0, 1) };
        }
        CircularProgressMotion CircularProgressMotionAt(std::int64_t nanos) {
            static const auto global = anim::InfiniteRepeatable(anim::Tween(6000, 0, anim::Linear()));
            static const auto rotation = anim::InfiniteRepeatable(anim::Keyframes(6000, {
                { 300, { 90, 0, 0, 0 }, tokens::MotionTokens::EasingEmphasizedDecelerateCubicBezier.ToEasing() },
                { 1500, { 90, 0, 0, 0 } },
                { 1800, { 180, 0, 0, 0 }, tokens::MotionTokens::EasingEmphasizedDecelerateCubicBezier.ToEasing() },
                { 3000, { 180, 0, 0, 0 } },
                { 3300, { 270, 0, 0, 0 }, tokens::MotionTokens::EasingEmphasizedDecelerateCubicBezier.ToEasing() },
                { 4500, { 270, 0, 0, 0 } },
                { 4800, { 360, 0, 0, 0 }, tokens::MotionTokens::EasingEmphasizedDecelerateCubicBezier.ToEasing() },
                { 6000, { 360, 0, 0, 0 } }
            }));
            static const auto progress = anim::InfiniteRepeatable(anim::Keyframes(6000, {
                { 3000, { 0.87f, 0, 0, 0 }, tokens::MotionTokens::EasingStandardCubicBezier.ToEasing() },
                { 6000, { 0.1f, 0, 0, 0 } }
            }));
            return { At(global, nanos, 0, 1080), At(rotation, nanos, 0, 360), At(progress, nanos, 0.1f, 0.87f) };
        }
    }

    void LinearProgressIndicator(std::function<float()> progress, const Modifier& modifier, const LinearProgressIndicatorOptions& o) {
        const auto color = o.color.Specified() ? o.color : ProgressIndicatorDefaults::linearColor();
        const auto track = o.trackColor.Specified() ? o.trackColor : ProgressIndicatorDefaults::linearTrackColor();
        const auto m = SizeMod(ProgressSemantics(internal::IncreaseVerticalSemanticsBounds(modifier), progress), ProgressIndicatorDefaults::LinearIndicatorWidth, ProgressIndicatorDefaults::LinearIndicatorHeight);
        Canvas(m, [=](DrawScope& s) {
            const auto size = s.GetSize();
            const float current = Coerce(progress());
            const float gap = (s.ToPx(o.gapSize) + ((o.strokeCap == StrokeCap::Butt || size.height > size.width) ? 0 : size.height)) / size.width;
            const float start = current + std::min(current, gap);
            if (start <= 1) LinearSegment(s, start, 1, track, size.height, o.strokeCap);
            LinearSegment(s, 0, current, color, size.height, o.strokeCap);
            if (!o.drawStopIndicator) ProgressIndicatorDefaults::DrawStopIndicator(s, ProgressIndicatorDefaults::LinearTrackStopIndicatorSize, color, o.strokeCap);
            else if (*o.drawStopIndicator) (*o.drawStopIndicator)(s);
        });
    }
    void LinearProgressIndicator(const Modifier& modifier, const LinearProgressIndicatorOptions& o) {
        const auto color = o.color.Specified() ? o.color : ProgressIndicatorDefaults::linearColor();
        const auto track = o.trackColor.Specified() ? o.trackColor : ProgressIndicatorDefaults::linearTrackColor();
        auto* animation = &RememberInfiniteTransition();
        const auto m = SizeMod(ProgressSemantics(internal::IncreaseVerticalSemanticsBounds(modifier)), ProgressIndicatorDefaults::LinearIndicatorWidth, ProgressIndicatorDefaults::LinearIndicatorHeight);
        Canvas(m, [=](DrawScope& s) {
            const auto size = s.GetSize();
            const auto v = internal::LinearProgressMotionAt(animation->playTime.Get());
            const float gap = (s.ToPx(o.gapSize) + ((o.strokeCap == StrokeCap::Butt || size.height > size.width) ? 0 : size.height)) / size.width;
            if (v.firstHead < 1 - gap) LinearSegment(s, v.firstHead > 0 ? v.firstHead + gap : 0, 1, track, size.height, o.strokeCap);
            if (v.firstHead > v.firstTail) LinearSegment(s, v.firstHead, v.firstTail, color, size.height, o.strokeCap);
            if (v.firstTail > gap) LinearSegment(s, v.secondHead > 0 ? v.secondHead + gap : 0, v.firstTail < 1 ? v.firstTail - gap : 1, track, size.height, o.strokeCap);
            if (v.secondHead > v.secondTail) LinearSegment(s, v.secondHead, v.secondTail, color, size.height, o.strokeCap);
            if (v.secondTail > gap) LinearSegment(s, 0, v.secondTail < 1 ? v.secondTail - gap : 1, track, size.height, o.strokeCap);
        });
    }
    void CircularProgressIndicator(std::function<float()> progress, const Modifier& modifier, const CircularProgressIndicatorOptions& o) {
        const auto color = o.color.Specified() ? o.color : ProgressIndicatorDefaults::circularColor();
        const auto track = o.trackColor.Specified() ? o.trackColor : ProgressIndicatorDefaults::circularDeterminateTrackColor();
        Canvas(SizeMod(ProgressSemantics(modifier, progress), ProgressIndicatorDefaults::CircularIndicatorDiameter, ProgressIndicatorDefaults::CircularIndicatorDiameter), [=](DrawScope& s) {
            CircularDrawing(s, 270, Coerce(progress()) * 360, color, track, o.strokeWidth, o.gapSize, o.strokeCap);
        });
    }
    void CircularProgressIndicator(const Modifier& modifier, const CircularProgressIndicatorOptions& o) {
        const auto color = o.color.Specified() ? o.color : ProgressIndicatorDefaults::circularColor();
        const auto track = o.trackColor.Specified() ? o.trackColor : ProgressIndicatorDefaults::circularIndeterminateTrackColor();
        auto* animation = &RememberInfiniteTransition();
        Canvas(SizeMod(ProgressSemantics(modifier), ProgressIndicatorDefaults::CircularIndicatorDiameter, ProgressIndicatorDefaults::CircularIndicatorDiameter), [=](DrawScope& s) {
            const auto v = internal::CircularProgressMotionAt(animation->playTime.Get());
            CircularDrawing(s, v.globalRotation + v.additionalRotation, v.progress * 360, color, track, o.strokeWidth, o.gapSize, o.strokeCap);
        });
    }
    namespace ProgressIndicatorDefaults {
        Color linearColor() { return Value(tokens::ProgressIndicatorTokens::ActiveIndicatorColor); }
        Color circularColor() { return Value(tokens::ProgressIndicatorTokens::ActiveIndicatorColor); }
        Color linearTrackColor() { return Value(tokens::ProgressIndicatorTokens::TrackColor); }
        Color circularDeterminateTrackColor() { return Value(tokens::ProgressIndicatorTokens::TrackColor); }
        Color circularIndeterminateTrackColor() { return Color::Transparent(); }
        anim::SpecPtr progressAnimationSpec() { return anim::Spring(1, anim::spring::StiffnessVeryLow, 0.001f); }
        void DrawStopIndicator(DrawScope& s, Dp stopSize, Color color, StrokeCap cap) {
            const auto size = s.GetSize();
            const float stop = std::min(s.ToPx(stopSize), size.height);
            const float offset = std::min((size.height - stop) / 2, s.ToPx(Dp(6)));
            const Offset origin{ s.Direction() == LayoutDirection::Rtl ? offset : size.width - stop - offset, (size.height - stop) / 2 };
            if (cap == StrokeCap::Round) s.DrawRoundRect(color, origin, { stop, stop }, stop / 2);
            else s.DrawRect(color, origin, { stop, stop });
        }
    }
}
