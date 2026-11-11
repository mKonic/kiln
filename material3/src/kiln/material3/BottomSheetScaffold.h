#pragma once

// BottomSheetScaffold.kt: a screen with a standard bottom sheet that peeks over its body. The
// sheet rests partially expanded (showing its peek height) or expanded, and hidden when its
// state allows; drags, flings, its drag handle and accessibility actions move it. A top bar sits
// over the body, and the snackbar host rides just above the peeking sheet.

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/SheetState.h"
#include "kiln/material3/Snackbar.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct BottomSheetScaffoldState {
        std::shared_ptr<SheetState> bottomSheetState;
        std::shared_ptr<SnackbarHostState> snackbarHostState;
    };

    // rememberBottomSheetScaffoldState. null bottomSheetState: partially expanded, without a
    // hidden state; null snackbarHostState: a remembered one.
    std::shared_ptr<BottomSheetScaffoldState> RememberBottomSheetScaffoldState(std::shared_ptr<SheetState> bottomSheetState = nullptr,
                                                                               std::shared_ptr<SnackbarHostState> snackbarHostState = nullptr);

    // rememberStandardBottomSheetState (deprecated in Compose): with the legacy rule that drops
    // the partially expanded state when the sheet is shorter than half the screen.
    std::shared_ptr<SheetState> RememberStandardBottomSheetState(SheetValue initialValue = SheetValue::PartiallyExpanded,
                                                                 std::function<bool(SheetValue)> confirmValueChange = {},
                                                                 bool skipHiddenState = true);

    struct BottomSheetScaffoldOptions {
        std::shared_ptr<BottomSheetScaffoldState> scaffoldState;   // null: RememberBottomSheetScaffoldState()
        ui::Dp sheetPeekHeight = BottomSheetDefaults::SheetPeekHeight;
        ui::Dp sheetMaxWidth = BottomSheetDefaults::SheetMaxWidth;
        foundation::ShapePtr sheetShape;                           // null: BottomSheetDefaults::ExpandedShape
        std::optional<ui::Color> sheetContainerColor, sheetContentColor;
        ui::Dp sheetTonalElevation{ 0 };
        std::optional<ui::Dp> sheetShadowElevation;                // unset: BottomSheetDefaults::Elevation
        std::function<void()> sheetDragHandle = [] { BottomSheetDefaults::DragHandle(); };   // null: none
        bool sheetSwipeEnabled = true;
        std::function<void()> topBar;
        std::function<void(SnackbarHostState&)> snackbarHost;      // null: SnackbarHost
        std::optional<ui::Color> containerColor, contentColor;     // unset: surface, and its content colour
    };

    // `content` gets the bottom padding the peeking sheet covers.
    void BottomSheetScaffold(std::function<void()> sheetContent, const ui::Modifier& modifier, const BottomSheetScaffoldOptions& options,
                             std::function<void(const foundation::PaddingValues&)> content);

}
