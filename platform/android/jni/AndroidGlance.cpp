// kiln::glance on Android: dev.kiln.KilnWidgetReceiver asks for a widget's drawings, runs its
// callbacks and reports deletions, each a native task on the main dispatcher; the host updates,
// lists, measures and pins widgets through it.
#include "AndroidGlance.h"
#include "AndroidServices.h"

#include "kiln/glance/Glance.h"
#include "kiln/jvm/Codec.h"
#include "kiln/jvm/Jvm.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Receiver;
        jmethodID g_UpdateAll = nullptr, g_Update = nullptr, g_Ids = nullptr, g_Sizes = nullptr, g_RequestPin = nullptr, g_MinimumSize = nullptr, g_SetPreview = nullptr;

        using Result = std::optional<std::vector<std::uint8_t>>;

        // The drawing's bytes for KilnWidgetReceiver.show.
        rt::Task<Result> DrawTask(std::string widget, int id, std::vector<ui::Size> launcher, ui::Size minimum, ui::Bundle options) {
            jvm::Writer w;
            const glance::GlanceAppWidget* g = glance::FindWidget(widget);
            if (!g) {
                w.Bool(false);
                co_return w.Take();
            }
            std::vector<ui::Size> sizes = glance::SizesToDraw(*g, launcher, minimum);
            glance::Drawing d = co_await glance::Draw(widget, { widget, id, {}, std::move(options) }, sizes);
            w.Bool(d.found);
            w.Bool(d.failed);
            w.Str(d.errorLayout);
            w.I32(std::int32_t(d.contents.size()));
            for (std::size_t i = 0; i < d.contents.size(); ++i) {
                w.F32(sizes[i].width);
                w.F32(sizes[i].height);
                w.Bytes(d.contents[i]);
            }
            co_return w.Take();
        }

        void DrawJni(JNIEnv* env, jclass, jstring name, jint id, jfloatArray launcher, jfloat minWidth, jfloat minHeight, jobjectArray options, jobject done) {
            try {
                std::vector<ui::Size> sizes;
                jsize n = launcher ? env->GetArrayLength(launcher) : 0;
                std::vector<float> v(static_cast<std::size_t>(n));
                if (n) env->GetFloatArrayRegion(launcher, 0, n, v.data());
                for (jsize i = 0; i + 1 < n; i += 2) sizes.push_back({ v[std::size_t(i)], v[std::size_t(i) + 1] });
                ui::Bundle o = options ? BundleFromJavaArrays(env, options) : ui::Bundle{};
                jvm::detail::StartTask(env, done, DrawTask(jvm::ToString(env, name), int(id), std::move(sizes), { minWidth, minHeight }, std::move(o)));
            } catch (...) {
                jvm::ThrowCurrent(env);
            }
        }

        rt::Task<Result> CallbackTask(glance::ActionContext c, std::optional<std::string> key, std::optional<std::string> lambda) {
            if (lambda) {
                if (!co_await glance::RunLambdaAction(c.widget, c.appWidgetId, *lambda))
                    __android_log_print(ANDROID_LOG_ERROR, "kiln", "widget %s: no lambda %s in its content", c.widget.c_str(), lambda->c_str());
                co_return std::nullopt;
            }
            const std::string k = key.value_or("");
            if (!co_await glance::RunActionCallback(k, c))
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "widget: no action callback %s (register it with glance::ActionCallbackRegistration)", k.c_str());
            co_return std::nullopt;
        }

        void RunCallbackJni(JNIEnv* env, jclass, jstring name, jint id, jstring key, jstring lambda, jbyteArray params, jobject done) {
            try {
                glance::ActionContext c;
                c.widget = jvm::ToString(env, name);
                c.appWidgetId = id;
                c.parameters = glance::ParametersFromBytes(jvm::ToBytes(env, params));
                std::optional<std::string> k, l;
                if (key) k = jvm::ToString(env, key);
                if (lambda) l = jvm::ToString(env, lambda);
                jvm::detail::StartTask(env, done, CallbackTask(std::move(c), std::move(k), std::move(l)));
            } catch (...) {
                jvm::ThrowCurrent(env);
            }
        }

        rt::Task<Result> DeletedTask(std::string widget, int id) {
            co_await glance::Deleted(std::move(widget), id);
            co_return std::nullopt;
        }

        void DeletedJni(JNIEnv* env, jclass, jstring name, jint id, jobject done) {
            try {
                jvm::detail::StartTask(env, done, DeletedTask(jvm::ToString(env, name), int(id)));
            } catch (...) {
                jvm::ThrowCurrent(env);
            }
        }

        jboolean RedrawsOnResizeJni(JNIEnv* env, jclass, jstring name, jint sdk) {
            const glance::GlanceAppWidget* w = glance::FindWidget(jvm::ToString(env, name));
            return w && glance::RedrawsOnResize(*w, int(sdk)) ? JNI_TRUE : JNI_FALSE;
        }

        bool Check(JNIEnv* env) {
            if (!env->ExceptionCheck()) return true;
            env->ExceptionDescribe();
            env->ExceptionClear();
            return false;
        }
    }

    void InstallGlance(JNIEnv* env) {
        try {
            g_Receiver = jvm::FindClass("dev/kiln/KilnWidgetReceiver");
            jclass c = g_Receiver.get();
            static const JNINativeMethod natives[] = {
                { "draw", "(Ljava/lang/String;I[FFF[Ljava/lang/Object;Ldev/kiln/bridge/Completion;)V", reinterpret_cast<void*>(&DrawJni) },
                { "runCallback", "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;[BLdev/kiln/bridge/Completion;)V", reinterpret_cast<void*>(&RunCallbackJni) },
                { "deleted", "(Ljava/lang/String;ILdev/kiln/bridge/Completion;)V", reinterpret_cast<void*>(&DeletedJni) },
                { "redrawsOnResize", "(Ljava/lang/String;I)Z", reinterpret_cast<void*>(&RedrawsOnResizeJni) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 4);
            g_UpdateAll = jvm::detail::StaticMethod(env, c, "updateAll", "(Ljava/lang/String;)V");
            g_Update = jvm::detail::StaticMethod(env, c, "update", "(Ljava/lang/String;I)V");
            g_Ids = jvm::detail::StaticMethod(env, c, "appWidgetIds", "(Ljava/lang/String;)[I");
            g_Sizes = jvm::detail::StaticMethod(env, c, "appWidgetSizes", "(I)[F");
            g_RequestPin = jvm::detail::StaticMethod(env, c, "requestPin", "(Ljava/lang/String;[BLjava/lang/String;)Z");
            g_MinimumSize = jvm::detail::StaticMethod(env, c, "minimumSize", "(Ljava/lang/String;)[F");
            g_SetPreview = jvm::detail::StaticMethod(env, c, "setWidgetPreview", "(Ljava/lang/String;I[F[[B)Z");
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "widget: no host: %s", e.what());
            return;
        }
        glance::GlanceHost h;
        h.updateAll = [](const std::string& name) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            env->CallStaticVoidMethod(g_Receiver.get(), g_UpdateAll, n.get());
            Check(env);
        };
        h.update = [](const std::string& name, int id) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            env->CallStaticVoidMethod(g_Receiver.get(), g_Update, n.get(), jint(id));
            Check(env);
        };
        h.appWidgetIds = [](const std::string& name) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            jvm::Local<jintArray> a(static_cast<jintArray>(env->CallStaticObjectMethod(g_Receiver.get(), g_Ids, n.get())), env);
            std::vector<int> out;
            if (!Check(env) || !a) return out;
            out.resize(std::size_t(env->GetArrayLength(a.get())));
            if (!out.empty()) env->GetIntArrayRegion(a.get(), 0, jsize(out.size()), reinterpret_cast<jint*>(out.data()));
            return out;
        };
        h.appWidgetSizes = [](int id) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jfloatArray> a(static_cast<jfloatArray>(env->CallStaticObjectMethod(g_Receiver.get(), g_Sizes, jint(id))), env);
            std::vector<ui::Size> out;
            if (!Check(env) || !a) return out;
            std::vector<float> v(std::size_t(env->GetArrayLength(a.get())));
            if (!v.empty()) env->GetFloatArrayRegion(a.get(), 0, jsize(v.size()), v.data());
            for (std::size_t i = 0; i + 1 < v.size(); i += 2) out.push_back({ v[i], v[i + 1] });
            return out;
        };
        h.requestPin = [](const std::string& name, const std::vector<std::uint8_t>& preview, const std::string& link) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            jvm::Local<jbyteArray> p = jvm::ToJava(env, std::span<const std::uint8_t>(preview));
            jvm::Local<jstring> l = jvm::ToJava(env, link);
            jboolean ok = env->CallStaticBooleanMethod(g_Receiver.get(), g_RequestPin, n.get(), p.get(), l.get());
            return Check(env) && ok == JNI_TRUE;
        };
        h.minimumSize = [](const std::string& name) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            jvm::Local<jfloatArray> a(static_cast<jfloatArray>(env->CallStaticObjectMethod(g_Receiver.get(), g_MinimumSize, n.get())), env);
            float v[2] = { 0, 0 };
            if (Check(env) && a && env->GetArrayLength(a.get()) == 2) env->GetFloatArrayRegion(a.get(), 0, 2, v);
            return ui::Size{ v[0], v[1] };
        };
        h.setWidgetPreview = [](const std::string& name, int category, const std::vector<ui::Size>& sizes, const std::vector<std::vector<std::uint8_t>>& contents) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> n = jvm::ToJava(env, name);
            std::vector<float> flat;
            for (const ui::Size& s : sizes) { flat.push_back(s.width); flat.push_back(s.height); }
            jvm::Local<jfloatArray> sz(env->NewFloatArray(jsize(flat.size())), env);
            if (!flat.empty()) env->SetFloatArrayRegion(sz.get(), 0, jsize(flat.size()), flat.data());
            jvm::Local<jclass> bytesClass(env->FindClass("[B"), env);
            jvm::Local<jobjectArray> arr(env->NewObjectArray(jsize(contents.size()), bytesClass.get(), nullptr), env);
            for (std::size_t i = 0; i < contents.size(); ++i) {
                jvm::Local<jbyteArray> b = jvm::ToJava(env, std::span<const std::uint8_t>(contents[i]));
                env->SetObjectArrayElement(arr.get(), jsize(i), b.get());
            }
            jboolean ok = env->CallStaticBooleanMethod(g_Receiver.get(), g_SetPreview, n.get(), jint(category), sz.get(), arr.get());
            return Check(env) && ok == JNI_TRUE;
        };
        glance::SetGlanceHost(std::move(h));
    }

}
