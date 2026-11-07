#pragma once

// GPU textures for ImageBitmaps, created on first draw and released a few frames after the
// last handle to the bitmap is gone (frames in flight may still sample them).

#include "kiln/QuadRenderer.h"
#include "kiln/graphics/Image.h"

#include <unordered_map>
#include <vector>

namespace kiln {

    class ImageCache {
    public:
        ImageCache(hearth::Device& device, QuadRenderer& quads) : m_Device(device), m_Quads{ &quads } {}
        // Other renderers drawing the same images (extra windows): told when a page goes.
        void AddRenderer(QuadRenderer& quads) { m_Quads.push_back(&quads); }
        void RemoveRenderer(QuadRenderer& quads) { std::erase(m_Quads, &quads); }
        void BeginFrame();
        const AtlasPage* PageFor(const gfx::ImageBitmap& image);

    private:
        struct Entry { std::unique_ptr<AtlasPage> page; std::weak_ptr<const void> token; int deadFrames = 0; };
        hearth::Device& m_Device;
        std::vector<QuadRenderer*> m_Quads;
        std::unordered_map<std::uint64_t, Entry> m_Entries;
    };

}
