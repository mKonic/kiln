#pragma once

#include <jni.h>

#include "kiln/ui/Credentials.h"

namespace kiln::android {

    // ui::SetApplicationLocales (dev.kiln.KilnLocales).
    void InstallLocales(JNIEnv* env);
    void InstallProcessLifecycle(JNIEnv* env);
    // security::SecureStore's key store (dev.kiln.KilnSecure, the Android Keystore).
    void InstallSecurity(JNIEnv* env);
    // ui::Biometric (dev.kiln.KilnBiometric) and security::KeyStore (dev.kiln.KilnSecure).
    void InstallBiometric(JNIEnv* env);
    // kiln::alarm (dev.kiln.KilnAlarms, AlarmManager).
    void InstallAlarms(JNIEnv* env);
    void InstallSpeech(JNIEnv* env);
    // ui::CustomTabsClient (dev.kiln.KilnCustomTabs, the browser's binder service).
    void InstallCustomTabs(JNIEnv* env);
    // KilnBundles' {keys, kinds, values} arrays.
    jobjectArray BundleToJavaArrays(JNIEnv* env, const kiln::ui::Bundle& b);
    kiln::ui::Bundle BundleFromJavaArrays(JNIEnv* env, jobjectArray b);

}
