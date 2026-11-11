#pragma once

// AlertDialog.kt: a dialog that interrupts with an icon, title, text and confirm and dismiss
// buttons (AlertDialog), or with any content (BasicAlertDialog). 280 to 560 dp wide; buttons sit
// at the end in a row (confirm last) that wraps into a column (confirm first).

#include "kiln/foundation/Dialog.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <optional>

namespace kiln::material3 {

    namespace AlertDialogDefaults {
        foundation::ShapePtr shape();
        inline constexpr ui::Dp IconSize{ 24 };
        ui::Color containerColor();
        ui::Color iconContentColor();
        ui::Color titleContentColor();
        ui::Color textContentColor();
        inline constexpr ui::Dp TonalElevation{ 0 };
    }

    // BasicAlertDialog: the dialog window, its content between the minimum and maximum widths,
    // announced as a dialog pane.
    void BasicAlertDialog(std::function<void()> onDismissRequest, const ui::Modifier& modifier, const foundation::DialogProperties& properties,
                          std::function<void()> content);

    struct AlertDialogOptions {
        std::function<void()> dismissButton, icon, title, text;
        foundation::ShapePtr shape;   // null: AlertDialogDefaults::shape
        std::optional<ui::Color> containerColor, iconContentColor, titleContentColor, textContentColor;
        ui::Dp tonalElevation = AlertDialogDefaults::TonalElevation;
        foundation::DialogProperties properties;
    };
    void AlertDialog(std::function<void()> onDismissRequest, std::function<void()> confirmButton, const ui::Modifier& modifier = {},
                     const AlertDialogOptions& options = {});

}
