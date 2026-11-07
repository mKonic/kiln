#pragma once

// keepScreenOn (KeepScreenOn.kt) and sensitiveContent (SensitiveContent.kt): while a node with
// them is attached, the screen stays on, or the window is treated as showing sensitive content
// (hidden from screen sharing and recording, API 35).

#include "kiln/foundation/Compose.h"

namespace kiln::foundation {

    Modifier KeepScreenOn(Modifier m);
    Modifier SensitiveContent(Modifier m, bool isContentSensitive = true);

}
