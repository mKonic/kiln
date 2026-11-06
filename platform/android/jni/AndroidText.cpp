#include "AndroidText.h"

#include <android/bitmap.h>
#include <android/log.h>

#include <chrono>
#include <string>

namespace kiln::android {

    namespace {
        JNIEnv* Env(JavaVM* vm) {
            JNIEnv* env = nullptr;
            vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            return env;
        }
    }

    AndroidText::AndroidText(JNIEnv* env, jobject kilnText) {
        env->GetJavaVM(&m_Vm);
        m_Text = env->NewGlobalRef(kilnText);
        jclass cls = env->GetObjectClass(kilnText);
        m_Layout = env->GetMethodID(cls, "layout", "(Ljava/lang/String;FI)[F");
        m_PageCount = env->GetMethodID(cls, "pageCount", "()I");
        m_PageBitmap = env->GetMethodID(cls, "pageBitmap", "(I)Landroid/graphics/Bitmap;");
        m_PageIsNew = env->GetMethodID(cls, "pageIsNew", "(I)Z");
        m_TakeDirtyRows = env->GetMethodID(cls, "takeDirtyRows", "(I)J");
        env->DeleteLocalRef(cls);
    }

    AndroidText::~AndroidText() {
        if (JNIEnv* env = Env(m_Vm)) env->DeleteGlobalRef(m_Text);
    }

    ParagraphData AndroidText::Layout(std::string_view utf8, float sizeSp, float widthPx) {
        ParagraphData out;
        JNIEnv* env = Env(m_Vm);
        if (!env) return out;
        const auto t0 = std::chrono::steady_clock::now();
        // NewStringUTF wants modified UTF-8, which differs for supplementary characters (emoji),
        // so go through a byte array and String(bytes, UTF_8).
        jbyteArray bytes = env->NewByteArray(jsize(utf8.size()));
        env->SetByteArrayRegion(bytes, 0, jsize(utf8.size()), reinterpret_cast<const jbyte*>(utf8.data()));
        jclass stringClass = env->FindClass("java/lang/String");
        jmethodID ctor = env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
        jstring charset = env->NewStringUTF("UTF-8");
        auto text = static_cast<jstring>(env->NewObject(stringClass, ctor, bytes, charset));
        auto result = static_cast<jfloatArray>(env->CallObjectMethod(m_Text, m_Layout, text, sizeSp, jint(widthPx)));
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); return out; }
        const jsize n = env->GetArrayLength(result);
        jfloat* f = env->GetFloatArrayElements(result, nullptr);
        out.width = f[0];
        out.height = f[1];
        out.lineCount = int(f[2]);
        for (jsize i = 3; i + 10 <= n; i += 10) {
            GlyphQuad q;
            q.page = std::uint32_t(f[i]);
            q.rect = { f[i + 1], f[i + 2], f[i + 3], f[i + 4] };
            q.uv = { f[i + 5], f[i + 6], f[i + 7], f[i + 8] };
            q.color = f[i + 9] != 0;
            out.glyphs.push_back(q);
        }
        env->ReleaseFloatArrayElements(result, f, JNI_ABORT);
        env->DeleteLocalRef(result);
        env->DeleteLocalRef(text);
        env->DeleteLocalRef(charset);
        env->DeleteLocalRef(stringClass);
        env->DeleteLocalRef(bytes);
        m_Pending = true;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        __android_log_print(ANDROID_LOG_DEBUG, "kiln", "text layout: %zu bytes, %zu glyphs, %d lines in %lld us",
                            utf8.size(), out.glyphs.size(), out.lineCount, static_cast<long long>(us));
        return out;
    }

    void AndroidText::SyncAtlas(AtlasSink& sink) {
        if (!m_Pending) return;
        m_Pending = false;
        JNIEnv* env = Env(m_Vm);
        if (!env) return;
        const jint pages = env->CallIntMethod(m_Text, m_PageCount);
        for (jint i = 0; i < pages; ++i) {
            const bool fresh = env->CallBooleanMethod(m_Text, m_PageIsNew, i);
            const jlong rows = env->CallLongMethod(m_Text, m_TakeDirtyRows, i);
            if (!fresh && rows == 0) continue;
            jobject bitmap = env->CallObjectMethod(m_Text, m_PageBitmap, i);
            AndroidBitmapInfo info{};
            AndroidBitmap_getInfo(env, bitmap, &info);
            if (fresh) sink.EnsurePage(std::uint32_t(i), info.width, info.height);
            void* pixels = nullptr;
            if (rows != 0 && AndroidBitmap_lockPixels(env, bitmap, &pixels) == ANDROID_BITMAP_RESULT_SUCCESS) {
                const auto y0 = std::uint32_t(rows >> 32), y1 = std::uint32_t(rows & 0xffffffff);
                sink.UploadPage(std::uint32_t(i), static_cast<const std::uint8_t*>(pixels) + std::size_t(y0) * info.stride,
                                info.stride, 0, y0, info.width, y1 - y0);
                AndroidBitmap_unlockPixels(env, bitmap);
            }
            env->DeleteLocalRef(bitmap);
        }
    }

}
