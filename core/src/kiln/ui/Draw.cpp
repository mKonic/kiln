#include "kiln/ui/Draw.h"

#include "kiln/QuadRenderer.h"
#include "kiln/TextBackend.h"

namespace kiln::ui {

    namespace {
        Rgba ToRgba(Color c) { return Rgba::FromArgb(c.argb); }
    }

    void DrawScope::DrawRect(Color color, Offset p, Size s) {
        if (color.Alpha() <= 0) return;
        m_Target.quads->FillRect({ m_Translate.x + p.x, m_Translate.y + p.y, s.width, s.height }, ToRgba(color));
    }

    void DrawScope::DrawRoundRect(Color color, Offset p, Size s, float r) {
        if (color.Alpha() <= 0) return;
        m_Target.quads->FillRect({ m_Translate.x + p.x, m_Translate.y + p.y, s.width, s.height }, ToRgba(color), r);
    }

    void DrawScope::DrawParagraph(const ParagraphData& para, Offset p, Color color) {
        const Rgba tint = ToRgba(color);
        for (const GlyphQuad& g : para.glyphs) {
            const AtlasPage* page = m_Target.page ? m_Target.page(g.page) : nullptr;
            if (!page) continue;
            m_Target.quads->DrawAtlas({ m_Translate.x + p.x + g.rect.x, m_Translate.y + p.y + g.rect.y, g.rect.w, g.rect.h },
                                      g.uv, *page, g.color ? Rgba{ tint.a, tint.a, tint.a, tint.a } : tint,
                                      g.color ? QuadRenderer::Mode::Color : QuadRenderer::Mode::TintedCoverage);
        }
    }

}
