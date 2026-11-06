#include "kiln/ui/Draw.h"

#include "kiln/PathCache.h"
#include "kiln/graphics/Path.h"

#include <cmath>

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

    void DrawScope::DrawPath(const gfx::Path& path, Color color, const gfx::Stroke* stroke) {
        if (!m_Target.quads || !m_Target.paths || path.IsEmpty() || color.Alpha() * m_State.alpha <= 0) return;
        // Integer part of the translation is applied when drawing; the mask is built for the
        // scale and the quarter-pixel fraction so moving by whole pixels reuses it.
        const float ox = std::floor(m_State.origin.x), oy = std::floor(m_State.origin.y);
        const float fx = std::round((m_State.origin.x - ox) * 4) / 4, fy = std::round((m_State.origin.y - oy) * 4) / 4;
        const gfx::Matrix m{ m_State.sx, 0, 0, m_State.sy, fx, fy };
        // Visible region relative to the integer origin; a mask clipped by it is position-keyed.
        ui::Rect visible{ -ox, -oy, m_Target.viewportWidth - ox, m_Target.viewportHeight - oy };
        if (m_State.clip) visible = visible.Intersect(m_State.clipRect.Translate({ -ox, -oy }));
        if (visible.Empty()) return;

        std::uint64_t key = path.Hash();
        auto mix = [&key](const void* data, std::size_t n) {
            const auto* b = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) { key ^= b[i]; key *= 1099511628211ull; }
        };
        const float params[4] = { m_State.sx, m_State.sy, fx, fy };
        mix(params, sizeof(params));
        if (stroke) { mix(stroke, sizeof(gfx::Stroke)); key ^= 0x5354524Bull; }
        const ui::Rect bounds = path.Bounds();
        const float pad = stroke ? std::max(stroke->width, 1.0f) * std::max(stroke->miter, 1.0f) : 1.0f;
        const ui::Rect device{ bounds.left * m.a + m.e - pad, bounds.top * m.d + m.f - pad,
                               bounds.right * m.a + m.e + pad, bounds.bottom * m.d + m.f + pad };
        const bool clipped = device.left < visible.left || device.top < visible.top ||
                             device.right > visible.right || device.bottom > visible.bottom;
        if (clipped) {
            const float pos[6] = { ox, oy, visible.left, visible.top, visible.right, visible.bottom };
            mix(pos, sizeof(pos));
        }

        const PathCache::Entry* e = m_Target.paths->Find(key);
        if (!e) {
            const std::vector<gfx::Polyline> polys = stroke ? gfx::StrokeToPolygons(path, *stroke, m) : path.Flatten(m);
            const gfx::Mask mask = gfx::Rasterize(polys, stroke ? gfx::FillType::NonZero : path.fillType, visible);
            if (mask.width <= 0) return;
            e = m_Target.paths->Insert(key, mask, 0, 0);
            if (!e) return;
        }
        ApplyClip();
        m_Target.quads->DrawAtlas({ ox + float(e->x), oy + float(e->y), float(e->w), float(e->h) }, e->uv, *e->page,
                                  Scaled(ToRgba(color), m_State.alpha), QuadRenderer::Mode::TintedCoverage);
    }

}
