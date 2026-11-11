#include "kiln/material3/Slider.h"
#include "kiln/runtime/Saveable.h"

#include <cmath>
#include <stdexcept>

namespace kiln::material3 {
    namespace {
        void Validate(int steps, SliderRange range) {
            if (steps < 0) throw std::invalid_argument("steps should be >= 0");
            if (!std::isfinite(range.start) || !std::isfinite(range.endInclusive) || range.start > range.endInclusive)
                throw std::invalid_argument("Slider track range must be finite and ordered");
        }
        std::vector<float> Ticks(int steps) {
            std::vector<float> result;
            if (steps > 0) {
                result.resize(std::size_t(steps) + 2);
                for (std::size_t i = 0; i < result.size(); ++i) result[i] = float(i) / float(steps + 1);
            }
            return result;
        }
        float Fraction(float a, float b, float position) { return std::clamp(b == a ? 0.0f : (position - a) / (b - a), 0.0f, 1.0f); }
        float Scale(float a, float b, float value, float start, float end) { return start + (end - start) * Fraction(a, b, value); }
        float Snap(float value, const std::vector<float>& ticks, SliderRange range) {
            if (ticks.empty()) return value;
            float result = value, distance = INFINITY;
            // Use doubles for tick positions, as snapValueToTick does, and keep the first tie.
            for (std::size_t i = 0; i < ticks.size(); ++i) {
                const float tick = float(double(range.start) + (double(range.endInclusive) - range.start) * double(i) / double(ticks.size() - 1));
                const float next = std::abs(tick - value);
                if (next < distance) { distance = next; result = tick; }
            }
            return result;
        }
    }
    SliderState::SliderState(float value, int steps, SliderRange range) : m_Value(value), m_Steps(steps), m_Range(range) {
        Validate(steps, range);
        m_Ticks = Ticks(steps);
    }
    float SliderState::SnappedValue(float value) const { return Snap(std::clamp(value, m_Range.start, m_Range.endInclusive), m_Ticks, m_Range); }
    void SliderState::SetValue(float value) { m_Value.Set(shouldAutoSnap ? SnappedValue(value) : value); }
    float SliderState::CoercedValueAsFraction() const { return Fraction(m_Range.start, m_Range.endInclusive, std::clamp(Value(), m_Range.start, m_Range.endInclusive)); }
    void SliderState::Configure(foundation::Orientation orientation, bool rtl, bool reverseVertical) {
        m_Orientation = orientation; m_Rtl = rtl; m_ReverseVertical = reverseVertical;
    }
    void SliderState::UpdateDimensions(int width, int height, int thumbWidth, int thumbHeight) {
        m_Width = width; m_Height = height;
        m_ThumbWidth.Set(thumbWidth); m_ThumbHeight.Set(thumbHeight);
    }
    void SliderState::OnPress(ui::Offset position) {
        const float to = m_Orientation == foundation::Orientation::Vertical ? (m_ReverseVertical ? float(m_Height) - position.y : position.y)
                                                                         : (m_Rtl ? float(m_Width) - position.x : position.x);
        m_PressOffset = to - m_RawOffset;
    }
    void SliderState::DispatchRawDelta(float delta, const std::function<void(float)>& onChange) {
        const bool vertical = m_Orientation == foundation::Orientation::Vertical;
        const float half = float(vertical ? ThumbHeight() : ThumbWidth()) / 2;
        const float max = std::max(float(vertical ? m_Height : m_Width) - half, 0.0f), min = std::min(half, max);
        m_RawOffset += delta + m_PressOffset;
        m_PressOffset = 0;
        const float value = SnappedValue(Scale(min, max, std::clamp(m_RawOffset, min, max), m_Range.start, m_Range.endInclusive));
        if (value != Value()) { if (onChange) onChange(value); else SetValue(value); }
    }

    RangeSliderState::RangeSliderState(float start, float end, int steps, SliderRange range) : m_Start(start), m_End(end), m_Steps(steps), m_Range(range) {
        Validate(steps, range);
        start = std::clamp(start, range.start, range.endInclusive); end = std::clamp(end, range.start, range.endInclusive);
        m_Start.Set(std::min(start, end)); m_End.Set(std::max(start, end));
        m_Ticks = Ticks(steps);
    }
    void RangeSliderState::SetStartValue(float value) { m_Start.Set(Snap(std::clamp(value, m_Range.start, EndValue()), m_Ticks, m_Range)); }
    void RangeSliderState::SetEndValue(float value) { m_End.Set(Snap(std::clamp(value, StartValue(), m_Range.endInclusive), m_Ticks, m_Range)); }
    float RangeSliderState::CoercedStartValueAsFraction() const { return Fraction(m_Range.start, m_Range.endInclusive, StartValue()); }
    float RangeSliderState::CoercedEndValueAsFraction() const { return Fraction(m_Range.start, m_Range.endInclusive, EndValue()); }
    int RangeSliderState::StartSteps() const { return int(std::floor(float(m_Steps) * CoercedEndValueAsFraction())); }
    int RangeSliderState::EndSteps() const { return int(std::floor(float(m_Steps) * (1 - CoercedStartValueAsFraction()))); }
    void RangeSliderState::UpdateDimensions(int width, ui::IntSize startThumb, ui::IntSize endThumb) {
        m_Width = width; m_StartThumb.Set(startThumb); m_EndThumb.Set(endThumb);
        UpdateMinMaxPx();
    }
    void RangeSliderState::UpdateMinMaxPx() {
        if (IsDragging()) return;
        m_MaxPx = std::max(float(m_Width) - float(EndThumbSize().width) / 2, 0.0f);
        m_MinPx = std::min(float(StartThumbSize().width) / 2, m_MaxPx);
        m_RawStart = Scale(m_Range.start, m_Range.endInclusive, StartValue(), m_MinPx, m_MaxPx);
        m_RawEnd = Scale(m_Range.start, m_Range.endInclusive, EndValue(), m_MinPx, m_MaxPx);
    }
    void RangeSliderState::OnDrag(bool start, float delta, const std::function<void(SliderRange)>& onChange) {
        SliderRange value{ StartValue(), EndValue() };
        if (start) {
            m_RawStart += delta;
            m_RawEnd = Scale(m_Range.start, m_Range.endInclusive, EndValue(), m_MinPx, m_MaxPx);
            const float offset = std::clamp(m_RawStart, m_MinPx, m_RawEnd);
            value.start = std::min(Snap(Scale(m_MinPx, m_MaxPx, offset, m_Range.start, m_Range.endInclusive), m_Ticks, m_Range), EndValue());
        } else {
            m_RawEnd += delta;
            m_RawStart = Scale(m_Range.start, m_Range.endInclusive, StartValue(), m_MinPx, m_MaxPx);
            const float offset = std::clamp(m_RawEnd, m_RawStart, m_MaxPx);
            value.endInclusive = std::max(Snap(Scale(m_MinPx, m_MaxPx, offset, m_Range.start, m_Range.endInclusive), m_Ticks, m_Range), StartValue());
        }
        if (value != SliderRange{ StartValue(), EndValue() }) {
            if (onChange) onChange(value);
            else { SetStartValue(value.start); SetEndValue(value.endInclusive); }
        }
    }

    SliderState& RememberSliderState(float value, int steps, SliderRange range, rt::Key key) {
        rt::Saver<std::shared_ptr<SliderState>> saver{
            [](const std::shared_ptr<SliderState>& s) { return rt::SavedValue(double(s->Value())); },
            [=](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<SliderState>> {
                if (auto number = rt::SavedNumber(saved)) return std::make_shared<SliderState>(float(*number), steps, range);
                return std::nullopt;
            }
        };
        return *rt::RememberSaveable<std::shared_ptr<SliderState>>([=] { return std::make_shared<SliderState>(value, steps, range); }, saver, {}, key);
    }
    RangeSliderState& RememberRangeSliderState(float start, float end, int steps, SliderRange range, rt::Key key) {
        auto saver = rt::ListSaver<std::shared_ptr<RangeSliderState>>(
            [](const std::shared_ptr<RangeSliderState>& s) { return rt::SavedValue::List{ double(s->StartValue()), double(s->EndValue()) }; },
            [=](const rt::SavedValue::List& saved) -> std::optional<std::shared_ptr<RangeSliderState>> {
                if (saved.size() < 2) return std::nullopt;
                const auto a = rt::SavedNumber(saved[0]), b = rt::SavedNumber(saved[1]);
                if (!a || !b) return std::nullopt;
                return std::make_shared<RangeSliderState>(float(*a), float(*b), steps, range);
            });
        return *rt::RememberSaveable<std::shared_ptr<RangeSliderState>>([=] { return std::make_shared<RangeSliderState>(start, end, steps, range); }, saver, {}, key);
    }
}
