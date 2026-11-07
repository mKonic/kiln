#pragma once

// HandlePopup (AndroidSelectionHandles.android.kt) with HandlePositionProvider
// (SelectionHandles.kt): a selection or cursor handle in its own popup, its reference point (left,
// centre or right of its top edge, not mirrored) at the position the provider gives in the
// coordinates of the layout it is called in. While the provider has no position (NaN) the last
// one is kept.

#include "kiln/foundation/Compose.h"

#include <functional>

namespace kiln::foundation {

    // referenceX: 0 for AbsoluteAlignment.TopLeft, 0.5 for TopCenter, 1 for TopRight.
    void HandlePopup(std::function<Offset()> positionProvider, float referenceX, std::function<void()> content, Key key = {});

}
