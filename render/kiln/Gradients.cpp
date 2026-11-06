#include "kiln/Gradients.h"

#include <algorithm>
#include <cstring>

namespace kiln {

    GradientRamps::GradientRamps(hearth::Device& device)
        : m_Page(device, 256, kRows), m_Rgba(std::size_t(256) * kRows * 4, 0) {}

    void GradientRamps::BeginFrame() {
        if (!m_Overflowed) return;
        m_Overflowed = false;
        m_Rows.clear();
        m_Next = 0;
    }

    float GradientRamps::RowFor(const gfx::Brush& b) {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&](const void* data, std::size_t n) {
            const auto* p = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        };
        mix(b.colors.data(), b.colors.size() * sizeof(gfx::Color));
        mix(b.stops.data(), b.stops.size() * sizeof(float));
        auto it = m_Rows.find(h);
        std::uint32_t row;
        if (it != m_Rows.end()) {
            row = it->second;
        } else {
            if (m_Next >= kRows) { m_Overflowed = true; row = kRows - 1; }   // reused this frame, reset next
            else row = m_Next++;
            gfx::BuildRamp(b, &m_Rgba[std::size_t(row) * 256 * 4]);
            m_Rows[h] = row;
            m_DirtyFrom = std::min(m_DirtyFrom, row);
            m_DirtyTo = std::max(m_DirtyTo, row + 1);
        }
        return (float(row) + 0.5f) / float(kRows);
    }

    void GradientRamps::Sync() {
        if (m_DirtyTo <= m_DirtyFrom) return;
        m_Page.Upload(&m_Rgba[std::size_t(m_DirtyFrom) * 256 * 4], 256 * 4, 0, m_DirtyFrom, 256, m_DirtyTo - m_DirtyFrom);
        m_DirtyFrom = kRows;
        m_DirtyTo = 0;
    }

}
