// Assets from the APK through AAssetManager. The Java AssetManager is kept alive by a global
// reference so the native manager stays valid for the process.

#include "kiln/Assets.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include <string>

namespace kiln::android {

    void InstallAssetReader(JNIEnv* env, jobject assets) {
        static jobject ref = nullptr;
        if (ref) env->DeleteGlobalRef(ref);
        ref = env->NewGlobalRef(assets);
        AAssetManager* mgr = AAssetManager_fromJava(env, ref);
        SetAssetReader([mgr](std::string_view path) -> std::optional<std::vector<std::uint8_t>> {
            AAsset* a = AAssetManager_open(mgr, std::string(path).c_str(), AASSET_MODE_BUFFER);
            if (!a) return std::nullopt;
            const auto* data = static_cast<const std::uint8_t*>(AAsset_getBuffer(a));
            std::optional<std::vector<std::uint8_t>> out;
            if (data) out.emplace(data, data + AAsset_getLength64(a));
            AAsset_close(a);
            return out;
        });
    }

}
