#pragma once

#include <jni.h>

namespace kiln::android {

    // ui::SetApplicationLocales (dev.kiln.KilnLocales).
    void InstallLocales(JNIEnv* env);
    // security::SecureStore's key store (dev.kiln.KilnSecure, the Android Keystore).
    void InstallSecurity(JNIEnv* env);
    // kiln::alarm (dev.kiln.KilnAlarms, AlarmManager).
    void InstallAlarms(JNIEnv* env);
    void InstallSpeech(JNIEnv* env);

}
