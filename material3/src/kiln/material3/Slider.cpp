#include "kiln/material3/Slider.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/internal/AccessibilityUtil.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Slider.h"
#include "kiln/material3/tokens/SliderTokens.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace kiln::material3 {
    using namespace ui;
    using namespace foundation;
    namespace {
        std::string SpokenValue(float value) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << float(RoundJava(value * 100)) / 100;
            auto text = out.str();
            while (text.size() >= 2 && text.back() == '0' && text[text.size() - 2] != '.') text.pop_back();
            return text;
        }
        float SemanticsValue(float target, SliderRange range, int steps) {
            float value = std::clamp(target, range.start, range.endInclusive), distance = value;
            const float original = value;
            // Keep source semantics tie-breaking (last equal-distance step), distinct from the
            // state setter's double-precision, first-equal-distance snapping.
            if (steps > 0) for (int i = 0; i <= steps + 1; ++i) {
                const float step = range.start + (range.endInclusive - range.start) * (float(i) / float(steps + 1));
                if (std::abs(step - original) <= distance) { distance = std::abs(step - original); value = step; }
            }
            return value;
        }
        bool KeyValue(const KeyEvent& event, float value, SliderRange range, int steps, bool reverse, bool vertical,
                      const std::function<void(float)>& change, const std::function<void()>& finished) {
            const int key = event.keyCode;
            const bool home = key == keys::MoveHome, end = key == keys::MoveEnd, pageUp = key == keys::PageUp, pageDown = key == keys::PageDown;
            const bool increase = key == (vertical ? keys::DpadDown : keys::DpadRight), decrease = key == (vertical ? keys::DpadUp : keys::DpadLeft);
            if (!home && !end && !pageUp && !pageDown && !increase && !decrease) return false;
            if (event.type == KeyEventType::KeyUp) { if (finished) finished(); return true; }
            if (event.type != KeyEventType::KeyDown) return false;
            const int actual = steps > 0 ? steps + 1 : 100;
            const float delta = std::abs(range.endInclusive - range.start) / float(actual), sign = reverse ? -1.0f : 1.0f;
            const int page = std::clamp(actual / 10, 1, 10);
            if (home) value = range.start;
            else if (end) value = range.endInclusive;
            else if (increase) value += sign * delta;
            else if (decrease) value -= sign * delta;
            else if (vertical) value += (pageDown ? 1 : -1) * float(page) * sign * delta;
            else value += (pageUp ? 1 : -1) * float(page) * delta;
            if (change) change(std::clamp(value, range.start, range.endInclusive));
            return true;
        }
        Modifier FocusRing(Modifier m, const std::shared_ptr<MutableInteractionSource>& source, bool inset, bool focused) {
            if (!inset) return m;
            RippleOptions ripple;
            ripple.focusRingShape = Value(tokens::SliderTokens::HandleShape);
            ripple.enablePressIndication = ripple.enableDragIndication = ripple.enableHoverIndication = false;
            m = Ripple(ripple)->Create(m, source);
            return Padding(m, focused ? Dp(4) : Dp(0));
        }
        Modifier MinimumSize(Modifier m, bool vertical) {
            return SizeIn(MinimumInteractiveComponentSize(m), vertical ? SliderDefaults::TrackHeight : SliderDefaults::ThumbWidth,
                vertical ? SliderDefaults::ThumbWidth : SliderDefaults::TrackHeight, Dp::Unspecified(), Dp::Unspecified(), false);
        }
        int Corner(Placeable& track) { const int value = track.Get(internal::SliderCornerSizeAlignmentLine); return value == AlignmentLine::Unspecified ? 0 : value; }
        int ThumbOffset(float fraction, int steps, const std::vector<float>& ticks, int length, int corner) {
            const bool extreme = !ticks.empty() && (fraction == ticks.front() || fraction == ticks.back());
            return steps > 0 && !extreme ? RoundJava(float(length - corner * 2) * fraction) + corner : RoundJava(float(length) * fraction);
        }
        void Single(SliderState& state, std::function<void(float)> onChange, const Modifier& modifier, const SliderOptions& o, bool vertical, bool topToBottom) {
            auto& c = *rt::Composition::Current();
            const bool rtl = c.Read(LocalLayoutDirection) == LayoutDirection::Rtl;
            state.Configure(vertical ? Orientation::Vertical : Orientation::Horizontal, rtl, vertical && !topToBottom);
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const bool focused = source->IsFocused(); state.SetFocused(focused);
            const bool inset = c.Read(LocalRippleThemeConfiguration).focus.has_value();
            const auto colors = o.colors.value_or(SliderDefaults::colors());
            auto m = Semantics(MinimumSize(modifier, vertical), [&state, o, onChange](SemanticsConfiguration& config) {
                const auto range = state.TrackRange();
                if (!o.enabled) config.disabled = true;
                if (range.start < 0) config.stateDescription = SpokenValue(state.Value());
                config.progressBarRangeInfo = ProgressBarRangeInfo{ std::clamp(state.Value(), range.start, range.endInclusive), range.start, range.endInclusive, state.Steps() };
                config.setProgress = [&state, onChange, finished = o.onValueChangeFinished](float target) {
                    const float value = SemanticsValue(target, state.TrackRange(), state.Steps());
                    if (value == state.Value()) return false;
                    if (onChange) onChange(value);
                    if (finished) finished();
                    return true;
                };
            });
            m = vertical ? internal::IncreaseVerticalSemanticsBounds(m) : internal::IncreaseHorizontalSemanticsBounds(m);
            if (o.enabled) m = Focusable(m, Focusability::Always, {}, source);
            const bool reverse = vertical ? !topToBottom : rtl;
            m = OnKeyEvent(m, [&state, o, onChange, reverse, vertical](const KeyEvent& event) {
                return o.enabled && KeyValue(event, state.Value(), state.TrackRange(), state.Steps(), reverse, vertical, onChange, o.onValueChangeFinished);
            });
            m = internal::SliderGestures(m, state, source, o.enabled, onChange, o.onValueChangeFinished);
            auto policy = std::make_shared<LambdaMeasurePolicy>([&state, vertical, inset, focused](MeasureScope& s, std::span<Measurable* const> children, const Constraints& constraints) {
                const int padding = inset && focused ? s.RoundToPx(Dp(4)) : 0;
                auto& thumb = children[0]->Measure(constraints);
                auto trackConstraints = constraints.Offset(vertical ? 0 : -(thumb.Width() - padding * 2), vertical ? -(thumb.Height() - padding * 2) : 0);
                if (vertical) trackConstraints.minWidth = 0; else trackConstraints.minHeight = 0;
                auto& track = children[1]->Measure(trackConstraints);
                const int coreWidth = thumb.Width() - padding * 2, coreHeight = thumb.Height() - padding * 2;
                const int width = vertical ? std::max(track.Width(), coreWidth) : coreWidth + track.Width();
                const int height = vertical ? coreHeight + track.Height() : std::max(track.Height(), coreHeight);
                const int trackX = vertical ? (width - track.Width()) / 2 : coreWidth / 2;
                const int trackY = vertical ? coreHeight / 2 : (height - track.Height()) / 2;
                state.UpdateDimensions(width, height, coreWidth, coreHeight);
                const int corner = Corner(track);
                return s.Layout(width, height, [&state, &thumb, &track, vertical, width, height, trackX, trackY, corner](PlacementScope& place) {
                    const int fraction = ThumbOffset(state.CoercedValueAsFraction(), state.Steps(), state.TickFractions(), vertical ? track.Height() : track.Width(), corner);
                    const int thumbX = vertical ? (width - thumb.Width()) / 2 : fraction + trackX - thumb.Width() / 2;
                    const int thumbY = vertical ? (state.ReverseVerticalDirection() ? track.Height() - fraction : fraction) + trackY - thumb.Height() / 2 : (height - thumb.Height()) / 2;
                    place.PlaceRelative(track, trackX, trackY);
                    place.PlaceRelative(thumb, thumbX, thumbY);
                });
            });
            Layout(m, policy, [&state, o, source, colors, inset, focused, vertical] {
                auto thumb = FocusRing(WrapContent({}, vertical ? Axis::Vertical : Axis::Horizontal), source, inset, focused);
                Box(thumb, Alignment::TopStart(), false, [&state, o, source, colors, vertical] {
                    if (o.thumb) o.thumb(state);
                    else SliderDefaults::Thumb(source, {}, { .enabled = o.enabled, .isVertical = vertical, .colors = colors });
                }, rt::Key(0));
                Box({}, Alignment::TopStart(), false, [&state, o, colors, vertical] {
                    if (o.track) o.track(state);
                    else SliderDefaults::Track(state, {}, { .enabled = o.enabled, .colors = colors, .enableCornerShrinking = vertical });
                }, rt::Key(1));
            });
        }
    }
    void Slider(SliderState& state, std::function<void(float)> onChange, const Modifier& modifier, const SliderOptions& options) { Single(state, std::move(onChange), modifier, options, false, true); }
    void VerticalSlider(SliderState& state, std::function<void(float)> onChange, const Modifier& modifier, const SliderOptions& options, bool topToBottom) { Single(state, std::move(onChange), modifier, options, true, topToBottom); }

    void RangeSlider(RangeSliderState& state, std::function<void(SliderRange)> onChange, const Modifier& modifier, const RangeSliderOptions& o) {
        auto& c = *rt::Composition::Current();
        state.Configure(c.Read(LocalLayoutDirection) == LayoutDirection::Rtl);
        const auto startSource = o.startThumbInteractionSource ? o.startThumbInteractionSource : RememberInteractionSource();
        const auto endSource = o.endThumbInteractionSource ? o.endThumbInteractionSource : RememberInteractionSource();
        const bool startFocused = startSource->IsFocused(), endFocused = endSource->IsFocused();
        state.SetFocused(true, startFocused); state.SetFocused(false, endFocused);
        const bool inset = c.Read(LocalRippleThemeConfiguration).focus.has_value();
        const auto colors = o.colors.value_or(SliderDefaults::colors());
        const auto startDescription = GetString(Strings::SliderRangeStart), endDescription = GetString(Strings::SliderRangeEnd);
        auto m = internal::RangeSliderGestures(MinimumSize(modifier, false), state, startSource, endSource, o.enabled, onChange, o.onValueChangeFinished);
        auto policy = std::make_shared<LambdaMeasurePolicy>([&state, inset, startFocused, endFocused](MeasureScope& s, std::span<Measurable* const> children, const Constraints& constraints) {
            const int startPadding = inset && startFocused ? s.RoundToPx(Dp(4)) : 0, endPadding = inset && endFocused ? s.RoundToPx(Dp(4)) : 0;
            auto& start = children[0]->Measure(constraints);
            auto& end = children[1]->Measure(constraints);
            const int startWidth = start.Width() - startPadding * 2, endWidth = end.Width() - endPadding * 2;
            auto trackConstraints = constraints.Offset(-(startWidth + endWidth) / 2, 0); trackConstraints.minHeight = 0;
            auto& track = children[2]->Measure(trackConstraints);
            const int width = track.Width() + (startWidth + endWidth) / 2;
            const int height = std::max({ track.Height(), start.Height() - startPadding * 2, end.Height() - endPadding * 2 });
            state.UpdateDimensions(width, { startWidth, start.Height() - startPadding * 2 }, { endWidth, end.Height() - endPadding * 2 });
            const int trackX = start.Width() / 2 - startPadding, trackY = (height - track.Height()) / 2;
            const int startY = (height - start.Height()) / 2, endY = (height - end.Height()) / 2;
            const int correction = (startWidth - endWidth) / 2, corner = Corner(track);
            return s.Layout(width, height, [&state, &start, &end, &track, startPadding, endPadding, trackX, trackY, startY, endY, correction, corner](PlacementScope& place) {
                const float a = state.CoercedStartValueAsFraction(), b = state.CoercedEndValueAsFraction();
                const int startX = ThumbOffset(a, state.Steps(), state.TickFractions(), track.Width(), corner);
                const int endX = ThumbOffset(b, state.Steps(), state.TickFractions(), track.Width(), corner) + correction;
                place.PlaceRelative(track, trackX, trackY);
                place.PlaceRelative(start, startX - startPadding, startY);
                place.PlaceRelative(end, endX - endPadding, endY);
            });
        });
        Layout(m, policy, [&state, o, onChange, startSource, endSource, colors, inset, startFocused, endFocused, startDescription, endDescription] {
            for (bool isStart : { true, false }) {
                const auto source = isStart ? startSource : endSource;
                auto m = WrapContent(FocusRing({}, source, inset, isStart ? startFocused : endFocused), Axis::Horizontal);
                m = Semantics(m, [&state, o, onChange, isStart](SemanticsConfiguration& config) {
                    const auto full = state.TrackRange();
                    const SliderRange range = isStart ? SliderRange{ full.start, state.EndValue() } : SliderRange{ state.StartValue(), full.endInclusive };
                    const float value = isStart ? state.StartValue() : state.EndValue();
                    const int steps = isStart ? state.StartSteps() : state.EndSteps();
                    if (!o.enabled) config.disabled = true;
                    if (full.start < 0) config.stateDescription = SpokenValue(value);
                    config.progressBarRangeInfo = ProgressBarRangeInfo{ std::clamp(value, range.start, range.endInclusive), range.start, range.endInclusive, steps };
                    config.setProgress = [&state, onChange, finished = o.onValueChangeFinished, isStart](float target) {
                        const auto full = state.TrackRange();
                        const auto range = isStart ? SliderRange{ full.start, state.EndValue() } : SliderRange{ state.StartValue(), full.endInclusive };
                        const float value = SemanticsValue(target, range, isStart ? state.StartSteps() : state.EndSteps());
                        if (value == (isStart ? state.StartValue() : state.EndValue())) return false;
                        if (onChange) onChange(isStart ? SliderRange{ value, state.EndValue() } : SliderRange{ state.StartValue(), value });
                        if (finished) finished();
                        return true;
                    };
                });
                m = internal::IncreaseHorizontalSemanticsBounds(m);
                m = Semantics(m, [description = isStart ? startDescription : endDescription](SemanticsConfiguration& config) { config.contentDescription = { description }; }, true);
                m = OnKeyEvent(m, [&state, o, onChange, isStart](const KeyEvent& event) {
                    const auto full = state.TrackRange();
                    // Keyboard delta uses the full track, but thumb values cannot cross.
                    return o.enabled && KeyValue(event, isStart ? state.StartValue() : state.EndValue(), full, state.Steps(), state.IsRtl(), false,
                        [&](float value) {
                            if (onChange) onChange(isStart ? SliderRange{ std::min(value, state.EndValue()), state.EndValue() } : SliderRange{ state.StartValue(), std::max(value, state.StartValue()) });
                        }, o.onValueChangeFinished);
                });
                if (o.enabled) m = Focusable(m, Focusability::Always, {}, source);
                Box(m, Alignment::TopStart(), false, [&state, o, isStart, source, colors] {
                    const auto thumb = isStart ? o.startThumb : o.endThumb;
                    if (thumb) thumb(state); else SliderDefaults::Thumb(source, {}, { .enabled = o.enabled, .colors = colors });
                }, rt::Key(isStart ? 0 : 1));
            }
            Box({}, Alignment::TopStart(), false, [&state, o, colors] {
                if (o.track) o.track(state); else SliderDefaults::Track(state, {}, { .enabled = o.enabled, .colors = colors });
            }, rt::Key(2));
        });
    }
}
