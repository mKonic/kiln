#pragma once

// Image, painters and Modifier.paint (foundation Image.kt, ui PainterModifier.kt, ContentScale.kt).

#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Image.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace kiln::foundation {

    using ui::Alignment;
    using ui::DrawScope;
    using ui::Modifier;
    using ui::Size;

    struct ScaleFactor {
        float x = 1, y = 1;
        bool operator==(const ScaleFactor&) const = default;
        ScaleFactor operator*(float k) const { return { x * k, y * k }; }
        ScaleFactor operator/(float k) const { return { x / k, y / k }; }
    };
    // Size.times / Size.div with a ScaleFactor.
    inline ui::Size operator*(ui::Size s, ScaleFactor f) { return { s.width * f.x, s.height * f.y }; }
    inline ui::Size operator/(ui::Size s, ScaleFactor f) { return { s.width / f.x, s.height / f.y }; }

    struct ContentScale {
        enum class Kind : std::uint8_t { Crop, Fit, FillHeight, FillWidth, Inside, None, FillBounds, Fixed };
        Kind kind = Kind::Fit;
        float fixed = 1;
        static constexpr ContentScale Crop() { return { Kind::Crop }; }
        static constexpr ContentScale Fit() { return { Kind::Fit }; }
        static constexpr ContentScale FillHeight() { return { Kind::FillHeight }; }
        static constexpr ContentScale FillWidth() { return { Kind::FillWidth }; }
        static constexpr ContentScale Inside() { return { Kind::Inside }; }
        static constexpr ContentScale None() { return { Kind::None }; }
        static constexpr ContentScale FillBounds() { return { Kind::FillBounds }; }
        static constexpr ContentScale FixedScale(float v) { return { Kind::Fixed, v }; }
        ScaleFactor ComputeScaleFactor(Size src, Size dst) const;
        bool operator==(const ContentScale&) const = default;
    };

    // Something that draws itself into a size; intrinsic size in px, nullopt when it has none.
    class Painter {
    public:
        virtual ~Painter() = default;
        virtual std::optional<Size> IntrinsicSize() const = 0;
        virtual void Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) = 0;
        virtual bool Equals(const Painter& other) const { return this == &other; }
    };

    class BitmapPainter final : public Painter {
    public:
        explicit BitmapPainter(gfx::ImageBitmap image, gfx::FilterQuality quality = gfx::FilterQuality::Low)
            : m_Image(std::move(image)), m_Quality(quality) {}
        std::optional<Size> IntrinsicSize() const override { return Size{ float(m_Image.Width()), float(m_Image.Height()) }; }
        void Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) override;
        bool Equals(const Painter& o) const override {
            auto* b = dynamic_cast<const BitmapPainter*>(&o);
            return b && b->m_Image == m_Image && b->m_Quality == m_Quality;
        }
    private:
        gfx::ImageBitmap m_Image;
        gfx::FilterQuality m_Quality;
    };

    Modifier Paint(Modifier m, std::shared_ptr<Painter> painter, bool sizeToIntrinsics = true,
                   Alignment alignment = Alignment::Center(), ContentScale contentScale = ContentScale::Inside(), float alpha = 1,
                   std::optional<gfx::ColorFilter> colorFilter = std::nullopt);

    void Image(std::shared_ptr<Painter> painter, const std::optional<std::string>& contentDescription, const Modifier& modifier = {},
               Alignment alignment = Alignment::Center(), ContentScale contentScale = ContentScale::Fit(), float alpha = 1,
               std::optional<gfx::ColorFilter> colorFilter = std::nullopt);
    void Image(const gfx::ImageBitmap& bitmap, const std::optional<std::string>& contentDescription, const Modifier& modifier = {},
               Alignment alignment = Alignment::Center(), ContentScale contentScale = ContentScale::Fit(), float alpha = 1,
               std::optional<gfx::ColorFilter> colorFilter = std::nullopt, gfx::FilterQuality filterQuality = gfx::FilterQuality::Low);

}
