#pragma once

#include <functional>

namespace kiln::material3::internal {
    // PrecisionPointer.android.kt: an opted-in theme registers once per hierarchy. Sibling
    // hierarchies share their owner's listener; nested themes inherit the sizing state.
    void EnsurePrecisionPointerListenersRegistered(const std::function<void()>& content);
    bool ShouldUsePrecisionPointerComponentSizing();
}
