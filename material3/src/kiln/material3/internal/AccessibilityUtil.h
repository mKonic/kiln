#pragma once

#include "kiln/ui/Modifier.h"

namespace kiln::material3::internal {
    // AccessibilityUtil.kt: expand semantics by 10 dp on each side of one axis without
    // changing the visual appearance or outer layout allocation.
    ui::Modifier IncreaseHorizontalSemanticsBounds(ui::Modifier modifier);
    ui::Modifier IncreaseVerticalSemanticsBounds(ui::Modifier modifier);
}
