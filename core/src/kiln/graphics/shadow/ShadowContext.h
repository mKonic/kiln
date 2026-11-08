#pragma once

#include "kiln/graphics/Image.h"
#include "kiln/graphics/Shape.h"
#include "kiln/graphics/shadow/Shadow.h"

namespace kiln::gfx::shadow {
    struct ShadowMask {
        // Opaque packed channels: R = bitmap-shader alpha, G = final geometry coverage.
        // They must stay separate for Src/Clear and alpha-changing colour filters.
        ImageBitmap image;
        ui::Offset origin;
    };

    // Shared by an owner's painters. Drop offsets and fill properties do not change a mask;
    // inner offsets do. Weak entries let animated, no-longer-used masks go away with their painters.
    class ShadowContext {
    public:
        std::shared_ptr<const ShadowMask> Obtain(std::shared_ptr<const Shape> shape, ui::Size size,
                                                 ui::LayoutDirection direction, const ui::Density& density,
                                                 const Shadow& shadow, bool inner);
        void ClearCache() { m_Cache.clear(); }
    private:
        struct Entry {
            std::shared_ptr<const Shape> shape;
            ui::Size size;
            ui::LayoutDirection direction;
            float density, radius, spread;
            ui::Offset offset;
            bool inner;
            std::weak_ptr<const ShadowMask> mask;
        };
        std::vector<Entry> m_Cache;
    };
}
