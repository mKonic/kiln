#pragma once

// TimePickerDialog.kt: a dialog holding a time picker (content) with a title and the mode
// toggle, dismiss and confirm buttons; the vibrant form has no title. Landscape content (a dial
// beside its display) puts the buttons under the content's start.

#include "kiln/foundation/Dialog.h"
#include <kiln/Function.h>
#include "kiln/foundation/Shapes.h"
#include "kiln/ui/Modifier.h"

#include <cstdint>
#include <functional>

namespace kiln::material3 {

    enum class TimePickerDisplayMode : std::uint8_t { Picker, Input, Scroll };

    namespace TimePickerDialogDefaults {
        ui::Color containerColor();
        foundation::ShapePtr shape();
        ui::Color vibrantContainerColor();
        foundation::ShapePtr vibrantShape();
        // The screen height a picker (dial) needs.
        inline constexpr ui::Dp MinHeightForTimePicker{ 300 };
        // Switches between the dial (Picker) and the fields (Input).
        void DisplayModeToggle(kiln::Function<void()> onDisplayModeChange, TimePickerDisplayMode displayMode, const ui::Modifier& modifier = {});
        // Switches between the wheels (Scroll) and the fields (Input).
        void ScrollDisplayModeToggle(kiln::Function<void()> onDisplayModeChange, TimePickerDisplayMode displayMode, const ui::Modifier& modifier = {});
        // "Select time" / "Enter time".
        void Title(TimePickerDisplayMode displayMode, const ui::Modifier& modifier = {});
    }

    struct TimePickerDialogOptions {
        foundation::DialogProperties properties{ .usePlatformDefaultWidth = false };
        kiln::Function<void()> modeToggleButton, dismissButton;
        foundation::ShapePtr shape;                  // null: the defaults' (vibrant or not)
        std::optional<ui::Color> containerColor;
    };
    // onDismissRequest: a click outside or Back (not the dismiss button).
    void TimePickerDialog(kiln::Function<void()> onDismissRequest, kiln::Function<void()> confirmButton, kiln::Function<void()> title,
                          const ui::Modifier& modifier, const TimePickerDialogOptions& options, kiln::Function<void()> content);
    void VibrantTimePickerDialog(kiln::Function<void()> onDismissRequest, kiln::Function<void()> confirmButton, const ui::Modifier& modifier,
                                 const TimePickerDialogOptions& options, kiln::Function<void()> content);

}
