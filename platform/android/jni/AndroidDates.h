#pragma once

#include <jni.h>

namespace kiln::android {
    // Installs the date service backed by KilnDates (ICU and java.time). Call on the UI thread.
    void InstallDateFormat(JNIEnv* env);
}
