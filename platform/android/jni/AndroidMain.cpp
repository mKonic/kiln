// JNI entry points for dev.kiln.KilnNative and the Choreographer-driven frame loop.
#include "AndroidText.h"

#include "kiln/App.h"

#include <hearth/Log.h>
#include <hearth/surface/AndroidSurface.h>

#include <android/choreographer.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <memory>
#include <string>

namespace {

    struct Host {
        std::unique_ptr<kiln::android::AndroidText> text;
        std::unique_ptr<kiln::App> app;
        kiln_app handle{};
        hearth::AndroidSurface surface;
        ANativeWindow* window = nullptr;
        bool visible = false;
        bool callbackPosted = false;
    };
    Host* g_Host = nullptr;

    void OnVsync(int64_t frameTimeNanos, void*) {
        if (!g_Host) return;
        g_Host->callbackPosted = false;
        if (g_Host->visible && g_Host->window) g_Host->app->Frame(double(frameTimeNanos) * 1e-9);
    }

    void PostFrame(void*) {
        if (!g_Host || g_Host->callbackPosted || !g_Host->visible) return;
        g_Host->callbackPosted = true;
        AChoreographer_postFrameCallback64(AChoreographer_getInstance(), &OnVsync, nullptr);
    }

    void LogToLogcat(hearth::LogLevel level, std::string_view message, void*) {
        int prio = ANDROID_LOG_INFO;
        switch (level) {
            case hearth::LogLevel::Trace: prio = ANDROID_LOG_VERBOSE; break;
            case hearth::LogLevel::Warn:  prio = ANDROID_LOG_WARN; break;
            case hearth::LogLevel::Error: prio = ANDROID_LOG_ERROR; break;
            default: break;
        }
        __android_log_print(prio, "kiln", "%.*s", int(message.size()), message.data());
    }

    void Create(JNIEnv* env, jclass, jobject /*view*/, jobject text, jfloat density, jfloat fontScale) {
        if (g_Host) {   // Activity recreated: keep the app, swap nothing for M0.
            g_Host->app->SetDensity(density, fontScale);
            return;
        }
        hearth::SetLogSink(&LogToLogcat);
        g_Host = new Host();
        g_Host->text = std::make_unique<kiln::android::AndroidText>(env, text);
        g_Host->app = std::make_unique<kiln::App>(kiln::HostServices{ g_Host->text.get(), &PostFrame, nullptr });
        g_Host->app->SetDensity(density, fontScale);
        g_Host->handle.impl = g_Host->app.get();
        kiln_main(&g_Host->handle);
    }

    void SetVisible(JNIEnv*, jclass, jboolean visible) {
        g_Host->visible = visible;
        if (visible && g_Host->app->FramePending()) {
            g_Host->callbackPosted = false;
            PostFrame(nullptr);
        }
    }

    void SetDensity(JNIEnv*, jclass, jfloat density, jfloat fontScale) {
        g_Host->app->SetDensity(density, fontScale);
        g_Host->app->RequestFrame();
    }

    void SurfaceCreated(JNIEnv* env, jclass, jobject surface) {
        g_Host->window = ANativeWindow_fromSurface(env, surface);
        g_Host->surface.Reset(g_Host->window);
        g_Host->app->SurfaceCreated(g_Host->surface);
    }

    void SurfaceChanged(JNIEnv*, jclass, jint width, jint height) {
        g_Host->app->SurfaceResized(std::uint32_t(width), std::uint32_t(height));
    }

    void SurfaceDestroyed(JNIEnv*, jclass) {
        g_Host->app->SurfaceDestroyed();
        g_Host->surface.Reset(nullptr);
        if (g_Host->window) ANativeWindow_release(g_Host->window);
        g_Host->window = nullptr;
    }

}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = env->FindClass("dev/kiln/KilnNative");
    if (!cls) return JNI_ERR;
    static const JNINativeMethod methods[] = {
        { "create", "(Ldev/kiln/KilnView;Ldev/kiln/KilnText;FF)V", reinterpret_cast<void*>(&Create) },
        { "setVisible", "(Z)V", reinterpret_cast<void*>(&SetVisible) },
        { "setDensity", "(FF)V", reinterpret_cast<void*>(&SetDensity) },
        { "surfaceCreated", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(&SurfaceCreated) },
        { "surfaceChanged", "(II)V", reinterpret_cast<void*>(&SurfaceChanged) },
        { "surfaceDestroyed", "()V", reinterpret_cast<void*>(&SurfaceDestroyed) },
    };
    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}
