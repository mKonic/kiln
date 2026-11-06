#include "AndroidText.h"

#include <android/bitmap.h>
#include <android/log.h>

#include <chrono>
#include <string>
#include <vector>

namespace kiln::android {

    namespace {
        JNIEnv* Env(JavaVM* vm) {
            JNIEnv* env = nullptr;
            vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            return env;
        }
    }

    // Request and result layouts (mirror KilnText.java).
    namespace {
        constexpr int kParagraphInts = 13, kRunInts = 8, kParagraphFloats = 6, kRunFloats = 6, kRunStrings = 4;
        constexpr int kHeader = 12, kGlyph = 11, kLine = 14, kSegment = 5, kMetrics = 4;
    }

    AndroidText::AndroidText(JNIEnv* env, jobject kilnText) {
        env->GetJavaVM(&m_Vm);
        m_Text = env->NewGlobalRef(kilnText);
        jclass cls = env->GetObjectClass(kilnText);
        constexpr const char* req = "(Ljava/lang/String;[I[F[Ljava/lang/String;)[F";
        m_Layout = env->GetMethodID(cls, "layout", req);
        m_Intrinsics = env->GetMethodID(cls, "intrinsics", req);
        m_Horizontal = env->GetMethodID(cls, "horizontal", "(IIZ)F");
        m_CursorRect = env->GetMethodID(cls, "cursorRect", "(II)[F");
        m_OffsetFor = env->GetMethodID(cls, "offsetForPosition", "(IFF)I");
        m_BoundingBox = env->GetMethodID(cls, "boundingBox", "(II)[F");
        m_SelectionRects = env->GetMethodID(cls, "selectionRects", "(III)[F");
        m_WordBoundary = env->GetMethodID(cls, "wordBoundary", "(II)J");
        m_IsRtl = env->GetMethodID(cls, "isRtlCharAt", "(II)Z");
        m_Release = env->GetMethodID(cls, "release", "(I)V");
        m_PageCount = env->GetMethodID(cls, "pageCount", "()I");
        m_PageBitmap = env->GetMethodID(cls, "pageBitmap", "(I)Landroid/graphics/Bitmap;");
        m_PageIsNew = env->GetMethodID(cls, "pageIsNew", "(I)Z");
        m_TakeDirtyRows = env->GetMethodID(cls, "takeDirtyRows", "(I)J");
        env->DeleteLocalRef(cls);
        jclass str = env->FindClass("java/lang/String");
        m_StringClass = static_cast<jclass>(env->NewGlobalRef(str));
        m_StringCtor = env->GetMethodID(str, "<init>", "([BLjava/lang/String;)V");
        env->DeleteLocalRef(str);
    }

    AndroidText::~AndroidText() {
        if (JNIEnv* env = Env(m_Vm)) {
            env->DeleteGlobalRef(m_Text);
            env->DeleteGlobalRef(m_StringClass);
        }
    }

    namespace {
        // NewStringUTF wants modified UTF-8, which differs for supplementary characters (emoji),
        // so go through a byte array and String(bytes, UTF_8).
        jstring JavaString(JNIEnv* env, jclass cls, jmethodID ctor, std::string_view utf8) {
            jbyteArray bytes = env->NewByteArray(jsize(utf8.size()));
            env->SetByteArrayRegion(bytes, 0, jsize(utf8.size()), reinterpret_cast<const jbyte*>(utf8.data()));
            jstring charset = env->NewStringUTF("UTF-8");
            auto s = static_cast<jstring>(env->NewObject(cls, ctor, bytes, charset));
            env->DeleteLocalRef(charset);
            env->DeleteLocalRef(bytes);
            return s;
        }
        bool Check(JNIEnv* env) {
            if (!env->ExceptionCheck()) return true;
            env->ExceptionDescribe();
            env->ExceptionClear();
            return false;
        }
    }

    AndroidText::Args AndroidText::Encode(JNIEnv* env, const ParagraphRequest& r) {
        const int nRuns = int(r.runs.size());
        std::vector<jint> ints(std::size_t(kParagraphInts + (nRuns + 1) * kRunInts));
        std::vector<jfloat> floats(std::size_t(kParagraphFloats + (nRuns + 1) * kRunFloats));
        ints[0] = nRuns;
        ints[1] = r.maxLines;
        ints[2] = jint(r.overflow);
        ints[3] = r.textAlign;
        ints[4] = r.textDirection;
        ints[5] = r.lineHeightTrim;
        ints[6] = r.lineHeightMode;
        ints[7] = r.hasLineHeightStyle;
        ints[8] = r.breakStrategy;
        ints[9] = r.breakStrictness;
        ints[10] = r.wordBreak;
        ints[11] = r.hyphens;
        ints[12] = r.includeFontPadding;
        floats[0] = r.widthPx;
        floats[1] = r.maxHeightPx;
        floats[2] = r.lineHeightPx;
        floats[3] = r.lineHeightAlignment;
        floats[4] = r.indentFirstPx;
        floats[5] = r.indentRestPx;
        jobjectArray strs = env->NewObjectArray(jsize((nRuns + 1) * kRunStrings), m_StringClass, nullptr);
        for (int i = 0; i <= nRuns; ++i) {
            const TextRunRequest& run = i == 0 ? r.base : r.runs[std::size_t(i - 1)];
            jint* in = &ints[std::size_t(kParagraphInts + i * kRunInts)];
            in[0] = run.start;
            in[1] = run.end;
            in[2] = jint(run.font.kind);
            in[3] = run.font.weight;
            in[4] = run.font.italic;
            in[5] = run.font.fakeBold;
            in[6] = run.font.fakeItalic;
            jfloat* fl = &floats[std::size_t(kParagraphFloats + i * kRunFloats)];
            fl[0] = run.sizePx;
            fl[1] = run.letterSpacingPx;
            fl[2] = run.letterSpacingEm;
            fl[3] = run.baselineShift;
            fl[4] = run.scaleX;
            fl[5] = run.skewX;
            const std::string* values[kRunStrings] = { &run.font.name, &run.font.variationSettings, &run.fontFeatureSettings, &run.localeList };
            for (int k = 0; k < kRunStrings; ++k) {
                jstring v = JavaString(env, m_StringClass, m_StringCtor, *values[k]);
                env->SetObjectArrayElement(strs, i * kRunStrings + k, v);
                env->DeleteLocalRef(v);
            }
        }
        Args a;
        a.text = JavaString(env, m_StringClass, m_StringCtor, r.text);
        a.ints = env->NewIntArray(jsize(ints.size()));
        env->SetIntArrayRegion(a.ints, 0, jsize(ints.size()), ints.data());
        a.floats = env->NewFloatArray(jsize(floats.size()));
        env->SetFloatArrayRegion(a.floats, 0, jsize(floats.size()), floats.data());
        a.strs = strs;
        return a;
    }

    void AndroidText::Free(JNIEnv* env, const Args& a) {
        env->DeleteLocalRef(a.text);
        env->DeleteLocalRef(a.ints);
        env->DeleteLocalRef(a.floats);
        env->DeleteLocalRef(a.strs);
    }

    ParagraphData AndroidText::Layout(const ParagraphRequest& request) {
        ParagraphData out;
        JNIEnv* env = Env(m_Vm);
        if (!env) return out;
        const auto t0 = std::chrono::steady_clock::now();
        const Args args = Encode(env, request);
        auto result = static_cast<jfloatArray>(env->CallObjectMethod(m_Text, m_Layout, args.text, args.ints, args.floats, args.strs));
        Free(env, args);
        if (!Check(env) || !result) return out;
        const jsize n = env->GetArrayLength(result);
        jfloat* f = env->GetFloatArrayElements(result, nullptr);
        out.width = f[0];
        out.height = f[1];
        out.lineCount = int(f[2]);
        out.handle = std::int64_t(f[3]);
        out.firstBaseline = f[4];
        out.lastBaseline = f[5];
        out.didExceedMaxLines = f[6] != 0;
        const int nGlyphs = int(f[7]), nLines = int(f[8]), nSegments = int(f[9]), nRuns = int(f[10]);
        if (kHeader + nGlyphs * kGlyph + nLines * kLine + nSegments * kSegment + nRuns * kMetrics <= n) {
            const jfloat* p = f + kHeader;
            out.glyphs.reserve(std::size_t(nGlyphs));
            for (int i = 0; i < nGlyphs; ++i, p += kGlyph) {
                GlyphQuad q;
                q.page = std::uint32_t(p[0]);
                q.rect = { p[1], p[2], p[3], p[4] };
                q.uv = { p[5], p[6], p[7], p[8] };
                q.color = p[9] != 0;
                q.run = int(p[10]);
                out.glyphs.push_back(q);
            }
            for (int i = 0; i < nLines; ++i, p += kLine) {
                LineData l;
                l.top = p[0]; l.bottom = p[1]; l.baseline = p[2]; l.ascent = p[3]; l.descent = p[4];
                l.left = p[5]; l.right = p[6];
                l.start = int(p[7]); l.end = int(p[8]); l.visibleEnd = int(p[9]);
                l.ellipsisStart = int(p[10]); l.ellipsisCount = int(p[11]);
                l.rtl = p[12] != 0;
                out.lines.push_back(l);
            }
            for (int i = 0; i < nSegments; ++i, p += kSegment)
                out.segments.push_back({ int(p[0]), int(p[1]), p[2], p[3], p[4] });
            for (int i = 0; i < nRuns; ++i, p += kMetrics)
                out.runMetrics.push_back({ p[0], p[1], p[2], p[3] });
        }
        env->ReleaseFloatArrayElements(result, f, JNI_ABORT);
        env->DeleteLocalRef(result);
        m_Pending = true;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        __android_log_print(ANDROID_LOG_DEBUG, "kiln", "text layout: %zu bytes, %zu runs, %zu glyphs, %d lines in %lld us",
                            request.text.size(), request.runs.size(), out.glyphs.size(), out.lineCount, static_cast<long long>(us));
        return out;
    }

    ParagraphIntrinsicWidths AndroidText::Intrinsics(const ParagraphRequest& request) {
        JNIEnv* env = Env(m_Vm);
        if (!env) return {};
        const Args args = Encode(env, request);
        auto result = static_cast<jfloatArray>(env->CallObjectMethod(m_Text, m_Intrinsics, args.text, args.ints, args.floats, args.strs));
        Free(env, args);
        if (!Check(env) || !result) return {};
        float v[2] = { 0, 0 };
        env->GetFloatArrayRegion(result, 0, 2, v);
        env->DeleteLocalRef(result);
        return { v[0], v[1] };
    }

    float AndroidText::GetHorizontalPosition(std::int64_t handle, int offset, bool primary) {
        return Env(m_Vm)->CallFloatMethod(m_Text, m_Horizontal, jint(handle), jint(offset), jboolean(primary));
    }

    CursorRect AndroidText::GetCursorRect(std::int64_t handle, int offset) {
        JNIEnv* env = Env(m_Vm);
        auto arr = static_cast<jfloatArray>(env->CallObjectMethod(m_Text, m_CursorRect, jint(handle), jint(offset)));
        float v[3] = { 0, 0, 0 };
        env->GetFloatArrayRegion(arr, 0, 3, v);
        env->DeleteLocalRef(arr);
        return { v[0], v[1], v[2] };
    }

    int AndroidText::GetOffsetForPosition(std::int64_t handle, float x, float y) {
        return Env(m_Vm)->CallIntMethod(m_Text, m_OffsetFor, jint(handle), x, y);
    }

    RectF AndroidText::GetBoundingBox(std::int64_t handle, int offset) {
        JNIEnv* env = Env(m_Vm);
        auto arr = static_cast<jfloatArray>(env->CallObjectMethod(m_Text, m_BoundingBox, jint(handle), jint(offset)));
        float v[4] = { 0, 0, 0, 0 };
        env->GetFloatArrayRegion(arr, 0, 4, v);
        env->DeleteLocalRef(arr);
        return { v[0], v[1], v[2] - v[0], v[3] - v[1] };
    }

    std::vector<RectF> AndroidText::GetSelectionRects(std::int64_t handle, int start, int end) {
        JNIEnv* env = Env(m_Vm);
        auto arr = static_cast<jfloatArray>(env->CallObjectMethod(m_Text, m_SelectionRects, jint(handle), jint(start), jint(end)));
        std::vector<RectF> out;
        if (!arr) return out;
        const jsize n = env->GetArrayLength(arr);
        std::vector<float> v(static_cast<std::size_t>(n));
        env->GetFloatArrayRegion(arr, 0, n, v.data());
        env->DeleteLocalRef(arr);
        for (jsize i = 0; i + 4 <= n; i += 4) out.push_back({ v[i], v[i + 1], v[i + 2] - v[i], v[i + 3] - v[i + 1] });
        return out;
    }

    std::pair<int, int> AndroidText::GetWordBoundary(std::int64_t handle, int offset) {
        const jlong packed = Env(m_Vm)->CallLongMethod(m_Text, m_WordBoundary, jint(handle), jint(offset));
        return { int(packed >> 32), int(packed & 0xffffffff) };
    }

    bool AndroidText::IsRtlCharAt(std::int64_t handle, int offset) {
        return Env(m_Vm)->CallBooleanMethod(m_Text, m_IsRtl, jint(handle), jint(offset));
    }

    void AndroidText::Release(std::int64_t handle) {
        if (handle) Env(m_Vm)->CallVoidMethod(m_Text, m_Release, jint(handle));
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
