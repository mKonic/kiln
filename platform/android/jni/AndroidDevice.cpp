#include "AndroidDevice.h"

#include "kiln/ui/Biometric.h"
#include "kiln/ui/Location.h"
#include "kiln/ui/Notifications.h"
#include "kiln/ui/Sensors.h"
#include "kiln/ui/Vibration.h"

#include <android/log.h>
#include <android/looper.h>
#include <android/sensor.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

namespace kiln::android {

    namespace {
        JavaVM* g_Vm = nullptr;
        jclass g_Device = nullptr;
        kiln::Function<void()> g_RequestFrame;
        int g_NextId = 1;
        // Main thread only.
        std::map<int, kiln::Function<void(std::optional<ui::Location>)>> g_Current;
        std::map<int, kiln::Function<void(const ui::Location&)>> g_Updates;
        std::map<int, kiln::Function<void(ui::BiometricResult)>> g_Prompts;

        JNIEnv* Env() {
            JNIEnv* env = nullptr;
            g_Vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            return env;
        }
        // A missing method (a kiln build mismatch) is logged, not left as a pending exception.
        jmethodID Static(JNIEnv* env, const char* name, const char* sig) {
            jmethodID m = env->GetStaticMethodID(g_Device, name, sig);
            if (!m) {
                env->ExceptionClear();
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "KilnDevice.%s%s is missing", name, sig);
            }
            return m;
        }
        void Clear(JNIEnv* env) { if (env->ExceptionCheck()) env->ExceptionClear(); }
        std::string Str(JNIEnv* env, jstring s) {
            if (!s) return {};
            const char* c = env->GetStringUTFChars(s, nullptr);
            std::string out = c ? c : "";
            if (c) env->ReleaseStringUTFChars(s, c);
            return out;
        }
        ui::Location Unpack(JNIEnv* env, jdoubleArray values, jstring provider) {
            ui::Location l;
            double v[7] = {};
            env->GetDoubleArrayRegion(values, 0, 7, v);
            l.latitude = v[0];
            l.longitude = v[1];
            if (!std::isnan(v[2])) l.altitude = v[2];
            if (!std::isnan(v[3])) l.accuracy = float(v[3]);
            if (!std::isnan(v[4])) l.speed = float(v[4]);
            if (!std::isnan(v[5])) l.bearing = float(v[5]);
            l.timeMillis = std::int64_t(v[6]);
            l.provider = Str(env, provider);
            return l;
        }

        void LocationResult(JNIEnv* env, jclass, jint id, jboolean hasFix, jdoubleArray values, jstring provider, jboolean done) {
            std::optional<ui::Location> fix;
            if (hasFix && values) fix = Unpack(env, values, provider);
            if (done) {
                auto it = g_Current.find(id);
                if (it == g_Current.end()) return;
                auto fn = std::move(it->second);
                g_Current.erase(it);
                fn(fix);
            } else if (fix) {
                if (auto it = g_Updates.find(id); it != g_Updates.end()) { auto fn = it->second; fn(*fix); }
            }
            if (g_RequestFrame) g_RequestFrame();
        }
        void BiometricResult(JNIEnv* env, jclass, jint id, jboolean ok, jint code, jstring message, jint type) {
            auto it = g_Prompts.find(id);
            if (it == g_Prompts.end()) return;
            auto fn = std::move(it->second);
            g_Prompts.erase(it);
            fn(ui::BiometricResult{ bool(ok), int(code), Str(env, message), int(type) });
            if (g_RequestFrame) g_RequestFrame();
        }

        // --- sensors (NDK) ----------------------------------------------------------------------

        struct SensorSub {
            ASensorEventQueue* queue = nullptr;
            const ASensor* sensor = nullptr;
            ui::SensorType type;
            kiln::Function<void(const ui::SensorEvent&)> listener;
        };
        ASensorManager* g_Sensors = nullptr;
        ALooper* g_Looper = nullptr;
        std::map<int, std::unique_ptr<SensorSub>> g_SensorSubs;

        int OnSensorEvents(int, int, void* data) {
            const int id = int(reinterpret_cast<std::intptr_t>(data));
            auto it = g_SensorSubs.find(id);
            if (it == g_SensorSubs.end()) return 0;   // unregistered: stop calling
            SensorSub& s = *it->second;
            ASensorEvent events[16];
            ssize_t n;
            bool any = false;
            while ((n = ASensorEventQueue_getEvents(s.queue, events, 16)) > 0) {
                for (ssize_t i = 0; i < n; ++i) {
                    ui::SensorEvent e;
                    e.type = s.type;
                    e.timestampNanos = events[i].timestamp;
                    for (int k = 0; k < 16; ++k) e.values[std::size_t(k)] = events[i].data[k];
                    // The values that type reports (SensorEvent.values' length).
                    switch (s.type) {
                        case ui::SensorType::Light: case ui::SensorType::Pressure: case ui::SensorType::Proximity:
                        case ui::SensorType::RelativeHumidity: case ui::SensorType::AmbientTemperature:
                        case ui::SensorType::StepDetector: case ui::SensorType::HeartRate: e.valueCount = 1; break;
                        case ui::SensorType::StepCounter: e.valueCount = 1; e.values[0] = float(events[i].u64.step_counter); break;
                        case ui::SensorType::RotationVector: case ui::SensorType::GeomagneticRotationVector: e.valueCount = 5; break;
                        case ui::SensorType::GameRotationVector: e.valueCount = 4; break;
                        default: e.valueCount = 3; break;
                    }
                    e.accuracy = events[i].vector.status;
                    s.listener(e);
                    any = true;
                    // The listener may have unsubscribed itself.
                    if (!g_SensorSubs.count(id)) return 0;
                }
            }
            if (any && g_RequestFrame) g_RequestFrame();
            return 1;
        }

        std::string PackageName() {
            char buf[256] = {};
            if (FILE* f = std::fopen("/proc/self/cmdline", "rb")) { std::fread(buf, 1, sizeof buf - 1, f); std::fclose(f); }
            return buf;
        }
    }

    void InstallDevice(JNIEnv* env, kiln::Function<void()> requestFrame) {
        env->GetJavaVM(&g_Vm);
        g_RequestFrame = std::move(requestFrame);
        jclass c = env->FindClass("dev/kiln/KilnDevice");
        if (!c) { env->ExceptionClear(); return; }
        g_Device = static_cast<jclass>(env->NewGlobalRef(c));
        env->DeleteLocalRef(c);
        static const JNINativeMethod natives[] = {
            { "locationResult", "(IZ[DLjava/lang/String;Z)V", reinterpret_cast<void*>(&LocationResult) },
            { "biometricResult", "(IZILjava/lang/String;I)V", reinterpret_cast<void*>(&BiometricResult) },
        };
        env->RegisterNatives(g_Device, natives, 2);

        static jmethodID check = Static(env, "checkSelfPermission", "(Ljava/lang/String;)Z");
        ui::SetPermissionCheck([](const std::string& permission) {
            JNIEnv* env = Env();
            jstring p = env->NewStringUTF(permission.c_str());
            const bool granted = env->CallStaticBooleanMethod(g_Device, check, p);
            Clear(env);
            env->DeleteLocalRef(p);
            return granted;
        });

        // Vibration.
        static jmethodID vibrate = Static(env, "vibrate", "(I[J[III[I[F[I)Z");
        static jmethodID cancelVibration = Static(env, "cancelVibration", "()V");
        static jmethodID hasVibrator = Static(env, "hasVibrator", "()Z");
        static jmethodID hasAmplitude = Static(env, "hasAmplitudeControl", "()Z");
        ui::SetVibrationHost({
            .vibrate = [](const ui::VibrationEffect& e) -> bool {
                JNIEnv* env = Env();
                jlongArray timings = env->NewLongArray(jsize(e.timings.size()));
                env->SetLongArrayRegion(timings, 0, jsize(e.timings.size()), reinterpret_cast<const jlong*>(e.timings.data()));
                jintArray amps = env->NewIntArray(jsize(e.amplitudes.size()));
                env->SetIntArrayRegion(amps, 0, jsize(e.amplitudes.size()), reinterpret_cast<const jint*>(e.amplitudes.data()));
                const jsize np = jsize(e.primitives.size());
                std::vector<jint> prims, delays;
                std::vector<jfloat> scales;
                for (const auto& p : e.primitives) { prims.push_back(p.primitive); scales.push_back(p.scale); delays.push_back(p.delayMs); }
                jintArray jp = env->NewIntArray(np), jd = env->NewIntArray(np);
                jfloatArray js = env->NewFloatArray(np);
                env->SetIntArrayRegion(jp, 0, np, prims.data());
                env->SetIntArrayRegion(jd, 0, np, delays.data());
                env->SetFloatArrayRegion(js, 0, np, scales.data());
                const bool ok = env->CallStaticBooleanMethod(g_Device, vibrate, jint(e.kind), timings, amps, jint(e.repeat), jint(e.effect), jp, js, jd);
                Clear(env);
                for (jobject o : { (jobject)timings, (jobject)amps, (jobject)jp, (jobject)jd, (jobject)js }) env->DeleteLocalRef(o);
                return ok;
            },
            .cancel = [] { JNIEnv* env = Env(); env->CallStaticVoidMethod(g_Device, cancelVibration); Clear(env); },
            .hasVibrator = [] { JNIEnv* env = Env(); const bool r = env->CallStaticBooleanMethod(g_Device, hasVibrator); Clear(env); return r; },
            .hasAmplitudeControl = [] { JNIEnv* env = Env(); const bool r = env->CallStaticBooleanMethod(g_Device, hasAmplitude); Clear(env); return r; },
        });

        // Location.
        static jmethodID lastKnown = Static(env, "lastKnown", "()[Ljava/lang/Object;");
        static jmethodID current = Static(env, "current", "(ILjava/lang/String;)Z");
        static jmethodID updates = Static(env, "updates", "(ILjava/lang/String;JF)Z");
        static jmethodID cancel = Static(env, "cancel", "(I)V");
        static jmethodID enabled = Static(env, "locationEnabled", "()Z");
        ui::SetLocationHost({
            .lastKnown = []() -> std::optional<ui::Location> {
                JNIEnv* env = Env();
                auto r = static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Device, lastKnown));
                Clear(env);
                if (!r) return std::nullopt;
                auto values = static_cast<jdoubleArray>(env->GetObjectArrayElement(r, 0));
                auto provider = static_cast<jstring>(env->GetObjectArrayElement(r, 1));
                ui::Location l = Unpack(env, values, provider);
                for (jobject o : { (jobject)values, (jobject)provider, (jobject)r }) env->DeleteLocalRef(o);
                return l;
            },
            .current = [](const std::string& provider, kiln::Function<void(std::optional<ui::Location>)> done) -> int {
                JNIEnv* env = Env();
                const int id = g_NextId++;
                g_Current[id] = std::move(done);
                jstring p = env->NewStringUTF(provider.c_str());
                const bool ok = env->CallStaticBooleanMethod(g_Device, current, jint(id), p);
                Clear(env);
                env->DeleteLocalRef(p);
                if (!ok) { g_Current.erase(id); return -1; }
                return id;
            },
            .updates = [](const std::string& provider, std::int64_t interval, float minDistance, kiln::Function<void(const ui::Location&)> fix) -> int {
                JNIEnv* env = Env();
                const int id = g_NextId++;
                g_Updates[id] = std::move(fix);
                jstring p = env->NewStringUTF(provider.c_str());
                const bool ok = env->CallStaticBooleanMethod(g_Device, updates, jint(id), p, jlong(interval), jfloat(minDistance));
                Clear(env);
                env->DeleteLocalRef(p);
                if (!ok) { g_Updates.erase(id); return -1; }
                return id;
            },
            .cancel = [](int id) {
                g_Current.erase(id);
                g_Updates.erase(id);
                g_Prompts.erase(id);
                JNIEnv* env = Env();
                env->CallStaticVoidMethod(g_Device, cancel, jint(id));
                Clear(env);
            },
            .enabled = [] { JNIEnv* env = Env(); const bool r = env->CallStaticBooleanMethod(g_Device, enabled); Clear(env); return r; },
        });

        // Biometric prompt.
        static jmethodID canAuthenticate = Static(env, "canAuthenticate", "(I)I");
        static jmethodID authenticate = Static(env, "authenticate", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IZ)Z");
        ui::SetBiometricHost({
            .canAuthenticate = [](int a) { JNIEnv* env = Env(); const int r = env->CallStaticIntMethod(g_Device, canAuthenticate, jint(a)); Clear(env); return r; },
            .authenticate = [](const ui::BiometricPromptInfo& info, kiln::Function<void(ui::BiometricResult)> done) -> int {
                JNIEnv* env = Env();
                const int id = g_NextId++;
                g_Prompts[id] = std::move(done);
                jstring t = env->NewStringUTF(info.title.c_str()), s = env->NewStringUTF(info.subtitle.c_str()),
                        d = env->NewStringUTF(info.description.c_str()), n = env->NewStringUTF(info.negativeButtonText.c_str());
                const bool ok = env->CallStaticBooleanMethod(g_Device, authenticate, jint(id), t, s, d, n, jint(info.allowedAuthenticators), jboolean(info.confirmationRequired));
                Clear(env);
                for (jobject o : { (jobject)t, (jobject)s, (jobject)d, (jobject)n }) env->DeleteLocalRef(o);
                if (!ok) { g_Prompts.erase(id); return -1; }
                return id;
            },
            .cancel = [](int id) {
                g_Prompts.erase(id);
                JNIEnv* env = Env();
                env->CallStaticVoidMethod(g_Device, cancel, jint(id));
                Clear(env);
            },
        });

        // Notifications.
        if (jclass nc = env->FindClass("dev/kiln/KilnNotifications")) {
            static jclass notes = static_cast<jclass>(env->NewGlobalRef(nc));
            env->DeleteLocalRef(nc);
            auto get = [env](const char* name, const char* sig) {
                jmethodID m = env->GetStaticMethodID(notes, name, sig);
                if (!m) env->ExceptionClear();
                return m;
            };
            static jmethodID createChannel = get("createChannel", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Z");
            static jmethodID deleteChannel = get("deleteChannel", "(Ljava/lang/String;)V");
            static jmethodID notify = get("notify", "([Ljava/lang/String;[IJ[Ljava/lang/String;)Z");
            static jmethodID cancelOne = get("cancel", "(ILjava/lang/String;)V");
            static jmethodID cancelAll = get("cancelAll", "()V");
            static jmethodID enabledFn = get("enabled", "()Z");
            auto strings = [](JNIEnv* e, const std::vector<std::string>& v) {
                jclass sc = e->FindClass("java/lang/String");
                jobjectArray a = e->NewObjectArray(jsize(v.size()), sc, nullptr);
                for (std::size_t i = 0; i < v.size(); ++i) {
                    jstring js = e->NewStringUTF(v[i].c_str());
                    e->SetObjectArrayElement(a, jsize(i), js);
                    e->DeleteLocalRef(js);
                }
                e->DeleteLocalRef(sc);
                return a;
            };
            ui::SetNotificationHost({
                .createChannel = [](const ui::NotificationChannel& c) {
                    JNIEnv* e = Env();
                    jstring id = e->NewStringUTF(c.id.c_str()), name = e->NewStringUTF(c.name.c_str()), d = e->NewStringUTF(c.description.c_str());
                    const bool ok = e->CallStaticBooleanMethod(notes, createChannel, id, name, d, jint(c.importance));
                    Clear(e);
                    for (jobject o : { (jobject)id, (jobject)name, (jobject)d }) e->DeleteLocalRef(o);
                    return ok;
                },
                .deleteChannel = [](const std::string& id) {
                    JNIEnv* e = Env();
                    jstring js = e->NewStringUTF(id.c_str());
                    e->CallStaticVoidMethod(notes, deleteChannel, js);
                    Clear(e);
                    e->DeleteLocalRef(js);
                },
                .notify = [strings](const ui::Notification& n) {
                    JNIEnv* e = Env();
                    jobjectArray s = strings(e, { n.channelId, n.title, n.text, n.bigText, n.smallIcon, n.data, n.tag, n.group });
                    std::vector<std::string> actions;
                    for (const auto& a : n.actions) { actions.push_back(a.title); actions.push_back(a.data); actions.push_back(a.icon); }
                    jobjectArray acts = strings(e, actions);
                    const jint ints[4] = { n.id, n.progress, n.progressMax,
                                           (n.autoCancel ? 1 : 0) | (n.ongoing ? 2 : 0) | (n.onlyAlertOnce ? 4 : 0) | (n.indeterminate ? 8 : 0) };
                    jintArray ji = e->NewIntArray(4);
                    e->SetIntArrayRegion(ji, 0, 4, ints);
                    const bool ok = e->CallStaticBooleanMethod(notes, notify, s, ji, jlong(n.whenMillis), acts);
                    Clear(e);
                    for (jobject o : { (jobject)s, (jobject)acts, (jobject)ji }) e->DeleteLocalRef(o);
                    return ok;
                },
                .cancel = [](int id, const std::string& tag) {
                    JNIEnv* e = Env();
                    jstring js = e->NewStringUTF(tag.c_str());
                    e->CallStaticVoidMethod(notes, cancelOne, jint(id), js);
                    Clear(e);
                    e->DeleteLocalRef(js);
                },
                .cancelAll = [] { JNIEnv* e = Env(); e->CallStaticVoidMethod(notes, cancelAll); Clear(e); },
                .enabled = [] { JNIEnv* e = Env(); const bool r = e->CallStaticBooleanMethod(notes, enabledFn); Clear(e); return r; },
            });
        } else {
            env->ExceptionClear();
        }

        // Sensors, on this (the main) thread's looper.
        g_Looper = ALooper_forThread();
        g_Sensors = ASensorManager_getInstanceForPackage(PackageName().c_str());
        ui::SetSensorHost({
            .has = [](ui::SensorType t) { return g_Sensors && ASensorManager_getDefaultSensor(g_Sensors, int(t)) != nullptr; },
            .start = [](ui::SensorType t, int periodUs, kiln::Function<void(const ui::SensorEvent&)> listener) -> int {
                if (!g_Sensors || !g_Looper) return -1;
                const ASensor* sensor = ASensorManager_getDefaultSensor(g_Sensors, int(t));
                if (!sensor) return -1;
                const int id = g_NextId++;
                auto sub = std::make_unique<SensorSub>();
                sub->sensor = sensor;
                sub->type = t;
                sub->listener = std::move(listener);
                sub->queue = ASensorManager_createEventQueue(g_Sensors, g_Looper, ALOOPER_POLL_CALLBACK, &OnSensorEvents,
                                                             reinterpret_cast<void*>(std::intptr_t(id)));
                if (!sub->queue) return -1;
                // Faster than the sensor allows is its fastest.
                ASensorEventQueue_registerSensor(sub->queue, sensor, std::max(periodUs, ASensor_getMinDelay(sensor)), 0);
                g_SensorSubs[id] = std::move(sub);
                return id;
            },
            .stop = [](int id) {
                auto it = g_SensorSubs.find(id);
                if (it == g_SensorSubs.end()) return;
                ASensorEventQueue_disableSensor(it->second->queue, it->second->sensor);
                ASensorManager_destroyEventQueue(g_Sensors, it->second->queue);
                g_SensorSubs.erase(it);
            },
        });
    }

}
