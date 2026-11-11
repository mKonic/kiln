#pragma once

// TimePicker.kt: a time picked on a clock dial (hours, then minutes; 24-hour clocks have an
// inner ring), typed into two fields (TimeInput), or scrolled on two wheels (TimeScroll), with
// an AM/PM toggle for 12-hour clocks. The vibrant forms (with TimePickerShapes) are Expressive.

#include "kiln/foundation/Shapes.h"
#include "kiln/material3/TextFieldColors.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Modifier.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    enum class TimePickerSelectionMode : std::uint8_t { Hour, Minute };
    enum class TimePickerLayoutType : std::uint8_t { Horizontal, Vertical };

    // TimePickerState: hour 0-23 and minute 0-59; the inputs hold what the text fields typed
    // (possibly out of range: they set the time only when valid).
    class TimePickerState {
    public:
        virtual ~TimePickerState() = default;
        virtual int Minute() const = 0;
        virtual void SetMinute(int minute) = 0;
        virtual int Hour() const = 0;
        virtual void SetHour(int hour) = 0;
        virtual int HourInput() const { return Hour(); }
        // Sets the hour too when it is valid for the clock and period.
        virtual void SetHourInput(int hour);
        virtual int MinuteInput() const { return Minute(); }
        virtual void SetMinuteInput(int minute);
        virtual bool Is24Hour() const = 0;
        virtual void SetIs24Hour(bool is24Hour) = 0;
        virtual TimePickerSelectionMode Selection() const = 0;
        virtual void SetSelection(TimePickerSelectionMode selection) = 0;
    };
    bool IsPm(const TimePickerState& state);
    bool IsHourInputValid(const TimePickerState& state);
    bool IsMinuteInputValid(const TimePickerState& state);
    inline bool IsInputValid(const TimePickerState& s) { return IsMinuteInputValid(s) && IsHourInputValid(s); }

    // TimePickerState(initialHour, initialMinute, is24Hour, initialSelection): the hour 0-23 and
    // minute 0-59 (others throw std::invalid_argument).
    std::shared_ptr<TimePickerState> MakeTimePickerState(int initialHour, int initialMinute, bool is24Hour,
                                                         TimePickerSelectionMode initialSelection = TimePickerSelectionMode::Hour);
    // rememberTimePickerState: the hour, minute, clock and selection survive re-creation;
    // is24Hour unset follows the platform setting.
    std::shared_ptr<TimePickerState> RememberTimePickerState(int initialHour = 0, int initialMinute = 0,
                                                             std::optional<bool> is24Hour = std::nullopt, rt::Key key = {});

    struct TimePickerColors {
        ui::Color clockDialColor, selectorColor, containerColor, periodSelectorBorderColor, clockDialSelectedContentColor,
            clockDialContentColor, periodSelectorSelectedContainerColor, periodSelectorContainerColor,
            periodSelectorSelectedContentColor, periodSelectorContentColor, timeSelectorSelectedContainerColor,
            timeSelectorContainerColor, timeSelectorSelectedContentColor, timeSelectorContentColor;
        ui::Color PeriodSelectorContainerColor(bool selected) const { return selected ? periodSelectorSelectedContainerColor : periodSelectorContainerColor; }
        ui::Color PeriodSelectorContentColor(bool selected) const { return selected ? periodSelectorSelectedContentColor : periodSelectorContentColor; }
        ui::Color TimeSelectorContainerColor(bool selected) const { return selected ? timeSelectorSelectedContainerColor : timeSelectorContainerColor; }
        ui::Color TimeSelectorContentColor(bool selected) const { return selected ? timeSelectorSelectedContentColor : timeSelectorContentColor; }
        ui::Color ClockDialContentColor(bool selected) const { return selected ? clockDialSelectedContentColor : clockDialContentColor; }
        bool operator==(const TimePickerColors&) const = default;
    };

    struct TimeInputColors {
        ui::Color containerColor, periodSelectorBorderColor, periodSelectorSelectedContainerColor, periodSelectorContainerColor,
            periodSelectorSelectedContentColor, periodSelectorContentColor;
        TextFieldColors timeTextFieldColors;
        ui::Color PeriodSelectorContainerColor(bool selected) const { return selected ? periodSelectorSelectedContainerColor : periodSelectorContainerColor; }
        ui::Color PeriodSelectorContentColor(bool selected) const { return selected ? periodSelectorSelectedContentColor : periodSelectorContentColor; }
        ui::Color TimeSelectorContainerColor(bool selected) const { return timeTextFieldColors.ContainerColor(true, false, selected); }
        ui::Color TimeSelectorContentColor(bool selected) const { return timeTextFieldColors.TextColor(true, false, selected); }
        bool operator==(const TimeInputColors&) const = default;
    };

    struct TimePickerShapes {
        foundation::ShapePtr timeFieldShape, periodSelectorShape;
    };

    namespace TimePickerDefaults {
        TimePickerColors colors();
        // The Expressive colours (lowest-container fields and dial).
        TimePickerColors vibrantColors();
        // Vertical in portrait, horizontal in landscape.
        TimePickerLayoutType layoutType();
        TimePickerShapes shapes();
    }
    namespace TimeInputDefaults {
        TimeInputColors colors();
        TimeInputColors vibrantColors();
        inline TimePickerShapes shapes() { return TimePickerDefaults::shapes(); }
    }

    // The clock dial picker. With shapes: the vibrant (Expressive) form.
    struct TimePickerOptions {
        std::optional<TimePickerColors> colors;            // unset: colors() (vibrantColors() with shapes)
        std::optional<TimePickerLayoutType> layoutType;    // unset: TimePickerDefaults::layoutType()
        std::optional<TimePickerShapes> shapes;
    };
    void TimePicker(TimePickerState& state, const ui::Modifier& modifier = {}, const TimePickerOptions& options = {});

    // The two text fields. With shapes: vibrant; a toggle shows in a side column (AM/PM above it).
    struct TimeInputOptions {
        std::optional<TimeInputColors> colors;
        std::optional<TimePickerShapes> shapes;
        std::function<void()> toggle;
    };
    void TimeInput(TimePickerState& state, const ui::Modifier& modifier = {}, const TimeInputOptions& options = {});

    // The two scroll fields (always vibrant).
    struct TimeScrollOptions {
        std::optional<TimePickerColors> colors;            // unset: vibrantColors()
        std::function<void()> toggle;
    };
    void TimeScroll(TimePickerState& state, const TimePickerShapes& shapes, const ui::Modifier& modifier = {},
                    const TimeScrollOptions& options = {});

}
