#include "AndroidHttp.h"

#include "kiln/net/Http.h"

#include <string>

namespace kiln::android {

    namespace {
        // A JNIEnv for the calling thread, attaching it (as a daemon, detached at thread exit)
        // when it is not a VM thread: requests run on the IO pool.
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
        std::string Take(JNIEnv* env, jobject s) {
            if (!s) return {};
            auto* js = static_cast<jstring>(s);
            const char* c = env->GetStringUTFChars(js, nullptr);
            std::string out = c ? c : "";
            if (c) env->ReleaseStringUTFChars(js, c);
            env->DeleteLocalRef(s);
            return out;
        }
        net::HttpError::Kind KindNamed(const std::string& name) {
            using K = net::HttpError::Kind;
            if (name == "InvalidUrl") return K::InvalidUrl;
            if (name == "UnknownHost") return K::UnknownHost;
            if (name == "Connect") return K::Connect;
            if (name == "Timeout") return K::Timeout;
            if (name == "Tls") return K::Tls;
            if (name == "Cleartext") return K::Cleartext;
            if (name == "Permission") return K::Permission;
            if (name == "TooManyRedirects") return K::TooManyRedirects;
            if (name == "Cancelled") return K::Cancelled;
            return K::Io;
        }

        class AndroidHttpEngine final : public net::HttpEngine {
        public:
            explicit AndroidHttpEngine(JNIEnv* env, jclass c) {
                env->GetJavaVM(&m_Vm);
                m_Class = static_cast<jclass>(env->NewGlobalRef(c));
                m_Execute = env->GetStaticMethodID(m_Class, "execute",
                    "(JLjava/lang/String;Ljava/lang/String;[Ljava/lang/String;[BLjava/lang/String;Ljava/lang/String;IIZ)[Ljava/lang/Object;");
                m_Cancel = env->GetStaticMethodID(m_Class, "cancel", "(J)V");
                jclass s = env->FindClass("java/lang/String");
                m_String = static_cast<jclass>(env->NewGlobalRef(s));
                env->DeleteLocalRef(s);
                jclass i = env->FindClass("java/lang/Integer");
                m_IntValue = env->GetMethodID(i, "intValue", "()I");
                env->DeleteLocalRef(i);
            }

            net::HttpResponse Execute(const net::HttpRequest& request, net::HttpCall& call) override {
                using K = net::HttpError::Kind;
                JNIEnv* env = EnvFor(m_Vm);
                if (!env) throw net::HttpError(K::Io, "kiln: no JNI environment");
                if (env->PushLocalFrame(32) != 0) { env->ExceptionClear(); throw net::HttpError(K::Io, "kiln: out of memory"); }
                struct Pop { JNIEnv* env; ~Pop() { env->PopLocalFrame(nullptr); } } pop{ env };

                const jlong handle = static_cast<jlong>(reinterpret_cast<std::intptr_t>(&call));
                const auto& fields = request.headers.Fields();
                jobjectArray headers = env->NewObjectArray(jsize(fields.size() * 2), m_String, nullptr);
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    jstring n = env->NewStringUTF(fields[i].first.c_str()), v = env->NewStringUTF(fields[i].second.c_str());
                    env->SetObjectArrayElement(headers, jsize(i * 2), n);
                    env->SetObjectArrayElement(headers, jsize(i * 2 + 1), v);
                    env->DeleteLocalRef(n);
                    env->DeleteLocalRef(v);
                }
                jbyteArray body = nullptr;
                if (!request.body.empty()) {
                    body = env->NewByteArray(jsize(request.body.size()));
                    if (!body) { env->ExceptionClear(); throw net::HttpError(K::Io, "kiln: request body too large"); }
                    env->SetByteArrayRegion(body, 0, jsize(request.body.size()), reinterpret_cast<const jbyte*>(request.body.data()));
                }
                jstring method = env->NewStringUTF(request.method.c_str());
                jstring url = env->NewStringUTF(request.url.c_str());
                jstring bodyFile = request.bodyFile.empty() ? nullptr : env->NewStringUTF(request.bodyFile.c_str());
                jstring responseFile = request.responseFile.empty() ? nullptr : env->NewStringUTF(request.responseFile.c_str());

                // Cancel reaches the connection through KilnHttp's table, keyed by the call.
                call.SetAbort([this, handle] {
                    if (JNIEnv* e = EnvFor(m_Vm)) {
                        e->CallStaticVoidMethod(m_Class, m_Cancel, handle);
                        if (e->ExceptionCheck()) e->ExceptionClear();
                    }
                });
                auto* result = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Execute, handle, method, url, headers, body, bodyFile,
                    responseFile, jint(request.connectTimeoutMs), jint(request.readTimeoutMs), jboolean(request.followRedirects)));
                call.SetAbort(nullptr);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    throw net::HttpError(call.Cancelled() ? K::Cancelled : K::Io, "kiln: request failed");
                }
                if (call.Cancelled()) throw net::HttpError(K::Cancelled, "cancelled");
                if (!result) throw net::HttpError(K::Io, "kiln: request failed");
                if (jobject kind = env->GetObjectArrayElement(result, 0)) {
                    const std::string name = Take(env, kind);
                    throw net::HttpError(KindNamed(name), Take(env, env->GetObjectArrayElement(result, 1)));
                }
                net::HttpResponse response;
                jobject status = env->GetObjectArrayElement(result, 1);
                response.status = env->CallIntMethod(status, m_IntValue);
                response.statusText = Take(env, env->GetObjectArrayElement(result, 2));
                auto* names = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 3));
                for (jsize i = 0; i + 1 < env->GetArrayLength(names); i += 2) {
                    std::string name = Take(env, env->GetObjectArrayElement(names, i));
                    response.headers.Add(std::move(name), Take(env, env->GetObjectArrayElement(names, i + 1)));
                }
                auto* bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(result, 4));
                response.body.resize(std::size_t(env->GetArrayLength(bytes)));
                env->GetByteArrayRegion(bytes, 0, jsize(response.body.size()), reinterpret_cast<jbyte*>(response.body.data()));
                response.url = Take(env, env->GetObjectArrayElement(result, 5));
                return response;
            }

        private:
            JavaVM* m_Vm = nullptr;
            jclass m_Class = nullptr, m_String = nullptr;
            jmethodID m_Execute{}, m_Cancel{}, m_IntValue{};
        };

        // KilnHttp.progress: on the IO thread running the call, which keeps it alive.
        jboolean Progress(JNIEnv*, jclass, jlong call, jlong received, jlong total) {
            auto* c = reinterpret_cast<net::HttpCall*>(static_cast<std::intptr_t>(call));
            return c->Progress(received, total) ? JNI_TRUE : JNI_FALSE;
        }
        // KilnHttp.networkChanged: on the main thread.
        void NetworkChanged(JNIEnv*, jclass, jboolean connected, jboolean validated, jboolean metered, jint transport) {
            net::NetworkState s;
            s.connected = connected;
            s.validated = validated;
            s.metered = metered;
            s.transport = static_cast<net::NetworkState::Transport>(transport);
            net::Connectivity().SetValue(s);
        }
    }

    void InstallHttp(JNIEnv* env) {
        jclass c = env->FindClass("dev/kiln/KilnHttp");
        if (!c) { env->ExceptionClear(); return; }
        static const JNINativeMethod methods[] = {
            { "progress", "(JJJ)Z", reinterpret_cast<void*>(&Progress) },
            { "networkChanged", "(ZZZI)V", reinterpret_cast<void*>(&NetworkChanged) },
        };
        env->RegisterNatives(c, methods, 2);
        net::SetHttpEngine(std::make_shared<AndroidHttpEngine>(env, c));
        env->DeleteLocalRef(c);
    }

}
