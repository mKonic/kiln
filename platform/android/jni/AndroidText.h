#pragma once

#include "kiln/TextBackend.h"

#include <jni.h>

namespace kiln::android {

    // TextBackend over dev.kiln.KilnText (StaticLayout + TextRunShaper + Canvas.drawGlyphs).
    class AndroidText final : public TextBackend {
    public:
        AndroidText(JNIEnv* env, jobject kilnText);
        ~AndroidText() override;

        ParagraphData Layout(const ParagraphRequest& request) override;
        ParagraphIntrinsicWidths Intrinsics(const ParagraphRequest& request) override;
        void SyncAtlas(AtlasSink& sink) override;
        float GetHorizontalPosition(std::int64_t handle, int offset, bool usePrimaryDirection) override;
        CursorRect GetCursorRect(std::int64_t handle, int offset) override;
        int GetOffsetForPosition(std::int64_t handle, float x, float y) override;
        RectF GetBoundingBox(std::int64_t handle, int offset) override;
        std::vector<RectF> GetSelectionRects(std::int64_t handle, int start, int end) override;
        std::pair<int, int> GetWordBoundary(std::int64_t handle, int offset) override;
        bool IsRtlCharAt(std::int64_t handle, int offset) override;
        void Release(std::int64_t handle) override;

    private:
        // Java arguments for a request: text, ints, floats, strings (local refs).
        struct Args { jstring text; jintArray ints; jfloatArray floats; jobjectArray strs; };
        Args Encode(JNIEnv* env, const ParagraphRequest& r);
        void Free(JNIEnv* env, const Args& a);

        JavaVM* m_Vm = nullptr;
        jobject m_Text = nullptr;
        jclass m_StringClass = nullptr;
        jmethodID m_StringCtor;
        jmethodID m_Layout, m_Intrinsics, m_Horizontal, m_CursorRect, m_OffsetFor, m_BoundingBox, m_SelectionRects,
            m_WordBoundary, m_IsRtl, m_Release, m_PageCount, m_PageBitmap, m_PageIsNew, m_TakeDirtyRows;
        bool m_Pending = false;   // a layout ran since the last sync
    };

}
