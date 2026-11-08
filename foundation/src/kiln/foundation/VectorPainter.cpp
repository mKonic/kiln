#include "kiln/foundation/VectorPainter.h"

#include "kiln/graphics/PathMeasure.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Locals.h"

#include <cmath>

namespace kiln::foundation {

    using namespace gfx::vector;

    VectorPainter::VectorPainter(ImageVectorPtr image, const ui::Density& density) : m_Image(std::move(image)) {
        // obtainSizePx / obtainViewportSize: an unset (NaN) viewport takes the default size.
        m_Size = { density.ToPx(m_Image->defaultWidth), density.ToPx(m_Image->defaultHeight) };
        m_Viewport = { std::isnan(m_Image->viewportWidth) ? m_Size.width : m_Image->viewportWidth,
                       std::isnan(m_Image->viewportHeight) ? m_Size.height : m_Image->viewportHeight };
        if (m_Image->tintColor.Specified()) m_IntrinsicFilter = gfx::ColorFilter::Tint(m_Image->tintColor, m_Image->tintBlendMode);
        if (m_Image->root) m_Root = Build(*m_Image->root);
    }

    VectorPainter::RenderGroup VectorPainter::Build(const VectorGroup& group) {
        RenderGroup g{ &group, std::nullopt, {} };
        if (!group.clipPathData.empty()) {
            gfx::Path clip;
            ToPath(group.clipPathData, clip);
            g.clip = std::move(clip);
        }
        for (const VectorNode& node : group.children) {
            if (const auto* vp = std::get_if<VectorPath>(&node)) {
                RenderPath rp{ vp, {}, { vp->strokeLineWidth, vp->strokeLineMiter, vp->strokeLineCap, vp->strokeLineJoin } };
                gfx::Path path;
                path.fillType = vp->pathFillType;
                ToPath(vp->pathData, path);
                // PathComponent.updateRenderPath: trim along the first contour, wrapping past the end.
                if (vp->trimPathStart != 0 || vp->trimPathEnd != 1) {
                    gfx::PathMeasure measure;
                    measure.SetPath(&path, false);
                    const float length = measure.Length();
                    const float start = std::fmod(vp->trimPathStart + vp->trimPathOffset, 1.0f) * length;
                    const float end = std::fmod(vp->trimPathEnd + vp->trimPathOffset, 1.0f) * length;
                    gfx::Path trimmed;
                    trimmed.fillType = path.fillType;
                    if (start > end) {
                        gfx::Path a, b;
                        measure.GetSegment(start, length, a, true);
                        trimmed.AddPath(a);
                        measure.GetSegment(0, end, b, true);
                        trimmed.AddPath(b);
                    } else {
                        measure.GetSegment(start, end, trimmed, true);
                    }
                    path = std::move(trimmed);
                }
                rp.path = std::move(path);
                g.children.emplace_back(std::move(rp));
            } else if (const auto& child = std::get<std::shared_ptr<const VectorGroup>>(node)) {
                g.children.emplace_back(Build(*child));
            }
        }
        return g;
    }

    void VectorPainter::DrawGroup(DrawScope& s, const RenderGroup& g, const gfx::ColorFilter* filter) const {
        const VectorGroup& v = *g.source;
        s.Save();
        // GroupComponent.updateMatrix: translate by translation + pivot, rotate, scale, back by pivot.
        s.Translate({ v.translationX + v.pivotX, v.translationY + v.pivotY });
        if (v.rotation != 0) s.Rotate(v.rotation, {});
        if (v.scaleX != 1 || v.scaleY != 1) s.Scale(v.scaleX, v.scaleY, {});
        s.Translate({ -v.pivotX, -v.pivotY });
        if (g.clip) s.BeginLayer(g.clip->Bounds(), 1, &*g.clip);
        for (const auto& child : g.children) {
            if (const auto* rp = std::get_if<RenderPath>(&child)) {
                const VectorPath& p = *rp->source;
                if (p.fill) s.DrawPath(rp->path, *p.fill, nullptr, p.fillAlpha, filter);
                if (p.stroke) s.DrawPath(rp->path, *p.stroke, &rp->stroke, p.strokeAlpha, filter);
            } else {
                DrawGroup(s, std::get<RenderGroup>(child), filter);
            }
        }
        if (g.clip) s.EndLayer();
        s.Restore();
    }

    void VectorPainter::Draw(DrawScope& s, Size size, float alpha, const gfx::ColorFilter* colorFilter) {
        if (!(size.width > 0) || !(size.height > 0) || alpha <= 0 || !m_Image->root) return;
        const gfx::ColorFilter* filter = colorFilter ? colorFilter : m_IntrinsicFilter ? &*m_IntrinsicFilter : nullptr;
        s.Save();
        if (m_Image->autoMirror && s.Direction() == ui::LayoutDirection::Rtl) s.Scale(-1, 1, { size.width / 2, size.height / 2 });
        // DrawCache renders into an image of the (rounded up) size and draws that with the alpha
        // and filter. Drawing straight through gives the same pixels at full alpha with no filter
        // or a SrcIn tint (which keeps each pixel's coverage); otherwise an offscreen layer.
        const bool direct = alpha >= 1 && (!filter || (filter->kind == gfx::ColorFilter::Kind::Tint && filter->mode == gfx::BlendMode::SrcIn));
        if (!direct) s.BeginLayer({ 0, 0, std::ceil(size.width), std::ceil(size.height) }, alpha, nullptr, gfx::BlendMode::SrcOver, filter);
        s.Scale(size.width / m_Viewport.width, size.height / m_Viewport.height, {});
        DrawGroup(s, m_Root, direct ? filter : nullptr);
        if (!direct) s.EndLayer();
        s.Restore();
    }

    std::shared_ptr<VectorPainter> RememberVectorPainter(const ImageVectorPtr& image) {
        rt::Composition* c = rt::Composition::Current();
        const ui::Density density = c ? c->Read(ui::LocalDensity) : ui::Density{};
        if (!c) return std::make_shared<VectorPainter>(image, density);
        return c->Remember<std::shared_ptr<VectorPainter>>([&] { return std::make_shared<VectorPainter>(image, density); },
                                                           rt::Keys(image ? image->genId : -1, density.density));
    }

}
