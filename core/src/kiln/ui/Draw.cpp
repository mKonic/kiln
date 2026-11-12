#include "kiln/ui/Draw.h"

#include <cstring>

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
#include <optional>

namespace kiln::ui {

    namespace {
        // A copy of an optional argument, for a recorded command.
        template<typename T> std::optional<T> Opt(const T* p) { return p ? std::optional<T>(*p) : std::nullopt; }
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

    void DrawScope::Concat(const gfx::Matrix& t) {
        if (m_Recording) m_Recording->push_back([t](DrawScope& d) { d.Concat(t); });
        m_State.m = Mul(m_State.m, t);
    }

    void DrawScope::BeginRecording(DisplayList& list) {
        m_Recordings.push_back({ m_Recording, m_State, std::move(m_Stack) });
        m_Recording = &list;
        m_State = State{};
        m_Stack.clear();
    }

    void DrawScope::EndRecording() {
        if (m_Recordings.empty()) return;
        Recording r = std::move(m_Recordings.back());
        m_Recordings.pop_back();
        m_Recording = r.list;
        m_State = r.state;
        m_Stack = std::move(r.stack);
    }

    void DrawScope::Record(std::function<void(DrawScope&)> op) {
        if (m_Recording) m_Recording->push_back(std::move(op));
        else op(*this);
    }

    void DrawScope::Replay(const DisplayList& list) {
        Save();
        for (const auto& op : list) op(*this);
        Restore();
    }
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
        }
        // Each side's two corners fit along it (the clip shader rounds each corner in its own box).
        const float w = out.Width() + 0.01f, h = out.Height() + 0.01f;
        return radii[0] + radii[1] <= w && radii[3] + radii[2] <= w && radii[0] + radii[3] <= h && radii[1] + radii[2] <= h;
    }

    void DrawScope::ClipRect(Rect local, float radius) {
        if (m_Recording) { m_Recording->push_back([local, radius](DrawScope& d) { d.ClipRect(local, radius); }); return; }
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
        // A plain device rect on either side: the rounded one stays the clip and the rect clips
        // too, unless one is already there.
        const bool noOuter = old.clipOuter.left <= -1e8f && old.clipOuter.right >= 1e8f && old.clipOuter.top <= -1e8f && old.clipOuter.bottom >= 1e8f;
        const auto plainDevice = [](const Rect&, const ClipRadii& cr, const gfx::Matrix& ci) {
            return cr[0] == 0 && cr[1] == 0 && cr[2] == 0 && cr[3] == 0 && SameMatrix(ci, {});
        };
        if (plainDevice(r, radii, inv)) {
            m_State.clipOuter = old.clipOuter.Intersect(r);
            ApplyClip();
            return;
        }
        if (noOuter && plainDevice(old.clipRect, old.clipRadii, old.clipInverse)) {
            const Rect outer = old.clipRect;
            set(r, radii, inv);
            m_State.clipOuter = outer;
            ApplyClip();
            return;
        }
        // Otherwise content renders into a layer masked to the new clip, which is composited
        // under the old one.
        gfx::Path path;
        path.AddRoundRect(gfx::RoundRect::Uniform(local, radius));
        BeginLayerImpl(local, 1, &path, true);
    }

    void DrawScope::Restore() {
        if (m_Recording) {
            m_Recording->push_back([](DrawScope& d) { d.Restore(); });
            if (!m_Stack.empty()) { m_State = m_Stack.back(); m_Stack.pop_back(); }
            return;
        }
        if (m_Stack.empty()) return;
        while (!m_Layers.empty() && m_Layers.back().forClip && m_State.clipLayers > m_Stack.back().clipLayers) EndLayerPlain(0, 0);
        m_State = m_Stack.back();
        m_Stack.pop_back();
        ApplyClip();
    }

    void DrawScope::Finish() { if (m_Recording) return; while (!m_Layers.empty()) EndLayerPlain(0, 0); }

    void DrawScope::ApplyClip() {
        if (!m_Target.quads) return;
        QuadRenderer::Clip c;
        c.enabled = m_State.clip;
        c.x0 = m_State.clipRect.left; c.y0 = m_State.clipRect.top;
        c.x1 = m_State.clipRect.right; c.y1 = m_State.clipRect.bottom;
        for (int i = 0; i < 4; ++i) c.radii[i] = m_State.clipRadii[i];
        c.inverse = ToQuad(m_State.clipInverse);
        c.outer[0] = m_State.clipOuter.left; c.outer[1] = m_State.clipOuter.top;
        c.outer[2] = m_State.clipOuter.right; c.outer[3] = m_State.clipOuter.bottom;
        m_Target.quads->SetClip(c);
    }

    void DrawScope::DrawRect(Color color, Offset p, Size s) {
        if (m_Recording) { m_Recording->push_back([=](DrawScope& d) { d.DrawRect(color, p, s); }); return; }
        if (!m_Target.quads) return;   // a headless draw (tests): nothing to record
        if (color.Alpha() * m_State.alpha <= 0) return;
        ApplyTransform();
        m_Target.quads->FillRect({ p.x, p.y, s.width, s.height }, Scaled(ToRgba(color), m_State.alpha));
    }

    void DrawScope::DrawRoundRect(Color color, Offset p, Size s, float r) {
        if (m_Recording) { m_Recording->push_back([=](DrawScope& d) { d.DrawRoundRect(color, p, s, r); }); return; }
        if (!m_Target.quads) return;   // a headless draw (tests): nothing to record
        if (color.Alpha() * m_State.alpha <= 0) return;
        ApplyTransform();
        m_Target.quads->FillRect({ p.x, p.y, s.width, s.height }, Scaled(ToRgba(color), m_State.alpha), r);
    }

    void DrawScope::DrawParagraph(const ParagraphData& para, Offset p, Color color) { DrawParagraph(para, p, color, {}); }

    void DrawScope::DrawParagraph(const ParagraphData& para, Offset p, Color color, std::span<const Color> runColors) {
        if (m_Recording) {
            auto copy = std::make_shared<ParagraphData>(para);
            copy->handle = 0;   // the copy only draws
            std::vector<Color> colors(runColors.begin(), runColors.end());
            m_Recording->push_back([copy, p, color, colors](DrawScope& d) { d.DrawParagraph(*copy, p, color, colors); });
            return;
        }
        if (!m_Target.quads) return;   // a headless draw (tests): nothing to record
        ApplyTransform();
        // TextMotion.Static (no subpixel positioning): Skia puts each glyph on a whole device
        // pixel. Under a translation that is a whole-pixel shift of the glyph's quad; scaled or
        // rotated text is drawn where it falls.
        const gfx::Matrix& m = m_State.m;
        const bool snap = m.a == 1 && m.d == 1 && m.b == 0 && m.c == 0;
        for (const GlyphQuad& g : para.glyphs) {
            float x = p.x + g.rect.x, y = p.y + g.rect.y;
            if (snap) {
                x = std::floor(x + m.e + 0.5f) - m.e;
                y = std::floor(y + m.f + 0.5f) - m.f;
            }
            Color c = color;
            if (g.run >= 0 && std::size_t(g.run) < runColors.size() && runColors[std::size_t(g.run)].Specified()) c = runColors[std::size_t(g.run)];
            if (!c.Specified() || c.Alpha() * m_State.alpha <= 0) continue;
            const AtlasPage* page = m_Target.page ? m_Target.page(g.page) : nullptr;
            if (!page) continue;
            const Rgba tint = Scaled(ToRgba(c), m_State.alpha);
            if (g.color) m_Target.quads->DrawAtlas({ x, y, g.rect.w, g.rect.h }, g.uv, *page, Rgba{ tint.a, tint.a, tint.a, tint.a }, QuadRenderer::Mode::Color);
            else m_Target.quads->DrawGlyph({ x, y, g.rect.w, g.rect.h }, g.uv, *page, tint, para.maskGamma);
        }
    }

    void DrawScope::DrawPath(const gfx::Path& path, Color color, const gfx::Stroke* stroke) {
        if (m_Recording) { m_Recording->push_back([path, color, st = Opt(stroke)](DrawScope& d) { d.DrawPath(path, color, st ? &*st : nullptr); }); return; }
        if (color.Alpha() * m_State.alpha <= 0) return;
        PathCoverage(path, stroke, Scaled(ToRgba(color), m_State.alpha), nullptr);
    }

    const void* DrawScope::PathMask(const gfx::Path& path, const gfx::Stroke* stroke, float& outX, float& outY, const Rect* deviceVisible) {
        if (!m_Target.quads || !m_Target.paths || path.IsEmpty()) return nullptr;
        // Integer part of the translation is applied when drawing; the mask is built for the
        // scale and the quarter-pixel fraction so moving by whole pixels reuses it.
        const gfx::Matrix& cur = m_State.m;
        const float ox = std::floor(cur.e), oy = std::floor(cur.f);
        const float fx = std::round((cur.e - ox) * 4) / 4, fy = std::round((cur.f - oy) * 4) / 4;
        const gfx::Matrix m{ cur.a, cur.b, cur.c, cur.d, fx, fy };
        // Visible region relative to the integer origin; a mask clipped by it is position-keyed.
        // (A rotated clip is applied by the shader; only device-space clips shrink the mask.)
        ui::Rect visible = deviceVisible ? deviceVisible->Translate({ -ox, -oy })
                                        : ui::Rect{ -ox, -oy, m_Target.viewportWidth - ox, m_Target.viewportHeight - oy };
        if (m_State.clip && m_State.clipInverse.b == 0 && m_State.clipInverse.c == 0 && m_State.clipInverse.a == 1 && m_State.clipInverse.d == 1)
            visible = visible.Intersect(m_State.clipRect.Translate({ -ox, -oy }));
        if (m_State.clip) visible = visible.Intersect(m_State.clipOuter.Translate({ -ox, -oy }));
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
        if (!m_Target.quads) return;   // a headless draw (tests): nothing to record
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
        if (m_Recording) { m_Recording->push_back([=](DrawScope& d) { d.DrawRect(b, topLeft, size, alpha); }); return; }
        if (b.IsLayered()) {
            const Rect r{ topLeft.x, topLeft.y, topLeft.x + size.width, topLeft.y + size.height };
            FillLayered([&] { DrawRect(Color(0xFFFFFFFFu), topLeft, size); }, r, b, alpha, nullptr);
            return;
        }
        DrawRoundRect(b, topLeft, size, 0, alpha);
    }

    void DrawScope::DrawRoundRect(const gfx::Brush& b, Offset p, Size size, float r, float alpha) {
        if (m_Recording) { m_Recording->push_back([=](DrawScope& d) { d.DrawRoundRect(b, p, size, r, alpha); }); return; }
        if (b.IsLayered()) {
            const Rect bounds{ p.x, p.y, p.x + size.width, p.y + size.height };
            FillLayered([&] { DrawRoundRect(Color(0xFFFFFFFFu), p, size, r); }, bounds, b, alpha, nullptr);
            return;
        }
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

    void DrawScope::FillLayered(const std::function<void()>& coverage, Rect bounds, const gfx::Brush& brush, float alpha, const gfx::ColorFilter* filter) {
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
        BeginLayer(bounds, alpha, nullptr, gfx::BlendMode::SrcOver, filter);
        coverage();
        Save();
        SetBlendMode(gfx::BlendMode::SrcIn);
        PaintBrush(bounds, brush);
        Restore();
        EndLayer();
    }

    void DrawScope::PaintBrush(Rect bounds, const gfx::Brush& brush) {
        switch (brush.kind) {
            case gfx::Brush::Kind::Shader:
                if (brush.shader && *brush.shader) PaintBrush(bounds, (*brush.shader)(m_Size));
                return;
            case gfx::Brush::Kind::Composite: {
                // CompositeShader: dst, then src blended onto it, as one image.
                BeginLayer(bounds, 1);
                if (brush.dst) PaintBrush(bounds, *brush.dst);
                Save();
                SetBlendMode(brush.blend);
                if (brush.src) PaintBrush(bounds, *brush.src);
                Restore();
                EndLayer();
                return;
            }
            case gfx::Brush::Kind::Image:
                PaintImageTiles(bounds, brush);
                return;
            default:
                DrawRect(brush, { bounds.left, bounds.top }, { bounds.Width(), bounds.Height() });
                return;
        }
    }

    namespace {
        // One axis of an image shader across [lo, hi): pieces of the image (src span, in image
        // pixels) placed over spans of the axis, flipped for the mirrored tiles.
        struct TileSpan { float dst0, dst1, src0, src1; bool flip; };
        std::vector<TileSpan> TileSpans(gfx::TileMode mode, float lo, float hi, float n) {
            std::vector<TileSpan> out;
            if (n <= 0 || hi <= lo) return out;
            switch (mode) {
                case gfx::TileMode::Decal:
                    if (hi > 0 && lo < n) out.push_back({ 0, n, 0, n, false });
                    break;
                case gfx::TileMode::Clamp:
                    // The edge pixels stretch past the image.
                    if (lo < 0) out.push_back({ lo, 0, 0, 1, false });
                    if (hi > 0 && lo < n) out.push_back({ 0, n, 0, n, false });
                    if (hi > n) out.push_back({ n, hi, n - 1, n, false });
                    break;
                case gfx::TileMode::Repeated:
                case gfx::TileMode::Mirror:
                    for (long i = long(std::floor(lo / n)); float(i) * n < hi; ++i)
                        out.push_back({ float(i) * n, float(i + 1) * n, 0, n, mode == gfx::TileMode::Mirror && (i & 1) != 0 });
                    break;
            }
            return out;
        }
        // A small image repeated (mirrored as the tile modes say) into one at least `min` px on
        // each side, so filling a large area takes few quads.
        gfx::ImageBitmap Enlarge(const gfx::ImageBitmap& img, gfx::TileMode mx, gfx::TileMode my, int repX, int repY) {
            const int w = img.Width(), h = img.Height();
            const int W = w * repX, H = h * repY;
            std::vector<std::uint8_t> out(std::size_t(W) * std::size_t(H) * 4);
            const std::uint8_t* px = img.Pixels();
            for (int y = 0; y < H; ++y) {
                const int ty = y / h;
                int sy = y % h;
                if (my == gfx::TileMode::Mirror && (ty & 1)) sy = h - 1 - sy;
                for (int x = 0; x < W; ++x) {
                    const int tx = x / w;
                    int sx = x % w;
                    if (mx == gfx::TileMode::Mirror && (tx & 1)) sx = w - 1 - sx;
                    std::memcpy(&out[(std::size_t(y) * std::size_t(W) + std::size_t(x)) * 4], px + (std::size_t(sy) * std::size_t(w) + std::size_t(sx)) * 4, 4);
                }
            }
            return gfx::ImageBitmap::FromPixels(W, H, out, true);
        }
    }

    void DrawScope::PaintImageTiles(Rect bounds, const gfx::Brush& brush) {
        gfx::ImageBitmap image = brush.image;
        if (!image) return;
        float w = float(image.Width()), h = float(image.Height());
        // Many small tiles: enlarge the repeated image first (an even count keeps mirroring right).
        auto reps = [](gfx::TileMode m, float extent, float n) {
            if (m != gfx::TileMode::Repeated && m != gfx::TileMode::Mirror) return 1;
            const float tiles = extent / n;
            if (tiles <= 16) return 1;
            int r = int(std::ceil(tiles / 16));
            return std::min(r + (r & 1), std::max(1, 1024 / int(n)));
        };
        const int rx = reps(brush.tile, bounds.Width(), w), ry = reps(brush.tileY, bounds.Height(), h);
        if (rx > 1 || ry > 1) {
            image = Enlarge(image, brush.tile, brush.tileY, rx, ry);
            w *= float(rx);
            h *= float(ry);
        }
        for (const TileSpan& sx : TileSpans(brush.tile, bounds.left, bounds.right, w)) {
            for (const TileSpan& sy : TileSpans(brush.tileY, bounds.top, bounds.bottom, h)) {
                const Rect src{ sx.src0, sy.src0, sx.src1, sy.src1 };
                if (!sx.flip && !sy.flip) {
                    DrawImage(image, src, { sx.dst0, sy.dst0 }, { sx.dst1 - sx.dst0, sy.dst1 - sy.dst0 }, 1);
                    continue;
                }
                Save();
                const Offset centre{ (sx.dst0 + sx.dst1) / 2, (sy.dst0 + sy.dst1) / 2 };
                Scale(sx.flip ? -1.0f : 1.0f, sy.flip ? -1.0f : 1.0f, centre);
                DrawImage(image, src, { sx.dst0, sy.dst0 }, { sx.dst1 - sx.dst0, sy.dst1 - sy.dst0 }, 1);
                Restore();
            }
        }
    }

    namespace {
        // A draw with a blend mode other than SrcOver runs between a Save / Restore.
        template<typename F>
        void WithBlend(DrawScope& d, gfx::BlendMode mode, F&& draw) {
            if (mode == gfx::BlendMode::SrcOver) { draw(); return; }
            d.Save();
            d.SetBlendMode(mode);
            draw();
            d.Restore();
        }
        gfx::Stroke LineStroke(float width, gfx::StrokeCap cap, std::shared_ptr<const gfx::PathEffect> effect) {
            gfx::Stroke s;
            s.width = width;
            s.cap = cap;
            s.pathEffect = std::move(effect);
            return s;
        }
    }

    void DrawScope::DrawPath(const gfx::Path& path, const gfx::Brush& brush, float alpha, const DrawStyle& style, const gfx::ColorFilter* colorFilter,
                             gfx::BlendMode blendMode) {
        WithBlend(*this, blendMode, [&] { DrawPath(path, brush, style.stroke ? &*style.stroke : nullptr, alpha, colorFilter); });
    }

    void DrawScope::DrawLine(const gfx::Brush& brush, Offset start, Offset end, float strokeWidth, gfx::StrokeCap cap,
                             std::shared_ptr<const gfx::PathEffect> pathEffect, float alpha, const gfx::ColorFilter* colorFilter, gfx::BlendMode blendMode) {
        gfx::Path p;
        p.MoveTo(start.x, start.y);
        p.LineTo(end.x, end.y);
        DrawPath(p, brush, alpha, DrawStyle::Stroke(LineStroke(strokeWidth, cap, std::move(pathEffect))), colorFilter, blendMode);
    }

    void DrawScope::DrawCircle(const gfx::Brush& brush, float radius, std::optional<Offset> center, float alpha, const DrawStyle& style,
                               const gfx::ColorFilter* colorFilter, gfx::BlendMode blendMode) {
        const float r = radius < 0 ? m_Size.MinDimension() / 2 : radius;
        const Offset c = center.value_or(Offset{ m_Size.width / 2, m_Size.height / 2 });
        gfx::Path p;
        p.AddOval({ c.x - r, c.y - r, c.x + r, c.y + r });
        DrawPath(p, brush, alpha, style, colorFilter, blendMode);
    }

    void DrawScope::DrawOval(const gfx::Brush& brush, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                             const gfx::ColorFilter* colorFilter, gfx::BlendMode blendMode) {
        const Size s = size.value_or(Size{ m_Size.width - topLeft.x, m_Size.height - topLeft.y });
        gfx::Path p;
        p.AddOval({ topLeft.x, topLeft.y, topLeft.x + s.width, topLeft.y + s.height });
        DrawPath(p, brush, alpha, style, colorFilter, blendMode);
    }

    void DrawScope::DrawArc(const gfx::Brush& brush, float startAngle, float sweepAngle, bool useCenter, Offset topLeft, std::optional<Size> size,
                            float alpha, const DrawStyle& style, const gfx::ColorFilter* colorFilter, gfx::BlendMode blendMode) {
        const Size s = size.value_or(Size{ m_Size.width - topLeft.x, m_Size.height - topLeft.y });
        const Rect oval{ topLeft.x, topLeft.y, topLeft.x + s.width, topLeft.y + s.height };
        gfx::Path p;
        if (useCenter) {
            // Canvas.drawArc with useCenter: a wedge from the centre.
            p.MoveTo((oval.left + oval.right) / 2, (oval.top + oval.bottom) / 2);
            p.ArcTo(oval, startAngle, sweepAngle, false);
            p.Close();
        } else {
            p.AddArc(oval, startAngle, sweepAngle);
        }
        DrawPath(p, brush, alpha, style, colorFilter, blendMode);
    }

    void DrawScope::DrawPoints(const std::vector<Offset>& points, PointMode mode, const gfx::Brush& brush, float strokeWidth, gfx::StrokeCap cap,
                               std::shared_ptr<const gfx::PathEffect> pathEffect, float alpha, const gfx::ColorFilter* colorFilter,
                               gfx::BlendMode blendMode) {
        if (points.empty()) return;
        gfx::Path p;
        const gfx::Stroke stroke = LineStroke(strokeWidth, cap, std::move(pathEffect));
        switch (mode) {
            case PointMode::Points:
                // Each point a zero-length segment: the cap draws the dot (Butt draws nothing,
                // as Canvas.drawPoints with a butt cap draws a square of the stroke width).
                for (const Offset& pt : points) {
                    if (cap == gfx::StrokeCap::Butt) {
                        const float h = std::max(strokeWidth, 1.0f) / 2;
                        p.AddRect({ pt.x - h, pt.y - h, pt.x + h, pt.y + h });
                    } else {
                        p.MoveTo(pt.x, pt.y);
                        p.LineTo(pt.x, pt.y);
                    }
                }
                if (cap == gfx::StrokeCap::Butt) { DrawPath(p, brush, alpha, DrawStyle::Fill(), colorFilter, blendMode); return; }
                break;
            case PointMode::Lines:
                for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
                    p.MoveTo(points[i].x, points[i].y);
                    p.LineTo(points[i + 1].x, points[i + 1].y);
                }
                break;
            case PointMode::Polygon:
                p.MoveTo(points[0].x, points[0].y);
                for (std::size_t i = 1; i < points.size(); ++i) p.LineTo(points[i].x, points[i].y);
                break;
        }
        DrawPath(p, brush, alpha, DrawStyle::Stroke(stroke), colorFilter, blendMode);
    }

    void DrawScope::DrawRect(const gfx::Brush& brush, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                             const gfx::ColorFilter* colorFilter, gfx::BlendMode blendMode) {
        const Size s = size.value_or(Size{ m_Size.width - topLeft.x, m_Size.height - topLeft.y });
        if (!style.stroke && !colorFilter) {
            WithBlend(*this, blendMode, [&] { DrawRect(brush, topLeft, s, alpha); });
            return;
        }
        gfx::Path p;
        p.AddRect({ topLeft.x, topLeft.y, topLeft.x + s.width, topLeft.y + s.height });
        DrawPath(p, brush, alpha, style, colorFilter, blendMode);
    }

    void DrawScope::DrawRoundRect(const gfx::Brush& brush, Offset topLeft, std::optional<Size> size, Offset cornerRadius, float alpha,
                                  const DrawStyle& style, const gfx::ColorFilter* colorFilter, gfx::BlendMode blendMode) {
        const Size s = size.value_or(Size{ m_Size.width - topLeft.x, m_Size.height - topLeft.y });
        if (!style.stroke && !colorFilter && cornerRadius.x == cornerRadius.y) {
            WithBlend(*this, blendMode, [&] { DrawRoundRect(brush, topLeft, s, cornerRadius.x, alpha); });
            return;
        }
        gfx::Path p;
        const Rect r{ topLeft.x, topLeft.y, topLeft.x + s.width, topLeft.y + s.height };
        p.AddRoundRect({ r, cornerRadius, cornerRadius, cornerRadius, cornerRadius });
        DrawPath(p, brush, alpha, style, colorFilter, blendMode);
    }

    void DrawScope::DrawImage(const gfx::ImageBitmap& image, Offset topLeft, float alpha) {
        const float w = float(image.Width()), h = float(image.Height());
        DrawImage(image, { 0, 0, w, h }, topLeft, { w, h }, alpha);
    }

    void DrawScope::ClipPath(const gfx::Path& path, ClipOp op) {
        if (m_Recording) { m_Recording->push_back([path, op](DrawScope& d) { d.ClipPath(path, op); }); return; }
        if (op == ClipOp::Intersect) {
            BeginLayerImpl(path.Bounds(), 1, &path, true);
            return;
        }
        // Difference: a mask of everything around the path, out to well past the scope.
        constexpr float kFar = 1e4f;
        const Rect far{ -kFar, -kFar, m_Size.width + kFar, m_Size.height + kFar };
        gfx::Path around;
        around.AddRect(far);
        gfx::Path mask;
        if (!mask.Op(around, path, gfx::PathOperation::Difference)) return;
        BeginLayerImpl(far, 1, &mask, true);
    }

    void DrawScope::DrawPath(const gfx::Path& path, const gfx::Brush& b, const gfx::Stroke* stroke, float alpha, const gfx::ColorFilter* filter) {
        if (m_Recording) { m_Recording->push_back([path, b, st = Opt(stroke), alpha, f = Opt(filter)](DrawScope& d) { d.DrawPath(path, b, st ? &*st : nullptr, alpha, f ? &*f : nullptr); }); return; }
        if (b.IsLayered()) {
            Rect bounds = path.Bounds();
            if (stroke) {
                const float grow = std::max(stroke->width, 1.0f) * std::max(stroke->miter, 1.0f);
                bounds = { bounds.left - grow, bounds.top - grow, bounds.right + grow, bounds.bottom + grow };
            }
            FillLayered([&] { DrawPath(path, Color(0xFFFFFFFFu), stroke); }, bounds, b, alpha, filter);
            return;
        }
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
        if (m_Recording) { m_Recording->push_back([image, src, dst, size, alpha, f = Opt(filter), quality](DrawScope& d) { d.DrawImage(image, src, dst, size, alpha, f ? &*f : nullptr, quality); }); return; }
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

    void DrawScope::DrawMaskShader(const gfx::ImageBitmap& mask, const gfx::Brush& brush, Offset origin, float alpha,
                                   const gfx::ColorFilter* filter) {
        if (m_Recording) { m_Recording->push_back([mask, brush, origin, alpha, f = Opt(filter)](DrawScope& d) { d.DrawMaskShader(mask, brush, origin, alpha, f ? &*f : nullptr); }); return; }
        if (brush.IsLayered()) {
            const Rect bounds{ origin.x, origin.y, origin.x + float(mask.Width()), origin.y + float(mask.Height()) };
            FillLayered([&] { DrawMaskShader(mask, gfx::Brush::SolidColor(Color(0xFFFFFFFFu)), origin, 1); }, bounds, brush, alpha, filter);
            return;
        }
        if (!m_Target.quads || !m_Target.images || !mask) return;
        const AtlasPage* page = m_Target.images->PageFor(mask);
        if (!page) return;
        QuadRenderer::Paint paint;
        Color solid;
        float a;
        const bool gradient = MakePaint(brush, alpha, &paint, solid, a);
        if (a <= 0) return;
        ApplyTransform();
        m_Target.quads->DrawAtlas({ origin.x, origin.y, float(mask.Width()), float(mask.Height()) }, { 0, 0, 1, 1 }, *page,
                                  gradient ? Rgba{ a, a, a, a } : Scaled(ToRgba(solid), a),
                                  QuadRenderer::Mode::MaskShader, gradient ? &paint : nullptr, FilterRow(filter));
    }

    void DrawScope::DrawRipple(const Ripple& r) {
        if (m_Recording) { m_Recording->push_back([r](DrawScope& d) { d.DrawRipple(r); }); return; }
        if (!m_Target.quads || !m_Target.filters || m_State.alpha <= 0 || !(r.radius > 0)) return;
        // AnimatedRippleDrawable::draw, in its float arithmetic (in_noisePhase = phase * 0.001).
        constexpr float kPi = 3.1415926535897932384626f, kRight = kPi * 0.0078125f, kLeft = kPi * -0.0078125f, kScale = 1.5f;
        const float cx1 = float(0.01 * std::cos(kScale * 0.55)), cy1 = float(0.01 * std::sin(kScale * 0.55));
        const float cx2 = float(-0.0066 * std::cos(kScale * 0.45)), cy2 = float(-0.0066 * std::sin(kScale * 0.45));
        const float cx3 = float(-0.0066 * std::cos(kScale * 0.35)), cy3 = float(-0.0066 * std::sin(kScale * 0.35));
        const float phase = r.turbulencePhase;
        const float rotation1 = float(phase * kRight + 1.7 * kPi);
        const float rotation2 = float(phase * kLeft + 2 * kPi);
        const float rotation3 = float(phase * kRight + 2.75 * kPi);
        const float data[12] = {
            float(kScale * 0.5 + phase * cx1), float(kScale * 0.5 + phase * cy1),
            float(kScale * 0.2 + phase * cx2), float(kScale * 0.2 + phase * cy2),
            float(kScale + phase * cx3), float(kScale + phase * cy3),
            std::cos(rotation1), std::sin(rotation1),
            std::cos(rotation2), std::sin(rotation2),
            std::cos(rotation3), std::sin(rotation3),
        };
        const int row = m_Target.filters->RowForData(data, std::size(data));
        if (row < 0) return;
        ApplyTransform();
        QuadRenderer::Ripple q;
        q.originX = r.origin.x; q.originY = r.origin.y;
        q.touchX = r.touch.x; q.touchY = r.touch.y;
        q.radius = r.radius;
        q.progress = r.progress;
        q.maxRadius = r.maxRadius;
        q.width = r.resolution.width; q.height = r.resolution.height;
        q.noisePhase = float(double(phase) * 0.001);
        // layout(color) uniforms: straight sRGB components.
        q.color = { ((r.color.argb >> 16) & 0xff) / 255.0f, ((r.color.argb >> 8) & 0xff) / 255.0f, (r.color.argb & 0xff) / 255.0f, r.color.Alpha() };
        q.sparkleAlpha = r.sparkleColor.Alpha();
        q.alpha = m_State.alpha;
        q.dataRow = row;
        m_Target.quads->DrawRipple(q);
    }

    void DrawScope::DrawVertices(const gfx::Vertices& v, Color color, float alpha) {
        if (m_Recording) { m_Recording->push_back([v, color, alpha](DrawScope& d) { d.DrawVertices(v, color, alpha); }); return; }
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

    namespace {
        // HWUI multiplyAlpha: SkColorSetA(color, alpha * SkColorGetA(color)).
        Color ShadowMultiplyAlpha(Color c, float alpha) {
            const std::uint32_t a = std::uint32_t(alpha * float((c.argb >> 24) & 0xff));
            return Color(((a & 0xff) << 24) | (c.argb & 0xFFFFFFu));
        }
        // SkRRect's view of the outline: a rect, a circle, a simple circular round rect, or none
        // of these (another shape the analytic path cannot draw).
        struct ShadowRRect { Rect rect; float radius = 0; bool isRect = false, isOval = false, ok = false; };
        ShadowRRect ClassifyOutline(const gfx::Outline& o) {
            ShadowRRect r;
            if (const auto* rect = std::get_if<Rect>(&o.value)) {
                r.rect = *rect;
                r.isRect = r.ok = true;
                return r;
            }
            const auto* rr = std::get_if<gfx::RoundRect>(&o.value);
            if (!rr) return r;
            r.rect = rr->rect;
            const Offset rad = rr->topLeft;
            const bool equal = rr->topRight == rad && rr->bottomRight == rad && rr->bottomLeft == rad;
            if (!equal) return r;
            float xr = rad.x, yr = rad.y;
            // SkRRect::setRectXY: radii scaled down to fit, square corners for a zero radius.
            const float w = r.rect.Width(), h = r.rect.Height();
            if (w < xr + xr || h < yr + yr) {
                const float scale = std::min(w / (xr + xr), h / (yr + yr));
                xr = std::min(xr * scale, w / 2);
                yr = std::min(yr * scale, h / 2);
            }
            if (xr <= 0 || yr <= 0) {
                r.isRect = r.ok = true;
                return r;
            }
            if (std::fabs(xr - yr) > 1.0f / 4096) return r;   // IsNearlySimpleCircular
            r.radius = xr;
            r.isOval = xr >= w / 2 && yr >= h / 2;
            // drawFastShadow handles ovals only when they are circles.
            r.ok = !r.isOval || std::fabs(w - h) <= 1.0f / 4096;
            return r;
        }
    }

    void DrawScope::DrawShadow(const gfx::Outline& outline, float elevation, Color ambientColor, Color spotColor, float casterAlpha,
                               const gfx::ShadowProjection* projection) {
        if (m_Recording) { m_Recording->push_back([outline, elevation, ambientColor, spotColor, casterAlpha, pr = Opt(projection)](DrawScope& d) { d.DrawShadow(outline, elevation, ambientColor, spotColor, casterAlpha, pr ? &*pr : nullptr); }); return; }
        if (!m_Target.quads || !std::isfinite(elevation) || elevation <= 0 || !std::isfinite(casterAlpha)) return;
        const gfx::ShadowLight& light = m_Target.shadowLight;
        // EndReorderBarrierDrawable::drawShadow
        if (casterAlpha <= 0.0f) return;
        const float ambientAlpha = (light.ambientAlpha / 255.f) * casterAlpha;
        const float spotAlpha = (light.spotAlpha / 255.f) * casterAlpha;
        const Color ambient = ShadowMultiplyAlpha(ambientColor, ambientAlpha);
        const Color spot = ShadowMultiplyAlpha(spotColor, spotAlpha);
        const bool transparent = casterAlpha < 1.0f;
        const float occluderHeight = elevation;   // zParams (0, 0, z)

        // SurfaceDrawContext::drawFastShadow: rects, circles and simple circular round rects under
        // a similarity that keeps rects axis-aligned.
        const gfx::Matrix& m = m_State.m;
        const bool rectStaysRect = (m.b == 0 && m.c == 0) || (m.a == 0 && m.d == 0);
        const float sx2 = m.a * m.a + m.b * m.b, sy2 = m.c * m.c + m.d * m.d;
        const bool similarity = std::fabs(sx2 - sy2) <= 1e-4f * std::max(sx2, 1.0f) && std::fabs(m.a * m.c + m.b * m.d) <= 1e-4f * std::max(sx2, 1.0f);
        const ShadowRRect rr = ClassifyOutline(outline);
        if (projection || !rectStaysRect || !similarity || !rr.ok) {
            gfx::ShadowProjection dev;
            if (projection) dev = *projection;
            else dev.zPlane = { 0, 0, elevation };
            const auto h = dev.matrix;
            // Current affine transform * the optional local homography.
            for (int j = 0; j < 3; ++j) {
                dev.matrix[j] = m.a * h[j] + m.c * h[3 + j] + m.e * h[6 + j];
                dev.matrix[3 + j] = m.b * h[j] + m.d * h[3 + j] + m.f * h[6 + j];
            }
            gfx::Path path;
            // Outline.setRoundRect's native SkPath is clockwise (unlike Compose Path's default).
            if (const auto* r = std::get_if<Rect>(&outline.value)) path.AddRect(*r, gfx::Path::Direction::Clockwise);
            else if (const auto* r = std::get_if<gfx::RoundRect>(&outline.value)) path.AddRoundRect(*r, gfx::Path::Direction::Clockwise);
            else path = std::get<gfx::Path>(outline.value);
            auto draw = [&](Color color, const gfx::ShadowLight* source) {
                if (color.Alpha() <= 0) return;
                const gfx::PathShadow shadow = gfx::MakePathShadow(path, dev, source, transparent);
                if (shadow.blurred) { DrawBlurredShadowPath(shadow, color); return; }
                const Rgba c = Scaled(ToRgba(color), m_State.alpha);
                std::vector<QuadRenderer::Vertex> out;
                out.reserve(shadow.triangles.size());
                for (const gfx::ShadowMeshVertex& v : shadow.triangles)
                    out.push_back({ v.position.x, v.position.y, c.r, c.g, c.b, c.a, v.coverage, 0, 0, 2.0f });
                if (out.empty()) return;
                ApplyClip();
                m_Target.quads->DrawTriangles(out.data(), out.size());
            };
            draw(ambient, nullptr);
            draw(spot, &light);
            return;
        }
        if (rr.rect.Empty() || !std::isfinite(sx2) || sx2 == 0) return;
        const float stateAlpha = m_State.alpha;

        // The light in device space; 1/scale of the matrix.
        const Offset devLight{ light.x, light.y };
        const bool scaleTranslate = m.b == 0 && m.c == 0;
        const float devToSrcScale = scaleTranslate ? 1.0f / std::fabs(m.a) : 1.0f / std::sqrt(m.a * m.a + m.c * m.c);
        auto mapRect = [&](Rect r) {
            const Offset p0 = m.Map({ r.left, r.top }), p1 = m.Map({ r.right, r.bottom });
            return Rect{ std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.x, p1.x), std::max(p0.y, p1.y) };
        };
        const float matrixFactor = std::fabs(m.a + m.c);   // viewMatrix[kMScaleX] + viewMatrix[kMSkewX]
        std::vector<gfx::ShadowVertex> verts;
        auto emit = [&](Color color, Rect srcRect, float srcRadius, bool oval, float blurWidth, float insetWidth) {
            // ShadowRRectOp::Make
            const float scaledRadius = std::fabs(srcRadius * matrixFactor);
            const float scaledInsetWidth = std::fabs(insetWidth * matrixFactor);
            if (scaledInsetWidth <= 0) return;
            const Rgba c = Scaled(ToRgba(color), stateAlpha);
            const float rgba[4] = { c.r, c.g, c.b, c.a };
            gfx::AppendShadowRRect(verts, rgba, mapRect(srcRect), scaledRadius, oval, blurWidth, scaledInsetWidth);
        };

        if (((ambient.argb >> 24) & 0xff) > 0) {
            float devSpaceInsetWidth = gfx::ShadowMetrics::AmbientBlurRadius(occluderHeight);
            const float umbraRecipAlpha = gfx::ShadowMetrics::AmbientRecipAlpha(occluderHeight);
            const float devSpaceAmbientBlur = devSpaceInsetWidth * umbraRecipAlpha;
            // Outset to the border of the penumbra.
            const float ambientPathOutset = devSpaceInsetWidth * devToSrcScale;
            const Rect outsetRect{ rr.rect.left - ambientPathOutset, rr.rect.top - ambientPathOutset, rr.rect.right + ambientPathOutset,
                                   rr.rect.bottom + ambientPathOutset };
            const float outsetRad = rr.isOval ? outsetRect.Width() / 2 : rr.radius + ambientPathOutset;
            if (transparent) devSpaceInsetWidth = outsetRect.Width();   // a large inset forces a fill
            emit(ambient, outsetRect, outsetRad, rr.isOval, devSpaceAmbientBlur, devSpaceInsetWidth);
        }

        if (((spot.argb >> 24) & 0xff) > 0) {
            float devSpaceSpotBlur, spotScale;
            Offset spotOffset;
            gfx::ShadowMetrics::GetSpotParams(occluderHeight, devLight.x, devLight.y, light.z, light.radius, &devSpaceSpotBlur, &spotScale, &spotOffset);
            const float srcSpaceSpotBlur = devSpaceSpotBlur * devToSrcScale;
            // The translate under the scale, mapped back into source space.
            spotOffset.x += spotScale * m.e;
            spotOffset.y += spotScale * m.f;
            const float det = m.a * m.d - m.b * m.c;
            if (det == 0) return;
            {
                // ctmInverse->mapPoint(spotOffset)
                const float x = spotOffset.x - m.e, y = spotOffset.y - m.f;
                spotOffset = { (m.d * x - m.c * y) / det, (-m.b * x + m.a * y) / det };
            }
            // The transformed shadow rrect (scale about the origin, then the offset).
            const Rect shadowRect{ rr.rect.left * spotScale + spotOffset.x, rr.rect.top * spotScale + spotOffset.y,
                                   rr.rect.right * spotScale + spotOffset.x, rr.rect.bottom * spotScale + spotOffset.y };
            const float spotRadius = rr.isRect ? 0.0f : (rr.isOval ? shadowRect.Width() / 2 : rr.radius * spotScale);
            const float blurOutset = srcSpaceSpotBlur;
            float insetWidth = blurOutset;
            if (transparent) {
                insetWidth += shadowRect.Width();
            } else {
                float maxOffset;
                if (rr.isRect) {
                    maxOffset = std::max(std::max(std::fabs(shadowRect.left - rr.rect.left), std::fabs(shadowRect.top - rr.rect.top)),
                                         std::max(std::fabs(shadowRect.right - rr.rect.right), std::fabs(shadowRect.bottom - rr.rect.bottom)));
                } else {
                    const float dr = spotRadius - rr.radius;
                    const Offset ul{ shadowRect.left - rr.rect.left + dr, shadowRect.top - rr.rect.top + dr };
                    const Offset lr{ shadowRect.right - rr.rect.right - dr, shadowRect.bottom - rr.rect.bottom - dr };
                    maxOffset = std::sqrt(std::max(ul.x * ul.x + ul.y * ul.y, lr.x * lr.x + lr.y * lr.y)) + dr;
                }
                insetWidth += std::max(blurOutset, maxOffset);
            }
            const Rect outsetRect{ shadowRect.left - blurOutset, shadowRect.top - blurOutset, shadowRect.right + blurOutset,
                                   shadowRect.bottom + blurOutset };
            const float outsetRad = rr.isOval ? outsetRect.Width() / 2 : spotRadius + blurOutset;
            emit(spot, outsetRect, outsetRad, rr.isOval, 2.0f * devSpaceSpotBlur, insetWidth);
        }

        if (verts.empty()) return;
        std::vector<QuadRenderer::Vertex> out;
        out.reserve(verts.size());
        for (const gfx::ShadowVertex& v : verts) out.push_back({ v.x, v.y, v.r, v.g, v.b, v.a, v.px, v.py, v.pz, 1.0f });
        ApplyClip();
        m_Target.quads->DrawTriangles(out.data(), out.size());
    }

    void DrawScope::DrawBlurredShadowPath(const gfx::PathShadow& shadow, Color color) {
        if (!m_Target.paths || !m_Target.quads || m_State.alpha <= 0) return;
        gfx::Path path = shadow.blurPath;
        if (shadow.strokeWidth > 0) {
            // SkPaint::kStrokeAndFill_Style: fill the union once, not two translucent draws.
            gfx::Stroke stroke;
            stroke.width = shadow.strokeWidth;
            gfx::Path edge;
            for (const gfx::Polyline& c : gfx::StrokeToPolygons(path, stroke, {})) {
                if (c.points.empty()) continue;
                edge.MoveTo(c.points[0].x, c.points[0].y);
                for (std::size_t i = 1; i < c.points.size(); ++i) edge.LineTo(c.points[i].x, c.points[i].y);
                edge.Close();
            }
            gfx::Path filled;
            if (!filled.Op(path, edge, gfx::PathOperation::Union)) return;
            path = std::move(filled);
        }
        const float pad = std::ceil(3 * shadow.sigma) + 1;
        const Rect bounds = path.Bounds();
        // Keep sources just outside the viewport and clip: they can blur into visible pixels.
        // The enclosing clip applies only when the finished shadow is composited.
        Rect d{ std::floor(bounds.left - pad), std::floor(bounds.top - pad),
                std::ceil(bounds.right + pad), std::ceil(bounds.bottom + pad) };
        d = d.Intersect({ -pad, -pad, m_Target.viewportWidth + pad, m_Target.viewportHeight + pad });
        if (d.Empty()) return;
        const State saved = m_State;
        m_State.m = {};
        m_State.clip = false;
        float ox = 0, oy = 0;
        const auto* mask = static_cast<const PathCache::Entry*>(PathMask(path, nullptr, ox, oy, &d));
        m_State = saved;
        if (!mask) return;
        QuadRenderer& q = *m_Target.quads;
        q.SetTransform({});
        q.SetClip({});
        q.SetBlendMode(int(gfx::BlendMode::SrcOver));
        const int x = int(d.left), y = int(d.top), w = int(d.Width()), h = int(d.Height());
        q.BeginLayer(x, y, w, h);
        q.DrawAtlas({ ox + float(mask->x), oy + float(mask->y), float(mask->w), float(mask->h) }, mask->uv, *mask->page,
                    { 1, 1, 1, 1 }, QuadRenderer::Mode::TintedCoverage);
        auto image = q.EndLayer();
        const kiln::RectF rect{ d.left, d.top, d.Width(), d.Height() };
        auto uv = [&] { return kiln::RectF{ 0, 0, d.Width() / image.page->Width(), d.Height() / image.page->Height() }; };
        for (int pass = 0; pass < 2 && image.page && shadow.sigma > 0; ++pass) {
            q.BeginLayer(x, y, w, h);
            q.DrawBlur(rect, uv(), *image.page, 1, pass == 0 ? 1.0f / image.page->Width() : 0,
                       pass == 1 ? 1.0f / image.page->Height() : 0, shadow.sigma, true);
            image = q.EndLayer();
        }
        ApplyClip();
        ApplyBlend(m_State.blend);
        if (image.page) q.DrawAtlas(rect, uv(), *image.page, Scaled(ToRgba(color), m_State.alpha), QuadRenderer::Mode::TintedCoverage);
    }

    void DrawScope::BeginLayer(Rect bounds, float alpha, const gfx::Path* clipPath, gfx::BlendMode blend, const gfx::ColorFilter* colorFilter) {
        if (m_Recording) { m_Recording->push_back([bounds, alpha, cp = Opt(clipPath), blend, f = Opt(colorFilter)](DrawScope& d) { d.BeginLayer(bounds, alpha, cp ? &*cp : nullptr, blend, f ? &*f : nullptr); }); return; }
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
        if (m_State.clip)
            d = d.Intersect({ std::floor(m_State.clipOuter.left), std::floor(m_State.clipOuter.top),
                              std::ceil(m_State.clipOuter.right), std::ceil(m_State.clipOuter.bottom) });
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
        if (m_Recording) {
            std::array<float, 9> m;
            std::copy(h, h + 9, m.begin());
            m_Recording->push_back([m](DrawScope& d) { d.EndLayerProjective(m.data()); });
            return;
        }
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
        if (m_Recording) { m_Recording->push_back([blurs, offset](DrawScope& d) { d.EndLayerEffects(blurs, offset); }); return; }
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

    void DrawScope::EndLayerProgressiveBlur(float maxRadius, const gfx::Brush& intensity, bool decal) {
        if (m_Recording) { m_Recording->push_back([=](DrawScope& d) { d.EndLayerProgressiveBlur(maxRadius, intensity, decal); }); return; }
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
        // The intensity gradient in device space (the layer's transform).
        QuadRenderer::Paint paint;
        Color solid;
        float unused;
        if (!MakePaint(intensity, 1, &paint, solid, unused)) {
            paint.kind = QuadRenderer::Paint::Kind::Linear;   // constant intensity: a flat ramp
            const gfx::Brush flat = gfx::Brush::LinearGradient({ solid, solid }, { 0, 0 }, { 1, 0 });
            MakePaint(flat, 1, &paint, solid, unused);
        }
        const gfx::Matrix& m = l.saved.m;
        const kiln::RectF rect{ l.device.left, l.device.top, l.device.Width(), l.device.Height() };
        const int x = int(l.device.left), y = int(l.device.top), w = int(l.device.Width()), h = int(l.device.Height());
        auto uvOf = [&](const QuadRenderer::LayerResult& lr) {
            return kiln::RectF{ 0, 0, rect.w / float(lr.page->Width()), rect.h / float(lr.page->Height()) };
        };
        q.SetTransform({});
        q.SetClip({});
        // Horizontal then vertical, as Compose chains its two runtime-shader passes.
        for (int pass = 0; pass < 2; ++pass) {
            const float scale = pass == 0 ? std::hypot(m.a, m.b) : std::hypot(m.c, m.d);
            q.BeginLayer(x, y, w, h);
            const float su = pass == 0 ? 1.0f / float(r.page->Width()) : 0, sv = pass == 0 ? 0 : 1.0f / float(r.page->Height());
            q.DrawProgressiveBlur(rect, uvOf(r), *r.page, su, sv, std::min(maxRadius * scale, 150.0f), decal, paint);
            r = q.EndLayer();
            if (!r.page) { ApplyClip(); return; }
        }
        ApplyClip();
        CompositeLayer(l, rect, uvOf(r), *r.page);
    }

    void DrawScope::EndLayer(float stretchX, float stretchY) {
        if (m_Recording) { m_Recording->push_back([=](DrawScope& d) { d.EndLayer(stretchX, stretchY); }); return; }
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
