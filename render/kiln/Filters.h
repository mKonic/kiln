#pragma once

// Colour filters for the quad shader: one row per distinct filter in an RGBA32F table texture
// (columns 0-3 matrix rows, 4 offsets / tint colour, 5 kind + blend mode), reset when full.

#include "kiln/QuadRenderer.h"
#include "kiln/graphics/ColorFilter.h"

#include <unordered_map>

namespace kiln {

    class FilterTable {
    public:
        static constexpr std::uint32_t kRows = 256, kCols = 6;
        explicit FilterTable(hearth::Device& device);
        void BeginFrame();
        int RowFor(const gfx::ColorFilter& f);   // >= 0
        void Sync();
        hearth::Ref<hearth::Texture> Texture() const { return m_Texture; }

    private:
        hearth::Ref<hearth::Texture> m_Texture;
        std::vector<float> m_Data;
        std::unordered_map<std::uint64_t, int> m_Rows;
        int m_Next = 0;
        bool m_Dirty = false, m_Overflowed = false;
    };

}
