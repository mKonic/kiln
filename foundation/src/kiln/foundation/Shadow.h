#pragma once

#include "kiln/foundation/Image.h"
#include "kiln/graphics/shadow/ShadowContext.h"

namespace kiln::foundation {
    // ShadowScope values are pixels, as in ui/draw/Shadow.kt; convert dp with ToPx in a block.
    struct ShadowScope : ui::Density {
        float radius = 0, spread = 0;
        ui::Offset offset;
        ui::Color color{ 0xff000000 };
        std::optional<gfx::Brush> brush;
        float alpha = 1;
        gfx::BlendMode blendMode = gfx::BlendMode::SrcOver;
    };
    using DropShadowScope = ShadowScope;
    using InnerShadowScope = ShadowScope;

    class PaintedShadowPainter : public Painter {
    public:
        std::optional<Size> IntrinsicSize() const override { return std::nullopt; }
        void Draw(DrawScope& scope, Size size, float alpha = 1, const gfx::ColorFilter* filter = nullptr) override;
    protected:
        PaintedShadowPainter(std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow, bool inner,
                             std::shared_ptr<gfx::shadow::ShadowContext> context);
    private:
        std::shared_ptr<const gfx::Shape> m_Shape;
        gfx::shadow::Shadow m_Shadow;
        bool m_Inner;
        std::shared_ptr<gfx::shadow::ShadowContext> m_Context;
        std::shared_ptr<const gfx::shadow::ShadowMask> m_Mask;
    };

    class DropShadowPainter final : public PaintedShadowPainter {
    public:
        DropShadowPainter(std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow,
                          std::shared_ptr<gfx::shadow::ShadowContext> context = {})
            : PaintedShadowPainter(std::move(shape), std::move(shadow), false, std::move(context)) {}
    };
    class InnerShadowPainter final : public PaintedShadowPainter {
    public:
        InnerShadowPainter(std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow,
                           std::shared_ptr<gfx::shadow::ShadowContext> context = {})
            : PaintedShadowPainter(std::move(shape), std::move(shadow), true, std::move(context)) {}
    };

    Modifier DropShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow);
    Modifier DropShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, std::function<void(DropShadowScope&)> block);
    Modifier InnerShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, gfx::shadow::Shadow shadow);
    Modifier InnerShadow(Modifier m, std::shared_ptr<const gfx::Shape> shape, std::function<void(InnerShadowScope&)> block);
}
