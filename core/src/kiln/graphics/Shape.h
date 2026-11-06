#pragma once

// Shape -> Outline (androidx.compose.ui.graphics.Shape / Outline): what clip, background and
// border draw for a size. Rect and rounded-rect outlines take fast paths; anything else is a path.

#include "kiln/graphics/Path.h"
#include "kiln/ui/Geometry.h"

#include <memory>
#include <functional>
#include <variant>

namespace kiln::gfx {

    struct Outline {
        std::variant<ui::Rect, RoundRect, Path> value;
        ui::Rect Bounds() const {
            if (auto* r = std::get_if<ui::Rect>(&value)) return *r;
            if (auto* rr = std::get_if<RoundRect>(&value)) return rr->rect;
            return std::get<Path>(value).Bounds();
        }
        // The outline as a path (for clipping and filling generically).
        Path ToPath() const {
            if (auto* p = std::get_if<Path>(&value)) return *p;
            Path p;
            if (auto* r = std::get_if<ui::Rect>(&value)) p.AddRect(*r);
            else p.AddRoundRect(std::get<RoundRect>(value));
            return p;
        }
        // isInOutline (ShapeContainingUtil.kt): the rect, the round rect's corner ellipses, or
        // the path by its fill type.
        bool Contains(ui::Offset p) const;
    };

    class Shape {
    public:
        virtual ~Shape() = default;
        virtual Outline CreateOutline(ui::Size size, ui::LayoutDirection dir, const ui::Density& density) const = 0;
        virtual bool Equals(const Shape& other) const { return this == &other; }
    };

    // GenericShape { size, direction -> path }.
    class GenericShape final : public Shape {
    public:
        using Builder = std::function<void(Path&, ui::Size, ui::LayoutDirection)>;
        explicit GenericShape(Builder b) : m_Builder(std::move(b)) {}
        Outline CreateOutline(ui::Size size, ui::LayoutDirection dir, const ui::Density&) const override {
            Path p;
            m_Builder(p, size, dir);
            return { std::move(p) };
        }
    private:
        Builder m_Builder;
    };

}
