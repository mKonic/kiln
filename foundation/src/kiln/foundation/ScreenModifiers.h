#pragma once

// keepScreenOn (KeepScreenOn.kt) and sensitiveContent (SensitiveContent.kt): while a node with
// them is attached, the screen stays on, or the window is treated as showing sensitive content
// (hidden from screen sharing and recording, API 35).

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>

namespace kiln::foundation {

    Modifier KeepScreenOn(Modifier m);
    // systemGestureExclusion (API 29) / preferKeepClear (API 33): the node's bounds, or the rect
    // `rect` gives for its size (in its own space), kept clear of system gestures / overlays.
    Modifier SystemGestureExclusion(Modifier m, kiln::Function<Rect(ui::Size)> rect = {});
    Modifier PreferKeepClear(Modifier m, kiln::Function<Rect(ui::Size)> rect = {});
    Modifier SensitiveContent(Modifier m, bool isContentSensitive = true);

}
