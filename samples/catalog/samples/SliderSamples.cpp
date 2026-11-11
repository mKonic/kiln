// SliderSamples.kt
#include "SliderSamples.h"
#include "Common.h"

#include <kiln/animation/Animatable.h>
#include <kiln/foundation/Draw.h>
#include <kiln/foundation/Interaction.h>
#include <kiln/foundation/Shapes.h>
#include <kiln/foundation/VectorPainter.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Label.h>
#include <kiln/material3/Ripple.h>
#include <kiln/material3/Slider.h>
#include <kiln/material3/Tooltip.h>

#include <cmath>
#include <cstdio>

namespace catalog::samples {
    namespace SD = m3::SliderDefaults;

    namespace {
        // SliderWithTrackIconsSample's icon size, icon padding and thumb gap.
        constexpr Dp iconSizeDp{ 20 }, iconPadding{ 10 }, thumbTrackGapSize{ 6 };
    }

    namespace {
        // "%.2f".format(v)
        std::string Format2(float v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.2f", double(v));
            return buf;
        }
        void Padded(std::function<void()> content) { Column(Padding({}, 16_dp, 0_dp), Arrangement::Top(), AlignmentH::Start(), std::move(content)); }
        std::function<void(float)> SetTo(m3::SliderState& s) { return [&s](float v) { s.SetValue(v); }; }
        std::function<void(m3::SliderRange)> SetTo(m3::RangeSliderState& s) {
            return [&s](m3::SliderRange r) {
                s.SetStartValue(r.start);
                s.SetEndValue(r.endInclusive);
            };
        }
        std::shared_ptr<MutableInteractionSource> Source() { return RememberInteractionSource(); }

        // The legacy thumb: 20 dp with a 1 dp shadow and an unbounded 20 dp ripple.
        void LegacyThumb(std::shared_ptr<MutableInteractionSource> source) {
            m3::RippleOptions ripple;
            ripple.bounded = false;
            ripple.radius = 20_dp;
            const Modifier modifier = IndicationMod(ShadowMod(SizeMod({}, 20_dp, 20_dp), 1_dp, CircleShape(), false), source, m3::Ripple(ripple));
            SD::Thumb(source, modifier);
        }
        m3::SliderTrackOptions LegacyTrack() {
            m3::SliderTrackOptions o;
            o.thumbTrackGapSize = 0_dp;
            o.trackInsideCornerSize = 0_dp;
            o.drawStopIndicator = std::function<void(DrawScope&, Offset)>{};
            return o;
        }

        // The vertical samples: the slider jumps while dragged, otherwise animates to the value.
        void Vertical(m3::SliderRange range, bool centered) {
            auto animateJob = Here().Remember<std::shared_ptr<kiln::rt::Job>>([] { return std::make_shared<kiln::rt::Job>(); });
            // Only allow multiples of 10. Excluding the endpoints of `valueRange`, there are 9 steps (10, 20, ..., 90).
            auto& sliderState = m3::RememberSliderState(0, 9, range);
            const kiln::anim::SpecPtr snapAnimationSpec = m3::MaterialTheme::motionScheme()->FastEffectsSpec();
            auto& currentValue = RememberSaveableState(sliderState.Value());
            sliderState.shouldAutoSnap = false;
            auto source = Source();
            Padded([&, snapAnimationSpec, animateJob, source, centered] {
                m3::Text(Format2(sliderState.Value()), AlignInColumn({}, AlignmentH::CenterHorizontally()));
                Spacer(Height({}, 16_dp));
                m3::SliderOptions o;
                o.onValueChangeFinished = [&sliderState, &currentValue, snapAnimationSpec, animateJob] {
                    *animateJob = kiln::rt::Launch(kiln::rt::Dispatcher::Main(), kiln::anim::Animate(sliderState.Value(), currentValue.Peek(), snapAnimationSpec,
                                                              [&sliderState](float value, float) { sliderState.SetValue(value); }));
                };
                o.interactionSource = source;
                if (centered) {
                    o.track = [](m3::SliderState& s) { SD::CenteredTrack(s); };
                } else {
                    o.track = [](m3::SliderState& s) {
                        m3::SliderTrackOptions t;
                        t.trackCornerSize = 12_dp;
                        SD::Track(s, Width({}, 36_dp), t);
                    };
                }
                const auto r = sliderState.TrackRange();
                m3::VerticalSlider(sliderState, [&sliderState, &currentValue, animateJob](float newValue) {
                    currentValue.Set(newValue);
                    // only update the sliderState instantly if dragging
                    if (sliderState.IsDragging()) {
                        animateJob->Cancel();
                        sliderState.SetValue(newValue);
                    }
                }, ProgressSemantics(AlignInColumn(Height({}, 300_dp), AlignmentH::CenterHorizontally()), currentValue.Get(), r.start, r.endInclusive, sliderState.Steps()),
                   o, false);
            });
        }
    }

    void SliderSample() {
        auto& sliderState = m3::RememberSliderState();
        Padded([&sliderState] {
            m3::Text(Format2(sliderState.Value()));
            m3::Slider(sliderState, SetTo(sliderState));
        });
    }

    void LegacySliderSample() {
        auto& sliderState = m3::RememberSliderState();
        auto source = Source();
        Padded([&sliderState, source] {
            m3::Text(Format2(sliderState.Value()));
            m3::SliderOptions o;
            o.interactionSource = source;
            o.thumb = [source](m3::SliderState&) { LegacyThumb(source); };
            o.track = [](m3::SliderState& s) { SD::Track(s, Height({}, 4_dp), LegacyTrack()); };
            m3::Slider(sliderState, SetTo(sliderState), SizeIn({}, 20_dp, 4_dp, Dp::Unspecified(), Dp::Unspecified(), false), o);
        });
    }

    void StepsSliderSample() {
        // Only allow multiples of 10. Excluding the endpoints of `valueRange`, there are 9 steps (10, 20, ..., 90).
        auto& sliderState = m3::RememberSliderState(0, 9, { 0, 100 });
        Padded([&sliderState] {
            m3::Text(Format2(sliderState.Value()));
            m3::SliderOptions o;
            o.onValueChangeFinished = [] {
                // launch some business logic update with the state you hold
            };
            m3::Slider(sliderState, SetTo(sliderState), {}, o);
        });
    }

    void SliderWithCustomThumbSample() {
        auto& sliderState = m3::RememberSliderState(0, 0, { 0, 100 });
        auto source = Source();
        Padded([&sliderState, source] {
            m3::SliderOptions o;
            o.interactionSource = source;
            o.onValueChangeFinished = [] {};
            o.thumb = [&sliderState, source](m3::SliderState&) {
                m3::LabelOptions label;
                label.interactionSource = source;
                m3::Label([&sliderState](const m3::TooltipScope& scope) {
                    m3::PlainTooltip(scope, WrapContent(SizeIn({}, 45_dp, 25_dp, Dp::Unspecified(), Dp::Unspecified()), Axis::Horizontal), {},
                                     [&sliderState] { m3::Text(Format2(sliderState.Value())); });
                }, {}, label, [] {
                    m3::Icon(Icons::Filled::Favorite(), std::nullopt, Size({}, m3::ButtonDefaults::IconSize), Color(0xFFFF0000u));
                });
            };
            m3::Slider(sliderState, SetTo(sliderState), {}, o);
        });
    }

    void SliderWithCustomTrackAndThumbSample() {
        auto& sliderState = m3::RememberSliderState(0, 0, { 0, 100 });
        auto source = Source();
        m3::SliderColors overrides;
        overrides.thumbColor = overrides.activeTrackColor = Color(0xFFFF0000u);
        const m3::SliderColors colors = SD::colors(overrides);
        Padded([&sliderState, source, colors] {
            m3::Text(Format2(sliderState.Value()));
            m3::SliderOptions o;
            o.interactionSource = source;
            o.onValueChangeFinished = [] {};
            o.thumb = [source, colors](m3::SliderState&) {
                m3::SliderThumbOptions t;
                t.colors = colors;
                SD::Thumb(source, {}, t);
            };
            o.track = [colors](m3::SliderState& s) {
                m3::SliderTrackOptions t;
                t.colors = colors;
                SD::Track(s, {}, t);
            };
            m3::Slider(sliderState, SetTo(sliderState), {}, o);
        });
    }

    void SliderWithTrackIconsSample() {
        auto& sliderState = m3::RememberSliderState(0, 0, { 0, 100 });
        auto source = Source();
        auto startIcon = RememberVectorPainter(Icons::Filled::MusicNote());
        auto endIcon = RememberVectorPainter(Icons::Filled::MusicOff());
        Padded([&sliderState, source, startIcon, endIcon] {
            m3::Text(Format2(sliderState.Value()));
            m3::SliderOptions o;
            o.interactionSource = source;
            o.onValueChangeFinished = [] {};
            o.track = [&sliderState, startIcon, endIcon](m3::SliderState&) {
                const Color activeIconColor = SD::colors().activeTickColor, inactiveIconColor = SD::colors().inactiveTickColor;
                const auto drawIcon = [](DrawScope& d, const std::shared_ptr<VectorPainter>& icon, Offset at, kiln::ui::Size size, Color color) {
                    d.Save();
                    d.Translate(at);
                    const auto filter = kiln::gfx::ColorFilter::Tint(color);
                    icon->Draw(d, size, 1, &filter);
                    d.Restore();
                };
                m3::SliderTrackOptions t;
                t.trackCornerSize = 12_dp;
                t.drawStopIndicator = std::function<void(DrawScope&, Offset)>{};
                t.thumbTrackGapSize = thumbTrackGapSize;
                SD::Track(sliderState, DrawWithContent(Height({}, 36_dp), [&sliderState, startIcon, endIcon, activeIconColor, inactiveIconColor,
                                                                          drawIcon](ContentDrawScope& d) {
                    d.DrawContent();
                    const kiln::ui::Size size = d.GetSize();
                    const kiln::ui::Size iconSize{ d.ToPx(iconSizeDp), d.ToPx(iconSizeDp) };
                    const float padding = d.ToPx(iconPadding), gap = d.ToPx(thumbTrackGapSize);
                    const float yOffset = size.height / 2 - iconSize.height / 2;
                    const float activeTrackStart = 0;
                    const float activeTrackEnd = size.width * sliderState.CoercedValueAsFraction() - gap;
                    const float inactiveTrackStart = activeTrackEnd + gap * 2;
                    const float inactiveTrackEnd = size.width;
                    const float activeTrackWidth = activeTrackEnd - activeTrackStart;
                    const float inactiveTrackWidth = inactiveTrackEnd - inactiveTrackStart;
                    const auto start = [&](float x, Color c) { drawIcon(d, startIcon, { x + padding, yOffset }, iconSize, c); };
                    const auto end = [&](float x, Color c) { drawIcon(d, endIcon, { x - padding - iconSize.width, yOffset }, iconSize, c); };
                    if (iconSize.width < activeTrackWidth - padding * 2) {
                        start(activeTrackStart, activeIconColor);
                        end(activeTrackEnd, activeIconColor);
                    }
                    if (iconSize.width < inactiveTrackWidth - padding * 2) {
                        start(inactiveTrackStart, inactiveIconColor);
                        end(inactiveTrackEnd, inactiveIconColor);
                    }
                }), t);
            };
            m3::Slider(sliderState, SetTo(sliderState), {}, o);
        });
    }

    void CenteredSliderSample() {
        auto& sliderState = m3::RememberSliderState(0, 0, { -50, 50 });
        auto source = Source();
        Padded([&sliderState, source] {
            m3::Text(Format2(sliderState.Value()));
            m3::SliderOptions o;
            o.interactionSource = source;
            o.onValueChangeFinished = [] {};
            o.thumb = [source](m3::SliderState&) { SD::Thumb(source); };
            o.track = [&sliderState](m3::SliderState&) { SD::CenteredTrack(sliderState); };
            m3::Slider(sliderState, SetTo(sliderState), {}, o);
        });
    }

    void VerticalSliderSample() { Vertical({ 0, 100 }, false); }
    void VerticalCenteredSliderSample() { Vertical({ -50, 50 }, true); }

    void RangeSliderSample() {
        auto& rangeSliderState = m3::RememberRangeSliderState(0, 100, 0, { 0, 100 });
        Padded([&rangeSliderState] {
            m3::Text(Format2(rangeSliderState.StartValue()) + " .. " + Format2(rangeSliderState.EndValue()));
            m3::RangeSliderOptions o;
            o.onValueChangeFinished = [] {};
            m3::RangeSlider(rangeSliderState, SetTo(rangeSliderState), {}, o);
        });
    }

    void LegacyRangeSliderSample() {
        auto& rangeSliderState = m3::RememberRangeSliderState(0, 100, 0, { 0, 100 });
        auto start = Source();
        auto end = Source();
        Padded([&rangeSliderState, start, end] {
            m3::Text(Format2(rangeSliderState.StartValue()) + " .. " + Format2(rangeSliderState.EndValue()));
            m3::RangeSliderOptions o;
            o.startThumbInteractionSource = start;
            o.endThumbInteractionSource = end;
            o.onValueChangeFinished = [] {};
            o.startThumb = [start](m3::RangeSliderState&) { LegacyThumb(start); };
            o.endThumb = [end](m3::RangeSliderState&) { LegacyThumb(end); };
            o.track = [](m3::RangeSliderState& s) { SD::Track(s, Height({}, 4_dp), LegacyTrack()); };
            m3::RangeSlider(rangeSliderState, SetTo(rangeSliderState), SizeIn({}, 20_dp, 4_dp, Dp::Unspecified(), Dp::Unspecified(), false), o);
        });
    }

    void StepRangeSliderSample() {
        // Only allow multiples of 10. Excluding the endpoints of `valueRange`, there are 9 steps (10, 20, ..., 90).
        auto& rangeSliderState = m3::RememberRangeSliderState(0, 100, 9, { 0, 100 });
        Padded([&rangeSliderState] {
            const int rangeStart = int(std::lround(rangeSliderState.StartValue())), rangeEnd = int(std::lround(rangeSliderState.EndValue()));
            m3::Text(std::to_string(rangeStart) + " .. " + std::to_string(rangeEnd));
            m3::RangeSliderOptions o;
            o.onValueChangeFinished = [] {};
            m3::RangeSlider(rangeSliderState, SetTo(rangeSliderState), {}, o);
        });
    }

    void RangeSliderWithCustomComponents() {
        auto& rangeSliderState = m3::RememberRangeSliderState(0, 100, 0, { 0, 100 });
        auto startSource = Source();
        auto endSource = Source();
        m3::SliderColors startOverrides, endOverrides;
        startOverrides.thumbColor = Color(0xFF0000FFu);
        startOverrides.activeTrackColor = Color(0xFFFF0000u);
        endOverrides.thumbColor = Color(0xFF00FF00u);
        const m3::SliderColors startThumbAndTrackColors = SD::colors(startOverrides), endThumbColors = SD::colors(endOverrides);
        Padded([&rangeSliderState, startSource, endSource, startThumbAndTrackColors, endThumbColors] {
            m3::RangeSliderOptions o;
            o.startThumbInteractionSource = startSource;
            o.endThumbInteractionSource = endSource;
            o.onValueChangeFinished = [] {};
            o.startThumb = [&rangeSliderState, startSource, startThumbAndTrackColors](m3::RangeSliderState&) {
                m3::LabelOptions label;
                label.interactionSource = startSource;
                m3::Label([&rangeSliderState](const m3::TooltipScope& scope) {
                    m3::PlainTooltip(scope, WrapContent(SizeIn({}, 45_dp, 25_dp, Dp::Unspecified(), Dp::Unspecified()), Axis::Horizontal), {},
                                     [&rangeSliderState] { m3::Text(Format2(rangeSliderState.StartValue())); });
                }, {}, label, [startSource, startThumbAndTrackColors] {
                    m3::SliderThumbOptions t;
                    t.colors = startThumbAndTrackColors;
                    SD::Thumb(startSource, {}, t);
                });
            };
            o.endThumb = [&rangeSliderState, endSource, endThumbColors](m3::RangeSliderState&) {
                m3::LabelOptions label;
                label.interactionSource = endSource;
                m3::Label([&rangeSliderState](const m3::TooltipScope& scope) {
                    m3::PlainTooltip(scope, WrapContent(RequiredSize({}, 45_dp, 25_dp), Axis::Horizontal), {},
                                     [&rangeSliderState] { m3::Text(Format2(rangeSliderState.EndValue())); });
                }, {}, label, [endSource, endThumbColors] {
                    m3::SliderThumbOptions t;
                    t.colors = endThumbColors;
                    SD::Thumb(endSource, {}, t);
                });
            };
            o.track = [startThumbAndTrackColors](m3::RangeSliderState& s) {
                m3::SliderTrackOptions t;
                t.colors = startThumbAndTrackColors;
                SD::Track(s, {}, t);
            };
            m3::RangeSlider(rangeSliderState, SetTo(rangeSliderState), {}, o);
        });
    }
}
