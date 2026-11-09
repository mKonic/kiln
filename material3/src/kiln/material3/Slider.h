#pragma once

#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Draw.h"

#include <optional>
#include <vector>

namespace kiln::material3 {
    struct SliderRange {
        float start = 0, endInclusive = 1;
        bool operator==(const SliderRange&) const = default;
    };

    class SliderState {
    public:
        explicit SliderState(float value = 0, int steps = 0, SliderRange trackRange = {});
        float Value() const { return m_Value.Get(); }
        void SetValue(float value);
        int Steps() const { return m_Steps; }
        SliderRange TrackRange() const { return m_Range; }
        float CoercedValueAsFraction() const;
        bool IsDragging() const { return m_Dragging.Get(); }
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
        bool shouldAutoSnap = true;
        void DispatchRawDelta(float delta, const std::function<void(float)>& onChange = {});

        // Layout/gesture bookkeeping shared by the component and its custom track.
        void Configure(foundation::Orientation orientation, bool rtl, bool reverseVertical);
        void UpdateDimensions(int width, int height, int thumbWidth, int thumbHeight);
        void OnPress(ui::Offset position);
        void SetDragging(bool value) { m_Dragging.Set(value); }
        void SetFocused(bool value) { m_Focused.Set(value); }
        bool IsFocused() const { return m_Focused.Get(); }
        int ThumbWidth() const { return m_ThumbWidth.Get(); }
        int ThumbHeight() const { return m_ThumbHeight.Get(); }
        foundation::Orientation Orientation() const { return m_Orientation; }
        bool IsRtl() const { return m_Rtl; }
        bool ReverseVerticalDirection() const { return m_ReverseVertical; }
        const std::vector<float>& TickFractions() const { return m_Ticks; }
    private:
        float SnappedValue(float value) const;
        rt::State<float> m_Value;
        int m_Steps;
        SliderRange m_Range;
        std::vector<float> m_Ticks;
        rt::State<bool> m_Dragging{ false }, m_Focused{ false };
        rt::State<int> m_ThumbWidth{ 0 }, m_ThumbHeight{ 0 };
        int m_Width = 0, m_Height = 0;
        foundation::Orientation m_Orientation = foundation::Orientation::Horizontal;
        bool m_Rtl = false, m_ReverseVertical = false;
        float m_RawOffset = 0, m_PressOffset = 0;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

    class RangeSliderState {
    public:
        explicit RangeSliderState(float start = 0, float end = 1, int steps = 0, SliderRange trackRange = {});
        float StartValue() const { return m_Start.Get(); }
        float EndValue() const { return m_End.Get(); }
        void SetStartValue(float value);
        void SetEndValue(float value);
        int Steps() const { return m_Steps; }
        SliderRange TrackRange() const { return m_Range; }
        float CoercedStartValueAsFraction() const;
        float CoercedEndValueAsFraction() const;
        int StartSteps() const;
        int EndSteps() const;
        bool IsDragging() const { return m_Dragging.Get(); }
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
        void OnDrag(bool start, float delta, const std::function<void(SliderRange)>& onChange = {});

        void SetDragging(bool value) { m_Dragging.Set(value); }
        void Configure(bool rtl) { m_Rtl = rtl; }
        bool IsRtl() const { return m_Rtl; }
        void UpdateDimensions(int width, ui::IntSize startThumb, ui::IntSize endThumb);
        void UpdateMinMaxPx();
        float RawOffsetStart() const { return m_RawStart; }
        float RawOffsetEnd() const { return m_RawEnd; }
        int TotalWidth() const { return m_Width; }
        ui::IntSize StartThumbSize() const { return m_StartThumb.Get(); }
        ui::IntSize EndThumbSize() const { return m_EndThumb.Get(); }
        void SetFocused(bool start, bool value) { (start ? m_StartFocused : m_EndFocused).Set(value); }
        bool IsFocused(bool start) const { return (start ? m_StartFocused : m_EndFocused).Get(); }
        const std::vector<float>& TickFractions() const { return m_Ticks; }
    private:
        rt::State<float> m_Start, m_End;
        int m_Steps;
        SliderRange m_Range;
        std::vector<float> m_Ticks;
        rt::State<bool> m_Dragging{ false }, m_StartFocused{ false }, m_EndFocused{ false };
        rt::State<ui::IntSize> m_StartThumb{ {} }, m_EndThumb{ {} };
        int m_Width = 0;
        bool m_Rtl = false;
        float m_RawStart = 0, m_RawEnd = 0, m_MinPx = 0, m_MaxPx = 0;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

    SliderState& RememberSliderState(float value = 0, int steps = 0, SliderRange trackRange = {});
    RangeSliderState& RememberRangeSliderState(float start = 0, float end = 1, int steps = 0, SliderRange trackRange = {});

    struct SliderColors {
        ui::Color thumbColor = ui::Color::Unspecified(), activeTrackColor = ui::Color::Unspecified(), activeTickColor = ui::Color::Unspecified();
        ui::Color inactiveTrackColor = ui::Color::Unspecified(), inactiveTickColor = ui::Color::Unspecified();
        ui::Color disabledThumbColor = ui::Color::Unspecified(), disabledActiveTrackColor = ui::Color::Unspecified(), disabledActiveTickColor = ui::Color::Unspecified();
        ui::Color disabledInactiveTrackColor = ui::Color::Unspecified(), disabledInactiveTickColor = ui::Color::Unspecified();
        SliderColors Copy(const SliderColors& overrides) const;
        ui::Color ThumbColor(bool enabled) const;
        ui::Color TrackColor(bool enabled, bool active) const;
        ui::Color TickColor(bool enabled, bool active) const;
        bool operator==(const SliderColors&) const = default;
    };

    struct SliderOptions {
        bool enabled = true;
        std::function<void()> onValueChangeFinished;
        std::optional<SliderColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        std::function<void(SliderState&)> thumb, track;
    };
    struct RangeSliderOptions {
        bool enabled = true;
        std::function<void()> onValueChangeFinished;
        std::optional<SliderColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> startThumbInteractionSource, endThumbInteractionSource;
        std::function<void(RangeSliderState&)> startThumb, endThumb, track;
    };
    struct SliderThumbOptions {
        bool enabled = true, isVertical = false;
        std::optional<SliderColors> colors;
        std::optional<ui::DpSize> thumbSize;
    };
    struct SliderTrackOptions {
        bool enabled = true;
        std::optional<SliderColors> colors;
        // Unset uses the default dot; an engaged empty callback disables stop indicators.
        std::optional<std::function<void(ui::DrawScope&, ui::Offset)>> drawStopIndicator;
        std::function<void(ui::DrawScope&, ui::Offset, ui::Color)> drawTick;
        ui::Dp thumbTrackGapSize{ 6 }, trackInsideCornerSize{ 2 };
        std::optional<ui::Dp> trackCornerSize;
        bool enableCornerShrinking = false;
    };

    void Slider(SliderState&, std::function<void(float)> onValueChange, const ui::Modifier& = {}, const SliderOptions& = {});
    void VerticalSlider(SliderState&, std::function<void(float)> onValueChange, const ui::Modifier& = {}, const SliderOptions& = {}, bool topToBottom = true);
    void RangeSlider(RangeSliderState&, std::function<void(SliderRange)> onValueChange, const ui::Modifier& = {}, const RangeSliderOptions& = {});

    namespace SliderDefaults {
        inline constexpr ui::Dp TrackHeight{ 16 }, ThumbWidth{ 4 }, ThumbHeight{ 44 };
        inline constexpr ui::Dp TrackStopIndicatorSize{ 4 }, TickSize{ 4 };
        SliderColors colors();
        SliderColors colors(const SliderColors& overrides);
        void Thumb(std::shared_ptr<foundation::MutableInteractionSource>, const ui::Modifier& = {}, const SliderThumbOptions& = {});
        void Track(SliderState&, const ui::Modifier& = {}, const SliderTrackOptions& = {});
        void CenteredTrack(SliderState&, const ui::Modifier& = {}, const SliderTrackOptions& = {});
        void Track(RangeSliderState&, const ui::Modifier& = {}, const SliderTrackOptions& = {});
        void DrawStopIndicator(ui::DrawScope&, ui::Offset, ui::Dp size, ui::Color);
    }
}
