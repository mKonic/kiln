#pragma once

// Gradient ramps: one 256-texel row per distinct gradient in a shared texture, rebuilt from
// scratch when the rows run out (at the next frame).

#include "kiln/QuadRenderer.h"
#include "kiln/graphics/Brush.h"

#include <unordered_map>

namespace kiln {

    class GradientRamps {
    public:
        static constexpr std::uint32_t kRows = 256;
        explicit GradientRamps(hearth::Device& device);

        void BeginFrame();
        // Texture v of the brush's ramp row.
        float RowFor(const gfx::Brush& brush);
        void Sync();
        hearth::Ref<hearth::Texture> Texture() const { return m_Page.Texture(); }

    private:
        AtlasPage m_Page;
        std::vector<std::uint8_t> m_Rgba;
        std::unordered_map<std::uint64_t, std::uint32_t> m_Rows;
        std::uint32_t m_Next = 0;
        std::uint32_t m_DirtyFrom = kRows, m_DirtyTo = 0;
        bool m_Overflowed = false;
    };

}
