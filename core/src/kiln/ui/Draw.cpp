#include "kiln/ui/Draw.h"

#include "kiln/PathCache.h"
#include "kiln/Gradients.h"
#include "kiln/Filters.h"
#include "kiln/ImageCache.h"
#include "kiln/graphics/Image.h"
#include "kiln/graphics/Brush.h"
#include "kiln/graphics/Path.h"
#include "kiln/graphics/PathEffect.h"
#include "kiln/graphics/Shape.h"

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

    namespace {
        gfx::Matrix Mul(const gfx::Matrix& x, const gfx::Matrix& y) {   // x * y (y applied first)
            return { x.a * y.a + x.c * y.b, x.b * y.a + x.d * y.b, x.a * y.c + x.c * y.d, x.b * y.c + x.d * y.d,
                     x.a * y.e + x.c * y.f + x.e, x.b * y.e + x.d * y.f + x.f };
        }
        gfx::Matrix Inverse(const gfx::Matrix& m) {
            const float det = m.a * m.d - m.b * m.c;
            if (std::abs(det) < 1e-12f) return {};
            const float i = 1 / det;
            return { m.d * i, -m.b * i, -m.c * i, m.a * i, (m.c * m.f - m.d * m.e) * i, (m.b * m.e - m.a * m.f) * i };
        }
        QuadRenderer::Transform ToQuad(const gfx::Matrix& m) { return { m.a, m.b, m.c, m.d, m.e, m.f }; }
    }

    void DrawScope::Concat(const gfx::Matrix& t) { m_State.m = Mul(m_State.m, t); }
    void DrawScope::Translate(Offset o) { Concat(gfx::Matrix::Translate(o.x, o.y)); }

    void DrawScope::Scale(float sx, float sy, Offset pivot) {
        Concat(gfx::Matrix::Translate(pivot.x, pivot.y));
        Concat(gfx::Matrix::Scale(sx, sy));
        Concat(gfx::Matrix::Translate(-pivot.x, -pivot.y));
    }

    void DrawScope::Rotate(float degrees, Offset pivot) {
        const float r = degrees * 3.14159265358979f / 180.0f, c = std::cos(r), s = std::sin(r);
        Concat(gfx::Matrix::Translate(pivot.x, pivot.y));
        Concat({ c, s, -s, c, 0, 0 });
        Concat(gfx::Matrix::Translate(-pivot.x, -pivot.y));
    }

    void DrawScope::ApplyTransform() { if (m_Target.quads) m_Target.quads->SetTransform(ToQuad(m_State.m)); }

    void DrawScope::ClipRect(Rect local, float radius) {
        const gfx::Matrix& m = m_State.m;
        const bool prevDevice = !m_State.clip || (m_State.clipInverse.a == 1 && m_State.clipInverse.b == 0 &&
                                                  m_State.clipInverse.c == 0 && m_State.clipInverse.d == 1 &&
                                                  m_State.clipInverse.e == 0 && m_State.clipInverse.f == 0);
        if (AxisAligned() && prevDevice) {
            const Offset p0 = m.Map({ local.left, local.top }), p1 = m.Map({ local.right, local.bottom });
            const Rect d{ std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.x, p1.x), std::max(p0.y, p1.y) };
            m_State.clipRect = m_State.clip ? m_State.clipRect.Intersect(d) : d;
            m_State.clipRadius = radius * std::min(std::abs(m.a), std::abs(m.d));
            m_State.clipInverse = {};
        } else {
            m_State.clipRect = local;
            m_State.clipRadius = radius;
            m_State.clipInverse = Inverse(m);
        }
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
        c.inverse = ToQuad(m_State.clipInverse);
        m_Target.quads->SetClip(c);
    }

    void DrawScope::DrawRect(Color color, Offset p, Size s) {
        if (color.Alpha() * m_State.alpha <= 0) return;
        ApplyTransform();
        m_Target.quads->FillRect({ p.x, p.y, s.width, s.height }, Scaled(ToRgba(color), m_State.alpha));
    }

    void DrawScope::DrawRoundRect(Color color, Offset p, Size s, float r) {
        if (color.Alpha() * m_State.alpha <= 0) return;
        ApplyTransform();
        m_Target.quads->FillRect({ p.x, p.y, s.width, s.height }, Scaled(ToRgba(color), m_State.alpha), r);
    }

    void DrawScope::DrawParagraph(const ParagraphData& para, Offset p, Color color) {
        const Rgba tint = Scaled(ToRgba(color), m_State.alpha);
        ApplyTransform();
        for (const GlyphQuad& g : para.glyphs) {
            const AtlasPage* page = m_Target.page ? m_Target.page(g.page) : nullptr;
            if (!page) continue;
            m_Target.quads->DrawAtlas({ p.x + g.rect.x, p.y + g.rect.y, g.rect.w, g.rect.h }, g.uv, *page,
                                      g.color ? Rgba{ tint.a, tint.a, tint.a, tint.a } : tint,
                                      g.color ? QuadRenderer::Mode::Color : QuadRenderer::Mode::TintedCoverage);
        }
    }

    void DrawScope::DrawPath(const gfx::Path& path, Color color, const gfx::Stroke* stroke) {
        if (color.Alpha() * m_State.alpha <= 0) return;
        PathCoverage(path, stroke, Scaled(ToRgba(color), m_State.alpha), nullptr);
    }

    const void* DrawScope::PathMask(const gfx::Path& path, const gfx::Stroke* stroke, float& outX, float& outY) {
        if (!m_Target.quads || !m_Target.paths || path.IsEmpty()) return nullptr;
        // Integer part of the translation is applied when drawing; the mask is built for the
        // scale and the quarter-pixel fraction so moving by whole pixels reuses it.
        const gfx::Matrix& cur = m_State.m;
        const float ox = std::floor(cur.e), oy = std::floor(cur.f);
        const float fx = std::round((cur.e - ox) * 4) / 4, fy = std::round((cur.f - oy) * 4) / 4;
        const gfx::Matrix m{ cur.a, cur.b, cur.c, cur.d, fx, fy };
        // Visible region relative to the integer origin; a mask clipped by it is position-keyed.
        // (A rotated clip is applied by the shader; only device-space clips shrink the mask.)
        ui::Rect visible{ -ox, -oy, m_Target.viewportWidth - ox, m_Target.viewportHeight - oy };
        if (m_State.clip && m_State.clipInverse.b == 0 && m_State.clipInverse.c == 0 && m_State.clipInverse.a == 1 && m_State.clipInverse.d == 1)
            visible = visible.Intersect(m_State.clipRect.Translate({ -ox, -oy }));
        if (visible.Empty()) return nullptr;

        std::uint64_t key = path.Hash();
        auto mix = [&key](const void* data, std::size_t n) {
            const auto* b = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) { key ^= b[i]; key *= 1099511628211ull; }
        };
        const float params[6] = { m.a, m.b, m.c, m.d, fx, fy };
        mix(params, sizeof(params));
        if (stroke) {
            const float sp[2] = { stroke->width, stroke->miter };
            const std::uint8_t cj[2] = { std::uint8_t(stroke->cap), std::uint8_t(stroke->join) };
            mix(sp, sizeof(sp));
            mix(cj, sizeof(cj));
            if (stroke->pathEffect) { const std::uint64_t e = stroke->pathEffect->Hash(); mix(&e, sizeof(e)); }
            key ^= 0x5354524Bull;
        }
        const ui::Rect bounds = path.Bounds();
        const float pad = stroke ? std::max(stroke->width, 1.0f) * std::max(stroke->miter, 1.0f) : 1.0f;
        ui::Rect device{ 1e30f, 1e30f, -1e30f, -1e30f };
        for (const Offset q : { Offset{ bounds.left, bounds.top }, Offset{ bounds.right, bounds.top },
                                Offset{ bounds.left, bounds.bottom }, Offset{ bounds.right, bounds.bottom } }) {
            const Offset d = m.Map(q);
            device = { std::min(device.left, d.x - pad), std::min(device.top, d.y - pad), std::max(device.right, d.x + pad), std::max(device.bottom, d.y + pad) };
        }
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
            if (mask.width <= 0) return nullptr;
            e = m_Target.paths->Insert(key, mask, 0, 0);
            if (!e) return nullptr;
        }
        outX = ox;
        outY = oy;
        return e;
    }

    void DrawScope::PathCoverage(const gfx::Path& path, const gfx::Stroke* stroke, Rgba tint, const void* paint, int filterRow) {
        float ox = 0, oy = 0;
        const auto* e = static_cast<const PathCache::Entry*>(PathMask(path, stroke, ox, oy));
        if (!e) return;
        ApplyClip();
        m_Target.quads->SetTransform({});   // masks are in device pixels
        m_Target.quads->DrawAtlas({ ox + float(e->x), oy + float(e->y), float(e->w), float(e->h) }, e->uv, *e->page,
                                  tint, QuadRenderer::Mode::TintedCoverage, static_cast<const QuadRenderer::Paint*>(paint), filterRow);
    }


    bool DrawScope::MakePaint(const gfx::Brush& b, float alpha, void* out, Color& solid, float& outAlpha) {
        outAlpha = alpha * m_State.alpha;
        if (b.kind == gfx::Brush::Kind::Solid || b.colors.size() < 2 || !m_Target.gradients) {
            solid = b.colors.empty() ? Color::Transparent() : b.colors[0];
            return false;
        }
        auto& p = *static_cast<QuadRenderer::Paint*>(out);
        float g[4];
        b.Resolve(m_Size, g);
        auto dev = [&](float x, float y) { return m_State.m.Map({ x, y }); };
        switch (b.kind) {
            case gfx::Brush::Kind::Linear: {
                const Offset a = dev(g[0], g[1]), c = dev(g[2], g[3]);
                p.kind = QuadRenderer::Paint::Kind::Linear;
                p.geometry[0] = a.x; p.geometry[1] = a.y; p.geometry[2] = c.x; p.geometry[3] = c.y;
                break;
            }
            case gfx::Brush::Kind::Radial: case gfx::Brush::Kind::Sweep: {
                const Offset c = dev(g[0], g[1]);
                p.kind = b.kind == gfx::Brush::Kind::Radial ? QuadRenderer::Paint::Kind::Radial : QuadRenderer::Paint::Kind::Sweep;
                p.geometry[0] = c.x; p.geometry[1] = c.y; p.geometry[2] = g[2] * std::sqrt(std::abs(m_State.m.a * m_State.m.d - m_State.m.b * m_State.m.c)); p.geometry[3] = 0;
                break;
            }
            default: break;
        }
        p.tile = std::uint8_t(b.tile);
        p.rampV = m_Target.gradients->RowFor(b);
        return true;
    }

    void DrawScope::DrawRect(const gfx::Brush& b, Offset topLeft, Size size, float alpha) {
        DrawRoundRect(b, topLeft, size, 0, alpha);
    }

    void DrawScope::DrawRoundRect(const gfx::Brush& b, Offset p, Size size, float r, float alpha) {
        if (!m_Target.quads) return;
        QuadRenderer::Paint paint;
        Color solid;
        float a;
        if (!MakePaint(b, alpha, &paint, solid, a)) {
            if (solid.Alpha() * a <= 0) return;
            ApplyTransform();
            m_Target.quads->FillRect({ p.x, p.y, size.width, size.height }, Scaled(ToRgba(solid), a), r);
            return;
        }
        if (a <= 0) return;
        ApplyTransform();
        m_Target.quads->FillRect({ p.x, p.y, size.width, size.height }, { a, a, a, a }, r, &paint);
    }

    int DrawScope::FilterRow(const gfx::ColorFilter* f) {
        return f && m_Target.filters ? m_Target.filters->RowFor(*f) : -1;
    }

    void DrawScope::DrawPath(const gfx::Path& path, const gfx::Brush& b, const gfx::Stroke* stroke, float alpha, const gfx::ColorFilter* filter) {
        QuadRenderer::Paint paint;
        Color solid;
        float a;
        if (!MakePaint(b, alpha, &paint, solid, a)) {
            if (solid.Alpha() * a <= 0) return;
            PathCoverage(path, stroke, Scaled(ToRgba(solid), a), nullptr, FilterRow(filter));
            return;
        }
        if (a <= 0) return;
        PathCoverage(path, stroke, { a, a, a, a }, &paint, FilterRow(filter));
    }

    void DrawScope::DrawImage(const gfx::ImageBitmap& image, Rect src, Offset dst, Size size, float alpha, const gfx::ColorFilter* filter) {
        if (!m_Target.quads || !m_Target.images || !image) return;
        const float a = alpha * m_State.alpha;
        if (a <= 0) return;
        const AtlasPage* page = m_Target.images->PageFor(image);
        if (!page) return;
        const float iw = float(image.Width()), ih = float(image.Height());
        ApplyTransform();
        m_Target.quads->DrawAtlas({ dst.x, dst.y, size.width, size.height },
                                  { src.left / iw, src.top / ih, src.Width() / iw, src.Height() / ih }, *page,
                                  { a, a, a, a }, QuadRenderer::Mode::Color, nullptr, FilterRow(filter));
    }

    void DrawScope::BeginLayer(Rect bounds, float alpha, const gfx::Path* clipPath) {
        Layer l{ m_State, alpha * m_State.alpha, {}, false, nullptr, m_State.m };
        if (clipPath) {
            l.clip = std::make_shared<gfx::Path>(*clipPath);
            bounds = bounds.Intersect(clipPath->Bounds());
        }
        // Device bounding box of the local bounds, within the viewport (and a device-space clip).
        Rect d{ 1e30f, 1e30f, -1e30f, -1e30f };
        for (const Offset q : { Offset{ bounds.left, bounds.top }, Offset{ bounds.right, bounds.top },
                                Offset{ bounds.left, bounds.bottom }, Offset{ bounds.right, bounds.bottom } }) {
            const Offset p = m_State.m.Map(q);
            d = { std::min(d.left, p.x), std::min(d.top, p.y), std::max(d.right, p.x), std::max(d.bottom, p.y) };
        }
        d = { std::floor(d.left), std::floor(d.top), std::ceil(d.right), std::ceil(d.bottom) };
        d = d.Intersect({ 0, 0, m_Target.viewportWidth, m_Target.viewportHeight });
        if (m_Target.quads && !d.Empty() && l.alpha > 0) {
            l.device = d;
            l.active = true;
            m_Target.quads->BeginLayer(int(d.left), int(d.top), int(d.Width()), int(d.Height()));
        }
        m_Layers.push_back(l);
        // Content draws opaque into the layer, clipped to its bounds.
        m_State.alpha = l.active ? 1.0f : 0.0f;
        ClipRect(bounds);
    }

    void DrawScope::EndLayerProjective(const float h[9]) {
        if (m_Layers.empty()) return;
        Layer l = m_Layers.back();
        m_Layers.pop_back();
        m_State = l.saved;
        ApplyClip();
        if (!l.active) return;
        const QuadRenderer::LayerResult r = m_Target.quads->EndLayer();
        if (!r.page) return;
        // The layer holds the content drawn under the parent transform P (device pixels);
        // device -> device is P * H * P^-1.
        const gfx::Matrix& p = l.saved.m;
        const double P[9] = { p.a, p.c, p.e, p.b, p.d, p.f, 0, 0, 1 };
        const double det = double(p.a) * p.d - double(p.b) * p.c;
        if (std::abs(det) < 1e-12) return;
        const double Pi[9] = { p.d / det, -p.c / det, (p.c * p.f - p.d * p.e) / det,
                               -p.b / det, p.a / det, (p.b * p.e - p.a * p.f) / det, 0, 0, 1 };
        auto mul = [](const double* x, const double* y, double* o) {
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) o[i * 3 + j] = x[i * 3] * y[j] + x[i * 3 + 1] * y[3 + j] + x[i * 3 + 2] * y[6 + j];
        };
        const double H[9] = { h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8] };
        double t[9], d[9];
        mul(P, H, t);
        mul(t, Pi, d);
        if (std::abs(d[8]) < 1e-12) return;
        for (double& v : d) v /= d[8];
        QuadRenderer::Transform x;
        x.a = float(d[0]); x.c = float(d[1]); x.e = float(d[2]);
        x.b = float(d[3]); x.d = float(d[4]); x.f = float(d[5]);
        x.g = float(d[6]); x.h = float(d[7]);
        const float a = l.alpha;
        const float u = l.device.Width() / float(r.page->Width()), v = l.device.Height() / float(r.page->Height());
        m_Target.quads->SetTransform(x);
        m_Target.quads->DrawAtlas({ l.device.left, l.device.top, l.device.Width(), l.device.Height() }, { 0, 0, u, v }, *r.page,
                                  { a, a, a, a }, QuadRenderer::Mode::Color);
        m_Target.quads->SetTransform({});
    }

    void DrawScope::EndLayer(float stretchX, float stretchY) {
        if (m_Layers.empty()) return;
        Layer l = m_Layers.back();
        m_Layers.pop_back();
        if (l.active && l.clip) {
            // Mask the layer to the path: dst *= coverage, under the layer's own transform.
            State inner = m_State;
            m_State = l.saved;
            m_State.clip = false;
            ApplyClip();
            float ox = 0, oy = 0;
            if (const auto* e = static_cast<const PathCache::Entry*>(PathMask(*l.clip, nullptr, ox, oy))) {
                m_Target.quads->SetTransform({});
                m_Target.quads->DrawMask({ ox + float(e->x), oy + float(e->y), float(e->w), float(e->h) }, e->uv, *e->page);
            }
            m_State = inner;
        }
        m_State = l.saved;
        ApplyClip();
        if (!l.active) return;
        const QuadRenderer::LayerResult r = m_Target.quads->EndLayer();
        if (!r.page) return;
        const float a = l.alpha;
        const float u = l.device.Width() / float(r.page->Width()), v = l.device.Height() / float(r.page->Height());
        m_Target.quads->SetTransform({});
        if (stretchX != 0 || stretchY != 0)
            m_Target.quads->DrawStretched({ l.device.left, l.device.top, l.device.Width(), l.device.Height() }, { 0, 0, u, v }, *r.page, a, stretchX, stretchY);
        else
            m_Target.quads->DrawAtlas({ l.device.left, l.device.top, l.device.Width(), l.device.Height() }, { 0, 0, u, v }, *r.page,
                                      { a, a, a, a }, QuadRenderer::Mode::Color);
    }

}
