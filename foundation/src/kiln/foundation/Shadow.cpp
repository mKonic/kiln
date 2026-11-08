#include "kiln/foundation/Shadow.h"

#include "kiln/runtime/State.h"

namespace kiln::foundation {
    PaintedShadowPainter::PaintedShadowPainter(std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow, bool inner,
                                               std::shared_ptr<gfx::shadow::ShadowContext> context)
        : m_Shape(std::move(shape)), m_Shadow(std::move(shadow)), m_Inner(inner),
          m_Context(context ? std::move(context) : std::make_shared<gfx::shadow::ShadowContext>()) {}

    void PaintedShadowPainter::Draw(DrawScope& s, Size size, float alpha, const gfx::ColorFilter* filter) {
        const float a = std::clamp(alpha * m_Shadow.alpha, 0.0f, 1.0f);
        if (!std::isfinite(a) || a <= 0) return;
        auto mask = m_Context->Obtain(m_Shape, size, s.Direction(), s, m_Shadow, m_Inner);
        m_Mask = std::move(mask);
        if (!m_Mask || !m_Mask->image) return;
        const auto& image = m_Mask->image;
        const Size savedSize = s.GetSize();
        const gfx::ColorFilter tint = gfx::ColorFilter::Tint(m_Shadow.color);
        s.Save();
        s.SetBlendMode(m_Shadow.blendMode);
        if (m_Inner) s.SetSize(size);
        else {
            const float padding = s.ToPx(m_Shadow.radius) + s.ToPx(m_Shadow.spread);
            s.Translate({ s.ToPx(m_Shadow.offset.x) - padding, s.ToPx(m_Shadow.offset.y) - padding });
            // DropShadowRenderer creates gradients against the padded bitmap's size.
            s.SetSize({ float(image.Width()), float(image.Height()) });
        }
        const gfx::Brush brush = m_Shadow.brush && !filter ? *m_Shadow.brush : gfx::Brush::SolidColor(ui::Color(0xff000000));
        s.DrawMaskShader(image, brush, m_Mask->origin, a, filter ? filter : (m_Shadow.brush ? nullptr : &tint));
        s.SetSize(savedSize);
        s.Restore();
    }

    namespace {
        struct Params {
            std::shared_ptr<const gfx::Shape> shape;
            gfx::shadow::Shadow shadow;
            std::shared_ptr<const std::function<void(ShadowScope&)>> block;
            bool operator==(const Params&) const = default;
        };

        template <bool Inner> struct ShadowNode final : ui::ModifierNode, ui::DrawModifier {
            Params params;
            explicit ShadowNode(Params p) : params(std::move(p)) { watch.node = this; }
            ui::DrawModifier* AsDraw() override { return this; }
            void RetirePainter() { if (painter) retired = std::move(painter); }
            void OnParamsChanged() { read = false; RetirePainter(); InvalidateDraw(); }
            void OnDetach() override { watch.Unsubscribe(); painter.reset(); retired.reset(); read = false; }
            struct Watch final : rt::Subscriber {
                ShadowNode* node = nullptr;
                void OnStateChanged() override { node->read = false; node->InvalidateDraw(); }
            } watch;
            bool read = false;
            ui::Density density;
            gfx::shadow::Shadow target;
            std::shared_ptr<Painter> painter, retired;
            void Draw(ui::ContentDrawScope& s) override {
                gfx::shadow::Shadow shadow = params.shadow;
                if (params.block) {
                    if (!read || density != static_cast<const ui::Density&>(s)) {
                        density = s;
                        ShadowScope scope;
                        static_cast<ui::Density&>(scope) = density;
                        rt::Observation::Track(watch, [&] { (*params.block)(scope); });
                        const ui::Dp radius = scope.ToDp(scope.radius), spread = scope.ToDp(scope.spread);
                        const ui::DpOffset offset{ scope.ToDp(scope.offset.x), scope.ToDp(scope.offset.y) };
                        target = scope.brush ? gfx::shadow::Shadow(radius, *scope.brush, spread, offset, scope.alpha, scope.blendMode)
                                             : gfx::shadow::Shadow(radius, scope.color, spread, offset, scope.alpha, scope.blendMode);
                        read = true;
                        // Drop offsets can recreate the painter, but the shared mask excludes them.
                        RetirePainter();
                    }
                    shadow = target;
                }
                if (!painter) {
                    auto* owner = Owner() ? Owner()->GetOwner() : nullptr;
                    auto context = owner ? owner->Shadows() : std::shared_ptr<gfx::shadow::ShadowContext>{};
                    if constexpr (Inner) painter = std::make_shared<InnerShadowPainter>(params.shape, shadow, context);
                    else painter = std::make_shared<DropShadowPainter>(params.shape, shadow, context);
                }
                painter->Draw(s, s.GetSize(), 1, nullptr);
                retired.reset();
                // Both modifier implementations draw before content in the Compose source.
                s.DrawContent();
            }
        };
    }

    Modifier DropShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow) {
        return m.With<ShadowNode<false>>(Params{ std::move(shape), std::move(shadow), {} });
    }
    Modifier DropShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, std::function<void(DropShadowScope&)> block) {
        return m.With<ShadowNode<false>>(Params{ std::move(shape), gfx::shadow::Shadow{}, std::make_shared<const std::function<void(ShadowScope&)>>(std::move(block)) });
    }
    Modifier InnerShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow) {
        return m.With<ShadowNode<true>>(Params{ std::move(shape), std::move(shadow), {} });
    }
    Modifier InnerShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, std::function<void(InnerShadowScope&)> block) {
        return m.With<ShadowNode<true>>(Params{ std::move(shape), gfx::shadow::Shadow{}, std::make_shared<const std::function<void(ShadowScope&)>>(std::move(block)) });
    }
}
