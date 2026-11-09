#pragma once

// ProgressIndicator.kt: determinate values are read during drawing, without implicit
// animation. Indeterminate indicators share the source's repeating animation specs.

#include "kiln/foundation/Draw.h"
#include "kiln/animation/Spec.h"

#include <optional>

namespace kiln::material3 {
    struct LinearProgressIndicatorOptions {
        ui::Color color = ui::Color::Unspecified(), trackColor = ui::Color::Unspecified();
        gfx::StrokeCap strokeCap = gfx::StrokeCap::Round;
        ui::Dp gapSize{4};
        // Unset: the default stop dot. An engaged, empty callback disables it.
        std::optional<std::function<void(ui::DrawScope&)>> drawStopIndicator;
    };
    struct CircularProgressIndicatorOptions {
        ui::Color color = ui::Color::Unspecified(), trackColor = ui::Color::Unspecified();
        ui::Dp strokeWidth{4}, gapSize{4};
        gfx::StrokeCap strokeCap = gfx::StrokeCap::Round;
    };
    void LinearProgressIndicator(std::function<float()> progress, const ui::Modifier& modifier = {}, const LinearProgressIndicatorOptions& options = {});
    void LinearProgressIndicator(const ui::Modifier& modifier = {}, const LinearProgressIndicatorOptions& options = {});
    void CircularProgressIndicator(std::function<float()> progress, const ui::Modifier& modifier = {}, const CircularProgressIndicatorOptions& options = {});
    void CircularProgressIndicator(const ui::Modifier& modifier = {}, const CircularProgressIndicatorOptions& options = {});

    namespace ProgressIndicatorDefaults {
        inline constexpr ui::Dp LinearIndicatorWidth{240}, LinearIndicatorHeight{4}, CircularIndicatorDiameter{40};
        inline constexpr ui::Dp CircularStrokeWidth{4}, LinearTrackStopIndicatorSize{4};
        inline constexpr ui::Dp LinearIndicatorTrackGapSize{4}, CircularIndicatorTrackGapSize{4};
        inline constexpr gfx::StrokeCap LinearStrokeCap = gfx::StrokeCap::Round;
        inline constexpr gfx::StrokeCap CircularDeterminateStrokeCap = gfx::StrokeCap::Round, CircularIndeterminateStrokeCap = gfx::StrokeCap::Round;
        ui::Color linearColor();
        ui::Color circularColor();
        ui::Color linearTrackColor();
        ui::Color circularDeterminateTrackColor();
        ui::Color circularIndeterminateTrackColor();
        anim::SpecPtr progressAnimationSpec();
        void DrawStopIndicator(ui::DrawScope& scope, ui::Dp stopSize, ui::Color color, gfx::StrokeCap strokeCap);
    }
}
