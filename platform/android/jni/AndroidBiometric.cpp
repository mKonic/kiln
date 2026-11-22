// ui::Biometric on Android: dev.kiln.KilnBiometric shows the prompt from an encoded request and
// reports its events; the key store's keys come from dev.kiln.KilnSecure.
#include "AndroidServices.h"

#include "kiln/jvm/Codec.h"
#include "kiln/jvm/Jvm.h"
#include "kiln/security/KeyStore.h"
#include "kiln/ui/Biometric.h"

#include <android/log.h>

#include <map>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Biometric, g_Secure;
        jmethodID g_CanAuthenticate = nullptr, g_Strings = nullptr, g_Authenticate = nullptr, g_Cancel = nullptr, g_Multiple = nullptr;
        jmethodID g_Generate = nullptr, g_Delete = nullptr, g_Contains = nullptr, g_PublicKey = nullptr;
        std::map<int, kiln::Function<void(const ui::BiometricEvent&)>> g_Prompts;
        int g_NextId = 1;

        bool Check(JNIEnv* env) {
            if (!env->ExceptionCheck()) return true;
            env->ExceptionDescribe();
            env->ExceptionClear();
            return false;
        }

        void WriteStrings(jvm::Writer& w, const std::vector<std::string>& v) {
            w.I32(std::int32_t(v.size()));
            for (const std::string& s : v) w.Str(s);
        }

        // The request, in KilnBiometric.authenticate's order.
        std::vector<std::uint8_t> Encode(const ui::BiometricPromptInfo& info, const std::optional<ui::CryptoObject>& crypto) {
            jvm::Writer w;
            w.Str(info.title);
            w.Str(info.subtitle);
            w.Str(info.description);
            w.Str(info.negativeButtonText);
            w.I32(int(info.negativeButton));
            w.I32(info.allowedAuthenticators);
            w.Bool(info.confirmationRequired);
            w.Str(info.logoRes);
            w.Bytes(info.logoBitmap);
            w.Str(info.logoDescription);
            w.Bool(info.contentView.has_value());
            w.I32(info.contentView ? int(info.contentView->kind) : 0);
            w.Str(info.contentView ? info.contentView->description : std::string());
            const auto items = info.contentView ? info.contentView->items : std::vector<ui::PromptContentItem>{};
            w.I32(std::int32_t(items.size()));
            for (const auto& i : items) {
                w.I32(int(i.kind));
                w.Str(i.text);
            }
            w.I32(std::int32_t(info.fallbackOptions.size()));
            for (const auto& f : info.fallbackOptions) {
                w.Str(f.text);
                w.I32(f.iconType);
            }
            w.Bool(crypto.has_value());
            if (crypto) {
                w.I32(int(crypto->kind));
                w.Str(crypto->keyAlias);
                w.Str(crypto->algorithm);
                w.I32(int(crypto->mode));
                w.Bytes(crypto->iv);
                w.Bytes(crypto->associatedData);
                w.Bytes(crypto->input);
                w.Bytes(crypto->peerPublicKey);
                w.I64(crypto->operationHandle);
            }
            return w.Take();
        }

        // Prompts by id, with the crypto object each was given (its output filled on success).
        std::map<int, std::optional<ui::CryptoObject>> g_Crypto;

        void EventJni(JNIEnv* env, jclass, jint id, jbyteArray bytes) {
            const std::vector<std::uint8_t> b = jvm::ToBytes(env, bytes);
            {   // On the main thread (the prompt's executor).
                auto it = g_Prompts.find(int(id));
                if (it == g_Prompts.end()) return;
                jvm::Reader r(b);
                ui::BiometricEvent e;
                e.kind = ui::BiometricEvent::Kind(r.I32());
                e.errorCode = r.I32();
                e.message = r.Str();
                e.authenticationType = r.I32();
                e.fallbackIndex = r.I32();
                const bool hasOutput = r.Bool();
                std::vector<std::uint8_t> output = r.Bytes(), outputIv = r.Bytes();
                if (hasOutput) {
                    e.crypto = g_Crypto[int(id)];
                    if (e.crypto) {
                        e.crypto->output = std::move(output);
                        e.crypto->outputIv = std::move(outputIv);
                    }
                }
                kiln::Function<void(const ui::BiometricEvent&)> fn = it->second;
                if (e.kind != ui::BiometricEvent::Kind::Failed) {
                    g_Prompts.erase(it);
                    g_Crypto.erase(int(id));
                }
                fn(e);
            }
        }
    }

    void InstallBiometric(JNIEnv* env) {
        try {
            g_Biometric = jvm::FindClass("dev/kiln/KilnBiometric");
            jclass c = g_Biometric.get();
            static const JNINativeMethod natives[] = {
                { "event", "(I[B)V", reinterpret_cast<void*>(&EventJni) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 1);
            g_CanAuthenticate = jvm::detail::StaticMethod(env, c, "canAuthenticate", "(I)I");
            g_Strings = jvm::detail::StaticMethod(env, c, "strings", "(I)[Ljava/lang/String;");
            g_Authenticate = jvm::detail::StaticMethod(env, c, "authenticate", "(I[B)Z");
            g_Cancel = jvm::detail::StaticMethod(env, c, "cancel", "(I)V");
            g_Multiple = jvm::detail::StaticMethod(env, c, "multipleFallbackOptions", "()Z");
            g_Secure = jvm::FindClass("dev/kiln/KilnSecure");
            jclass s = g_Secure.get();
            g_Generate = jvm::detail::StaticMethod(env, s, "generateKey", "(Ljava/lang/String;[B)Z");
            g_Delete = jvm::detail::StaticMethod(env, s, "deleteKey", "(Ljava/lang/String;)Z");
            g_Contains = jvm::detail::StaticMethod(env, s, "containsKey", "(Ljava/lang/String;)Z");
            g_PublicKey = jvm::detail::StaticMethod(env, s, "publicKey", "(Ljava/lang/String;)[B");
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "biometric: no host: %s", e.what());
            return;
        }
        ui::BiometricHost h;
        h.canAuthenticate = [](int a) {
            JNIEnv* env = jvm::Env();
            const int r = env->CallStaticIntMethod(g_Biometric.get(), g_CanAuthenticate, jint(a));
            return Check(env) ? r : ui::BIOMETRIC_ERROR_HW_UNAVAILABLE;
        };
        h.strings = [](int a) -> std::optional<ui::BiometricStrings> {
            JNIEnv* env = jvm::Env();
            jvm::Local<jobjectArray> arr(static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Biometric.get(), g_Strings, jint(a))), env);
            if (!Check(env) || !arr) return std::nullopt;
            ui::BiometricStrings out;
            std::optional<std::string>* fields[] = { &out.buttonLabel, &out.promptMessage, &out.settingName };
            for (jsize i = 0; i < 3 && i < env->GetArrayLength(arr.get()); ++i) {
                jvm::Local<jstring> s(static_cast<jstring>(env->GetObjectArrayElement(arr.get(), i)), env);
                if (s) *fields[i] = jvm::ToString(env, s.get());
            }
            return out;
        };
        h.authenticate = [](const ui::BiometricPromptInfo& info, const std::optional<ui::CryptoObject>& crypto,
                            kiln::Function<void(const ui::BiometricEvent&)> events) -> int {
            JNIEnv* env = jvm::Env();
            const int id = g_NextId++;
            g_Prompts[id] = std::move(events);
            g_Crypto[id] = crypto;
            const std::vector<std::uint8_t> request = Encode(info, crypto);
            jvm::Local<jbyteArray> b = jvm::ToJava(env, std::span<const std::uint8_t>(request));
            const jboolean ok = env->CallStaticBooleanMethod(g_Biometric.get(), g_Authenticate, jint(id), b.get());
            if (!Check(env) || !ok) {
                g_Prompts.erase(id);
                g_Crypto.erase(id);
                return -1;
            }
            return id;
        };
        h.cancel = [](int id) {
            // The platform then reports ERROR_CANCELED; a prompt whose owner is gone has no
            // callback left to receive it.
            JNIEnv* env = jvm::Env();
            env->CallStaticVoidMethod(g_Biometric.get(), g_Cancel, jint(id));
            Check(env);
        };
        h.multipleFallbackOptions = [] {
            JNIEnv* env = jvm::Env();
            const jboolean r = env->CallStaticBooleanMethod(g_Biometric.get(), g_Multiple);
            return Check(env) && r == JNI_TRUE;
        };
        ui::SetBiometricHost(std::move(h));

        security::KeyStoreHost k;
        k.generate = [](const std::string& alias, const security::KeyGenSpec& spec) {
            jvm::Writer w;
            w.I32(int(spec.algorithm));
            w.I32(spec.keySize);
            w.I32(spec.purposes);
            WriteStrings(w, spec.blockModes);
            WriteStrings(w, spec.encryptionPaddings);
            WriteStrings(w, spec.digests);
            WriteStrings(w, spec.signaturePaddings);
            w.Bool(spec.userAuthenticationRequired);
            w.I32(spec.userAuthenticationTimeoutSeconds);
            w.I32(spec.userAuthenticationTypes);
            w.Bool(spec.invalidatedByBiometricEnrollment);
            w.Bool(spec.unlockedDeviceRequired);
            w.Bool(spec.isStrongBoxBacked);
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> a = jvm::ToJava(env, alias);
            jvm::Local<jbyteArray> b = jvm::ToJava(env, std::span<const std::uint8_t>(w.Data()));
            const jboolean ok = env->CallStaticBooleanMethod(g_Secure.get(), g_Generate, a.get(), b.get());
            return Check(env) && ok == JNI_TRUE;
        };
        k.remove = [](const std::string& alias) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> a = jvm::ToJava(env, alias);
            const jboolean ok = env->CallStaticBooleanMethod(g_Secure.get(), g_Delete, a.get());
            return Check(env) && ok == JNI_TRUE;
        };
        k.contains = [](const std::string& alias) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> a = jvm::ToJava(env, alias);
            const jboolean ok = env->CallStaticBooleanMethod(g_Secure.get(), g_Contains, a.get());
            return Check(env) && ok == JNI_TRUE;
        };
        k.publicKey = [](const std::string& alias) -> std::optional<std::vector<std::uint8_t>> {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> a = jvm::ToJava(env, alias);
            jvm::Local<jbyteArray> out(static_cast<jbyteArray>(env->CallStaticObjectMethod(g_Secure.get(), g_PublicKey, a.get())), env);
            if (!Check(env) || !out) return std::nullopt;
            return jvm::ToBytes(env, out.get());
        };
        security::SetKeyStoreHost(std::move(k));
    }

}
