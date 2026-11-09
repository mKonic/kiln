#include "kiln/material3/Slider.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/internal/Slider.h"

namespace kiln::material3 {
    using namespace ui;
    using namespace foundation;
    namespace {
        struct CornerParams { std::optional<Dp> corner; bool vertical; bool operator==(const CornerParams&) const = default; };
        struct CornerNode final : ModifierNode, LayoutModifier {
            CornerParams params;
            explicit CornerNode(CornerParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                auto& p = m.Measure(c);
                const int corner = params.corner ? s.RoundToPx(*params.corner) : (params.vertical ? p.Width() : p.Height()) / 2;
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); }, { { &internal::SliderCornerSizeAlignmentLine, corner } });
            }
        };
        struct TrackValues {
            float start, end;
            std::vector<float> ticks;
            IntSize startThumb, endThumb;
            bool range = false, centered = false, vertical = false, reverseVertical = false;
            bool startFocused = false, endFocused = false;
        };
        void DrawTrack(DrawScope& s, const TrackValues& v, const SliderTrackOptions& o, const SliderColors& colors, bool insetRing) {
            const auto size = s.GetSize();
            const bool rtl = s.Direction() == LayoutDirection::Rtl, rtlHorizontal = rtl && !v.vertical;
            const float length = v.vertical ? size.height : size.width;
            const float corner = o.trackCornerSize ? s.ToPx(*o.trackCornerSize) : (v.vertical ? size.width : size.height) / 2;
            const float inside = s.ToPx(o.trackInsideCornerSize), center = length / 2;
            const auto endpoint = [&](float fraction) {
                const bool extreme = !v.ticks.empty() && (fraction == v.ticks.front() || fraction == v.ticks.back());
                return !v.ticks.empty() && !extreme ? (length - corner * 2) * fraction + corner : length * fraction;
            };
            const float valueStart = endpoint(v.start), valueEnd = endpoint(v.end);
            const float focusStart = insetRing && v.startFocused ? s.ToPx(Dp(4)) : 0;
            const float focusEnd = insetRing && v.endFocused ? s.ToPx(Dp(4)) : 0;
            const float gap = s.ToPx(o.thumbTrackGapSize);
            const float startGap = (v.centered || v.range) && gap + focusStart > 0
                ? float(v.vertical ? v.startThumb.height : v.startThumb.width) / 2 + gap + focusStart : 0;
            const float endGap = gap + focusEnd > 0 ? float(v.vertical ? v.endThumb.height : v.endThumb.width) / 2 + gap + focusEnd : 0;
            const auto active = colors.TrackColor(o.enabled, true), inactive = colors.TrackColor(o.enabled, false);
            const bool stops = !o.drawStopIndicator || bool(*o.drawStopIndicator);
            const auto stop = [&](float position) {
                const Offset offset = v.vertical ? Offset{ size.width / 2, position } : Offset{ rtl ? size.width - position : position, size.height / 2 };
                if (!o.drawStopIndicator) SliderDefaults::DrawStopIndicator(s, offset, SliderDefaults::TrackStopIndicatorSize, active);
                else if (*o.drawStopIndicator) (*o.drawStopIndicator)(s, offset);
            };
            const auto draw = [&](float start, float end, Color color, float leading, float trailing) {
                gfx::RoundRect rect;
                if (v.vertical) rect = { { 0, start, size.width, end }, { leading, leading }, { leading, leading }, { trailing, trailing }, { trailing, trailing } };
                else {
                    if (rtl) { const float left = size.width - end; end = size.width - start; start = left; }
                    rect = { { start, 0, end, size.height }, { leading, leading }, { trailing, trailing }, { trailing, trailing }, { leading, leading } };
                }
                gfx::Path path;
                path.AddRoundRect(rect);
                s.DrawPath(path, color);
            };
            float startThreshold = startGap, endThreshold = length - endGap;
            if (!o.enableCornerShrinking || !v.ticks.empty()) { startThreshold += corner; endThreshold -= corner; }
            const float adjustedEnd = v.centered ? std::min(valueEnd, center) : valueStart;
            if ((v.centered || v.range) && adjustedEnd > startThreshold) {
                draw(0, adjustedEnd - startGap, inactive, rtlHorizontal ? inside : corner, rtlHorizontal ? corner : inside);
                stop(corner);
            }
            const float adjustedStart = v.centered ? std::max(valueEnd, center) : valueEnd;
            if (adjustedStart < endThreshold) {
                draw(adjustedStart + endGap, length, inactive, rtlHorizontal ? corner : inside, rtlHorizontal ? inside : corner);
                stop(length - corner);
            }
            const float activeStart = v.centered ? adjustedEnd + (adjustedEnd < center ? startGap : 0) : v.range ? valueStart + startGap : 0;
            const float activeEnd = v.centered ? adjustedStart - (adjustedStart > center ? endGap : 0) : valueEnd - endGap;
            const float leadingCorner = rtlHorizontal || v.centered || v.range ? inside : corner;
            const float trailingCorner = rtlHorizontal && !v.centered && !v.range ? corner : inside;
            const float activeThreshold = !o.enableCornerShrinking || !v.ticks.empty() ? leadingCorner : 0;
            if (activeEnd - activeStart > activeThreshold) draw(activeStart, activeEnd, active, leadingCorner, trailingCorner);
            const float centerGap = valueEnd > center ? startGap : endGap;
            const float thumbGap = v.centered ? (valueEnd > center ? endGap : startGap) : endGap;
            for (std::size_t i = 0; i < v.ticks.size(); ++i) {
                if (stops && (i + 1 == v.ticks.size() || ((v.centered || v.range) && i == 0))) continue;
                const float tick = corner + (length - corner * 2) * v.ticks[i];
                const auto within = [tick](float a, float b) { return tick >= a && tick <= b; };
                if ((v.centered && within(center - centerGap, center + centerGap)) || (v.range && within(valueStart - startGap, valueStart + startGap)) || within(valueEnd - thumbGap, valueEnd + thumbGap)) continue;
                const Offset offset = v.vertical ? Offset{ size.width / 2, tick } : Offset{ rtl ? size.width - tick : tick, size.height / 2 };
                const auto color = colors.TickColor(o.enabled, within(activeStart, activeEnd));
                if (o.drawTick) o.drawTick(s, offset, color);
                else SliderDefaults::DrawStopIndicator(s, offset, SliderDefaults::TickSize, color);
            }
        }
        void Track(const Modifier& modifier, const SliderTrackOptions& o, std::function<TrackValues()> values) {
            const auto initial = values();
            const auto colors = o.colors.value_or(SliderDefaults::colors());
            const bool inset = rt::Composition::Current()->Read(LocalRippleThemeConfiguration).focus.has_value();
            auto m = initial.vertical ? FillMax(Width(modifier, SliderDefaults::TrackHeight), Axis::Vertical) : Height(FillMax(modifier, Axis::Horizontal), SliderDefaults::TrackHeight);
            m = m.With<CornerNode>(CornerParams{ initial.range ? std::nullopt : o.trackCornerSize, initial.vertical });
            Canvas(m, [=](DrawScope& s) {
                const auto v = values();
                s.Save();
                if (v.vertical && v.reverseVertical) { const auto size = s.GetSize(); s.Translate({ 0, size.height }); s.Scale(1, -1, {}); }
                DrawTrack(s, v, o, colors, inset);
                s.Restore();
            });
        }
        void SingleTrack(SliderState& state, const Modifier& modifier, const SliderTrackOptions& options, bool centered) {
            auto o = options;
            if (centered) o.enableCornerShrinking = true;
            Track(modifier, o, [&state, centered] {
                const auto size = IntSize{ state.ThumbWidth(), state.ThumbHeight() };
                const float fraction = state.CoercedValueAsFraction();
                return TrackValues{ 0, fraction, state.TickFractions(), centered && fraction <= 0.5f ? size : IntSize{}, !centered || fraction >= 0.5f ? size : IntSize{},
                    false, centered, state.Orientation() == Orientation::Vertical, state.ReverseVerticalDirection(), state.IsFocused(), state.IsFocused() };
            });
        }
    }
    namespace SliderDefaults {
        void Track(SliderState& state, const Modifier& modifier, const SliderTrackOptions& options) { SingleTrack(state, modifier, options, false); }
        void CenteredTrack(SliderState& state, const Modifier& modifier, const SliderTrackOptions& options) { SingleTrack(state, modifier, options, true); }
        void Track(RangeSliderState& state, const Modifier& modifier, const SliderTrackOptions& options) {
            auto o = options; o.enableCornerShrinking = false;
            material3::Track(modifier, o, [&state] {
                return TrackValues{ state.CoercedStartValueAsFraction(), state.CoercedEndValueAsFraction(), state.TickFractions(), state.StartThumbSize(), state.EndThumbSize(),
                    true, false, false, false, state.IsFocused(true), state.IsFocused(false) };
            });
        }
    }
}
