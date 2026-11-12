#pragma once

// Colour filters for the quad shader: one row per distinct filter in an RGBA32F table texture
// (columns 0-3 matrix rows, 4 offsets / tint colour, 5 kind + blend mode), reset when full.
// The last kDataRows rows hold per-frame shader data (ripple uniforms), reset every frame.

#include "kiln/QuadRenderer.h"
#include "kiln/graphics/ColorFilter.h"

#include <unordered_map>

namespace kiln {

    class FilterTable {
    public:
        static constexpr std::uint32_t kRows = 512, kCols = 6, kDataRows = 256;
        explicit FilterTable(hearth::Device& device);
        void BeginFrame();
        int RowFor(const gfx::ColorFilter& f);   // >= 0
        // Up to kCols * 4 floats for this frame's draws; -1 when the frame's data rows are used up.
        int RowForData(const float* data, std::size_t count);
        // `rows` consecutive data rows filled from `data` (rows * kCols * 4 floats); the first row,
        // or -1 when the frame's data rows cannot hold them.
        int RowsForData(const float* data, int rows);
        void Sync();
        hearth::Ref<hearth::Texture> Texture() const { return m_Texture; }

    private:
        hearth::Ref<hearth::Texture> m_Texture;
        std::vector<float> m_Data;
        std::unordered_map<std::uint64_t, int> m_Rows;
        int m_Next = 0, m_NextData = 0;
        bool m_Dirty = false, m_Overflowed = false;
    };

}
