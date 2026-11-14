#include "kiln/jvm/Jvm.h"

#include <android/log.h>
#include <android/looper.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>
#include <cxxabi.h>
#include <map>
#include <typeinfo>
#include <vector>

namespace kiln::jvm {

    namespace {
        JavaVM* g_Vm = nullptr;
        jobject g_Loader = nullptr;           // global: the app's ClassLoader
        jmethodID g_LoadClass = nullptr;
        jclass g_CppException = nullptr;      // global
        jmethodID g_CppExceptionInit = nullptr;
        jmethodID g_CompletionResume = nullptr, g_CompletionFail = nullptr;
        jmethodID g_JobCancel = nullptr;      // kotlinx.coroutines.Job.cancel, found on first use
        jmethodID g_ClassGetName = nullptr, g_ThrowableMessage = nullptr;

        std::vector<void (*)(JNIEnv*)>& Registrations() {
            static std::vector<void (*)(JNIEnv*)> r;
            return r;
        }

        std::string Utf16ToUtf8(const jchar* s, std::size_t n) {
            std::string out;
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                std::uint32_t c = s[i];
                if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                    ++i;
                } else if (c >= 0xD800 && c < 0xE000) {
                    c = 0xFFFD;   // an unpaired surrogate, as String.getBytes(UTF_8) writes '?'
                }
                if (c < 0x80) out += char(c);
                else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
                else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
                else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
            }
            return out;
        }
        std::vector<jchar> Utf8ToUtf16(std::string_view s) {
            std::vector<jchar> out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size();) {
                auto b = std::uint8_t(s[i]);
                std::uint32_t c;
                int extra;
                if (b < 0x80) { c = b; extra = 0; }
                else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
                else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
                else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
                else { out.push_back(0xFFFD); ++i; continue; }
                bool ok = true;
                for (int k = 1; k <= extra; ++k) {
                    if (i + k >= s.size() || (std::uint8_t(s[i + k]) & 0xC0) != 0x80) { ok = false; break; }
                    c = (c << 6) | (std::uint8_t(s[i + k]) & 0x3F);
                }
                if (!ok) { out.push_back(0xFFFD); ++i; continue; }
                i += std::size_t(extra) + 1;
                if (c >= 0x10000) {
                    c -= 0x10000;
                    out.push_back(jchar(0xD800 + (c >> 10)));
                    out.push_back(jchar(0xDC00 + (c & 0x3FF)));
                } else {
                    out.push_back(jchar(c));
                }
            }
            return out;
        }

        std::string TypeName(const std::type_info& t) {
            int status = 0;
            char* name = abi::__cxa_demangle(t.name(), nullptr, nullptr, &status);
            std::string out = status == 0 && name ? name : t.name();
            std::free(name);
            return out;
        }

        // --- the main pump ---------------------------------------------------------------------
        struct Pump {
            int eventFd = -1, timerFd = -1;
            bool installed = false;
        };
        Pump g_Pump;
        rt::Dispatcher* g_Main = nullptr;

        void PumpWake() {
            std::uint64_t one = 1;
            [[maybe_unused]] ssize_t n = write(g_Pump.eventFd, &one, sizeof one);
        }
        void ArmPumpTimer() {
            itimerspec spec{};
            if (auto next = g_Main->NextTimer()) {
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(*next - g_Main->Now()).count();
                if (ns < 1) ns = 1;
                spec.it_value.tv_sec = ns / 1000000000;
                spec.it_value.tv_nsec = ns % 1000000000;
            }
            timerfd_settime(g_Pump.timerFd, 0, &spec, nullptr);
        }
        int OnPump(int fd, int, void*) {
            std::uint64_t value = 0;
            [[maybe_unused]] ssize_t n = read(fd, &value, sizeof value);
            if (g_Pump.installed) {
                g_Main->RunPending();
                ArmPumpTimer();
            }
            return 1;
        }
        // The pump's hooks on the dispatcher (the activity's host replaces them while it runs).
        void HookPump() {
            g_Pump.installed = true;
            g_Main->wake = [] { PumpWake(); };
            g_Main->SetRemoteWake([] { PumpWake(); });
            PumpWake();
        }

        // --- calls C++ awaits ------------------------------------------------------------------
        std::map<jlong, std::weak_ptr<detail::KotlinCallState>> g_Calls;   // under KotlinCall::Mutex
        jlong g_NextCall = 1;

        void Finish(jlong handle, std::optional<std::vector<std::uint8_t>> value, std::exception_ptr error) {
            std::shared_ptr<detail::KotlinCallState> s;
            {
                std::lock_guard lock(detail::KotlinCall::Mutex());
                auto it = g_Calls.find(handle);
                if (it == g_Calls.end()) return;
                s = it->second.lock();
                g_Calls.erase(it);
                if (!s) return;
                s->finished = true;
                s->job = {};
            }
            s->value = std::move(value);
            s->error = error;
            s->home->PostFromAnyThread([s] {
                if (s->abandoned) return;
                auto j = s->waiter.lock();
                if (j) {
                    if (j->cancelled) return;
                    j->unregister = nullptr;
                }
                rt::ResumeJob(j, s->h);
            });
        }
        void CompleteJni(JNIEnv* env, jclass, jlong handle, jbyteArray value) {
            std::optional<std::vector<std::uint8_t>> v;
            if (value) v = ToBytes(env, value);
            Finish(handle, std::move(v), nullptr);
        }
        void FailJni(JNIEnv* env, jclass, jlong handle, jthrowable error) {
            Finish(handle, std::nullopt, std::make_exception_ptr(ToException(env, error)));
        }

        // --- tasks Kotlin awaits ---------------------------------------------------------------
        std::map<jlong, rt::Job> g_Tasks;   // main thread
        jlong g_NextTask = 1;

        rt::Task<> RunTask(jlong handle, Global<jobject> completion, rt::Task<std::optional<std::vector<std::uint8_t>>> task) {
            std::optional<std::vector<std::uint8_t>> value;
            std::string failType, failMessage;
            bool failed = false;
            Global<jthrowable> javaError;
            try {
                value = co_await std::move(task);
            } catch (const JavaException& e) {
                failed = true;
                javaError = e.throwable;
                failType = e.className;
                failMessage = e.what();
            } catch (const std::exception& e) {
                failed = true;
                failType = TypeName(typeid(e));
                failMessage = e.what();
            } catch (...) {
                failed = true;
                failType = "unknown";
            }
            JNIEnv* env = Env();
            if (!failed) {
                Local<jbyteArray> bytes;
                if (value) bytes = ToJava(env, std::span<const std::uint8_t>(*value));
                env->CallVoidMethod(completion.get(), g_CompletionResume, bytes.get());
            } else {
                Local<jstring> type = ToJava(env, failType);
                Local<jstring> message = ToJava(env, failMessage);
                env->CallVoidMethod(completion.get(), g_CompletionFail, type.get(), message.get());
            }
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            g_Main->Post([handle] { g_Tasks.erase(handle); });
        }
        void CancelJni(JNIEnv*, jclass, jlong handle) {
            if (g_Main) g_Main->PostFromAnyThread([handle] { g_Tasks.erase(handle); });
        }
    }

    JavaVM* Vm() { return g_Vm; }

    JNIEnv* Env() {
        JNIEnv* env = nullptr;
        if (g_Vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
        if (g_Vm->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) return nullptr;
        struct Detach {
            ~Detach() { g_Vm->DetachCurrentThread(); }
        };
        thread_local Detach detach;
        return env;
    }

    JavaException ToException(JNIEnv* env, jthrowable t) {
        Local<jclass> cls(env->GetObjectClass(t), env);
        Local<jstring> name(static_cast<jstring>(env->CallObjectMethod(cls.get(), g_ClassGetName)), env);
        Local<jstring> message(static_cast<jstring>(env->CallObjectMethod(t, g_ThrowableMessage)), env);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JavaException(ToString(env, name.get()), ToString(env, message.get()), Global<jthrowable>(t, env));
    }

    void Check(JNIEnv* env) {
        if (!env->ExceptionCheck()) return;
        Local<jthrowable> t(env->ExceptionOccurred(), env);
        env->ExceptionClear();
        throw ToException(env, t.get());
    }

    void ThrowCurrent(JNIEnv* env) {
        try {
            throw;
        } catch (const JavaException& e) {
            env->Throw(e.throwable.get());
        } catch (const std::exception& e) {
            Local<jstring> type = ToJava(env, TypeName(typeid(e)));
            Local<jstring> message = ToJava(env, e.what());
            Local<jobject> ex(env->NewObject(g_CppException, g_CppExceptionInit, type.get(), message.get()), env);
            if (ex) env->Throw(static_cast<jthrowable>(ex.get()));
        } catch (...) {
            Local<jstring> type = ToJava(env, "unknown");
            Local<jobject> ex(env->NewObject(g_CppException, g_CppExceptionInit, type.get(), nullptr), env);
            if (ex) env->Throw(static_cast<jthrowable>(ex.get()));
        }
    }

    Global<jclass> FindClass(const char* name) {
        JNIEnv* env = Env();
        std::string dotted = name;
        for (char& c : dotted) if (c == '/') c = '.';
        Local<jstring> s = ToJava(env, dotted);
        Local<jclass> c(static_cast<jclass>(env->CallObjectMethod(g_Loader, g_LoadClass, s.get())), env);
        Check(env);
        return Global<jclass>(c.get(), env);
    }

    std::string ToString(JNIEnv* env, jstring s) {
        if (!s) return {};
        jsize n = env->GetStringLength(s);
        std::vector<jchar> chars(static_cast<std::size_t>(n));
        env->GetStringRegion(s, 0, n, chars.data());
        return Utf16ToUtf8(chars.data(), chars.size());
    }
    Local<jstring> ToJava(JNIEnv* env, std::string_view utf8) {
        std::vector<jchar> chars = Utf8ToUtf16(utf8);
        return Local<jstring>(env->NewString(chars.data(), jsize(chars.size())), env);
    }
    std::vector<std::uint8_t> ToBytes(JNIEnv* env, jbyteArray a) {
        if (!a) return {};
        std::vector<std::uint8_t> out(std::size_t(env->GetArrayLength(a)));
        env->GetByteArrayRegion(a, 0, jsize(out.size()), reinterpret_cast<jbyte*>(out.data()));
        return out;
    }
    Local<jbyteArray> ToJava(JNIEnv* env, std::span<const std::uint8_t> bytes) {
        jbyteArray a = env->NewByteArray(jsize(bytes.size()));
        if (a) env->SetByteArrayRegion(a, 0, jsize(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
        return Local<jbyteArray>(a, env);
    }

    namespace detail {
        void EnsureMainPump() {
            if (g_Main) return;
            g_Main = &rt::Dispatcher::Main();
            g_Pump.eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            g_Pump.timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
            ALooper* looper = ALooper_forThread();
            ALooper_addFd(looper, g_Pump.eventFd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, &OnPump, nullptr);
            ALooper_addFd(looper, g_Pump.timerFd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, &OnPump, nullptr);
            // An activity already drives the dispatcher when it has a remote wake; its host
            // calls HostDetached when it stops.
            if (!g_Main->wake) HookPump();
        }
        rt::Dispatcher* MainDispatcher() { return g_Main; }

        jlong StartTask(JNIEnv* env, jobject completion, rt::Task<std::optional<std::vector<std::uint8_t>>> task) {
            EnsureMainPump();
            jlong handle = g_NextTask++;
            g_Tasks.emplace(handle, rt::Launch(*g_Main, RunTask(handle, Global<jobject>(completion, env), std::move(task))));
            return handle;
        }

        std::mutex& KotlinCall::Mutex() {
            static std::mutex m;
            return m;
        }
        void KotlinCall::Fail(jlong handle, std::exception_ptr error) { Finish(handle, std::nullopt, error); }

        jlong Register(std::shared_ptr<KotlinCallState> state) {
            std::lock_guard lock(KotlinCall::Mutex());
            jlong handle = g_NextCall++;
            g_Calls.emplace(handle, state);
            return handle;
        }
        void CancelKotlin(const std::shared_ptr<KotlinCallState>& state, jlong handle) {
            state->abandoned = true;
            Global<jobject> job;
            {
                std::lock_guard lock(KotlinCall::Mutex());
                g_Calls.erase(handle);
                job = std::move(state->job);
                state->finished = true;
            }
            if (!job) return;
            JNIEnv* env = Env();
            if (!g_JobCancel) {
                // Job.cancel(CancellationException?), through the interface (the app's class
                // loader: JNI's FindClass sees only the system's on a looper callback).
                try {
                    Global<jclass> iface = FindClass("kotlinx/coroutines/Job");
                    g_JobCancel = detail::Method(env, iface.get(), "cancel", "(Ljava/util/concurrent/CancellationException;)V");
                } catch (const JavaException& e) {
                    __android_log_print(ANDROID_LOG_ERROR, "kiln", "JVM bridge: cannot cancel a Kotlin call: %s", e.what());
                    return;
                }
            }
            env->CallVoidMethod(job.get(), g_JobCancel, nullptr);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        Registration::Registration(void (*fn)(JNIEnv*)) { Registrations().push_back(fn); }

        jmethodID Method(JNIEnv* env, jclass c, const char* name, const char* sig) {
            jmethodID m = env->GetMethodID(c, name, sig);
            Check(env);
            return m;
        }
        jmethodID StaticMethod(JNIEnv* env, jclass c, const char* name, const char* sig) {
            jmethodID m = env->GetStaticMethodID(c, name, sig);
            Check(env);
            return m;
        }
        void RegisterNatives(JNIEnv* env, jclass c, const JNINativeMethod* methods, int count) {
            env->RegisterNatives(c, methods, count);
            Check(env);
        }
    }

    void Install(JavaVM* vm, JNIEnv* env) {
        g_Vm = vm;
        Local<jclass> classClass(env->FindClass("java/lang/Class"), env);
        g_ClassGetName = env->GetMethodID(classClass.get(), "getName", "()Ljava/lang/String;");
        Local<jclass> throwable(env->FindClass("java/lang/Throwable"), env);
        g_ThrowableMessage = env->GetMethodID(throwable.get(), "getMessage", "()Ljava/lang/String;");

        // The class loader that loaded kiln's own classes is the app's.
        Local<jclass> bridge(env->FindClass("dev/kiln/bridge/KilnBridge"), env);
        if (!bridge) {
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_WARN, "kiln", "dev.kiln.bridge.KilnBridge is missing: no JVM bridge");
            return;
        }
        jmethodID getLoader = env->GetMethodID(classClass.get(), "getClassLoader", "()Ljava/lang/ClassLoader;");
        Local<jobject> loader(env->CallObjectMethod(bridge.get(), getLoader), env);
        g_Loader = env->NewGlobalRef(loader.get());
        Local<jclass> loaderClass(env->FindClass("java/lang/ClassLoader"), env);
        g_LoadClass = env->GetMethodID(loaderClass.get(), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

        Local<jclass> cppException(env->FindClass("dev/kiln/bridge/CppException"), env);
        g_CppException = static_cast<jclass>(env->NewGlobalRef(cppException.get()));
        g_CppExceptionInit = env->GetMethodID(g_CppException, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
        Local<jclass> completion(env->FindClass("dev/kiln/bridge/Completion"), env);
        g_CompletionResume = env->GetMethodID(completion.get(), "resume", "([B)V");
        g_CompletionFail = env->GetMethodID(completion.get(), "fail", "(Ljava/lang/String;Ljava/lang/String;)V");

        static const JNINativeMethod natives[] = {
            { "complete", "(J[B)V", reinterpret_cast<void*>(&CompleteJni) },
            { "fail", "(JLjava/lang/Throwable;)V", reinterpret_cast<void*>(&FailJni) },
            { "cancel", "(J)V", reinterpret_cast<void*>(&CancelJni) },
        };
        env->RegisterNatives(bridge.get(), natives, 3);
        if (env->ExceptionCheck()) env->ExceptionClear();

        for (auto fn : Registrations()) {
            try {
                fn(env);
            } catch (const std::exception& e) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "JVM bridge registration failed: %s", e.what());
            }
        }
    }

    void HostDetached() {
        if (g_Main && g_Pump.eventFd >= 0) HookPump();
    }

}
