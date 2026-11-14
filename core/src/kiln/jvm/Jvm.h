#pragma once

// Calling the app's JVM code from C++ and being called by it (Android). tools/gen-jvm writes the
// typed calls from annotated Kotlin interfaces (docs/android.md "Calling Kotlin and Java"); this
// is what they are made of, and what hand-written JNI uses:
//
//   JNIEnv* env = jvm::Env();                         // any thread: attaches it if needed
//   jvm::Local<jstring> s = jvm::ToJava(env, "hi");   // deleted when it goes out of scope
//   jvm::Global<jclass> c = jvm::FindClass("com/example/Notes");   // the app's classes too
//   env->CallStaticVoidMethod(c.get(), m, s.get());
//   jvm::Check(env);                                  // a pending Java exception -> JavaException
//
// A thread C++ attached is detached when it exits.

#include "kiln/jvm/Codec.h"
#include "kiln/runtime/Task.h"

#include <jni.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln::jvm {

    JavaVM* Vm();
    // The calling thread's JNIEnv, attaching the thread (as a daemon) when it is not a VM thread.
    JNIEnv* Env();

    // A local reference, deleted when the Local goes. Move-only.
    template<typename T = jobject>
    class Local {
    public:
        Local() = default;
        explicit Local(T ref, JNIEnv* env = nullptr) : m_Ref(ref), m_Env(env ? env : (ref ? Env() : nullptr)) {}
        Local(Local&& o) noexcept : m_Ref(std::exchange(o.m_Ref, nullptr)), m_Env(o.m_Env) {}
        Local& operator=(Local&& o) noexcept { if (this != &o) { Reset(); m_Ref = std::exchange(o.m_Ref, nullptr); m_Env = o.m_Env; } return *this; }
        ~Local() { Reset(); }
        T get() const { return m_Ref; }
        T release() { return std::exchange(m_Ref, nullptr); }
        explicit operator bool() const { return m_Ref != nullptr; }

    private:
        void Reset() { if (m_Ref) m_Env->DeleteLocalRef(m_Ref); m_Ref = nullptr; }
        T m_Ref = nullptr;
        JNIEnv* m_Env = nullptr;
    };

    // A global reference: valid on any thread until the last copy goes.
    template<typename T = jobject>
    class Global {
    public:
        Global() = default;
        explicit Global(T ref, JNIEnv* env = nullptr) {
            if (ref) m_Ref = static_cast<T>((env ? env : Env())->NewGlobalRef(ref));
        }
        Global(const Global& o) : Global(o.m_Ref) {}
        Global(Global&& o) noexcept : m_Ref(std::exchange(o.m_Ref, nullptr)) {}
        Global& operator=(Global o) noexcept { std::swap(m_Ref, o.m_Ref); return *this; }
        ~Global() { if (m_Ref) Env()->DeleteGlobalRef(m_Ref); }
        T get() const { return m_Ref; }
        explicit operator bool() const { return m_Ref != nullptr; }

    private:
        T m_Ref = nullptr;
    };

    // A Java exception that reached C++. `throwable` is the exception itself: Throw(env) on the
    // way back to Java rethrows the same object.
    class JavaException : public std::runtime_error {
    public:
        JavaException(std::string className, const std::string& message, Global<jthrowable> throwable)
            : std::runtime_error(message.empty() ? className : className + ": " + message),
              className(std::move(className)), throwable(std::move(throwable)) {}
        std::string className;   // java.io.IOException
        Global<jthrowable> throwable;
    };

    // Throws the pending Java exception, if any, as a JavaException (and clears it).
    void Check(JNIEnv* env);
    // The JavaException for a throwable.
    JavaException ToException(JNIEnv* env, jthrowable throwable);
    // Inside a catch block of a native method: makes the C++ exception being handled pending in
    // Java. A JavaException rethrows its throwable; anything else becomes a
    // dev.kiln.bridge.CppException with its type and message.
    void ThrowCurrent(JNIEnv* env);

    // A class by its binary name with slashes ("com/example/App$Inner"), found with the app's
    // class loader, so it works from threads C++ attached (where JNI's FindClass sees only the
    // system's classes). Throws JavaException when it does not exist.
    Global<jclass> FindClass(const char* name);

    // Strings are converted through UTF-16, so every character survives (JNI's "UTF" functions
    // use modified UTF-8).
    std::string ToString(JNIEnv* env, jstring s);
    Local<jstring> ToJava(JNIEnv* env, std::string_view utf8);
    std::vector<std::uint8_t> ToBytes(JNIEnv* env, jbyteArray a);
    Local<jbyteArray> ToJava(JNIEnv* env, std::span<const std::uint8_t> bytes);

    // The code tools/gen-jvm writes calls these.
    namespace detail {
        // Kiln's main thread drives rt::Dispatcher::Main() even without an activity (a Service
        // calling C++). Main thread only.
        void EnsureMainPump();
        rt::Dispatcher* MainDispatcher();

        // Kotlin awaits a C++ task: launched now on the main dispatcher (called on the main
        // thread); `completion` (dev.kiln.bridge.Completion) gets its encoded result or failure.
        // Returns the handle Kotlin cancels it by.
        jlong StartTask(JNIEnv* env, jobject completion, rt::Task<std::optional<std::vector<std::uint8_t>>> task);

        template<typename T>
        rt::Task<std::optional<std::vector<std::uint8_t>>> Encoded(rt::Task<T> task) {
            if constexpr (std::is_void_v<T>) {
                co_await std::move(task);
                co_return std::nullopt;
            } else {
                co_return Encode(co_await std::move(task));
            }
        }

        // C++ awaits a Kotlin suspend call. `start` calls the generated Kotlin adapter with the
        // handle and returns the kotlinx.coroutines.Job it launched; KilnBridge.complete or fail
        // reports the result (bytes, or null for Unit). Cancelling the waiting task cancels the Job.
        struct KotlinCallState {
            std::optional<std::vector<std::uint8_t>> value;
            std::exception_ptr error;
            Global<jobject> job;
            std::atomic<bool> abandoned{ false };
            std::atomic<bool> finished{ false };
            rt::Dispatcher* home = nullptr;
            std::weak_ptr<rt::JobState> waiter;
            std::coroutine_handle<> h;
        };
        jlong Register(std::shared_ptr<KotlinCallState> state);
        void CancelKotlin(const std::shared_ptr<KotlinCallState>& state, jlong handle);

        struct KotlinCall {
            kiln::Function<jobject(JNIEnv*, jlong)> start;
            std::shared_ptr<KotlinCallState> state = std::make_shared<KotlinCallState>();

            bool await_ready() const noexcept { return false; }
            template<typename P>
            void await_suspend(std::coroutine_handle<P> h) {
                std::shared_ptr<rt::JobState> job = h.promise().job;
                state->home = job && job->dispatcher ? job->dispatcher : &rt::Dispatcher::Main();
                state->waiter = job;
                state->h = h;
                jlong handle = Register(state);
                if (job) job->unregister = [s = state, handle] { CancelKotlin(s, handle); };
                JNIEnv* env = Env();
                Local<jobject> launched(start(env, handle), env);
                if (env->ExceptionCheck()) {
                    // The adapter itself failed: report it like a failure of the call.
                    jthrowable t = env->ExceptionOccurred();
                    env->ExceptionClear();
                    Fail(handle, std::make_exception_ptr(ToException(env, t)));
                    env->DeleteLocalRef(t);
                    return;
                }
                std::lock_guard lock(Mutex());
                if (!state->finished) state->job = Global<jobject>(launched.get(), env);
            }
            std::vector<std::uint8_t> await_resume() {
                if (state->error) std::rethrow_exception(state->error);
                return state->value ? std::move(*state->value) : std::vector<std::uint8_t>{};
            }

            static std::mutex& Mutex();
            static void Fail(jlong handle, std::exception_ptr error);
        };

        // Native methods are registered when kiln's library loads (JNI_OnLoad). Generated code
        // adds its registration with a static Registration.
        struct Registration {
            explicit Registration(void (*fn)(JNIEnv*));
        };

        // A method or field ID, or JavaException.
        jmethodID Method(JNIEnv* env, jclass c, const char* name, const char* sig);
        jmethodID StaticMethod(JNIEnv* env, jclass c, const char* name, const char* sig);
        void RegisterNatives(JNIEnv* env, jclass c, const JNINativeMethod* methods, int count);
    }

    // kiln's JNI_OnLoad: records the VM and the app's class loader, registers the bridge.
    void Install(JavaVM* vm, JNIEnv* env);
    // AndroidMain: the activity's host stopped driving the main dispatcher.
    void HostDetached();

}
