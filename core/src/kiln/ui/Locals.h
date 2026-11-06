#pragma once

// Platform composition locals (androidx.compose.ui.platform CompositionLocals).

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/NestedScroll.h"

namespace kiln::ui {

    // LocalLayoutDirection: the host view's layout direction (RTL locales mirror layouts and
    // reverse horizontal gestures).
    inline rt::Local<LayoutDirection> LocalLayoutDirection{ LayoutDirection::Ltr };

    // The host view's nested-scrolling parent (LocalView for rememberNestedScrollInteropConnection);
    // null when the host has none.
    inline rt::Local<HostNestedScrollParent*> LocalHostNestedScrollParent{ nullptr };

}
