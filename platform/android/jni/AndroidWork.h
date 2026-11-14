#pragma once

#include <jni.h>

namespace kiln::android {

    // kiln::work's JobScheduler host (JNI_OnLoad: a process started for a job has it too).
    void InstallWork(JNIEnv* env);

}
