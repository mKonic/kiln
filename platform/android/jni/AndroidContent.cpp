#include "AndroidContent.h"

#include "kiln/ui/ContentResolver.h"

#include <string>

namespace kiln::android {

    namespace {
        // A JNIEnv for the calling thread, attaching it (as a daemon, detached at thread exit)
        // when it is not a VM thread: content is read from background dispatchers.
        JNIEnv* EnvFor(JavaVM* vm) {
            JNIEnv* env = nullptr;
            if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
            if (vm->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) return nullptr;
            struct Detach {
                JavaVM* vm;
                ~Detach() { vm->DetachCurrentThread(); }
            };
            thread_local Detach detach{ vm };
            return env;
        }
        jstring Str(JNIEnv* env, std::string_view s) { return env->NewStringUTF(std::string(s).c_str()); }
        std::optional<std::string> Take(JNIEnv* env, jstring s) {
            if (!s) return std::nullopt;
            const char* c = env->GetStringUTFChars(s, nullptr);
            std::string out = c ? c : "";
            if (c) env->ReleaseStringUTFChars(s, c);
            env->DeleteLocalRef(s);
            return out;
        }
        void Clear(JNIEnv* env) { if (env->ExceptionCheck()) env->ExceptionClear(); }

        class AndroidContentResolver final : public ui::ContentResolver {
        public:
            explicit AndroidContentResolver(JNIEnv* env) {
                env->GetJavaVM(&m_Vm);
                jclass c = env->FindClass("dev/kiln/KilnContent");
                m_Class = static_cast<jclass>(env->NewGlobalRef(c));
                env->DeleteLocalRef(c);
                m_Open = env->GetStaticMethodID(m_Class, "openFd", "(Ljava/lang/String;Ljava/lang/String;)I");
                m_Query = env->GetStaticMethodID(m_Class, "query", "(Ljava/lang/String;)[Ljava/lang/String;");
                m_Take = env->GetStaticMethodID(m_Class, "takePersistable", "(Ljava/lang/String;ZZ)Z");
                m_Release = env->GetStaticMethodID(m_Class, "releasePersistable", "(Ljava/lang/String;ZZ)Z");
                m_Persisted = env->GetStaticMethodID(m_Class, "persisted", "()[Ljava/lang/String;");
            }
            int OpenFileDescriptor(std::string_view uri, std::string_view mode) const override {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return -1;
                jstring u = Str(env, uri), m = Str(env, mode);
                const int fd = env->CallStaticIntMethod(m_Class, m_Open, u, m);
                Clear(env);
                env->DeleteLocalRef(u); env->DeleteLocalRef(m);
                return fd;
            }
            std::optional<ui::ContentInfo> Query(std::string_view uri) const override {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return std::nullopt;
                jstring u = Str(env, uri);
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Query, u));
                Clear(env);
                env->DeleteLocalRef(u);
                if (!arr) return std::nullopt;
                ui::ContentInfo info;
                info.displayName = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, 0)));
                if (auto s = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, 1)))) info.size = std::stoll(*s);
                info.mimeType = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, 2)));
                env->DeleteLocalRef(arr);
                return info;
            }
            bool TakePersistableUriPermission(std::string_view uri, bool read, bool write) const override { return Grant(m_Take, uri, read, write); }
            bool ReleasePersistableUriPermission(std::string_view uri, bool read, bool write) const override { return Grant(m_Release, uri, read, write); }
            std::vector<std::string> PersistedUriPermissions() const override {
                JNIEnv* env = EnvFor(m_Vm);
                std::vector<std::string> out;
                if (!env) return out;
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Persisted));
                Clear(env);
                if (!arr) return out;
                for (jsize i = 0; i < env->GetArrayLength(arr); ++i)
                    if (auto s = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, i)))) out.push_back(*s);
                env->DeleteLocalRef(arr);
                return out;
            }

        private:
            bool Grant(jmethodID m, std::string_view uri, bool read, bool write) const {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return false;
                jstring u = Str(env, uri);
                const bool ok = env->CallStaticBooleanMethod(m_Class, m, u, jboolean(read), jboolean(write));
                Clear(env);
                env->DeleteLocalRef(u);
                return ok;
            }
            JavaVM* m_Vm = nullptr;
            jclass m_Class = nullptr;
            jmethodID m_Open{}, m_Query{}, m_Take{}, m_Release{}, m_Persisted{};
        };
    }

    void InstallContentResolver(JNIEnv* env) {
        ui::SetContentResolver(std::make_shared<AndroidContentResolver>(env));
    }

}
