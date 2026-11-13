// ProgressIndicatorSamples.kt
#include "ProgressIndicatorSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/foundation/Animate.h>
#include <kiln/material3/ProgressIndicator.h>
#include <kiln/material3/Slider.h>
#include <kiln/material3/WavyProgressIndicator.h>
#include <kiln/runtime/Task.h>
#include <kiln/ui/Locals.h>

#include <algorithm>
#include <cmath>

namespace catalog::samples {
    namespace PD = m3::ProgressIndicatorDefaults;
    namespace WD = m3::WavyProgressIndicatorDefaults;

    namespace {
        // The determinate samples: an indicator following a slider (animated), the slider below.
        void WithSlider(kiln::Function<void(kiln::Function<float()>)> indicator) {
            auto& sliderState = m3::RememberSliderState(0.1f);
            const float animatedProgress = AnimateFloatAsState(sliderState.Value(), PD::progressAnimationSpec());
            Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [&sliderState, animatedProgress, indicator] {
                indicator([animatedProgress] { return animatedProgress; });
                Spacer(SizeIn({}, Dp::Unspecified(), 30_dp, Dp::Unspecified(), 30_dp, false));   // requiredHeight
                m3::Text("Set progress:");
                m3::Slider(sliderState, [&sliderState](float v) { sliderState.SetValue(v); }, Width({}, 300_dp));
            });
        }
        void Centered(kiln::Function<void()> content) { Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), std::move(content)); }

        kiln::gfx::Stroke ThickStroke() {
            return kiln::gfx::Stroke{ Here().Read(LocalDensity).ToPx(8_dp), 4, kiln::gfx::StrokeCap::Round };
        }

        // NaN for three seconds (indeterminate), then up to 1 in steps of 0.03, then a pause.
        kiln::rt::Task<> CycleProgress(kiln::rt::State<float>& progress) {
            for (;;) {
                progress.Set(NAN);
                co_await kiln::rt::Delay(3000);
                progress.Set(0);
                while (progress.Peek() < 1) {
                    co_await kiln::rt::Delay(200);
                    progress.Set(std::min(progress.Peek() + 0.03f, 1.0f));
                }
                co_await kiln::rt::Delay(1500);
            }
        }
    }

    void LinearProgressIndicatorSample() {
        WithSlider([](kiln::Function<float()> p) { m3::LinearProgressIndicator(p); });
    }
    void LinearWavyProgressIndicatorSample() {
        WithSlider([](kiln::Function<float()> p) { m3::LinearWavyProgressIndicator(p); });
    }
    void LinearThickWavyProgressIndicatorSample() {
        const auto stroke = ThickStroke();
        WithSlider([stroke](kiln::Function<float()> p) {
            m3::LinearWavyProgressIndicatorOptions o;
            o.stroke = o.trackStroke = stroke;
            // Thick height is slightly higher than the WavyProgressIndicatorDefaults.LinearContainerHeight default
            m3::LinearWavyProgressIndicator(p, Height({}, 14_dp), o);
        });
    }
    void IndeterminateLinearProgressIndicatorSample() { Centered([] { m3::LinearProgressIndicator(); }); }

    void IndeterminateToDeterminateWavyProgressIndicatorSample() {
        auto& progress = RememberState<float>(NAN);
        const float animatedProgress = AnimateFloatAsState(std::isnan(progress.Get()) ? 0 : progress.Get(), WD::progressAnimationSpec());
        Here().LaunchedEffect(kiln::rt::Keys(0), [&progress] { return CycleProgress(progress); });
        Column({}, Arrangement::SpacedBy(24_dp), AlignmentH::CenterHorizontally(), [&progress, animatedProgress] {
            const auto indicatorProgress = [&progress, animatedProgress] { return std::isnan(progress.Get()) ? NAN : animatedProgress; };
            m3::LinearWavyProgressIndicator(indicatorProgress);
            m3::CircularWavyProgressIndicator(indicatorProgress);
        });
    }
    void IndeterminateLinearWavyProgressIndicatorSample() { Centered([] { m3::LinearWavyProgressIndicator(); }); }

    void CircularProgressIndicatorSample() {
        WithSlider([](kiln::Function<float()> p) { m3::CircularProgressIndicator(p); });
    }
    void CircularWavyProgressIndicatorSample() {
        WithSlider([](kiln::Function<float()> p) { m3::CircularWavyProgressIndicator(p); });
    }
    void CircularThickWavyProgressIndicatorSample() {
        const auto stroke = ThickStroke();
        WithSlider([stroke](kiln::Function<float()> p) {
            m3::CircularWavyProgressIndicatorOptions o;
            o.stroke = o.trackStroke = stroke;
            m3::CircularWavyProgressIndicator(p, Size({}, 52_dp), o);   // Thick size is slightly larger than the default
        });
    }
    void IndeterminateCircularProgressIndicatorSample() { Centered([] { m3::CircularProgressIndicator(); }); }
    void IndeterminateCircularWavyProgressIndicatorSample() { Centered([] { m3::CircularWavyProgressIndicator(); }); }

    void LegacyLinearProgressIndicatorSample() {
        WithSlider([](kiln::Function<float()> p) {
            m3::LinearProgressIndicatorOptions o;
            o.trackColor = m3::MaterialTheme::colorScheme().surfaceVariant;
            o.strokeCap = kiln::gfx::StrokeCap::Butt;
            o.gapSize = 0_dp;
            o.drawStopIndicator = kiln::Function<void(DrawScope&)>([](DrawScope&) {});
            m3::LinearProgressIndicator(p, {}, o);
        });
    }
    void LegacyIndeterminateLinearProgressIndicatorSample() {
        Centered([] {
            m3::LinearProgressIndicatorOptions o;
            o.trackColor = m3::MaterialTheme::colorScheme().surfaceVariant;
            o.strokeCap = kiln::gfx::StrokeCap::Butt;
            o.gapSize = 0_dp;
            m3::LinearProgressIndicator({}, o);
        });
    }
    void LegacyCircularProgressIndicatorSample() {
        WithSlider([](kiln::Function<float()> p) {
            m3::CircularProgressIndicatorOptions o;
            o.trackColor = Color::Transparent();
            o.strokeCap = kiln::gfx::StrokeCap::Butt;
            o.gapSize = 0_dp;
            m3::CircularProgressIndicator(p, {}, o);
        });
    }
    void LegacyIndeterminateCircularProgressIndicatorSample() {
        Centered([] {
            m3::CircularProgressIndicatorOptions o;
            o.strokeCap = kiln::gfx::StrokeCap::Butt;
            m3::CircularProgressIndicator({}, o);
        });
    }
}
