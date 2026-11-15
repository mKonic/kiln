// ui::SetApplicationLocales on Android: dev.kiln.KilnLocales (LocaleManager on 13+, AppCompat's
// stored locales before).
#include "AndroidServices.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/ui/AppLocales.h"
#include "kiln/ui/NightMode.h"
#include "kiln/lifecycle/ProcessLifecycleOwner.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Locales;
        jmethodID g_Get = nullptr, g_Set = nullptr;

        std::vector<std::string> Split(const std::string& tags) {
            std::vector<std::string> out;
            std::size_t start = 0;
            while (start < tags.size()) {
                std::size_t comma = tags.find(',', start);
                if (comma == std::string::npos) comma = tags.size();
                if (comma > start) out.push_back(tags.substr(start, comma - start));
                start = comma + 1;
            }
            return out;
        }
    }

    // lifecycle::ProcessLifecycleOwner: dev.kiln.KilnProcessLifecycle reports every activity.
    void InstallProcessLifecycle(JNIEnv* env) {
        jclass c = env->FindClass("dev/kiln/KilnProcessLifecycle");
        if (!c) {
            env->ExceptionClear();
            return;
        }
        using P = lifecycle::ProcessLifecycleOwner;
        static const JNINativeMethod methods[] = {
            { "started", "()V", reinterpret_cast<void*>(+[](JNIEnv*, jclass) { P::Get().ActivityStarted(); }) },
            { "resumed", "()V", reinterpret_cast<void*>(+[](JNIEnv*, jclass) { P::Get().ActivityResumed(); }) },
            { "paused", "()V", reinterpret_cast<void*>(+[](JNIEnv*, jclass) { P::Get().ActivityPaused(); }) },
            { "stopped", "()V", reinterpret_cast<void*>(+[](JNIEnv*, jclass) { P::Get().ActivityStopped(); }) },
        };
        if (env->RegisterNatives(c, methods, 4) != 0) env->ExceptionClear();
        else P::Get().SetPlatformDriven(true);
        env->DeleteLocalRef(c);
    }

    void InstallLocales(JNIEnv* env) {
        try {
            g_Locales = jvm::FindClass("dev/kiln/KilnLocales");
            g_Get = jvm::detail::StaticMethod(env, g_Locales.get(), "get", "()Ljava/lang/String;");
            g_Set = jvm::detail::StaticMethod(env, g_Locales.get(), "set", "(Ljava/lang/String;)V");
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "locales: no host: %s", e.what());
            return;
        }
        // ui::SetDefaultNightMode: dev.kiln.KilnNightMode (AppCompat's, in the process).
        static jvm::Global<jclass> nightClass;
        static jmethodID nightSet = nullptr;
        try {
            nightClass = jvm::FindClass("dev/kiln/KilnNightMode");
            nightSet = jvm::detail::StaticMethod(env, nightClass.get(), "set", "(I)V");
            ui::SetNightModeHost({ .set = [](ui::NightMode mode) {
                JNIEnv* e = jvm::Env();
                e->CallStaticVoidMethod(nightClass.get(), nightSet, jint(mode));
                if (e->ExceptionCheck()) {
                    e->ExceptionDescribe();
                    e->ExceptionClear();
                }
            } });
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "night mode: no host: %s", e.what());
        }
        ui::SetAppLocalesHost({
            .set = [](const std::vector<std::string>& tags) {
                std::string joined;
                for (const auto& t : tags) joined += (joined.empty() ? "" : ",") + t;
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> s = jvm::ToJava(env, joined);
                env->CallStaticVoidMethod(g_Locales.get(), g_Set, s.get());
                if (env->ExceptionCheck()) {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                }
            },
            .get = [] {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> s(static_cast<jstring>(env->CallStaticObjectMethod(g_Locales.get(), g_Get)), env);
                if (env->ExceptionCheck()) {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                    return std::vector<std::string>{};
                }
                return Split(jvm::ToString(env, s.get()));
            },
        });
    }

}
