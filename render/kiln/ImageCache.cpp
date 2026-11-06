#include "kiln/ImageCache.h"

namespace kiln {

    void ImageCache::BeginFrame() {
        const int keep = int(m_Device.Caps().framesInFlight) + 1;
        for (auto it = m_Entries.begin(); it != m_Entries.end();) {
            if (it->second.token.expired() && ++it->second.deadFrames > keep) { m_Quads.ForgetPage(it->second.page.get()); it = m_Entries.erase(it); }
            else ++it;
        }
    }

    const AtlasPage* ImageCache::PageFor(const gfx::ImageBitmap& image) {
        if (!image) return nullptr;
        auto it = m_Entries.find(image.Id());
        if (it != m_Entries.end()) return it->second.page.get();
        Entry e;
        e.page = std::make_unique<AtlasPage>(m_Device, std::uint32_t(image.Width()), std::uint32_t(image.Height()), image.Pixels(), true);
        e.token = image.Token();
        return (m_Entries[image.Id()] = std::move(e)).page.get();
    }

}
