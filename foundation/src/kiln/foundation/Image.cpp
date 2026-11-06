#include "kiln/foundation/Image.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"

#include <algorithm>

namespace kiln::foundation {

    using namespace ui;

    ScaleFactor ContentScale::ComputeScaleFactor(Size src, Size dst) const {
        const float w = dst.width / src.width, h = dst.height / src.height;
        switch (kind) {
            case Kind::Crop: { const float s = std::max(w, h); return { s, s }; }
            case Kind::Fit: { const float s = std::min(w, h); return { s, s }; }
            case Kind::FillHeight: return { h, h };
            case Kind::FillWidth: return { w, w };
            case Kind::Inside:
                if (src.width <= dst.width && src.height <= dst.height) return { 1, 1 };
                { const float s = std::min(w, h); return { s, s }; }
            case Kind::None: return { 1, 1 };
            case Kind::FillBounds: return { w, h };
            case Kind::Fixed: return { fixed, fixed };
        }
        return { 1, 1 };
    }

    void BitmapPainter::Draw(DrawScope& scope, Size size, float alpha) {
        // BitmapPainter.onDraw: the whole bitmap into the size rounded to whole pixels.
        scope.DrawImage(m_Image, { 0, 0, float(m_Image.Width()), float(m_Image.Height()) }, {},
                        { std::round(size.width), std::round(size.height) }, alpha);
    }

    namespace {
        bool Finite(float v) { return std::isfinite(v); }

        struct PaintParams {
            std::shared_ptr<Painter> painter;
            bool sizeToIntrinsics;
            Alignment alignment;
            ContentScale scale;
            float alpha;
            bool operator==(const PaintParams& o) const {
                const bool samePainter = painter == o.painter || (painter && o.painter && painter->Equals(*o.painter));
                return samePainter && sizeToIntrinsics == o.sizeToIntrinsics && alignment.horizontal == o.alignment.horizontal &&
                       alignment.vertical == o.alignment.vertical && scale == o.scale && alpha == o.alpha;
            }
        };

        // PainterNode.
        struct PaintNode final : ModifierNode, LayoutModifier, DrawModifier {
            PaintParams params;
            explicit PaintNode(PaintParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); InvalidateDraw(); }
            LayoutModifier* AsLayout() override { return this; }
            DrawModifier* AsDraw() override { return this; }

            std::optional<Size> Intrinsic() const { return params.painter ? params.painter->IntrinsicSize() : std::nullopt; }
            bool UseIntrinsic() const { return params.sizeToIntrinsics && Intrinsic().has_value(); }

            Size ScaledSize(Size dst) const {
                if (!UseIntrinsic()) return dst;
                const Size in = *Intrinsic();
                const Size src{ Finite(in.width) ? in.width : dst.width, Finite(in.height) ? in.height : dst.height };
                if (dst.width == 0 || dst.height == 0) return {};
                const ScaleFactor f = params.scale.ComputeScaleFactor(src, dst);
                return { src.width * f.x, src.height * f.y };
            }

            Constraints Modify(const Constraints& c) const {
                const bool bounded = c.HasBoundedWidth() && c.HasBoundedHeight();
                const bool fixed = c.HasFixedWidth() && c.HasFixedHeight();
                if ((!UseIntrinsic() && bounded) || fixed) return { c.maxWidth, c.maxWidth, c.maxHeight, c.maxHeight };
                const std::optional<Size> in = Intrinsic();
                const int iw = in && Finite(in->width) ? RoundJava(in->width) : c.minWidth;
                const int ih = in && Finite(in->height) ? RoundJava(in->height) : c.minHeight;
                const Size scaled = ScaledSize({ float(c.ConstrainWidth(iw)), float(c.ConstrainHeight(ih)) });
                return { c.ConstrainWidth(RoundJava(scaled.width)), c.maxWidth, c.ConstrainHeight(RoundJava(scaled.height)), c.maxHeight };
            }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(Modify(c));
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
            }
            int MinIntrinsicWidth(MeasureScope&, Measurable& m, int h) override {
                if (!UseIntrinsic()) return m.MinIntrinsicWidth(h);
                return std::max(Modify({ 0, Constraints::Infinity, 0, h }).minWidth, m.MinIntrinsicWidth(h));
            }
            int MaxIntrinsicWidth(MeasureScope&, Measurable& m, int h) override {
                if (!UseIntrinsic()) return m.MaxIntrinsicWidth(h);
                return std::max(Modify({ 0, Constraints::Infinity, 0, h }).minWidth, m.MaxIntrinsicWidth(h));
            }
            int MinIntrinsicHeight(MeasureScope&, Measurable& m, int w) override {
                if (!UseIntrinsic()) return m.MinIntrinsicHeight(w);
                return std::max(Modify({ 0, w, 0, Constraints::Infinity }).minHeight, m.MinIntrinsicHeight(w));
            }
            int MaxIntrinsicHeight(MeasureScope&, Measurable& m, int w) override {
                if (!UseIntrinsic()) return m.MaxIntrinsicHeight(w);
                return std::max(Modify({ 0, w, 0, Constraints::Infinity }).minHeight, m.MaxIntrinsicHeight(w));
            }

            void Draw(ContentDrawScope& s) override {
                if (params.painter) {
                    const Size size = s.GetSize();
                    const std::optional<Size> in = Intrinsic();
                    const Size src{ in && Finite(in->width) ? in->width : size.width, in && Finite(in->height) ? in->height : size.height };
                    Size scaled{};
                    if (size.width != 0 && size.height != 0) {
                        const ScaleFactor f = params.scale.ComputeScaleFactor(src, size);
                        scaled = { src.width * f.x, src.height * f.y };
                    }
                    const IntOffset at = params.alignment.Align({ RoundJava(scaled.width), RoundJava(scaled.height) },
                                                                { RoundJava(size.width), RoundJava(size.height) }, s.Direction());
                    s.Save();
                    s.Translate({ float(at.x), float(at.y) });
                    params.painter->Draw(s, scaled, params.alpha);
                    s.Restore();
                }
                s.DrawContent();
            }
        };

        // ImageMeasurePolicy: the minimum size; the paint modifier did the sizing.
        struct ImagePolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                return s.Layout(c.minWidth, c.minHeight, [](PlacementScope&) {});
            }
        };
    }

    Modifier Paint(Modifier m, std::shared_ptr<Painter> painter, bool sizeToIntrinsics, Alignment alignment, ContentScale scale, float alpha) {
        return m.With<PaintNode>(PaintParams{ std::move(painter), sizeToIntrinsics, alignment, scale, alpha });
    }

    void Image(std::shared_ptr<Painter> painter, const std::optional<std::string>& description, const Modifier& modifier,
               Alignment alignment, ContentScale scale, float alpha) {
        Modifier m = modifier;
        if (description) {
            const std::string d = *description;
            m = Semantics(std::move(m), [d](SemanticsConfiguration& c) { c.contentDescription.push_back(d); c.role = Role::Image; });
        }
        m = Paint(ClipToBounds(std::move(m)), std::move(painter), true, alignment, scale, alpha);
        static const auto policy = std::make_shared<ImagePolicy>();
        Layout(m, policy);
    }

    void Image(const gfx::ImageBitmap& bitmap, const std::optional<std::string>& description, const Modifier& modifier,
               Alignment alignment, ContentScale scale, float alpha) {
        // remember(bitmap) { BitmapPainter(bitmap) }
        auto& c = *rt::Composition::Current();
        auto& painter = c.Remember<std::shared_ptr<Painter>>([&] { return std::make_shared<BitmapPainter>(bitmap); }, rt::Keys(bitmap.Id()));
        Image(painter, description, modifier, alignment, scale, alpha);
    }

}
