#include "kiln/Filters.h"

#include <algorithm>

namespace kiln {

    FilterTable::FilterTable(hearth::Device& device) : m_Data(std::size_t(kRows) * kCols * 4, 0.0f) {
        hearth::TextureDesc d;
        d.width = kCols;
        d.height = kRows;
        d.format = hearth::Format::RGBA32_SFLOAT;
        d.minFilter = d.magFilter = hearth::Filter::Nearest;
        d.debugName = "kiln.filters";
        m_Texture = device.CreateTexture(d);
        m_Texture->Upload(m_Data.data(), m_Data.size() * sizeof(float));
    }

    void FilterTable::BeginFrame() {
        m_NextData = 0;
        if (!m_Overflowed) return;
        m_Overflowed = false;
        m_Rows.clear();
        m_Next = 0;
    }

    int FilterTable::RowFor(const gfx::ColorFilter& f) {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&](const void* p, std::size_t n) { auto* b = static_cast<const std::uint8_t*>(p); for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
        mix(&f.kind, 1); mix(&f.color, sizeof(f.color)); mix(&f.mode, 1); mix(f.matrix.data(), sizeof(f.matrix));
        if (auto it = m_Rows.find(h); it != m_Rows.end()) return it->second;
        constexpr int kFilterRows = int(kRows - kDataRows);
        int row = m_Next < kFilterRows ? m_Next++ : (m_Overflowed = true, kFilterRows - 1);
        float* r = &m_Data[std::size_t(row) * kCols * 4];
        if (f.kind == gfx::ColorFilter::Kind::Matrix) {
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) r[i * 4 + j] = f.matrix[std::size_t(i * 5 + j)];
                r[16 + i] = f.matrix[std::size_t(i * 5 + 4)] / 255.0f;
            }
            r[20] = 1;
        } else {
            const float a = float((f.color.argb >> 24) & 0xff) / 255.0f;   // premultiplied tint colour
            r[16] = float((f.color.argb >> 16) & 0xff) / 255.0f * a;
            r[17] = float((f.color.argb >> 8) & 0xff) / 255.0f * a;
            r[18] = float(f.color.argb & 0xff) / 255.0f * a;
            r[19] = a;
            r[20] = 2;
            r[21] = float(f.mode);
        }
        m_Rows[h] = row;
        m_Dirty = true;
        return row;
    }

    int FilterTable::RowForData(const float* data, std::size_t count) {
        if (m_NextData >= int(kDataRows) || count > kCols * 4) return -1;
        const int row = int(kRows - kDataRows) + m_NextData++;
        float* r = &m_Data[std::size_t(row) * kCols * 4];
        std::fill(r, r + kCols * 4, 0.0f);
        std::copy(data, data + count, r);
        m_Dirty = true;
        return row;
    }

    void FilterTable::Sync() {
        if (!m_Dirty) return;
        m_Dirty = false;
        m_Texture->Upload(m_Data.data(), m_Data.size() * sizeof(float));
    }

}
