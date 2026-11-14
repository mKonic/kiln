// kiln::speech's hosts on Android: dev.kiln.KilnSpeech (TextToSpeech, SpeechRecognizer).
#include "AndroidServices.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/speech/SpeechRecognizer.h"
#include "kiln/speech/TextToSpeech.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Speech;
        jmethodID g_TtsCreate, g_TtsShutdown, g_TtsSpeak, g_TtsSilence, g_TtsStop, g_TtsIsSpeaking, g_TtsRate, g_TtsAttributes,
            g_TtsLanguage, g_TtsLanguages, g_TtsVoices, g_TtsSetVoice, g_TtsEngine, g_TtsDefaultLanguage;
        jmethodID g_RecAvailable, g_RecCreate, g_RecStart, g_RecStop, g_RecCancel, g_RecDestroy;

        bool Clear(JNIEnv* env) {
            if (!env->ExceptionCheck()) return false;
            env->ExceptionDescribe();
            env->ExceptionClear();
            return true;
        }

        std::vector<std::string> Strings(JNIEnv* env, jobjectArray a) {
            std::vector<std::string> out;
            if (!a) return out;
            for (jsize i = 0, n = env->GetArrayLength(a); i < n; ++i) {
                jvm::Local<jstring> s(static_cast<jstring>(env->GetObjectArrayElement(a, i)), env);
                out.push_back(s ? jvm::ToString(env, s.get()) : std::string());
            }
            return out;
        }

        void TtsStatusJni(JNIEnv*, jclass, jint id, jboolean success) { speech::OnTtsStatus(id, success); }
        void TtsUtteranceJni(JNIEnv* env, jclass, jint id, jint event, jstring utteranceId, jint a, jint b) {
            speech::OnTtsUtterance(id, event, utteranceId ? jvm::ToString(env, utteranceId) : std::string(), a, b);
        }
        void RecognitionJni(JNIEnv* env, jclass, jint id, jint event, jfloat value, jobjectArray candidates, jfloatArray confidences) {
            speech::RecognitionResult r;
            r.candidates = Strings(env, candidates);
            if (confidences) {
                jsize n = env->GetArrayLength(confidences);
                r.confidences.resize(std::size_t(n));
                env->GetFloatArrayRegion(confidences, 0, n, r.confidences.data());
            }
            speech::OnRecognitionEvent(id, speech::RecognitionEvent(event), value, r);
        }

        jint CallInt(jmethodID m, auto... args) {
            JNIEnv* env = jvm::Env();
            jint r = env->CallStaticIntMethod(g_Speech.get(), m, args...);
            return Clear(env) ? -1 : r;
        }
        bool CallBool(jmethodID m, auto... args) {
            JNIEnv* env = jvm::Env();
            jboolean r = env->CallStaticBooleanMethod(g_Speech.get(), m, args...);
            return !Clear(env) && r;
        }
        void CallVoid(jmethodID m, auto... args) {
            JNIEnv* env = jvm::Env();
            env->CallStaticVoidMethod(g_Speech.get(), m, args...);
            Clear(env);
        }
        std::string CallString(jmethodID m, jint id) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jstring> s(static_cast<jstring>(env->CallStaticObjectMethod(g_Speech.get(), m, id)), env);
            if (Clear(env) || !s) return {};
            return jvm::ToString(env, s.get());
        }
        std::vector<std::string> CallStrings(jmethodID m, jint id) {
            JNIEnv* env = jvm::Env();
            jvm::Local<jobjectArray> a(static_cast<jobjectArray>(env->CallStaticObjectMethod(g_Speech.get(), m, id)), env);
            if (Clear(env)) return {};
            return Strings(env, a.get());
        }
    }

    void InstallSpeech(JNIEnv* env) {
        try {
            g_Speech = jvm::FindClass("dev/kiln/KilnSpeech");
            jclass c = g_Speech.get();
            using jvm::detail::StaticMethod;
            g_TtsCreate = StaticMethod(env, c, "ttsCreate", "(ILjava/lang/String;)V");
            g_TtsShutdown = StaticMethod(env, c, "ttsShutdown", "(I)V");
            g_TtsSpeak = StaticMethod(env, c, "ttsSpeak", "(ILjava/lang/String;Ljava/lang/String;IFF)I");
            g_TtsSilence = StaticMethod(env, c, "ttsSilence", "(IJLjava/lang/String;I)I");
            g_TtsStop = StaticMethod(env, c, "ttsStop", "(I)V");
            g_TtsIsSpeaking = StaticMethod(env, c, "ttsIsSpeaking", "(I)Z");
            g_TtsRate = StaticMethod(env, c, "ttsRateAndPitch", "(IFF)V");
            g_TtsAttributes = StaticMethod(env, c, "ttsAudioAttributes", "(III)V");
            g_TtsLanguage = StaticMethod(env, c, "ttsLanguage", "(ILjava/lang/String;Z)I");
            g_TtsLanguages = StaticMethod(env, c, "ttsLanguages", "(I)[Ljava/lang/String;");
            g_TtsVoices = StaticMethod(env, c, "ttsVoices", "(I)[Ljava/lang/String;");
            g_TtsSetVoice = StaticMethod(env, c, "ttsSetVoice", "(ILjava/lang/String;)Z");
            g_TtsEngine = StaticMethod(env, c, "ttsEngine", "(I)Ljava/lang/String;");
            g_TtsDefaultLanguage = StaticMethod(env, c, "ttsDefaultLanguage", "(I)Ljava/lang/String;");
            g_RecAvailable = StaticMethod(env, c, "recognitionAvailable", "(Z)Z");
            g_RecCreate = StaticMethod(env, c, "recognizerCreate", "(IZ)Z");
            g_RecStart = StaticMethod(env, c, "recognizerStart", "(ILjava/lang/String;ZIZZJ)V");
            g_RecStop = StaticMethod(env, c, "recognizerStop", "(I)V");
            g_RecCancel = StaticMethod(env, c, "recognizerCancel", "(I)V");
            g_RecDestroy = StaticMethod(env, c, "recognizerDestroy", "(I)V");
            static const JNINativeMethod natives[] = {
                { "ttsStatus", "(IZ)V", reinterpret_cast<void*>(&TtsStatusJni) },
                { "ttsUtterance", "(IILjava/lang/String;II)V", reinterpret_cast<void*>(&TtsUtteranceJni) },
                { "recognition", "(IIF[Ljava/lang/String;[F)V", reinterpret_cast<void*>(&RecognitionJni) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 3);
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "speech: no host: %s", e.what());
            return;
        }
        speech::SetTtsHost({
            .create = [](int id, const std::string& engine) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> e = jvm::ToJava(env, engine);
                env->CallStaticVoidMethod(g_Speech.get(), g_TtsCreate, jint(id), e.get());
                if (Clear(env)) speech::OnTtsStatus(id, false);
            },
            .shutdown = [](int id) { CallVoid(g_TtsShutdown, jint(id)); },
            .speak = [](int id, const std::string& text, const std::string& utteranceId, int queue, float volume, float pan) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> t = jvm::ToJava(env, text), u = jvm::ToJava(env, utteranceId);
                return int(CallInt(g_TtsSpeak, jint(id), t.get(), u.get(), jint(queue), jfloat(volume), jfloat(pan)));
            },
            .silence = [](int id, long long ms, const std::string& utteranceId, int queue) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> u = jvm::ToJava(env, utteranceId);
                return int(CallInt(g_TtsSilence, jint(id), jlong(ms), u.get(), jint(queue)));
            },
            .stop = [](int id) { CallVoid(g_TtsStop, jint(id)); },
            .isSpeaking = [](int id) { return CallBool(g_TtsIsSpeaking, jint(id)); },
            .setRateAndPitch = [](int id, float rate, float pitch) { CallVoid(g_TtsRate, jint(id), jfloat(rate), jfloat(pitch)); },
            .setAudioAttributes = [](int id, media::AudioAttributes a) {
                CallVoid(g_TtsAttributes, jint(id), jint(a.usage), jint(a.contentType));
            },
            .language = [](int id, const std::string& tag, bool set) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> t = jvm::ToJava(env, tag);
                jint r = CallInt(g_TtsLanguage, jint(id), t.get(), jboolean(set));
                return r < -2 ? -2 : int(r);
            },
            .languages = [](int id) { return CallStrings(g_TtsLanguages, jint(id)); },
            .voices = [](int id) {
                std::vector<std::string> flat = CallStrings(g_TtsVoices, jint(id));
                std::vector<speech::Voice> out;
                for (std::size_t i = 0; i + 6 <= flat.size(); i += 6)
                    out.push_back({ .name = flat[i], .locale = flat[i + 1], .quality = std::atoi(flat[i + 2].c_str()),
                                    .latency = std::atoi(flat[i + 3].c_str()), .requiresNetwork = flat[i + 4] == "1",
                                    .notInstalled = flat[i + 5] == "1" });
                return out;
            },
            .setVoice = [](int id, const std::string& name) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> n = jvm::ToJava(env, name);
                return CallBool(g_TtsSetVoice, jint(id), n.get());
            },
            .currentEngine = [](int id) { return CallString(g_TtsEngine, jint(id)); },
            .defaultLanguage = [](int id) { return CallString(g_TtsDefaultLanguage, jint(id)); },
        });
        speech::SetRecognizerHost({
            .create = [](int id, bool onDevice) { return CallBool(g_RecCreate, jint(id), jboolean(onDevice)); },
            .destroy = [](int id) { CallVoid(g_RecDestroy, jint(id)); },
            .start = [](int id, const speech::RecognitionOptions& o) {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> l = jvm::ToJava(env, o.language);
                CallVoid(g_RecStart, jint(id), l.get(), jboolean(o.partialResults), jint(o.maxResults), jboolean(o.preferOffline),
                         jboolean(o.webSearch), jlong(o.completeSilenceMillis));
            },
            .stop = [](int id) { CallVoid(g_RecStop, jint(id)); },
            .cancel = [](int id) { CallVoid(g_RecCancel, jint(id)); },
            .available = [](bool onDevice) { return CallBool(g_RecAvailable, jboolean(onDevice)); },
        });
    }

}
