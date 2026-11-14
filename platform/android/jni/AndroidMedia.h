#pragma once

#include <android/asset_manager.h>
#include <jni.h>

namespace kiln::android {

    // The app's assets (null until the activity's host installed them).
    AAssetManager* AssetManager();
    // media::Player's engine (AMediaExtractor, AMediaCodec, AAudio).
    void InstallMedia();
    // media::CameraHost (the NDK's camera2).
    void InstallCamera();
    // Audio focus, becoming noisy and media sessions (dev.kiln.KilnMedia, KilnMediaService).
    void InstallMediaSession(JNIEnv* env);

}
