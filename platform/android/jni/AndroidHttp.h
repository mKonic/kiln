#pragma once

#include <jni.h>

namespace kiln::android {
    // Installs the HTTP engine backed by KilnHttp (HttpURLConnection) and registers KilnHttp's
    // natives. Call on the UI thread.
    void InstallHttp(JNIEnv* env);
}
