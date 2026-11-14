// kiln::glance on Android: dev.kiln.KilnWidgetReceiver asks for a widget's content per size and
// runs its callbacks; the host updates, lists and pins widgets through it.
#include "AndroidGlance.h"

#include "kiln/glance/Glance.h"
#include "kiln/jvm/Jvm.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Receiver;
        jmethodID g_UpdateAll = nullptr, g_Ids = nullptr, g_RequestPin = nullptr;

        jobjectArray RenderJni(JNIEnv* env, jclass, jstring name, jint id, jfloatArray launcher, jfloat minWidth, jfloat minHeight) {
            try {
                const std::string widget = jvm::ToString(env, name);
                const glance::GlanceAppWidget* w = glance::FindWidget(widget);
                if (!w) return nullptr;
                std::vector<ui::Size> sizes;
                jsize n = launcher ? env->GetArrayLength(launcher) : 0;
                std::vector<float> v(static_cast<std::size_t>(n));
                if (n) env->GetFloatArrayRegion(launcher, 0, n, v.data());
                for (jsize i = 0; i + 1 < n; i += 2) sizes.push_back({ v[std::size_t(i)], v[std::size_t(i) + 1] });
                std::vector<ui::Size> draw = glance::SizesToDraw(*w, sizes, { minWidth, minHeight });

                jvm::Local<jfloatArray> drawn(env->NewFloatArray(jsize(draw.size() * 2)), env);
                std::vector<float> flat;
                for (const ui::Size& s : draw) { flat.push_back(s.width); flat.push_back(s.height); }
                env->SetFloatArrayRegion(drawn.get(), 0, jsize(flat.size()), flat.data());
                jvm::Local<jclass> bytesClass(env->FindClass("[B"), env);
                jvm::Local<jobjectArray> contents(env->NewObjectArray(jsize(draw.size()), bytesClass.get(), nullptr), env);
                for (std::size_t i = 0; i < draw.size(); ++i) {
                    std::vector<std::uint8_t> bytes = glance::Render(widget, { widget, int(id), draw[i] });
                    jvm::Local<jbyteArray> a = jvm::ToJava(env, std::span<const std::uint8_t>(bytes));
                    env->SetObjectArrayElement(contents.get(), jsize(i), a.get());
                }
                jvm::Local<jclass> objectClass(env->FindClass("java/lang/Object"), env);
                jobjectArray out = env->NewObjectArray(2, objectClass.get(), nullptr);
                env->SetObjectArrayElement(out, 0, drawn.get());
                env->SetObjectArrayElement(out, 1, contents.get());
                return out;
            } catch (...) {
                jvm::ThrowCurrent(env);
                return nullptr;
            }
        }

        jboolean RunCallbackJni(JNIEnv* env, jclass, jstring name, jint id, jstring key, jstring params, jint checked) {
            try {
                glance::ActionContext c;
                c.widget = jvm::ToString(env, name);
                c.appWidgetId = id;
                c.params = jvm::ToString(env, params);
                if (checked >= 0) c.checked = checked != 0;
                const std::string k = jvm::ToString(env, key);
                if (glance::RunActionCallback(k, c)) return JNI_TRUE;
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "widget: no action callback %s (register it with glance::ActionCallbackRegistration)", k.c_str());
                return JNI_FALSE;
            } catch (...) {
                jvm::ThrowCurrent(env);
                return JNI_FALSE;
            }
        }
    }

    void InstallGlance(JNIEnv* env) {
        try {
            g_Receiver = jvm::FindClass("dev/kiln/KilnWidgetReceiver");
            jclass c = g_Receiver.get();
            static const JNINativeMethod natives[] = {
                { "render", "(Ljava/lang/String;I[FFF)[Ljava/lang/Object;", reinterpret_cast<void*>(&RenderJni) },
                { "runCallback", "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;I)Z", reinterpret_cast<void*>(&RunCallbackJni) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 2);
            g_UpdateAll = jvm::detail::StaticMethod(env, c, "updateAll", "(Ljava/lang/String;)V");
            g_Ids = jvm::detail::StaticMethod(env, c, "appWidgetIds", "(Ljava/lang/String;)[I");
            g_RequestPin = jvm::detail::StaticMethod(env, c, "requestPin", "(Ljava/lang/String;)Z");
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "widget: no host: %s", e.what());
            return;
        }
        glance::GlanceHost h;
        h.updateAll = [](const std::string& name) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            env->CallStaticVoidMethod(g_Receiver.get(), g_UpdateAll, n.get());
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        };
        h.appWidgetIds = [](const std::string& name) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            jvm::Local<jintArray> a(static_cast<jintArray>(env->CallStaticObjectMethod(g_Receiver.get(), g_Ids, n.get())), env);
            std::vector<int> out;
            if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
            if (!a) return out;
            out.resize(std::size_t(env->GetArrayLength(a.get())));
            if (!out.empty()) env->GetIntArrayRegion(a.get(), 0, jsize(out.size()), reinterpret_cast<jint*>(out.data()));
            return out;
        };
        h.requestPin = [](const std::string& name) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            jboolean ok = env->CallStaticBooleanMethod(g_Receiver.get(), g_RequestPin, n.get());
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); return false; }
            return ok == JNI_TRUE;
        };
        glance::SetGlanceHost(std::move(h));
    }

}
