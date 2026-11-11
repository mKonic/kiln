#pragma once

// ModalBottomSheet.kt (+ .android.kt): a bottom sheet in its own full-screen window over a scrim.
// It shows (partially expanded when it can be) once its size is known, hides from a tap on the
// scrim, Back (following the back gesture) or a swipe down, and reports the dismissal after it
// has hidden.

#include "kiln/foundation/Dialog.h"
#include "kiln/material3/BottomSheet.h"

namespace kiln::material3 {

    struct ModalBottomSheetProperties {
        foundation::SecureFlagPolicy securePolicy = foundation::SecureFlagPolicy::Inherit;
        bool shouldDismissOnBackPress = true;
        bool shouldDismissOnClickOutside = true;
        bool operator==(const ModalBottomSheetProperties&) const = default;
    };

    struct ModalBottomSheetOptions {
        std::shared_ptr<SheetState> sheetState;         // null: RememberBottomSheetState(Hidden)
        ui::Dp sheetMaxWidth = BottomSheetDefaults::SheetMaxWidth;
        bool sheetGesturesEnabled = true;
        foundation::ShapePtr shape;                     // null: BottomSheetDefaults::ExpandedShape
        std::optional<ui::Color> containerColor, contentColor;
        ui::Dp tonalElevation{ 0 };
        std::optional<ui::Color> scrimColor;            // unset: BottomSheetDefaults::ScrimColor
        std::function<void()> dragHandle = [] { BottomSheetDefaults::DragHandle(); };   // null: none
        std::function<foundation::WindowInsets()> contentWindowInsets;                  // null: modalWindowInsets
        ModalBottomSheetProperties properties;
    };

    void ModalBottomSheet(std::function<void()> onDismissRequest, const ui::Modifier& modifier, const ModalBottomSheetOptions& options,
                          std::function<void()> content);

    // rememberModalBottomSheetState (deprecated in Compose): hidden, with the legacy partially
    // expanded height (half the window).
    std::shared_ptr<SheetState> RememberModalBottomSheetState(bool skipPartiallyExpanded = false,
                                                              std::function<bool(SheetValue)> confirmValueChange = {}, rt::Key key = {});

    namespace internal {
        // SheetWindowInsets: the sheet's offset as a top inset (what lies above the sheet).
        foundation::WindowInsets SheetWindowInsets(std::shared_ptr<SheetState> state);
    }

}
