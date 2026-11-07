#pragma once

// stylusHandwriting (Compose's StylusHandwriting.kt): a stylus that moves past the handwriting
// slop before the long-press timeout, starting on or near (40 dp above and below, 10 dp to the
// sides) an editor, focuses it and starts the IME's handwriting; the rest of that stroke is
// consumed. Android 14 (API 34) and later; the host says whether it is supported.

#include "kiln/foundation/Compose.h"

#include <functional>

namespace kiln::foundation {

    struct StylusHandwritingCallbacks {
        std::function<bool()> isFocused;
        std::function<void()> requestFocus;
        std::function<void()> onHandwritingSlopExceeded;
    };
    // `supported`: isStylusHandwritingSupported (the host's API level).
    Modifier StylusHandwriting(Modifier m, bool enabled, bool supported, bool showHoverIcon, StylusHandwritingCallbacks callbacks);

}
