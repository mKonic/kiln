#pragma once

// BottomSheet.kt: a standard bottom sheet in the layout (not a window). It rests hidden,
// partially expanded or expanded over its SheetState; drags and flings move it between those
// (content scrolled to its top drags it too), Back hides it (following the back gesture), and
// its drag handle toggles it with a tap or accessibility actions.

#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/SheetState.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct BottomSheetOptions {
        std::shared_ptr<SheetState> state;              // null: RememberBottomSheetState(Hidden)
        std::function<void()> onDismissRequest;         // after the sheet hides from a gesture, Back or the handle
        ui::Dp maxWidth = BottomSheetDefaults::SheetMaxWidth;
        bool gesturesEnabled = true, backHandlerEnabled = true;
        std::function<void()> dragHandle = [] { BottomSheetDefaults::DragHandle(); };   // null: none
        std::function<foundation::WindowInsets()> contentWindowInsets;                  // null: standardWindowInsets
        foundation::ShapePtr shape;                     // null: ExpandedShape
        std::optional<ui::Color> containerColor, contentColor;
        std::optional<ui::Dp> tonalElevation;           // unset: Elevation
        ui::Dp shadowElevation{ 0 };
    };

    void BottomSheet(const ui::Modifier& modifier, const BottomSheetOptions& options, std::function<void()> content);

    namespace internal {
        // BottomSheetImpl: the sheet itself, shrunk by `predictiveBackProgress` (read where it
        // draws). The modal sheet shares it.
        void BottomSheetImpl(std::function<float()> predictiveBackProgress, const ui::Modifier& modifier, std::shared_ptr<SheetState> state,
                             const BottomSheetOptions& options, std::function<void()> content);
    }

}
