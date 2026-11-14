#pragma once

#include <jni.h>

namespace kiln::android {
    // Installs the ContentResolver backed by KilnContent. Call on the UI thread, after
    // KilnContent.attach.
    void InstallContentResolver(JNIEnv* env);
}
