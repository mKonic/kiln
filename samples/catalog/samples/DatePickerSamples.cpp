// DatePickerSamples.kt
#include "DatePickerSamples.h"
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/Scroll.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/DatePicker.h>
#include <kiln/material3/DateRangePicker.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/Snackbar.h>

#include <chrono>
#include <optional>
#include <string>

namespace catalog::samples {
    namespace {
        std::string OrElse(std::optional<std::int64_t> v, const char* none) { return v ? std::to_string(*v) : std::string(none); }

        std::int64_t CurrentTimeMillis() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        kiln::rt::Task<> Show(m3::SnackbarHostState* host, std::string message) { co_await host->ShowSnackbar(std::move(message)); }

        Modifier CenterH() { return AlignInColumn({}, AlignmentH::CenterHorizontally()); }
    }

    void DatePickerSample() {
        Column(VerticalScroll({}, RememberScrollState()), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [] {
            // Pre-select a date for January 4, 2020
            const auto datePickerState = m3::RememberDatePickerState(1578096000000);
            m3::DatePicker(*datePickerState, Padding({}, 16_dp));
            m3::Text("Selected date timestamp: " + OrElse(datePickerState->SelectedDateMillis(), "no selection"), CenterH());
            m3::Button([datePickerState] {
                datePickerState->SetSelectedDateMillis(CurrentTimeMillis());
                datePickerState->SetDisplayedMonthMillis(CurrentTimeMillis());
            }, CenterH(), {}, [] { m3::Text("Set date to today"); });
        });
    }

    void DatePickerDialogSample() {
        // Decoupled snackbar host state from scaffold state for demo purposes.
        auto& snackState = m3::RememberSnackbarHostState();
        const LaunchScope snackScope = RememberCoroutineScope();
        m3::SnackbarHost(snackState, {});
        auto& openDialog = RememberState(true);
        // TODO demo how to read the selected date from the state.
        if (openDialog.Get()) {
            const auto datePickerState = m3::RememberDatePickerState();
            const bool confirmEnabled = datePickerState->SelectedDateMillis().has_value();
            m3::DatePickerDialogOptions o;
            o.dismissButton = [&openDialog] { m3::TextButton([&openDialog] { openDialog.Set(false); }, {}, {}, [] { m3::Text("Cancel"); }); };
            m3::DatePickerDialog(
                // Dismiss the dialog when the user clicks outside the dialog or on the back
                // button. If you want to disable that functionality, simply use an empty
                // onDismissRequest.
                [&openDialog] { openDialog.Set(false); },
                [&openDialog, &snackState, snackScope, datePickerState, confirmEnabled] {
                    m3::ButtonOptions b;
                    b.enabled = confirmEnabled;
                    m3::TextButton([&openDialog, &snackState, snackScope, datePickerState] {
                        openDialog.Set(false);
                        snackScope.Launch(Show(&snackState, "Selected date timestamp: " + OrElse(datePickerState->SelectedDateMillis(), "null")));
                    }, {}, b, [] { m3::Text("OK"); });
                },
                {}, o,
                [datePickerState] {
                    // The verticalScroll will allow scrolling to show the entire month in case there is not
                    // enough horizontal space (for example, when in landscape mode).
                    // Note that it's still currently recommended to use a DisplayMode.Input at the state in
                    // those cases.
                    m3::DatePicker(*datePickerState, VerticalScroll({}, RememberScrollState()));
                });
        }
    }

    void DatePickerWithDateSelectableDatesSample() {
        const auto datePickerState = m3::RememberDatePickerState(
            std::nullopt, std::nullopt, {}, m3::DisplayMode::Picker,
            m3::MakeSelectableDates(
                // Blocks Sunday and Saturday from being selected.
                [](std::int64_t utcTimeMillis) {
                    const std::chrono::sys_days day{ std::chrono::floor<std::chrono::days>(std::chrono::sys_time<std::chrono::milliseconds>(
                        std::chrono::milliseconds(utcTimeMillis))) };
                    const std::chrono::weekday dayOfWeek{ day };
                    return dayOfWeek != std::chrono::Sunday && dayOfWeek != std::chrono::Saturday;
                },
                // Allow selecting dates from year 2023 forward.
                [](int year) { return year > 2022; }));
        Column(VerticalScroll({}, RememberScrollState()), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [datePickerState] {
            m3::DatePicker(*datePickerState);
            m3::Text("Selected date timestamp: " + OrElse(datePickerState->SelectedDateMillis(), "no selection"), CenterH());
        });
    }

    void DateInputSample() {
        Column({}, Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [] {
            const auto state = m3::RememberDatePickerState(std::nullopt, std::nullopt, {}, m3::DisplayMode::Input);
            m3::DatePicker(*state, Padding({}, 16_dp));
            m3::Text("Entered date timestamp: " + OrElse(state->SelectedDateMillis(), "no input"), CenterH());
        });
    }

    void DateRangePickerSample() {
        // Decoupled snackbar host state from scaffold state for demo purposes.
        auto& snackState = m3::RememberSnackbarHostState();
        const LaunchScope snackScope = RememberCoroutineScope();
        m3::SnackbarHost(snackState, ZIndex({}, 1));

        const auto state = m3::RememberDateRangePickerState();
        Column(FillMax({}, Axis::Both), Arrangement::Top(), AlignmentH::Start(), [&snackState, snackScope, state] {
            // Add a row with "Save" and dismiss actions.
            Row(Padding(Background(FillMax({}, Axis::Horizontal), m3::DatePickerDefaults::colors().containerColor), PaddingValues{ 12_dp, 0_dp, 12_dp, 0_dp }),
                Arrangement::SpaceBetween(), AlignmentV::CenterVertically(), [&snackState, snackScope, state] {
                    PlainTooltipAbove("Close", [] {
                        m3::IconButton([] { /* dismiss the UI */ }, {}, {}, [] { m3::Icon(Icons::Filled::Close(), std::string("Close")); });
                    });
                    m3::ButtonOptions b;
                    b.enabled = state->SelectedEndDateMillis().has_value();
                    m3::TextButton([&snackState, snackScope, state] {
                        const std::string range = std::to_string(*state->SelectedStartDateMillis()) + ".." + std::to_string(*state->SelectedEndDateMillis());
                        snackScope.Launch(Show(&snackState, "Saved range (timestamps): " + range));
                    }, {}, b, [] { m3::Text("Save"); });
                });
            m3::DateRangePicker(*state, Weight({}, 1));
        });
    }
}
