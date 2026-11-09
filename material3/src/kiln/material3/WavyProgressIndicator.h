#pragma once

#include "kiln/foundation/Draw.h"
#include "kiln/animation/Spec.h"

#include <optional>

namespace kiln::material3 {
    struct LinearWavyProgressIndicatorOptions {
        ui::Color color = ui::Color::Unspecified(), trackColor = ui::Color::Unspecified();
        std::optional<gfx::Stroke> stroke, trackStroke; // widths are pixels, as in DrawScope
        ui::Dp gapSize{4}, stopSize{4};
        std::function<float(float)> amplitude;
        std::optional<ui::Dp> wavelength, waveSpeed;
    };
    struct CircularWavyProgressIndicatorOptions {
        ui::Color color = ui::Color::Unspecified(), trackColor = ui::Color::Unspecified();
        std::optional<gfx::Stroke> stroke, trackStroke;
        ui::Dp gapSize{4};
        std::function<float(float)> amplitude;
        ui::Dp wavelength{15};
        std::optional<ui::Dp> waveSpeed;
    };
    // Non-finite progress selects indeterminate motion, with a handoff when it becomes finite.
    void LinearWavyProgressIndicator(std::function<float()> progress, const ui::Modifier& modifier = {}, const LinearWavyProgressIndicatorOptions& options = {});
    void LinearWavyProgressIndicator(const ui::Modifier& modifier = {}, const LinearWavyProgressIndicatorOptions& options = {});
    void CircularWavyProgressIndicator(std::function<float()> progress, const ui::Modifier& modifier = {}, const CircularWavyProgressIndicatorOptions& options = {});
    void CircularWavyProgressIndicator(const ui::Modifier& modifier = {}, const CircularWavyProgressIndicatorOptions& options = {});

    namespace WavyProgressIndicatorDefaults {
        inline constexpr ui::Dp LinearContainerWidth{240}, LinearContainerHeight{10}, CircularContainerSize{48};
        inline constexpr ui::Dp LinearDeterminateWavelength{40}, LinearIndeterminateWavelength{20}, CircularWavelength{15};
        inline constexpr ui::Dp LinearTrackStopIndicatorSize{4}, LinearIndicatorTrackGapSize{4}, CircularIndicatorTrackGapSize{4};
        ui::Color indicatorColor();
        ui::Color trackColor();
        gfx::Stroke linearIndicatorStroke();
        gfx::Stroke linearTrackStroke();
        gfx::Stroke circularIndicatorStroke();
        gfx::Stroke circularTrackStroke();
        float indicatorAmplitude(float progress);
        anim::SpecPtr progressAnimationSpec();
    }
}
