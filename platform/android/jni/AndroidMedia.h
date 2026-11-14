#pragma once

#include <android/asset_manager.h>

namespace kiln::android {

    // The app's assets (null until the activity's host installed them).
    AAssetManager* AssetManager();
    // media::Player's engine (AMediaExtractor, AMediaCodec, AAudio).
    void InstallMedia();
    // media::CameraHost (the NDK's camera2).
    void InstallCamera();

}
