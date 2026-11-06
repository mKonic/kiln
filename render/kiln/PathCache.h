#pragma once

// Coverage masks of paths in atlas pages (Skia's atlas path renderer, simplified): masks are
// keyed by path, style, scale and subpixel offset, so a path moved by whole pixels reuses its
// mask. Pages keep a CPU copy and upload one dirty rectangle per frame.

#include "kiln/QuadRenderer.h"
#include "kiln/graphics/Rasterizer.h"

#include <memory>
#include <unordered_map>

namespace kiln {

    class PathCache {
    public:
        static constexpr std::uint32_t kPageSize = 2048;

        struct Entry {
            const AtlasPage* page = nullptr;
            RectF uv;
            int x = 0, y = 0, w = 0, h = 0;   // mask rect relative to the integer draw origin
        };

        explicit PathCache(hearth::Device& device) : m_Device(device) {}

        // Starts a frame: drops everything when the pages overflowed last frame.
        void BeginFrame();
        const Entry* Find(std::uint64_t key) const;
        // Copies the mask in; returns null when it cannot be placed (larger than a page).
        const Entry* Insert(std::uint64_t key, const gfx::Mask& mask, int originX, int originY);
        // Uploads what changed this frame.
        void Sync();

    private:
        struct Page {
            std::unique_ptr<AtlasPage> gpu;
            std::vector<std::uint8_t> rgba;
            int shelfY = 0, shelfH = 0, cursorX = 0;
            int dirtyX0 = INT32_MAX, dirtyY0 = INT32_MAX, dirtyX1 = 0, dirtyY1 = 0;
        };
        bool Place(Page& p, int w, int h, int& outX, int& outY);

        hearth::Device& m_Device;
        std::vector<Page> m_Pages;
        std::unordered_map<std::uint64_t, Entry> m_Entries;
        bool m_Overflowed = false;
    };

}
