#pragma once

#include <jni.h>

namespace kiln::android {

    // kiln::glance's host (dev.kiln.KilnWidgetReceiver), installed as the library loads.
    void InstallGlance(JNIEnv* env);

}
