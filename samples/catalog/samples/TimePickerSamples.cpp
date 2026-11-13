// TimePickerSamples.kt
#include "TimePickerSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/material3/Button.h>
#include <kiln/material3/Snackbar.h>
#include <kiln/material3/TimePicker.h>
#include <kiln/material3/TimePickerDialog.h>
#include <kiln/ui/Configuration.h>

#include <cstdio>
#include <memory>
#include <string>

namespace catalog::samples {
    namespace DD = m3::TimePickerDialogDefaults;
    using StatePtr = std::shared_ptr<m3::TimePickerState>;
    using Mode = m3::TimePickerDisplayMode;

    namespace {
        // SimpleDateFormat("hh:mm a", Locale.getDefault()) in the default (en) locale.
        std::string FormatTime(int hour, int minute) {
            char buffer[16];
            std::snprintf(buffer, sizeof buffer, "%02d:%02d %s", hour % 12 == 0 ? 12 : hour % 12, minute, hour < 12 ? "AM" : "PM");
            return buffer;
        }

        kiln::rt::Task<> Show(m3::SnackbarHostState* host, std::string message) { co_await host->ShowSnackbar(std::move(message)); }

        bool TallEnough() { return Dp(float(Here().Read(kiln::ui::LocalConfiguration).screenHeightDp)) > DD::MinHeightForTimePicker; }

        // The samples' frame: a centred button that opens the dialog, and a snackbar host for
        // the time it confirms.
        struct DialogSample {
            const char* buttonText;
            bool vibrant;
            const char* okText;
            kiln::Function<void()> title;                         // the plain dialog's
            kiln::Function<void()> toggle;                        // null: an empty toggle slot
            kiln::Function<void(m3::TimePickerState&)> content;
        };
        void Run(const DialogSample& sample, kiln::rt::State<bool>& showTimePicker, const StatePtr& state) {
            auto& snackState = m3::RememberSnackbarHostState();
            const LaunchScope snackScope = RememberCoroutineScope();
            Box({}, Alignment::TopStart(), false, [&showTimePicker, &snackState, text = sample.buttonText] {
                m3::Button([&showTimePicker] { showTimePicker.Set(true); }, AlignInBox({}, Alignment::Center()), {}, [text] { m3::Text(text); });
                m3::SnackbarHost(snackState);
            });
            if (!showTimePicker.Get()) return;
            const auto dismiss = [&showTimePicker] { showTimePicker.Set(false); };
            const auto confirm = [&showTimePicker, &snackState, snackScope, state, okText = sample.okText] {
                m3::ButtonOptions b;
                b.enabled = m3::IsInputValid(*state);
                m3::TextButton([&showTimePicker, &snackState, snackScope, state] {
                    snackScope.Launch(Show(&snackState, "Entered time: " + FormatTime(state->Hour(), state->Minute())));
                    showTimePicker.Set(false);
                }, {}, b, [okText] { m3::Text(okText); });
            };
            m3::TimePickerDialogOptions o;
            o.dismissButton = [dismiss] { m3::TextButton(dismiss, {}, {}, [] { m3::Text("Cancel"); }); };
            o.modeToggleButton = sample.toggle ? sample.toggle : kiln::Function<void()>([] {});
            const auto content = [state, c = sample.content] { c(*state); };
            if (sample.vibrant) m3::VibrantTimePickerDialog(dismiss, confirm, {}, o, content);
            else m3::TimePickerDialog(dismiss, confirm, sample.title, {}, o, content);
        }

        m3::TimeInputOptions InputShapes() {
            m3::TimeInputOptions o;
            o.shapes = m3::TimePickerDefaults::shapes();
            return o;
        }
        m3::TimePickerOptions PickerShapes() {
            m3::TimePickerOptions o;
            o.shapes = m3::TimePickerDefaults::shapes();
            return o;
        }
    }

    void TimePickerSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        Run({ "Set Time", false, "Ok", [] { DD::Title(Mode::Picker); }, nullptr, [](m3::TimePickerState& s) { m3::TimePicker(s); } }, showTimePicker, state);
    }

    void TimeInputSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        Run({ "Set Time", false, "Ok", [] { DD::Title(Mode::Input); }, nullptr, [](m3::TimePickerState& s) { m3::TimeInput(s); } }, showTimePicker, state);
    }

    void TimePickerSwitchableSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        auto& isPickerMode = RememberSaveableState(true);
        const Mode displayMode = isPickerMode.Get() ? Mode::Picker : Mode::Input;
        const bool tall = TallEnough();
        Run({ "Set Time", false, "Ok", [displayMode] { DD::Title(displayMode); },
              [&isPickerMode, displayMode, tall] {
                  if (tall) DD::DisplayModeToggle([&isPickerMode] { isPickerMode.Set(!isPickerMode.Peek()); }, displayMode);
              },
              [displayMode, tall](m3::TimePickerState& s) {
                  if (displayMode == Mode::Picker && tall) m3::TimePicker(s);
                  else m3::TimeInput(s);
              } },
            showTimePicker, state);
    }

    void VibrantTimePickerSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        Run({ "Set Time", true, "OK", nullptr, nullptr, [](m3::TimePickerState& s) { m3::TimePicker(s, {}, PickerShapes()); } }, showTimePicker, state);
    }

    void VibrantTimeInputSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        Run({ "Set Input", true, "OK", nullptr, nullptr, [](m3::TimePickerState& s) {
                 m3::TimeInputOptions o;
                 o.shapes = m3::TimeInputDefaults::shapes();
                 m3::TimeInput(s, {}, o);
             } },
            showTimePicker, state);
    }

    void VibrantTimePickerSwitchableSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        auto& isPickerMode = RememberSaveableState(true);
        const Mode displayMode = isPickerMode.Get() ? Mode::Picker : Mode::Input;
        const bool tall = TallEnough();
        Run({ "Set Time (Switchable)", true, "OK", nullptr,
              [&isPickerMode, displayMode, tall] {
                  if (tall) DD::DisplayModeToggle([&isPickerMode] { isPickerMode.Set(!isPickerMode.Peek()); }, displayMode);
              },
              [displayMode, tall](m3::TimePickerState& s) {
                  if (displayMode == Mode::Picker && tall) m3::TimePicker(s, {}, PickerShapes());
                  else m3::TimeInput(s, {}, InputShapes());
              } },
            showTimePicker, state);
    }

    void VibrantTimePickerScrollSample() {
        auto& showTimePicker = RememberSaveableState(false);
        const StatePtr state = m3::RememberTimePickerState();
        auto& isScrollMode = RememberSaveableState(true);
        const Mode displayMode = isScrollMode.Get() ? Mode::Scroll : Mode::Input;
        Run({ "Set Time (Switchable, Scroll)", true, "OK", nullptr,
              [&isScrollMode, displayMode] {
                  DD::ScrollDisplayModeToggle([&isScrollMode] { isScrollMode.Set(!isScrollMode.Peek()); }, displayMode);
              },
              [displayMode](m3::TimePickerState& s) {
                  if (displayMode == Mode::Input) m3::TimeInput(s, {}, InputShapes());
                  else m3::TimeScroll(s, m3::TimePickerDefaults::shapes());
              } },
            showTimePicker, state);
    }

    void UncontainedTimePickerSample() {
        const StatePtr state = m3::RememberTimePickerState();
        auto& isTextInputMode = RememberSaveableState(false);
        const Mode displayMode = isTextInputMode.Get() ? Mode::Input : Mode::Scroll;
        const auto toggle = [&isTextInputMode, displayMode] {
            DD::ScrollDisplayModeToggle([&isTextInputMode] { isTextInputMode.Set(!isTextInputMode.Peek()); }, displayMode);
        };
        Box({}, Alignment::TopStart(), false, [state, displayMode, toggle] {
            if (displayMode == Mode::Input) {
                m3::TimeInputOptions o = InputShapes();
                o.toggle = toggle;
                m3::TimeInput(*state, {}, o);
            } else {
                m3::TimeScrollOptions o;
                o.toggle = toggle;
                m3::TimeScroll(*state, m3::TimePickerDefaults::shapes(), {}, o);
            }
        });
    }
}
