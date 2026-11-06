#include "kiln/PathCache.h"

#include <algorithm>
#include <climits>

namespace kiln {

    void PathCache::BeginFrame() {
        if (!m_Overflowed) return;
        m_Overflowed = false;
        m_Entries.clear();
        m_Pages.resize(1);
        Page& p = m_Pages[0];
        p.shelfY = p.shelfH = p.cursorX = 0;
    }

    const PathCache::Entry* PathCache::Find(std::uint64_t key) const {
        auto it = m_Entries.find(key);
        return it == m_Entries.end() ? nullptr : &it->second;
    }

    bool PathCache::Place(Page& p, int w, int h, int& outX, int& outY) {
        const int size = int(kPageSize);
        if (p.cursorX + w > size) { p.shelfY += p.shelfH; p.shelfH = 0; p.cursorX = 0; }
        if (p.shelfY + h > size) return false;
        outX = p.cursorX;
        outY = p.shelfY;
        p.cursorX += w;
        p.shelfH = std::max(p.shelfH, h);
        return true;
    }

    const PathCache::Entry* PathCache::Insert(std::uint64_t key, const gfx::Mask& mask, int originX, int originY) {
        const int pad = 1;   // keeps bilinear-free sampling from bleeding into neighbours
        const int w = mask.width + 2 * pad, h = mask.height + 2 * pad;
        if (mask.width <= 0 || w > int(kPageSize) || h > int(kPageSize)) return nullptr;
        int px = 0, py = 0;
        Page* page = nullptr;
        for (Page& p : m_Pages)
            if (Place(p, w, h, px, py)) { page = &p; break; }
        if (!page) {
            m_Overflowed = true;   // reset at the next frame; this frame gets a new page
            m_Pages.emplace_back();
            Page& p = m_Pages.back();
            p.gpu = std::make_unique<AtlasPage>(m_Device, kPageSize, kPageSize);
            p.rgba.assign(std::size_t(kPageSize) * kPageSize * 4, 0);
            if (!Place(p, w, h, px, py)) return nullptr;
            page = &p;
        }
        if (!page->gpu) {
            page->gpu = std::make_unique<AtlasPage>(m_Device, kPageSize, kPageSize);
            page->rgba.assign(std::size_t(kPageSize) * kPageSize * 4, 0);
        }
        // White with coverage in alpha (the TintedCoverage mode reads .a); clear the padding.
        for (int y = 0; y < h; ++y) {
            std::uint8_t* row = &page->rgba[(std::size_t(py + y) * kPageSize + px) * 4];
            for (int x = 0; x < w; ++x) {
                const int mx = x - pad, my = y - pad;
                const bool inside = mx >= 0 && my >= 0 && mx < mask.width && my < mask.height;
                row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
                row[x * 4 + 3] = inside ? mask.alpha[std::size_t(my) * mask.width + mx] : 0;
            }
        }
        page->dirtyX0 = std::min(page->dirtyX0, px);
        page->dirtyY0 = std::min(page->dirtyY0, py);
        page->dirtyX1 = std::max(page->dirtyX1, px + w);
        page->dirtyY1 = std::max(page->dirtyY1, py + h);
        const float inv = 1.0f / float(kPageSize);
        Entry e;
        e.page = page->gpu.get();
        e.uv = { float(px + pad) * inv, float(py + pad) * inv, float(mask.width) * inv, float(mask.height) * inv };
        e.x = mask.x - originX;
        e.y = mask.y - originY;
        e.w = mask.width;
        e.h = mask.height;
        return &(m_Entries[key] = e);
    }

    void PathCache::Sync() {
        for (Page& p : m_Pages) {
            if (!p.gpu || p.dirtyX1 <= p.dirtyX0) continue;
            const std::uint32_t x = std::uint32_t(p.dirtyX0), y = std::uint32_t(p.dirtyY0);
            p.gpu->Upload(&p.rgba[(std::size_t(y) * kPageSize + x) * 4], kPageSize * 4, x, y,
                          std::uint32_t(p.dirtyX1 - p.dirtyX0), std::uint32_t(p.dirtyY1 - p.dirtyY0));
            p.dirtyX0 = p.dirtyY0 = INT32_MAX;
            p.dirtyX1 = p.dirtyY1 = 0;
        }
    }

}
