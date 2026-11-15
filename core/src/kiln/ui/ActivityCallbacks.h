#pragma once

// What ComponentActivity's listeners tell (addOnTrimMemoryListener,
// addOnMultiWindowModeChangedListener) for an app without an activity of its own to override.
//
//   int id = ui::AddOnTrimMemoryListener([](int level) {
//       if (level >= ui::kTrimMemoryBackground) cache.clear();   // the app is in the background
//   });
//   bool split = ui::IsInMultiWindowMode();   // in content: recomposes when it changes

#include <kiln/Function.h>

namespace kiln::ui {

    // ComponentCallbacks2.TRIM_MEMORY_*.
    inline constexpr int kTrimMemoryUiHidden = 20, kTrimMemoryBackground = 40, kTrimMemoryComplete = 80;

    // Called on the main thread with the level when the system asks the app to use less memory
    // (kiln has already trimmed its own caches by then).
    int AddOnTrimMemoryListener(kiln::Function<void(int level)> listener);
    void RemoveOnTrimMemoryListener(int id);

    // Activity.isInMultiWindowMode: split screen or a free-form window. Observable state.
    bool IsInMultiWindowMode();

    // The host.
    void DispatchTrimMemory(int level);
    void SetInMultiWindowMode(bool inMultiWindow);

}
