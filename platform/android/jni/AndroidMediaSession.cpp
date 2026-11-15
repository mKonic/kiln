// kiln::media's audio focus, becoming-noisy and media session hosts on Android, through
// dev.kiln.KilnMedia (AudioManager, android.media.session, the MediaStyle notification) and
// dev.kiln.KilnMediaService (the media foreground service).
#include "AndroidMedia.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/media/AudioFocus.h"
#include "kiln/media/Captioning.h"
#include "kiln/media/MediaSession.h"

#include <android/log.h>

namespace kiln::android {

    namespace {
        jvm::Global<jclass> g_Media;
        jmethodID g_Request = nullptr, g_Abandon = nullptr, g_Noisy = nullptr, g_Update = nullptr, g_Release = nullptr;
        jmethodID g_Layout = nullptr;
        jmethodID g_StreamVolume = nullptr, g_SetStreamVolume = nullptr, g_AdjustStreamVolume = nullptr, g_VolumeReceiver = nullptr, g_Locks = nullptr;

        void Clear(JNIEnv* env) {
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }

        void FocusChanged(JNIEnv*, jclass, jint id, jint change) { media::OnAudioFocusChange(id, change); }
        void BecomingNoisy(JNIEnv*, jclass, jint id) { media::OnAudioBecomingNoisy(id); }
        void VolumeChanged(JNIEnv*, jclass, jint id) { media::OnStreamVolumeChanged(id); }
        void SessionCommand(JNIEnv*, jclass, jint id, jint command, jlong positionMs, jfloat speed) {
            if (command < 0 || command > int(media::MediaSessionCommand::PlayerCommand)) return;
            media::OnMediaSessionCommand(id, media::MediaSessionCommand(command), positionMs, speed);
        }
        void CustomAction(JNIEnv* env, jclass, jint id, jstring action) { media::OnMediaSessionCustomAction(id, jvm::ToString(env, action)); }
        // android.media.Rating's style (RATING_HEART 1, THUMB_UP_DOWN 2, 3-5 STARS 3..5, PERCENTAGE 6).
        void SetRating(JNIEnv*, jclass, jint id, jint style, jboolean rated, jboolean liked, jfloat value) {
            media::Rating r;
            r.isRated = rated;
            if (style == 1) r.kind = media::Rating::Kind::Heart;
            else if (style == 2) r.kind = media::Rating::Kind::Thumb;
            else if (style >= 3 && style <= 5) {
                r.kind = media::Rating::Kind::Star;
                r.maxStars = style;
            } else if (style == 6) r.kind = media::Rating::Kind::Percentage;
            else return;
            r.liked = rated && liked;
            r.value = rated ? value : 0;
            media::OnMediaSessionSetRating(id, r);
        }
        void MediaRequest(JNIEnv* env, jclass, jint id, jstring mediaId, jstring uri, jstring query, jboolean play) {
            media::OnMediaSessionMediaRequest(id, jvm::ToString(env, mediaId), jvm::ToString(env, uri), jvm::ToString(env, query), play);
        }

        jvm::Local<jobjectArray> Strings(JNIEnv* env, const std::vector<std::string>& v) {
            jvm::Local<jclass> stringClass(env->FindClass("java/lang/String"), env);
            jvm::Local<jobjectArray> a(env->NewObjectArray(jsize(v.size()), stringClass.get(), nullptr), env);
            for (std::size_t i = 0; i < v.size(); ++i) {
                jvm::Local<jstring> s = jvm::ToJava(env, v[i]);
                env->SetObjectArrayElement(a.get(), jsize(i), s.get());
            }
            return a;
        }
        jvm::Local<jintArray> Ints(JNIEnv* env, const std::vector<int>& v) {
            jvm::Local<jintArray> a(env->NewIntArray(jsize(v.size())), env);
            if (!v.empty()) env->SetIntArrayRegion(a.get(), 0, jsize(v.size()), reinterpret_cast<const jint*>(v.data()));
            return a;
        }

        void PushLayout(JNIEnv* env, int id, const media::MediaSessionSnapshot& s) {
            std::vector<std::string> buttons, custom, queue, extras;
            std::vector<int> buttonIcons, customIcons;
            for (const auto& b : s.notificationButtons) {
                buttons.insert(buttons.end(), { b.drawable, b.displayName, b.customAction, std::to_string(b.playerCommand) });
                buttonIcons.push_back(b.icon);
            }
            for (const auto& b : s.customActions) {
                custom.insert(custom.end(), { b.drawable, b.displayName, b.customAction });
                customIcons.push_back(b.icon);
            }
            for (const auto& q : s.queue) queue.insert(queue.end(), { q.mediaId, q.title, q.subtitle, q.description, q.iconUri });
            for (const auto& [k, v] : s.extras) extras.insert(extras.end(), { k, v });
            float rating[4] = {};
            jsize ratingLength = 0;
            if (s.userRating && s.ratingType) {
                const media::Rating& r = *s.userRating;
                rating[0] = float(s.ratingType);
                rating[1] = r.isRated ? 1 : 0;
                rating[2] = r.liked ? 1 : 0;
                rating[3] = r.value;
                ratingLength = 4;
            }
            jvm::Local<jfloatArray> ratingArray(env->NewFloatArray(ratingLength), env);
            if (ratingLength) env->SetFloatArrayRegion(ratingArray.get(), 0, ratingLength, rating);
            jvm::Local<jstring> queueTitle = jvm::ToJava(env, s.queueTitle);
            env->CallStaticVoidMethod(g_Media.get(), g_Layout, jint(id), Strings(env, buttons).get(), Ints(env, buttonIcons).get(),
                                      Ints(env, s.compactView).get(), Strings(env, custom).get(), Ints(env, customIcons).get(),
                                      Strings(env, queue).get(), queueTitle.get(), jint(s.ratingType), ratingArray.get(),
                                      Strings(env, extras).get(), jlong(s.actions));
            Clear(env);
        }
    }

    void InstallMediaSession(JNIEnv* env) {
        try {
            g_Media = jvm::FindClass("dev/kiln/KilnMedia");
            jclass c = g_Media.get();
            g_Request = jvm::detail::StaticMethod(env, c, "requestFocus", "(IIIIZ)I");
            g_Abandon = jvm::detail::StaticMethod(env, c, "abandonFocus", "(I)V");
            g_Noisy = jvm::detail::StaticMethod(env, c, "setNoisyReceiver", "(IZ)V");
            g_Update = jvm::detail::StaticMethod(env, c, "update", "(I[Ljava/lang/String;IZZJJJFIZZZZZ)V");
            g_Release = jvm::detail::StaticMethod(env, c, "release", "(I)V");
            g_Layout = jvm::detail::StaticMethod(env, c, "layout", "(I[Ljava/lang/String;[I[I[Ljava/lang/String;[I[Ljava/lang/String;Ljava/lang/String;I[F[Ljava/lang/String;J)V");
            g_StreamVolume = jvm::detail::StaticMethod(env, c, "streamVolume", "(I)[I");
            g_SetStreamVolume = jvm::detail::StaticMethod(env, c, "setStreamVolume", "(III)V");
            g_AdjustStreamVolume = jvm::detail::StaticMethod(env, c, "adjustStreamVolume", "(III)V");
            g_VolumeReceiver = jvm::detail::StaticMethod(env, c, "setVolumeReceiver", "(IZ)V");
            g_Locks = jvm::detail::StaticMethod(env, c, "setLocks", "(IZZ)V");
            static const JNINativeMethod natives[] = {
                { "focusChanged", "(II)V", reinterpret_cast<void*>(&FocusChanged) },
                { "becomingNoisy", "(I)V", reinterpret_cast<void*>(&BecomingNoisy) },
                { "volumeChanged", "(I)V", reinterpret_cast<void*>(&VolumeChanged) },
                { "sessionCommand", "(IIJF)V", reinterpret_cast<void*>(&SessionCommand) },
                { "customAction", "(ILjava/lang/String;)V", reinterpret_cast<void*>(&CustomAction) },
                { "setRating", "(IIZZF)V", reinterpret_cast<void*>(&SetRating) },
                { "mediaRequest", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V", reinterpret_cast<void*>(&MediaRequest) },
            };
            jvm::detail::RegisterNatives(env, c, natives, 7);
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
            .streamVolume = [](int stream) {
                JNIEnv* env = jvm::Env();
                media::StreamVolume v;
                jvm::Local<jintArray> a(static_cast<jintArray>(env->CallStaticObjectMethod(g_Media.get(), g_StreamVolume, jint(stream))), env);
                Clear(env);
                if (a.get() && env->GetArrayLength(a.get()) == 4) {
                    jint x[4];
                    env->GetIntArrayRegion(a.get(), 0, 4, x);
                    v = { x[0], x[1] != 0, x[2], x[3] };
                }
                return v;
            },
            .setStreamVolume = [](int stream, int volume, int flags) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_SetStreamVolume, jint(stream), jint(volume), jint(flags));
                Clear(env);
            },
            .adjustStreamVolume = [](int stream, int direction, int flags) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_AdjustStreamVolume, jint(stream), jint(direction), jint(flags));
                Clear(env);
            },
            .setVolumeChangedReceiver = [](int id, bool enabled) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_VolumeReceiver, jint(id), jboolean(enabled));
                Clear(env);
            },
            .setLocks = [](int id, bool wake, bool wifi) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_Locks, jint(id), jboolean(wake), jboolean(wifi));
                Clear(env);
            },
        });
        media::SetMediaSessionHost({
            .update = [](int id, const media::MediaSessionSnapshot& s) {
                JNIEnv* env = jvm::Env();
                PushLayout(env, id, s);
                const std::string strings[] = { s.metadata.title, s.metadata.artist, s.metadata.albumTitle, s.metadata.artworkUri, s.error, s.sessionActivityUri, s.mediaId };
                jvm::Local<jclass> stringClass(env->FindClass("java/lang/String"), env);
                jvm::Local<jobjectArray> a(env->NewObjectArray(7, stringClass.get(), nullptr), env);
                for (int i = 0; i < 7; ++i) {
                    jvm::Local<jstring> v = jvm::ToJava(env, strings[i]);
                    env->SetObjectArrayElement(a.get(), i, v.get());
                }
                env->CallStaticVoidMethod(g_Media.get(), g_Update, jint(id), a.get(), jint(s.state), jboolean(s.playWhenReady), jboolean(s.isPlaying),
                                          jlong(s.positionMs), jlong(s.durationMs), jlong(s.bufferedPositionMs), jfloat(s.playbackSpeed),
                                          jint(s.activeQueueItemId), jboolean(s.canSkipToPrevious), jboolean(s.canSkipToNext), jboolean(s.canSeek),
                                          jboolean(s.showNotification), jboolean(s.foreground));
                Clear(env);
            },
            .release = [](int id) {
                JNIEnv* env = jvm::Env();
                env->CallStaticVoidMethod(g_Media.get(), g_Release, jint(id));
                Clear(env);
            },
        });
        // CaptioningManager, read when asked (the user may change it any time).
        static jmethodID enabled = jvm::detail::StaticMethod(env, g_Media.get(), "captioningEnabled", "()Z");
        static jmethodID locale = jvm::detail::StaticMethod(env, g_Media.get(), "captioningLocale", "()Ljava/lang/String;");
        static jmethodID scale = jvm::detail::StaticMethod(env, g_Media.get(), "captioningFontScale", "()F");
        static jmethodID style = jvm::detail::StaticMethod(env, g_Media.get(), "captioningStyle", "()[I");
        static jmethodID typeface = jvm::detail::StaticMethod(env, g_Media.get(), "captioningTypeface", "()Ljava/lang/String;");
        media::SetCaptioningHost({
            .isEnabled = [] {
                JNIEnv* env = jvm::Env();
                const bool on = env->CallStaticBooleanMethod(g_Media.get(), enabled);
                Clear(env);
                return on;
            },
            .localeTag = []() -> std::optional<std::string> {
                JNIEnv* env = jvm::Env();
                jvm::Local<jstring> tag(static_cast<jstring>(env->CallStaticObjectMethod(g_Media.get(), locale)), env);
                Clear(env);
                if (!tag.get()) return std::nullopt;
                return jvm::ToString(env, tag.get());
            },
            .userStyle = [] {
                JNIEnv* env = jvm::Env();
                media::CaptionStyle out;   // CaptionStyleCompat.DEFAULT where the user set nothing
                jvm::Local<jintArray> a(static_cast<jintArray>(env->CallStaticObjectMethod(g_Media.get(), style)), env);
                Clear(env);
                if (a.get() && env->GetArrayLength(a.get()) == 10) {
                    jint v[10];
                    env->GetIntArrayRegion(a.get(), 0, 10, v);
                    if (v[0]) out.foregroundColor = std::uint32_t(v[1]);
                    if (v[2]) out.backgroundColor = std::uint32_t(v[3]);
                    if (v[4]) out.windowColor = std::uint32_t(v[5]);
                    if (v[6]) out.edgeType = v[7];
                    if (v[8]) out.edgeColor = std::uint32_t(v[9]);
                }
                jvm::Local<jstring> family(static_cast<jstring>(env->CallStaticObjectMethod(g_Media.get(), typeface)), env);
                Clear(env);
                if (family.get()) out.typeface = jvm::ToString(env, family.get());
                return out;
            },
            .fontScale = [] {
                JNIEnv* env = jvm::Env();
                const float f = env->CallStaticFloatMethod(g_Media.get(), scale);
                Clear(env);
                return f > 0 ? f : 1.0f;
            },
        });
    }

}
