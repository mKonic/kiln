#pragma once

#include <hearth/Device.h>

#include <cstdint>
#include <vector>

namespace kiln {

    // Premultiplied linear-in-gamma RGBA, the space Android's HWUI blends in (the swapchain is
    // UNORM on purpose, see hearth VulkanSwapchain).
    struct Rgba {
        float r = 0, g = 0, b = 0, a = 1;
        static Rgba FromArgb(std::uint32_t argb) {
            const float a = ((argb >> 24) & 0xff) / 255.0f;
            return { ((argb >> 16) & 0xff) / 255.0f * a, ((argb >> 8) & 0xff) / 255.0f * a,
                     (argb & 0xff) / 255.0f * a, a };
        }
    };

    struct RectF { float x = 0, y = 0, w = 0, h = 0; };

    // An RGBA8 texture page that glyphs or images are packed into. The CPU copy lives with
    // whoever rasterises into it; Upload() pushes the dirty region to the GPU before drawing.
    class AtlasPage {
    public:
        AtlasPage(hearth::Device& device, std::uint32_t width, std::uint32_t height);
        void Upload(const void* rgba, std::uint32_t strideBytes, std::uint32_t x, std::uint32_t y,
                    std::uint32_t w, std::uint32_t h);
        const hearth::Ref<hearth::Texture>& Texture() const { return m_Texture; }
        std::uint32_t Width() const { return m_Width; }
        std::uint32_t Height() const { return m_Height; }

    private:
        hearth::Ref<hearth::Texture> m_Texture;
        std::uint32_t m_Width, m_Height;
        std::vector<std::uint8_t> m_Scratch;
    };

    // Batches instanced quads: anti-aliased rounded rects, tinted coverage glyphs and colour
    // glyphs/images. One draw per run of quads sharing an atlas page, in submission order.
    class QuadRenderer {
    public:
        enum class Mode : std::uint8_t { Solid = 0, TintedCoverage = 1, Color = 2 };

        explicit QuadRenderer(hearth::Device& device);

        void Begin(std::uint32_t viewportWidth, std::uint32_t viewportHeight);
        void FillRect(const RectF& rect, Rgba color, float cornerRadius = 0.0f);
        void DrawAtlas(const RectF& rect, const RectF& uv, const AtlasPage& page, Rgba color, Mode mode);
        void Flush(hearth::CommandList& cmd);

    private:
        struct Instance { float rect[4]; float uv[4]; float color[4]; float params[4]; };
        struct Run { const AtlasPage* page; std::uint32_t first, count; };

        void Push(const Instance& instance, const AtlasPage* page);
        hearth::Ref<hearth::BindGroup> GroupFor(const AtlasPage* page);

        hearth::Device& m_Device;
        hearth::Ref<hearth::Pipeline> m_Pipeline;
        hearth::Ref<hearth::Texture> m_White;
        std::vector<Instance> m_Instances;
        std::vector<Run> m_Runs;
        std::vector<hearth::Ref<hearth::Buffer>> m_Buffers;   // one per frame in flight
        std::vector<std::pair<const AtlasPage*, hearth::Ref<hearth::BindGroup>>> m_Groups;
        float m_Viewport[2] = { 1, 1 };
    };

}
