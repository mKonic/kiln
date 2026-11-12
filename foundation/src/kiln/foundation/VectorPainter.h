#pragma once

// VectorPainter (ui vector/VectorPainter.kt, Vector.kt, DrawCache.kt): draws an ImageVector
// scaled from its viewport to the size, mirrored in RTL when autoMirror, tinted by its own tint
// unless a colour filter is given, and composited as one image with the alpha.

#include "kiln/foundation/Image.h"
#include "kiln/graphics/vector/ImageVector.h"

namespace kiln::foundation {

    class VectorPainter final : public Painter {
    public:
        VectorPainter(gfx::vector::ImageVectorPtr image, const ui::Density& density);
        std::optional<Size> IntrinsicSize() const override { return m_Size; }
        void Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) override;
        const gfx::vector::ImageVectorPtr& Image() const { return m_Image; }

    private:
        struct RenderPath {
            const gfx::vector::VectorPath* source;
            gfx::Path path;   // trimmed when trimPathStart / End say so
            gfx::Stroke stroke;
        };
        struct RenderGroup {
            const gfx::vector::VectorGroup* source;
            std::optional<gfx::Path> clip;
            std::vector<std::variant<RenderPath, RenderGroup>> children;
        };
        static RenderGroup Build(const gfx::vector::VectorGroup& group);
        void DrawGroup(DrawScope& s, const RenderGroup& g, const gfx::ColorFilter* filter) const;

        gfx::vector::ImageVectorPtr m_Image;
        Size m_Size;
        Size m_Viewport;
        std::optional<gfx::ColorFilter> m_IntrinsicFilter;
        RenderGroup m_Root;
    };

    // rememberVectorPainter(image): one painter per image and density.
    std::shared_ptr<VectorPainter> RememberVectorPainter(const gfx::vector::ImageVectorPtr& image, rt::Key key = {});
    // rememberVectorPainter(defaultWidth, defaultHeight, ..., content): a painter of the vector
    // `content` builds (given the viewport size) in each composition; a new painter only when
    // what it built changed. An unset (NaN) viewport is the default size in px.
    std::shared_ptr<VectorPainter> RememberVectorPainter(ui::Dp defaultWidth, ui::Dp defaultHeight, float viewportWidth, float viewportHeight,
                                                         std::string name, ui::Color tintColor, gfx::BlendMode tintBlendMode, bool autoMirror,
                                                         const std::function<void(gfx::vector::ImageVector::Builder&, float viewportWidth, float viewportHeight)>& content,
                                                         rt::Key key = {});

}
