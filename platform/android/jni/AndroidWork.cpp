// kiln::work's host on Android: JobScheduler through dev.kiln.KilnWork, jobs run by
// dev.kiln.KilnJobService (installed as the library loads, so a process started for a job has it).
#include "AndroidWork.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/work/Work.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Work;
        jmethodID g_Schedule = nullptr, g_Cancel = nullptr, g_Pending = nullptr, g_Finished = nullptr;

        void StartJobJni(JNIEnv* env, jclass, jint jobId, jbyteArray bytes) {
            try {
                jvm::detail::EnsureMainPump();   // no activity may drive the main dispatcher
                work::StartJob(work::Decode(jvm::ToBytes(env, bytes)));
            } catch (const std::exception& e) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "work: job %d: %s", int(jobId), e.what());
                env->CallStaticVoidMethod(g_Work.get(), g_Finished, jobId, JNI_FALSE);
            }
        }
        jboolean StopJobJni(JNIEnv*, jclass, jint jobId) { return work::StopJob(jobId) ? JNI_TRUE : JNI_FALSE; }

        void Clear(JNIEnv* env) {
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
    }

    void InstallWork(JNIEnv* env) {
        try {
            g_Work = jvm::FindClass("dev/kiln/KilnWork");
            jclass c = g_Work.get();
            g_Schedule = jvm::detail::StaticMethod(env, c, "schedule", "(I[BJIZZZZZJZZ)Z");
            g_Cancel = jvm::detail::StaticMethod(env, c, "cancel", "(I)V");
            g_Pending = jvm::detail::StaticMethod(env, c, "pending", "()[[B");
            g_Finished = jvm::detail::StaticMethod(env, c, "finished", "(IZ)V");
            static const JNINativeMethod natives[] = {
                { "startJob", "(I[B)V", reinterpret_cast<void*>(&StartJobJni) },
                { "stopJob", "(I)Z", reinterpret_cast<void*>(&StopJobJni) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 2);
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "work: no JobScheduler host: %s", e.what());
            return;
        }
        work::WorkHost h;
        h.schedule = [](const work::WorkSpec& s, std::int64_t delay) {
            JNIEnv* env = jvm::Env();
            std::vector<std::uint8_t> bytes = work::Encode(s);
            jvm::Local<jbyteArray> a = jvm::ToJava(env, std::span<const std::uint8_t>(bytes));
            const work::WorkRequest& r = s.request;
            const work::Constraints& k = r.constraints;
            jboolean ok = env->CallStaticBooleanMethod(g_Work.get(), g_Schedule, jint(work::JobId(s.name)), a.get(), jlong(delay),
                                                       jint(k.requiredNetwork), jboolean(k.requiresCharging), jboolean(k.requiresDeviceIdle),
                                                       jboolean(k.requiresBatteryNotLow), jboolean(k.requiresStorageNotLow),
                                                       jboolean(r.backoffPolicy == work::BackoffPolicy::Linear), jlong(r.backoffDelayMillis),
                                                       jboolean(r.expedited), jboolean(s.runAttemptCount > 0));
            Clear(env);
            if (!ok) __android_log_print(ANDROID_LOG_ERROR, "kiln", "work: %s was not scheduled", s.name.c_str());
        };
        h.cancel = [](int id) {
            JNIEnv* env = jvm::Env();
            env->CallStaticVoidMethod(g_Work.get(), g_Cancel, jint(id));
            Clear(env);
        };
        h.pending = [] {
            JNIEnv* env = jvm::Env();
            std::vector<work::WorkSpec> out;
            jvm::Local<jobjectArray> all(static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Work.get(), g_Pending)), env);
            Clear(env);
            if (!all) return out;
            for (jsize i = 0, n = env->GetArrayLength(all.get()); i < n; ++i) {
                jvm::Local<jbyteArray> b(static_cast<jbyteArray>(env->GetObjectArrayElement(all.get(), i)), env);
                try {
                    out.push_back(work::Decode(jvm::ToBytes(env, b.get())));
                } catch (const std::exception&) {
                    // A job of an older format: not one this build understands.
                }
            }
            return out;
        };
        h.finished = [](int id, bool reschedule) {
            JNIEnv* env = jvm::Env();
            env->CallStaticVoidMethod(g_Work.get(), g_Finished, jint(id), jboolean(reschedule));
            Clear(env);
        };
        work::SetWorkHost(std::move(h));
    }

}
