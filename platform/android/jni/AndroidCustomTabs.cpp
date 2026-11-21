// ui::CustomTabsClient's host on Android: dev.kiln.KilnCustomTabs (the browser's
// ICustomTabsService through its binder; callbacks come back through the natives below).
#include "AndroidServices.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/ui/CustomTabsClient.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Tabs;
        jmethodID g_HasCategory = nullptr, g_Bind = nullptr, g_Unbind = nullptr, g_Package = nullptr, g_Warmup = nullptr, g_NewSession = nullptr, g_NewAuth = nullptr, g_Extra = nullptr, g_Call = nullptr;

        bool Clear(JNIEnv* env) {
            if (!env->ExceptionCheck()) return false;
            env->ExceptionDescribe();
            env->ExceptionClear();
            return true;
        }
        std::string Str(JNIEnv* env, jstring s) { return s ? jvm::ToString(env, s) : std::string(); }

        void ConnectedJni(JNIEnv*, jclass, jint connection, jboolean connected) { ui::DispatchCustomTabsConnection(connection, connected); }
        void CallbackJni(JNIEnv* env, jclass, jint session, jstring name, jobjectArray args) {
            ui::DispatchCustomTabsCallback(session, Str(env, name), BundleFromJavaArrays(env, args));
        }
        jobjectArray CallbackWithResultJni(JNIEnv* env, jclass, jint session, jstring name, jobjectArray args) {
            std::optional<ui::Bundle> r = ui::DispatchCustomTabsCallbackWithResult(session, Str(env, name), BundleFromJavaArrays(env, args));
            return r ? BundleToJavaArrays(env, *r) : nullptr;
        }
        void EngagementJni(JNIEnv* env, jclass, jint callback, jstring name, jobjectArray args) {
            ui::DispatchEngagementSignal(callback, Str(env, name), BundleFromJavaArrays(env, args));
        }
    }

    void InstallCustomTabs(JNIEnv* env) {
        try {
            g_Tabs = jvm::FindClass("dev/kiln/KilnCustomTabs");
            jclass c = g_Tabs.get();
            g_HasCategory = jvm::detail::StaticMethod(env, c, "packageHasCategory", "(Ljava/lang/String;Ljava/lang/String;)Z");
            g_Bind = jvm::detail::StaticMethod(env, c, "bind", "(Ljava/lang/String;Z)I");
            g_Unbind = jvm::detail::StaticMethod(env, c, "unbind", "(I)V");
            g_Package = jvm::detail::StaticMethod(env, c, "packageName", "([Ljava/lang/String;Z)Ljava/lang/String;");
            g_Warmup = jvm::detail::StaticMethod(env, c, "warmup", "(IJ)Z");
            g_NewSession = jvm::detail::StaticMethod(env, c, "newSession", "(IIZ)I");
            g_NewAuth = jvm::detail::StaticMethod(env, c, "newAuthTabSession", "(IIZ)I");
            g_Extra = jvm::detail::StaticMethod(env, c, "extraCommand", "(ILjava/lang/String;[Ljava/lang/Object;)[Ljava/lang/Object;");
            g_Call = jvm::detail::StaticMethod(env, c, "sessionCall", "(ILjava/lang/String;[Ljava/lang/Object;)[Ljava/lang/Object;");
            static const JNINativeMethod natives[] = {
                { "connected", "(IZ)V", reinterpret_cast<void*>(&ConnectedJni) },
                { "callback", "(ILjava/lang/String;[Ljava/lang/Object;)V", reinterpret_cast<void*>(&CallbackJni) },
                { "callbackWithResult", "(ILjava/lang/String;[Ljava/lang/Object;)[Ljava/lang/Object;", reinterpret_cast<void*>(&CallbackWithResultJni) },
                { "engagement", "(ILjava/lang/String;[Ljava/lang/Object;)V", reinterpret_cast<void*>(&EngagementJni) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 4);
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "custom tabs: no host: %s", e.what());
            return;
        }
        ui::SetCustomTabsHost({
            .bind = [](const std::string& package, bool preserve) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> p = jvm::ToJava(env, package);
                const jint id = env->CallStaticIntMethod(g_Tabs.get(), g_Bind, p.get(), jboolean(preserve));
                return Clear(env) ? 0 : int(id);
            },
            .unbind = [](int id) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Tabs.get(), g_Unbind, jint(id));
                Clear(env);
            },
            .packageName = [](const std::vector<std::string>& packages, bool ignoreDefault) -> std::optional<std::string> {
                JNIEnv* env = jvm::Env();
                jclass sc = env->FindClass("java/lang/String");
                jvm::Local<jobjectArray> arr(env->NewObjectArray(jsize(packages.size()), sc, nullptr), env);
                env->DeleteLocalRef(sc);
                for (std::size_t i = 0; i < packages.size(); ++i) {
                    jvm::Local<jstring> s = jvm::ToJava(env, packages[i]);
                    env->SetObjectArrayElement(arr.get(), jsize(i), s.get());
                }
                jvm::Local<jstring> r(static_cast<jstring>(env->CallStaticObjectMethod(g_Tabs.get(), g_Package, arr.get(), jboolean(ignoreDefault))), env);
                if (Clear(env) || !r) return std::nullopt;
                return jvm::ToString(env, r.get());
            },
            .packageHasCategory = [](const std::string& provider, const std::string& category) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> p = jvm::ToJava(env, provider), k = jvm::ToJava(env, category);
                const jboolean ok = env->CallStaticBooleanMethod(g_Tabs.get(), g_HasCategory, p.get(), k.get());
                return !Clear(env) && ok;
            },
            .warmup = [](int connection, std::int64_t flags) {
                JNIEnv* env = jvm::Env();
                const jboolean ok = env->CallStaticBooleanMethod(g_Tabs.get(), g_Warmup, jint(connection), jlong(flags));
                return !Clear(env) && ok;
            },
            .newSession = [](int connection, std::optional<int> id) {
                JNIEnv* env = jvm::Env();
                const jint s = env->CallStaticIntMethod(g_Tabs.get(), g_NewSession, jint(connection), jint(id.value_or(0)), jboolean(id.has_value()));
                return Clear(env) ? 0 : int(s);
            },
            .newAuthTabSession = [](int connection, std::optional<int> id) {
                JNIEnv* env = jvm::Env();
                const jint s = env->CallStaticIntMethod(g_Tabs.get(), g_NewAuth, jint(connection), jint(id.value_or(0)), jboolean(id.has_value()));
                return Clear(env) ? 0 : int(s);
            },
            .extraCommand = [](int connection, const std::string& name, const ui::Bundle& args) -> std::optional<ui::Bundle> {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> n = jvm::ToJava(env, name);
                jvm::Local<jobjectArray> a(BundleToJavaArrays(env, args), env);
                jvm::Local<jobjectArray> r(static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Tabs.get(), g_Extra, jint(connection), n.get(), a.get())), env);
                if (Clear(env) || !r) return std::nullopt;
                return BundleFromJavaArrays(env, r.get());
            },
            .sessionCall = [](int session, const std::string& method, const ui::Bundle& args) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> m = jvm::ToJava(env, method);
                jvm::Local<jobjectArray> a(BundleToJavaArrays(env, args), env);
                jvm::Local<jobjectArray> r(static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Tabs.get(), g_Call, jint(session), m.get(), a.get())), env);
                if (Clear(env) || !r) return ui::Bundle{};
                return BundleFromJavaArrays(env, r.get());
            },
        });
    }

}
