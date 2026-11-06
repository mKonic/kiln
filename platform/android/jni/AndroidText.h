#pragma once

#include "kiln/TextBackend.h"

#include <jni.h>

namespace kiln::android {

    // TextBackend over dev.kiln.KilnText (StaticLayout + TextRunShaper + Canvas.drawGlyphs).
    class AndroidText final : public TextBackend {
    public:
        AndroidText(JNIEnv* env, jobject kilnText);
        ~AndroidText() override;

        ParagraphData Layout(std::string_view utf8, float sizeSp, float widthPx) override;
        void SyncAtlas(AtlasSink& sink) override;

    private:
        JavaVM* m_Vm = nullptr;
        jobject m_Text = nullptr;
        jmethodID m_Layout, m_PageCount, m_PageBitmap, m_PageIsNew, m_TakeDirtyRows;
        bool m_Pending = false;   // a layout ran since the last sync
    };

}
