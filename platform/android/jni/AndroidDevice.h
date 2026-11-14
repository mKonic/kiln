#pragma once

#include <jni.h>

#include <kiln/Function.h>

namespace kiln::android {
    // Installs ui::Vibration, ui::Location and ui::Biometric over KilnDevice, and ui::Sensors
    // over the NDK's sensor manager on the calling (main) thread's looper. `requestFrame` runs
    // after events reach the app. Call on the UI thread.
    void InstallDevice(JNIEnv* env, kiln::Function<void()> requestFrame);
}
