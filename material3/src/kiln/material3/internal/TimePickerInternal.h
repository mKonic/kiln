#pragma once

// TimePicker.kt internals shared with the dialog and the tests: the dial's state and the dial.

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/MutatorMutex.h"
#include "kiln/material3/TimePicker.h"
#include "kiln/ui/Focus.h"

#include <array>
#include <memory>
#include <optional>

namespace kiln::material3::internal {

    // AnalogTimePickerState: the dial's view of a state. The selector's angle animates; setting
    // the hour or minute here also turns the hand.
    class AnalogTimePickerState final : public TimePickerState {
    public:
        // userOverride: set false while the dial itself changes the time (so the picker does not
        // snap the hand back); null: one of its own.
        explicit AnalogTimePickerState(TimePickerState& state, std::shared_ptr<std::optional<bool>> userOverride = nullptr);

        TimePickerState* state;
        std::shared_ptr<std::optional<bool>> userOverride;
        rt::State<ui::Dp> currentDiameter{ ui::Dp(0) };
        rt::State<bool> isDialFocusable{ false };
        ui::FocusRequester dialFocusRequester, hourNodeFocusRequester, minuteNodeFocusRequester, amPmNodeFocusRequester;
        foundation::MutatorMutex mutex;
        float hourAngle = 0, minuteAngle = 0;

        std::shared_ptr<anim::Animatable> Anim() const { return m_Anim.Get(); }
        float CurrentAngle() const { return m_Anim.Get()->Float(); }
        const std::array<int, 12>& ClockFaceValues() const;

        int Minute() const override { return state->Minute(); }
        void SetMinute(int value) override;
        int MinuteInput() const override { return state->MinuteInput(); }
        void SetMinuteInput(int value) override { SetMinute(value); }
        int Hour() const override { return state->Hour(); }
        void SetHour(int value) override;
        int HourInput() const override { return state->HourInput(); }
        void SetHourInput(int value) override { SetHour(value); }
        bool Is24Hour() const override { return state->Is24Hour(); }
        void SetIs24Hour(bool v) override { state->SetIs24Hour(v); }
        TimePickerSelectionMode Selection() const override { return state->Selection(); }
        void SetSelection(TimePickerSelectionMode s) override { state->SetSelection(s); }

        // The angle nearest the current one that points where `angle` does.
        float EndValueForAnimation(float angle) const;
        // The hand is not already heading for the selection's angle.
        bool IsUpdated() const;
        static float Normalize(float angle);
        static int ToHour(float angle);
        static int ToMinute(float angle);
        static float OffsetAngle(float angle);

    private:
        rt::State<std::shared_ptr<anim::Animatable>> m_Anim{ nullptr };
    };

    // The dial: numbers on a ring (two for a 24-hour clock's hours), picked by tap or drag.
    void ClockFace(const ui::Modifier& modifier, std::shared_ptr<AnalogTimePickerState> state, const TimePickerColors& colors, bool autoSwitchToMinute);
    // ClockFaceSizeModifier: the dial shrinks on short screens (256, 238 or 200 dp).
    ui::Modifier ClockFaceSizeModifier(ui::Modifier m);

}
