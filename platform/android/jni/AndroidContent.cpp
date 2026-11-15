#include "AndroidContent.h"

#include "kiln/image/ImageLoader.h"
#include "kiln/ui/ContentResolver.h"
#include "kiln/ui/DocumentFile.h"

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

    namespace {
        // ui::DocumentFile's host: dev.kiln.KilnDocuments.
        class AndroidDocuments final : public ui::DocumentFile::Host {
        public:
            explicit AndroidDocuments(JNIEnv* env) {
                env->GetJavaVM(&m_Vm);
                jclass c = env->FindClass("dev/kiln/KilnDocuments");
                m_Class = static_cast<jclass>(env->NewGlobalRef(c));
                env->DeleteLocalRef(c);
                const char* S = "(Ljava/lang/String;)Ljava/lang/String;";
                m_IsDoc = env->GetStaticMethodID(m_Class, "isDocumentUri", "(Ljava/lang/String;)Z");
                m_Tree = env->GetStaticMethodID(m_Class, "treeDocumentUri", S);
                m_Query = env->GetStaticMethodID(m_Class, "query", "(Ljava/lang/String;)[Ljava/lang/String;");
                m_Perm = env->GetStaticMethodID(m_Class, "hasPermission", "(Ljava/lang/String;Z)Z");
                m_Create = env->GetStaticMethodID(m_Class, "create", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
                m_Delete = env->GetStaticMethodID(m_Class, "delete", "(Ljava/lang/String;)Z");
                m_Rename = env->GetStaticMethodID(m_Class, "rename", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
                m_Children = env->GetStaticMethodID(m_Class, "children", "(Ljava/lang/String;)[Ljava/lang/String;");
            }
            bool IsDocumentUri(const std::string& uri) override {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return false;
                jstring u = Str(env, uri);
                const bool r = env->CallStaticBooleanMethod(m_Class, m_IsDoc, u);
                Clear(env);
                env->DeleteLocalRef(u);
                return r;
            }
            std::optional<std::string> TreeDocumentUri(const std::string& treeUri) override { return CallString(m_Tree, { treeUri }); }
            std::optional<ui::DocumentFile::DocumentInfo> Query(const std::string& uri) override {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return std::nullopt;
                jstring u = Str(env, uri);
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Query, u));
                Clear(env);
                env->DeleteLocalRef(u);
                if (!arr) return std::nullopt;
                ui::DocumentFile::DocumentInfo info;
                info.displayName = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, 0)));
                info.mimeType = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, 1)));
                auto number = [&](int i) -> std::int64_t {
                    auto s = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, i)));
                    if (!s) return 0;
                    try { return std::stoll(*s); } catch (...) { return 0; }
                };
                info.flags = number(2);
                info.lastModified = number(3);
                info.size = number(4);
                env->DeleteLocalRef(arr);
                return info;
            }
            bool HasPermission(const std::string& uri, bool write) override {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return false;
                jstring u = Str(env, uri);
                const bool r = env->CallStaticBooleanMethod(m_Class, m_Perm, u, jboolean(write));
                Clear(env);
                env->DeleteLocalRef(u);
                return r;
            }
            std::optional<std::string> Create(const std::string& parent, const std::string& mime, const std::string& name) override {
                return CallString(m_Create, { parent, mime, name });
            }
            bool Delete(const std::string& uri) override {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return false;
                jstring u = Str(env, uri);
                const bool r = env->CallStaticBooleanMethod(m_Class, m_Delete, u);
                Clear(env);
                env->DeleteLocalRef(u);
                return r;
            }
            std::optional<std::string> Rename(const std::string& uri, const std::string& name) override { return CallString(m_Rename, { uri, name }); }
            std::vector<std::string> Children(const std::string& uri) override {
                std::vector<std::string> out;
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return out;
                jstring u = Str(env, uri);
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Children, u));
                Clear(env);
                env->DeleteLocalRef(u);
                if (!arr) return out;
                for (jsize i = 0; i < env->GetArrayLength(arr); ++i)
                    if (auto s = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, i)))) out.push_back(*s);
                env->DeleteLocalRef(arr);
                return out;
            }

        private:
            std::optional<std::string> CallString(jmethodID m, std::initializer_list<std::string> args) {
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) return std::nullopt;
                std::vector<jstring> js;
                for (const auto& a : args) js.push_back(Str(env, a));
                jobject r = nullptr;
                if (js.size() == 1) r = env->CallStaticObjectMethod(m_Class, m, js[0]);
                else if (js.size() == 2) r = env->CallStaticObjectMethod(m_Class, m, js[0], js[1]);
                else r = env->CallStaticObjectMethod(m_Class, m, js[0], js[1], js[2]);
                Clear(env);
                for (jstring s : js) env->DeleteLocalRef(s);
                return Take(env, static_cast<jstring>(r));
            }
            JavaVM* m_Vm = nullptr;
            jclass m_Class = nullptr;
            jmethodID m_IsDoc{}, m_Tree{}, m_Query{}, m_Perm{}, m_Create{}, m_Delete{}, m_Rename{}, m_Children{};
        };
    }

    void InstallContentResolver(JNIEnv* env) {
        ui::DocumentFile::SetHost(std::make_shared<AndroidDocuments>(env));
        ui::SetContentResolver(std::make_shared<AndroidContentResolver>(env));
        // The image loader's memory cache follows the app's memory class.
        if (jclass c = env->FindClass("dev/kiln/KilnContent")) {
            jmethodID m = env->GetStaticMethodID(c, "memoryClass", "()[J");
            auto* arr = m ? static_cast<jlongArray>(env->CallStaticObjectMethod(c, m)) : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (arr) {
                jlong v[2] = { 0, 0 };
                env->GetLongArrayRegion(arr, 0, 2, v);
                if (v[0] > 0) image::SetMemoryClass(v[0], v[1] != 0);
                env->DeleteLocalRef(arr);
            }
            env->DeleteLocalRef(c);
        } else {
            env->ExceptionClear();
        }
    }

}
