#include "kiln/App.h"
#include <kiln/Function.h>

#include <cmath>
#include <kiln/kiln.hpp>

using namespace kiln;

extern "C" {

KILN_API void kiln_app_set_callbacks(kiln_app* app, const kiln_app_callbacks* callbacks) {
    if (app && callbacks) app->impl->SetCallbacks(*callbacks);
}
KILN_API void kiln_app_request_frame(kiln_app* app) { if (app) app->impl->RequestFrame(); }
KILN_API float kiln_app_density(const kiln_app* app) { return app ? app->impl->Density() : 1.0f; }
KILN_API float kiln_app_font_scale(const kiln_app* app) { return app ? app->impl->FontScale() : 1.0f; }

KILN_API void kiln_frame_size(const kiln_frame* frame, float* width, float* height) {
    if (width) *width = float(frame->app->Width());
    if (height) *height = float(frame->app->Height());
}
KILN_API void kiln_frame_clear(kiln_frame* frame, uint32_t argb) {
    frame->app->SetClearColor(Rgba::FromArgb(argb));
}
KILN_API void kiln_frame_fill_rect(kiln_frame* frame, float x, float y, float w, float h, uint32_t argb,
                                   float radius) {
    frame->app->Quads().FillRect({ x, y, w, h }, Rgba::FromArgb(argb), radius);
}
KILN_API void kiln_frame_draw_paragraph(kiln_frame* frame, const kiln_paragraph* p, float x, float y,
                                        uint32_t argb) {
    if (!p) return;
    // Pages a backend reported this frame may not exist until SyncAtlas runs; sync first.
    App& app = *frame->app;
    if (app.Text()) app.Text()->SyncAtlas(app);
    const Rgba tint = Rgba::FromArgb(argb);
    for (const GlyphQuad& g : p->data.glyphs) {
        const AtlasPage* page = app.Page(g.page);
        if (!page) continue;
        // Whole pixels, as DrawScope::DrawParagraph places glyphs.
        const RectF rect{ std::floor(x + g.rect.x + 0.5f), std::floor(y + g.rect.y + 0.5f), g.rect.w, g.rect.h };
        if (g.color) app.Quads().DrawAtlas(rect, g.uv, *page, Rgba{ tint.a, tint.a, tint.a, tint.a }, QuadRenderer::Mode::Color);
        else app.Quads().DrawGlyph(rect, g.uv, *page, tint, p->data.maskGamma);
    }
}

KILN_API kiln_paragraph* kiln_text_layout(kiln_app* app, const char* utf8, float sizeSp, float widthPx) {
    if (!app || !utf8 || !app->impl->Text()) return nullptr;
    const ui::Density d{ app->impl->Density(), app->impl->FontScale() };
    return new kiln_paragraph{ app->impl->Text()->LayoutPlain(utf8, d.SpToPx(sizeSp), widthPx) };
}
KILN_API float kiln_paragraph_width(const kiln_paragraph* p) { return p ? p->data.width : 0; }
KILN_API float kiln_paragraph_height(const kiln_paragraph* p) { return p ? p->data.height : 0; }
KILN_API int kiln_paragraph_line_count(const kiln_paragraph* p) { return p ? p->data.lineCount : 0; }
KILN_API void kiln_paragraph_free(kiln_paragraph* p) { delete p; }

}

namespace kiln {
    void SetContent(kiln_app* app, kiln::Function<void()> content) {
        if (app) app->impl->SetContent(std::move(content));
    }
}
