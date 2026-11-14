// security::SecureStore's key store on Android: dev.kiln.KilnSecure (the Android Keystore).
#include "AndroidServices.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/security/SecureStore.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Secure;
        jmethodID g_Encrypt = nullptr, g_Decrypt = nullptr;

        std::optional<std::vector<std::uint8_t>> Call(jmethodID m, const std::string& alias, const std::vector<std::uint8_t>& data, const std::string& aad) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> a = jvm::ToJava(env, alias), d = jvm::ToJava(env, aad);
            jvm::Local<jbyteArray> in = jvm::ToJava(env, std::span<const std::uint8_t>(data));
            jvm::Local<jbyteArray> out(static_cast<jbyteArray>(env->CallStaticObjectMethod(g_Secure.get(), m, a.get(), in.get(), d.get())), env);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                return std::nullopt;
            }
            if (!out) return std::nullopt;
            return jvm::ToBytes(env, out.get());
        }
    }

    void InstallSecurity(JNIEnv* env) {
        try {
            g_Secure = jvm::FindClass("dev/kiln/KilnSecure");
            g_Encrypt = jvm::detail::StaticMethod(env, g_Secure.get(), "encrypt", "(Ljava/lang/String;[BLjava/lang/String;)[B");
            g_Decrypt = jvm::detail::StaticMethod(env, g_Secure.get(), "decrypt", "(Ljava/lang/String;[BLjava/lang/String;)[B");
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "security: no key store host: %s", e.what());
            return;
        }
        security::SetCipherHost({
            .encrypt = [](const std::string& alias, const std::vector<std::uint8_t>& plain, const std::string& aad) {
                auto out = Call(g_Encrypt, alias, plain, aad);
                if (!out) throw std::runtime_error("SecureStore: the key store could not encrypt");
                return std::move(*out);
            },
            .decrypt = [](const std::string& alias, const std::vector<std::uint8_t>& sealed, const std::string& aad) {
                return Call(g_Decrypt, alias, sealed, aad);
            },
        });
    }

}
