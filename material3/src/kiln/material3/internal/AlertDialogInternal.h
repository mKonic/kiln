#pragma once

// AlertDialog.kt internals shared with the picker dialogs.

#include "kiln/ui/Geometry.h"

#include <functional>

namespace kiln::material3::internal {
    // AlertDialogFlowRow: buttons in a row (confirm last) that wraps into a column (confirm first).
    void AlertDialogFlowRow(ui::Dp mainAxisSpacing, ui::Dp crossAxisSpacing, std::function<void()> content);
}
