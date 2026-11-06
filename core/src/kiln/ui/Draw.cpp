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

    void DrawScope::ApplyTransform() {
        if (!m_Target.quads) return;
        m_Target.quads->SetTransform(ToQuad(m_State.m));
        m_Target.quads->SetBlendMode(int(m_State.blend));
    }

    void DrawScope::ApplyBlend(gfx::BlendMode mode) { if (m_Target.quads) m_Target.quads->SetBlendMode(int(mode)); }

    namespace {
        // Signed distance to a rect rounded per corner (negative inside).
        float RoundedRectSdf(Offset p, const Rect& r, const ClipRadii& radii) {
            const float hx = std::max(0.0f, r.Width() / 2), hy = std::max(0.0f, r.Height() / 2);
            const float x = p.x - (r.left + r.right) / 2, y = p.y - (r.top + r.bottom) / 2;
            const float rr = std::min(x < 0 ? (y < 0 ? radii[0] : radii[3]) : (y < 0 ? radii[1] : radii[2]), std::min(hx, hy));
            const float qx = std::abs(x) - hx + rr, qy = std::abs(y) - hy + rr;
            return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rr;
        }
        bool InsideRounded(Offset p, const Rect& r, const ClipRadii& radii) { return RoundedRectSdf(p, r, radii) <= 0.01f; }
        bool SameMatrix(const gfx::Matrix& x, const gfx::Matrix& y) {
            auto eq = [](float u, float v) { return std::abs(u - v) <= 1e-4f * std::max(1.0f, std::max(std::abs(u), std::abs(v))); };
            return eq(x.a, y.a) && eq(x.b, y.b) && eq(x.c, y.c) && eq(x.d, y.d) && eq(x.e, y.e) && eq(x.f, y.f);
        }
        // Every corner of rect `r` (space `from`, mapped by `toOther`) is inside the rounded rect.
        bool RectInside(const Rect& r, const gfx::Matrix& toOther, const Rect& o, const ClipRadii& radii) {
            for (const Offset q : { Offset{ r.left, r.top }, Offset{ r.right, r.top }, Offset{ r.right, r.bottom }, Offset{ r.left, r.bottom } })
                if (!InsideRounded(toOther.Map(q), o, radii)) return false;
            return true;
        }
    }

    bool IntersectRoundedRects(const Rect& a, const ClipRadii& ra, const Rect& b, const ClipRadii& rb, Rect& out, ClipRadii& radii) {
        out = a.Intersect(b);
        radii = {};
        if (out.Empty()) return true;
        const float halfOut = std::min(out.Width(), out.Height()) / 2;
        const float halfA = std::min(a.Width(), a.Height()) / 2, halfB = std::min(b.Width(), b.Height()) / 2;
        for (int k = 0; k < 4; ++k) {
            const bool right = k == 1 || k == 2, bottom = k >= 2;
            const Offset p{ right ? out.right : out.left, bottom ? out.bottom : out.top };
            const bool ownA = (right ? a.right : a.left) == p.x && (bottom ? a.bottom : a.top) == p.y;
            const bool ownB = (right ? b.right : b.left) == p.x && (bottom ? b.bottom : b.top) == p.y;
            const float rA = std::min(ra[k], halfA), rB = std::min(rb[k], halfB);
            if (ownA && ownB) {
                // The larger rounding lies inside the smaller one.
                radii[k] = std::max(rA, rB);
            } else if (ownA || ownB) {
                // One rect's corner, whose arc must lie inside the other rounded rect.
                const float r = ownA ? rA : rB;
                const Rect& o = ownA ? b : a;
                const ClipRadii& oRadii = ownA ? rb : ra;
                const float sx = right ? -r : r, sy = bottom ? -r : r;
                if (!InsideRounded(p, o, oRadii) || !InsideRounded({ p.x + sx, p.y }, o, oRadii) || !InsideRounded({ p.x, p.y + sy }, o, oRadii))
                    return false;
                radii[k] = r;
            } else {
                // Straight edges of each crossing: square, unless a rounded corner cuts in.
                if (!InsideRounded(p, a, ra) || !InsideRounded(p, b, rb)) return false;
            }
            if (radii[k] > halfOut + 0.01f) return false;
        }
        return true;
    }

    void DrawScope::ClipRect(Rect local, float radius) {
        const gfx::Matrix& m = m_State.m;
        // The new clip in its own space: device pixels when axis-aligned, else local.
        Rect r = local;
        float rad = radius;
        gfx::Matrix inv;
        if (AxisAligned()) {
            const Offset p0 = m.Map({ local.left, local.top }), p1 = m.Map({ local.right, local.bottom });
            r = { std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.x, p1.x), std::max(p0.y, p1.y) };
            rad = radius * std::min(std::abs(m.a), std::abs(m.d));
        } else {
            inv = Inverse(m);
        }
        const ClipRadii radii{ rad, rad, rad, rad };
        auto set = [&](const Rect& cr, const ClipRadii& cradii, const gfx::Matrix& cinv) {
            m_State.clip = true;
            m_State.clipRect = cr;
            m_State.clipRadii = cradii;
            m_State.clipInverse = cinv;
            ApplyClip();
        };
        if (!m_State.clip) { set(r, radii, inv); return; }
        const State& old = m_State;
        if (SameMatrix(inv, old.clipInverse)) {
            Rect out;
            ClipRadii outRadii;
            if (IntersectRoundedRects(old.clipRect, old.clipRadii, r, radii, out, outRadii)) { set(out, outRadii, inv); return; }
        } else {
            // Different spaces combine only when one contains the other.
            if (RectInside(old.clipRect, Mul(inv, Inverse(old.clipInverse)), r, radii)) return;
            if (RectInside(r, Mul(old.clipInverse, Inverse(inv)), old.clipRect, old.clipRadii)) { set(r, radii, inv); return; }
        }
        // Not one rounded rect: content renders into a layer masked to the new clip, which is
        // composited under the old one.
        gfx::Path path;
        path.AddRoundRect(gfx::RoundRect::Uniform(local, radius));
        BeginLayerImpl(local, 1, &path, true);
    }

    void DrawScope::Restore() {
        if (m_Stack.empty()) return;
        while (!m_Layers.empty() && m_Layers.back().forClip && m_State.clipLayers > m_Stack.back().clipLayers) EndLayerPlain(0, 0);
        m_State = m_Stack.back();
        m_Stack.pop_back();
        ApplyClip();
    }

    void DrawScope::Finish() { while (!m_Layers.empty()) EndLayerPlain(0, 0); }

    void DrawScope::ApplyClip() {
        if (!m_Target.quads) return;
        QuadRenderer::Clip c;
        c.enabled = m_State.clip;
        c.x0 = m_State.clipRect.left; c.y0 = m_State.clipRect.top;
        c.x1 = m_State.clipRect.right; c.y1 = m_State.clipRect.bottom;
        for (int i = 0; i < 4; ++i) c.radii[i] = m_State.clipRadii[i];
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
        float pad = stroke ? std::max(stroke->width, 1.0f) * std::max(stroke->miter, 1.0f) : 1.0f;
        if (stroke && stroke->pathEffect) pad += stroke->pathEffect->Outset() * std::max(1.0f, m.MaxScale());
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
        ApplyBlend(m_State.blend);
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

    void DrawScope::DrawImage(const gfx::ImageBitmap& image, Rect src, Offset dst, Size size, float alpha, const gfx::ColorFilter* filter,
                              gfx::FilterQuality quality) {
        if (!m_Target.quads || !m_Target.images || !image) return;
        const float a = alpha * m_State.alpha;
        if (a <= 0) return;
        const AtlasPage* page = m_Target.images->PageFor(image);
        if (!page) return;
        const float iw = float(image.Width()), ih = float(image.Height());
        ApplyTransform();
        m_Target.quads->DrawAtlas({ dst.x, dst.y, size.width, size.height },
                                  { src.left / iw, src.top / ih, src.Width() / iw, src.Height() / ih }, *page,
                                  { a, a, a, a }, QuadRenderer::Mode::Color, nullptr, FilterRow(filter), quality == gfx::FilterQuality::None);
    }

    void DrawScope::DrawVertices(const gfx::Vertices& v, Color color, float alpha) {
        if (!m_Target.quads) return;
        const float a = alpha * m_State.alpha;
        if (a <= 0) return;
        const std::vector<std::uint32_t> list = v.TriangleList();
        std::vector<QuadRenderer::Vertex> out;
        out.reserve(list.size());
        const gfx::Matrix& m = m_State.m;
        for (std::uint32_t i : list) {
            if (i >= v.positions.size()) return;
            const Offset p = m.Map(v.positions[i]);
            const Rgba c = Scaled(ToRgba(v.colors.empty() ? color : v.colors[std::min<std::size_t>(i, v.colors.size() - 1)]), a);
            out.push_back({ p.x, p.y, c.r, c.g, c.b, c.a });
        }
        ApplyClip();
        m_Target.quads->DrawTriangles(out.data(), out.size());
    }

    void DrawScope::BeginLayer(Rect bounds, float alpha, const gfx::Path* clipPath, gfx::BlendMode blend, const gfx::ColorFilter* colorFilter) {
        BeginLayerImpl(bounds, alpha, clipPath, false, blend, colorFilter);
    }

    void DrawScope::BeginLayerImpl(Rect bounds, float alpha, const gfx::Path* clipPath, bool forClip, gfx::BlendMode blend,
                                   const gfx::ColorFilter* colorFilter) {
        // A clip's layer composites at full alpha; its content keeps drawing with the current one.
        // It also carries the enclosing blend mode to its composite.
        Layer l{ m_State, forClip ? 1.0f : alpha * m_State.alpha, {}, false, nullptr, m_State.m, forClip,
                 forClip ? m_State.blend : blend, colorFilter ? std::make_shared<gfx::ColorFilter>(*colorFilter) : nullptr };
        if (clipPath) {
            l.clip = std::make_shared<gfx::Path>(*clipPath);
            bounds = bounds.Intersect(clipPath->Bounds());
        }
        // Device bounding box of the local bounds, within the viewport and the device-space clip.
        Rect d{ 1e30f, 1e30f, -1e30f, -1e30f };
        for (const Offset q : { Offset{ bounds.left, bounds.top }, Offset{ bounds.right, bounds.top },
                                Offset{ bounds.left, bounds.bottom }, Offset{ bounds.right, bounds.bottom } }) {
            const Offset p = m_State.m.Map(q);
            d = { std::min(d.left, p.x), std::min(d.top, p.y), std::max(d.right, p.x), std::max(d.bottom, p.y) };
        }
        d = { std::floor(d.left), std::floor(d.top), std::ceil(d.right), std::ceil(d.bottom) };
        d = d.Intersect({ 0, 0, m_Target.viewportWidth, m_Target.viewportHeight });
        if (m_State.clip && SameMatrix(m_State.clipInverse, {}))
            d = d.Intersect({ std::floor(m_State.clipRect.left), std::floor(m_State.clipRect.top),
                              std::ceil(m_State.clipRect.right), std::ceil(m_State.clipRect.bottom) });
        if (m_Target.quads && !d.Empty() && l.alpha > 0 && m_State.alpha > 0) {
            l.device = d;
            l.active = true;
            m_Target.quads->BeginLayer(int(d.left), int(d.top), int(d.Width()), int(d.Height()));
        }
        m_Layers.push_back(l);
        // Content draws opaque into the layer, clipped to its bounds; the enclosing clip applies
        // when the layer is composited.
        m_State.alpha = l.active ? (forClip ? l.saved.alpha : 1.0f) : 0.0f;
        m_State.clip = false;
        m_State.blend = gfx::BlendMode::SrcOver;
        if (forClip) ++m_State.clipLayers;
        ClipRect(bounds);
    }

    void DrawScope::EndLayerProjective(const float h[9]) {
        EndClipLayers();
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
        CompositeLayer(l, { l.device.left, l.device.top, l.device.Width(), l.device.Height() }, { 0, 0, u, v }, *r.page);
        m_Target.quads->SetTransform({});
    }

    void DrawScope::MaskLayer(const Layer& l) {
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
    }

    void DrawScope::EndLayerEffects(const std::vector<LayerBlur>& blurs, Offset offset) {
        EndClipLayers();
        if (m_Layers.empty()) return;
        Layer l = m_Layers.back();
        m_Layers.pop_back();
        MaskLayer(l);
        m_State = l.saved;
        if (!l.active) { ApplyClip(); return; }
        QuadRenderer& q = *m_Target.quads;
        QuadRenderer::LayerResult r = q.EndLayer();
        if (!r.page) { ApplyClip(); return; }
        const gfx::Matrix& m = l.saved.m;
        const kiln::RectF rect{ l.device.left, l.device.top, l.device.Width(), l.device.Height() };
        const int x = int(l.device.left), y = int(l.device.top), w = int(l.device.Width()), h = int(l.device.Height());
        auto uvOf = [&](const QuadRenderer::LayerResult& lr) {
            return kiln::RectF{ 0, 0, rect.w / float(lr.page->Width()), rect.h / float(lr.page->Height()) };
        };
        // Each blur is a horizontal then a vertical pass, into layers over the same device
        // pixels, unclipped. Sigmas are local; the passes run in device pixels.
        q.SetTransform({});
        q.SetClip({});
        for (const LayerBlur& b : blurs)
            for (int pass = 0; pass < 2; ++pass) {
                const float sigma = pass == 0 ? b.sigmaX * std::hypot(m.a, m.b) : b.sigmaY * std::hypot(m.c, m.d);
                if (sigma <= 0) continue;
                q.BeginLayer(x, y, w, h);
                const float su = pass == 0 ? 1.0f / float(r.page->Width()) : 0, sv = pass == 0 ? 0 : 1.0f / float(r.page->Height());
                q.DrawBlur(rect, uvOf(r), *r.page, 1, su, sv, sigma, b.decal);
                r = q.EndLayer();
                if (!r.page) { ApplyClip(); return; }
            }
        ApplyClip();
        // An offset commutes with the blurs (the image's edges move with it).
        const Offset d{ m.a * offset.x + m.c * offset.y, m.b * offset.x + m.d * offset.y };
        q.SetTransform({ 1, 0, 0, 1, d.x, d.y });
        CompositeLayer(l, rect, uvOf(r), *r.page);
        q.SetTransform({});
    }

    void DrawScope::EndLayer(float stretchX, float stretchY) {
        EndClipLayers();
        EndLayerPlain(stretchX, stretchY);
    }

    void DrawScope::EndLayerPlain(float stretchX, float stretchY) {
        if (m_Layers.empty()) return;
        Layer l = m_Layers.back();
        m_Layers.pop_back();
        MaskLayer(l);
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
            CompositeLayer(l, { l.device.left, l.device.top, l.device.Width(), l.device.Height() }, { 0, 0, u, v }, *r.page);
    }

    void DrawScope::CompositeLayer(const Layer& l, const kiln::RectF& rect, const kiln::RectF& uv, const AtlasPage& page) {
        const float a = l.alpha;
        ApplyBlend(l.blend);
        m_Target.quads->DrawAtlas(rect, uv, page, { a, a, a, a }, QuadRenderer::Mode::Color, nullptr, FilterRow(l.filter.get()));
        ApplyBlend(m_State.blend);
    }

}
