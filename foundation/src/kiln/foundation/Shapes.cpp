#include "kiln/foundation/Shapes.h"

namespace kiln::foundation {

    using namespace ui;

    gfx::Outline CornerBasedShape::CreateOutline(Size size, LayoutDirection dir, const Density& d) const {
        float ts = topStart.ToPx(size, d), te = topEnd.ToPx(size, d), be = bottomEnd.ToPx(size, d), bs = bottomStart.ToPx(size, d);
        const float minDim = size.MinDimension();
        if (ts + bs > minDim) { const float k = minDim / (ts + bs); ts *= k; bs *= k; }
        if (te + be > minDim) { const float k = minDim / (te + be); te *= k; be *= k; }
        return CreateOutline(size, std::max(ts, 0.0f), std::max(te, 0.0f), std::max(be, 0.0f), std::max(bs, 0.0f), dir);
    }

    gfx::Outline RoundedCornerShape::CreateOutline(Size size, float ts, float te, float be, float bs, LayoutDirection dir) const {
        if (ts + te + be + bs == 0) return { Rect{ 0, 0, size.width, size.height } };
        const bool ltr = absolute || dir == LayoutDirection::Ltr;
        auto r = [](float v) { return Offset{ v, v }; };
        return { gfx::RoundRect{ Rect{ 0, 0, size.width, size.height }, r(ltr ? ts : te), r(ltr ? te : ts), r(ltr ? be : bs), r(ltr ? bs : be) } };
    }

    bool RoundedCornerShape::Equals(const gfx::Shape& o) const {
        auto* x = dynamic_cast<const RoundedCornerShape*>(&o);
        return x && x->absolute == absolute && x->topStart == topStart && x->topEnd == topEnd && x->bottomEnd == bottomEnd && x->bottomStart == bottomStart;
    }

    gfx::Outline CutCornerShape::CreateOutline(Size size, float ts, float te, float be, float bs, LayoutDirection dir) const {
        if (ts + te + be + bs == 0) return { Rect{ 0, 0, size.width, size.height } };
        const bool ltr = absolute || dir == LayoutDirection::Ltr;
        gfx::Path p;
        float c = ltr ? ts : te;
        p.MoveTo(0, c);
        p.LineTo(c, 0);
        c = ltr ? te : ts;
        p.LineTo(size.width - c, 0);
        p.LineTo(size.width, c);
        c = ltr ? be : bs;
        p.LineTo(size.width, size.height - c);
        p.LineTo(size.width - c, size.height);
        c = ltr ? bs : be;
        p.LineTo(c, size.height);
        p.LineTo(0, size.height - c);
        p.Close();
        return { std::move(p) };
    }

    bool CutCornerShape::Equals(const gfx::Shape& o) const {
        auto* x = dynamic_cast<const CutCornerShape*>(&o);
        return x && x->absolute == absolute && x->topStart == topStart && x->topEnd == topEnd && x->bottomEnd == bottomEnd && x->bottomStart == bottomStart;
    }

    ShapePtr RoundedCorner(CornerSize a) { return std::make_shared<RoundedCornerShape>(a, a, a, a); }
    ShapePtr RoundedCorner(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) { return std::make_shared<RoundedCornerShape>(ts, te, be, bs); }
    ShapePtr AbsoluteRoundedCorner(CornerSize tl, CornerSize tr, CornerSize br, CornerSize bl) {
        auto s = std::make_shared<RoundedCornerShape>(tl, tr, br, bl);
        s->absolute = true;
        return s;
    }
    ShapePtr CutCorner(CornerSize a) { return std::make_shared<CutCornerShape>(a, a, a, a); }
    ShapePtr CutCorner(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) { return std::make_shared<CutCornerShape>(ts, te, be, bs); }
    ShapePtr AbsoluteCutCorner(CornerSize tl, CornerSize tr, CornerSize br, CornerSize bl) {
        auto s = std::make_shared<CutCornerShape>(tl, tr, br, bl);
        s->absolute = true;
        return s;
    }
    const ShapePtr& CircleShape() { static const ShapePtr s = RoundedCornerPercent(50); return s; }
    const ShapePtr& RectangleShape() { static const ShapePtr s = std::make_shared<RectangleShapeImpl>(); return s; }

    ShapePtr Lerp(const ShapePtr& a, const ShapePtr& b, float t) {
        // RectangleShape lerps as a zero-corner shape of the other's kind (RoundedCornerShape.lerp).
        auto corners = [](const ShapePtr& s) -> const CornerBasedShape* { return dynamic_cast<const CornerBasedShape*>(s.get()); };
        const CornerBasedShape* ca = corners(a);
        const CornerBasedShape* cb = corners(b);
        const bool aRect = dynamic_cast<const RectangleShapeImpl*>(a.get()) != nullptr;
        const bool bRect = dynamic_cast<const RectangleShapeImpl*>(b.get()) != nullptr;
        auto make = [&](const CornerBasedShape& kind, CornerSize ts, CornerSize te, CornerSize be, CornerSize bs) -> ShapePtr {
            if (auto* r = dynamic_cast<const RoundedCornerShape*>(&kind)) {
                auto s = std::make_shared<RoundedCornerShape>(ts, te, be, bs);
                s->absolute = r->absolute;
                return s;
            }
            auto s = std::make_shared<CutCornerShape>(ts, te, be, bs);
            s->absolute = static_cast<const CutCornerShape&>(kind).absolute;
            return s;
        };
        const CornerSize zero{};
        if (ca && cb && typeid(*ca) == typeid(*cb)) {
            return make(*ca, CornerSize::Lerp(ca->topStart, cb->topStart, t), CornerSize::Lerp(ca->topEnd, cb->topEnd, t),
                        CornerSize::Lerp(ca->bottomEnd, cb->bottomEnd, t), CornerSize::Lerp(ca->bottomStart, cb->bottomStart, t));
        }
        if (ca && bRect)
            return make(*ca, CornerSize::Lerp(ca->topStart, zero, t), CornerSize::Lerp(ca->topEnd, zero, t),
                        CornerSize::Lerp(ca->bottomEnd, zero, t), CornerSize::Lerp(ca->bottomStart, zero, t));
        if (cb && aRect)
            return make(*cb, CornerSize::Lerp(zero, cb->topStart, t), CornerSize::Lerp(zero, cb->topEnd, t),
                        CornerSize::Lerp(zero, cb->bottomEnd, t), CornerSize::Lerp(zero, cb->bottomStart, t));
        return t < 0.5f ? a : b;
    }

    namespace {
        // A rounded rect the SDF quad can draw: all four corners the same circular radius.
        bool UniformCircular(const gfx::RoundRect& rr) {
            return rr.topLeft == rr.topRight && rr.topLeft == rr.bottomRight && rr.topLeft == rr.bottomLeft && rr.topLeft.x == rr.topLeft.y;
        }
    }

    void DrawOutline(DrawScope& s, const gfx::Outline& o, Color color) {
        if (auto* r = std::get_if<Rect>(&o.value)) { s.DrawRect(color, { r->left, r->top }, { r->Width(), r->Height() }); return; }
        if (auto* rr = std::get_if<gfx::RoundRect>(&o.value); rr && UniformCircular(*rr)) {
            s.DrawRoundRect(color, { rr->rect.left, rr->rect.top }, { rr->rect.Width(), rr->rect.Height() }, rr->topLeft.x);
            return;
        }
        s.DrawPath(o.ToPath(), color);
    }

    void DrawOutline(DrawScope& s, const gfx::Outline& o, const gfx::Brush& b, float alpha) {
        if (auto* r = std::get_if<Rect>(&o.value)) { s.DrawRect(b, { r->left, r->top }, { r->Width(), r->Height() }, alpha); return; }
        if (auto* rr = std::get_if<gfx::RoundRect>(&o.value); rr && UniformCircular(*rr)) {
            s.DrawRoundRect(b, { rr->rect.left, rr->rect.top }, { rr->rect.Width(), rr->rect.Height() }, rr->topLeft.x, alpha);
            return;
        }
        s.DrawPath(o.ToPath(), b, nullptr, alpha);
    }

}
