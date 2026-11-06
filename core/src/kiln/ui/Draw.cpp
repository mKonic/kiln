#include "kiln/ui/Draw.h"

#include "kiln/QuadRenderer.h"
#include "kiln/TextBackend.h"

#include <algorithm>

namespace kiln::ui {

    namespace {
        Rgba ToRgba(Color c) { return Rgba::FromArgb(c.argb); }
    }

    namespace {
        Rgba Scaled(Rgba c, float a) { return { c.r * a, c.g * a, c.b * a, c.a * a }; }
    }

    void DrawScope::Scale(float sx, float sy, Offset pivot) {
        // Keep `pivot` fixed: origin' + pivot * scale' = origin + pivot * scale.
        m_State.origin = { m_State.origin.x + pivot.x * m_State.sx * (1 - sx), m_State.origin.y + pivot.y * m_State.sy * (1 - sy) };
        m_State.sx *= sx;
        m_State.sy *= sy;
    }

    void DrawScope::ClipRect(Rect local, float radius) {
        const Rect d = ToDevice({ local.left, local.top }, { local.Width(), local.Height() });
        m_State.clipRect = m_State.clip ? m_State.clipRect.Intersect(d) : d;
        m_State.clipRadius = radius * std::min(m_State.sx, m_State.sy);
        m_State.clip = true;
        ApplyClip();
    }

    void DrawScope::Restore() {
        if (m_Stack.empty()) return;
        m_State = m_Stack.back();
        m_Stack.pop_back();
        ApplyClip();
    }

    void DrawScope::ApplyClip() {
        QuadRenderer::Clip c;
        c.enabled = m_State.clip;
        c.x0 = m_State.clipRect.left; c.y0 = m_State.clipRect.top;
        c.x1 = m_State.clipRect.right; c.y1 = m_State.clipRect.bottom;
        c.radius = m_State.clipRadius;
        m_Target.quads->SetClip(c);
    }

    void DrawScope::DrawRect(Color color, Offset p, Size s) {
        if (color.Alpha() * m_State.alpha <= 0) return;
        const Rect d = ToDevice(p, s);
        m_Target.quads->FillRect({ d.left, d.top, d.Width(), d.Height() }, Scaled(ToRgba(color), m_State.alpha));
    }

    void DrawScope::DrawRoundRect(Color color, Offset p, Size s, float r) {
        if (color.Alpha() * m_State.alpha <= 0) return;
        const Rect d = ToDevice(p, s);
        m_Target.quads->FillRect({ d.left, d.top, d.Width(), d.Height() }, Scaled(ToRgba(color), m_State.alpha),
                                 r * std::min(m_State.sx, m_State.sy));
    }

    void DrawScope::DrawParagraph(const ParagraphData& para, Offset p, Color color) {
        const Rgba tint = Scaled(ToRgba(color), m_State.alpha);
        for (const GlyphQuad& g : para.glyphs) {
            const AtlasPage* page = m_Target.page ? m_Target.page(g.page) : nullptr;
            if (!page) continue;
            const Rect d = ToDevice({ p.x + g.rect.x, p.y + g.rect.y }, { g.rect.w, g.rect.h });
            m_Target.quads->DrawAtlas({ d.left, d.top, d.Width(), d.Height() }, g.uv, *page,
                                      g.color ? Rgba{ tint.a, tint.a, tint.a, tint.a } : tint,
                                      g.color ? QuadRenderer::Mode::Color : QuadRenderer::Mode::TintedCoverage);
        }
    }

}
