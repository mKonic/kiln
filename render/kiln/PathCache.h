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
        // Copies the mask in. A mask larger than a page (a full-screen path on a tall screen)
        // gets a page of its own; the few such pages are reused oldest first.
        const Entry* Insert(std::uint64_t key, const gfx::Mask& mask, int originX, int originY);
        // Uploads what changed this frame.
        void Sync();

    private:
        static constexpr std::size_t kMaxDedicatedPages = 4;
        struct Page {
            std::unique_ptr<AtlasPage> gpu;
            std::vector<std::uint8_t> rgba;
            int width = int(kPageSize), height = int(kPageSize);
            bool dedicated = false;          // one oversized mask
            std::uint64_t dedicatedKey = 0;  // the entry it holds
            std::uint64_t lastUse = 0;
            int shelfY = 0, shelfH = 0, cursorX = 0;
            int dirtyX0 = INT32_MAX, dirtyY0 = INT32_MAX, dirtyX1 = 0, dirtyY1 = 0;
        };
        bool Place(Page& p, int w, int h, int& outX, int& outY);
        Page* DedicatedPage(std::uint64_t key, int w, int h);

        hearth::Device& m_Device;
        std::vector<Page> m_Pages;
        std::unordered_map<std::uint64_t, Entry> m_Entries;
        // Pages replaced while a frame in flight may still sample them.
        static constexpr int kRetireFrames = 4;
        struct Retired { std::unique_ptr<AtlasPage> page; int frames; };
        std::vector<Retired> m_Retired;
        bool m_Overflowed = false;
        std::uint64_t m_Inserts = 0;
    };

}
