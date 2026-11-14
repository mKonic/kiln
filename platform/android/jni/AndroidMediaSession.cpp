// kiln::media's audio focus, becoming-noisy and media session hosts on Android, through
// dev.kiln.KilnMedia (AudioManager, android.media.session, the MediaStyle notification) and
// dev.kiln.KilnMediaService (the media foreground service).
#include "AndroidMedia.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/media/AudioFocus.h"
#include "kiln/media/MediaSession.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Media;
        jmethodID g_Request = nullptr, g_Abandon = nullptr, g_Noisy = nullptr, g_Update = nullptr, g_Release = nullptr;

        void Clear(JNIEnv* env) {
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }

        void FocusChanged(JNIEnv*, jclass, jint id, jint change) { media::OnAudioFocusChange(id, change); }
        void BecomingNoisy(JNIEnv*, jclass, jint id) { media::OnAudioBecomingNoisy(id); }
        void SessionCommand(JNIEnv*, jclass, jint id, jint command, jlong positionMs) {
            if (command < 0 || command > int(media::MediaSessionCommand::NotificationDismissed)) return;
            media::OnMediaSessionCommand(id, media::MediaSessionCommand(command), positionMs);
        }
    }

    void InstallMediaSession(JNIEnv* env) {
        try {
            g_Media = jvm::FindClass("dev/kiln/KilnMedia");
            jclass c = g_Media.get();
            g_Request = jvm::detail::StaticMethod(env, c, "requestFocus", "(IIIIZ)I");
            g_Abandon = jvm::detail::StaticMethod(env, c, "abandonFocus", "(I)V");
            g_Noisy = jvm::detail::StaticMethod(env, c, "setNoisyReceiver", "(IZ)V");
            g_Update = jvm::detail::StaticMethod(env, c, "update", "(I[Ljava/lang/String;IZZJJZZZZ)V");
            g_Release = jvm::detail::StaticMethod(env, c, "release", "(I)V");
            static const JNINativeMethod natives[] = {
                { "focusChanged", "(II)V", reinterpret_cast<void*>(&FocusChanged) },
                { "becomingNoisy", "(I)V", reinterpret_cast<void*>(&BecomingNoisy) },
                { "sessionCommand", "(IIJ)V", reinterpret_cast<void*>(&SessionCommand) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 3);
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "kiln", "media: no audio focus or session host: %s", e.what());
            return;
        }
        media::SetAudioHost({
            .requestFocus = [](int id, int gain, media::AudioAttributes a, bool pauseWhenDucked) {
                JNIEnv* env = jvm::Env();
                jint r = env->CallStaticIntMethod(g_Media.get(), g_Request, jint(id), jint(gain), jint(a.usage), jint(a.contentType), jboolean(pauseWhenDucked));
                if (env->ExceptionCheck()) {
                    Clear(env);
                    return media::kAudioFocusRequestGranted;   // no context yet: play as before
                }
                return int(r);
            },
            .abandonFocus = [](int id) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_Abandon, jint(id));
                Clear(env);
            },
            .setNoisyReceiver = [](int id, bool enabled) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_Noisy, jint(id), jboolean(enabled));
                Clear(env);
            },
        });
        media::SetMediaSessionHost({
            .update = [](int id, const media::MediaSessionSnapshot& s) {
                JNIEnv* env = jvm::Env();
                const std::string strings[] = { s.metadata.title, s.metadata.artist, s.metadata.albumTitle, s.metadata.artworkUri, s.error, s.sessionActivityUri };
                jvm::Local<jclass> stringClass(env->FindClass("java/lang/String"), env);
                jvm::Local<jobjectArray> a(env->NewObjectArray(6, stringClass.get(), nullptr), env);
                for (int i = 0; i < 6; ++i) {
                    jvm::Local<jstring> v = jvm::ToJava(env, strings[i]);
                    env->SetObjectArrayElement(a.get(), i, v.get());
                }
                env->CallStaticVoidMethod(g_Media.get(), g_Update, jint(id), a.get(), jint(s.state), jboolean(s.playWhenReady), jboolean(s.isPlaying),
                                          jlong(s.positionMs), jlong(s.durationMs), jboolean(s.canSkipToPrevious), jboolean(s.canSkipToNext),
                                          jboolean(s.showNotification), jboolean(s.foreground));
                Clear(env);
            },
            .release = [](int id) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_Release, jint(id));
                Clear(env);
            },
        });
    }

}
