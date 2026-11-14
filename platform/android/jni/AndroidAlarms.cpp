// kiln::alarm's host on Android: dev.kiln.KilnAlarms (AlarmManager), alarms going off through
// dev.kiln.KilnAlarmReceiver (a process may start for one).
#include "AndroidServices.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/work/Alarm.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Alarms;
        jmethodID g_Schedule = nullptr, g_Cancel = nullptr, g_Pending = nullptr, g_CanExact = nullptr, g_Settings = nullptr;

        void Clear(JNIEnv* env) {
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
        void FireJni(JNIEnv* env, jclass, jbyteArray bytes) {
            try {
                jvm::detail::EnsureMainPump();   // no activity may drive the main dispatcher
                alarm::Fire(alarm::Decode(jvm::ToBytes(env, bytes)));
            } catch (const std::exception& e) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "alarm: %s", e.what());
            }
        }
    }

    void InstallAlarms(JNIEnv* env) {
        try {
            g_Alarms = jvm::FindClass("dev/kiln/KilnAlarms");
            jclass c = g_Alarms.get();
            g_Schedule = jvm::detail::StaticMethod(env, c, "schedule", "([BLjava/lang/String;JIJLjava/lang/String;)Z");
            g_Cancel = jvm::detail::StaticMethod(env, c, "cancel", "(Ljava/lang/String;)V");
            g_Pending = jvm::detail::StaticMethod(env, c, "pending", "()[[B");
            g_CanExact = jvm::detail::StaticMethod(env, c, "canScheduleExact", "()Z");
            g_Settings = jvm::detail::StaticMethod(env, c, "openExactSettings", "()V");
            static const JNINativeMethod natives[] = { { "fire", "([B)V", reinterpret_cast<void*>(&FireJni) } };
            jvm::detail::RegisterNatives(env, c, natives, 1);
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "alarm: no AlarmManager host: %s", e.what());
            return;
        }
        alarm::SetAlarmHost({
            .schedule = [](const alarm::Alarm& a) {
                JNIEnv* env = jvm::Env();
                std::vector<std::uint8_t> bytes = alarm::Encode(a);
                jvm::Local<jbyteArray> b = jvm::ToJava(env, std::span<const std::uint8_t>(bytes));
                jvm::Local<jstring> name = jvm::ToJava(env, a.name), show = jvm::ToJava(env, a.showUri);
                jboolean ok = env->CallStaticBooleanMethod(g_Alarms.get(), g_Schedule, b.get(), name.get(), jlong(a.triggerAtMillis), jint(a.kind),
                                                           jlong(a.windowLengthMillis), show.get());
                if (env->ExceptionCheck()) {
                    Clear(env);
                    return false;
                }
                return bool(ok);
            },
            .cancel = [](const std::string& name) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> n = jvm::ToJava(env, name);
                env->CallStaticVoidMethod(g_Alarms.get(), g_Cancel, n.get());
                Clear(env);
            },
            .pending = [] {
                JNIEnv* env = jvm::Env();
                std::vector<alarm::Alarm> out;
                jvm::Local<jobjectArray> all(static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Alarms.get(), g_Pending)), env);
                Clear(env);
                if (!all) return out;
                for (jsize i = 0, n = env->GetArrayLength(all.get()); i < n; ++i) {
                    jvm::Local<jbyteArray> b(static_cast<jbyteArray>(env->GetObjectArrayElement(all.get(), i)), env);
                    try {
                        out.push_back(alarm::Decode(jvm::ToBytes(env, b.get())));
                    } catch (const std::exception&) {
                    }
                }
                return out;
            },
            .canScheduleExact = [] {
                JNIEnv* env = jvm::Env();
                jboolean ok = env->CallStaticBooleanMethod(g_Alarms.get(), g_CanExact);
                Clear(env);
                return bool(ok);
            },
            .openExactSettings = [] {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Alarms.get(), g_Settings);
                Clear(env);
            },
        });
    }

}
