#pragma once

#include <jni.h>

namespace kiln::android {
    // Installs the app's Android resources (KilnResources) as kiln's AppResources. Call on the
    // UI thread, after KilnResources.attach.
    void InstallAppResources(JNIEnv* env);
}
