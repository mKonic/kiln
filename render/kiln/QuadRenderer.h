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
        // A whole image uploaded once, with a mip chain for minified drawing.
        AtlasPage(hearth::Device& device, std::uint32_t width, std::uint32_t height, const void* rgba, bool mipmaps);
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

        // A gradient paint instead of the flat colour: geometry in framebuffer pixels (linear
        // x0,y0,x1,y1; radial cx,cy,r; sweep cx,cy), the ramp row in the ramp texture.
        struct Paint {
            enum class Kind : std::uint8_t { Color = 0, Linear = 1, Radial = 2, Sweep = 3 };
            Kind kind = Kind::Color;
            float geometry[4] = {};
            std::uint8_t tile = 0;       // gfx::TileMode
            float rampV = 0;             // texture v of the ramp row centre
        };

        explicit QuadRenderer(hearth::Device& device);

        void Begin(std::uint32_t viewportWidth, std::uint32_t viewportHeight);
        // With a paint, colour.a modulates the gradient.
        void FillRect(const RectF& rect, Rgba color, float cornerRadius = 0.0f, const Paint* paint = nullptr);
        void DrawAtlas(const RectF& rect, const RectF& uv, const AtlasPage& page, Rgba color, Mode mode,
                       const Paint* paint = nullptr);
        // The gradient ramp texture (bound next to every atlas page).
        // Drops the cached bind group of a page about to be destroyed.
        void ForgetPage(const AtlasPage* page) { std::erase_if(m_Groups, [page](const auto& g) { return g.first == page; }); }
        void SetRampTexture(hearth::Ref<hearth::Texture> ramp) { m_Ramp = std::move(ramp); m_Groups.clear(); }
        // Clip applied to quads submitted after this call: rect in framebuffer pixels (x0, y0,
        // x1, y1) with a corner radius; nullopt-like when enabled is false.
        struct Clip { float x0 = 0, y0 = 0, x1 = 0, y1 = 0, radius = 0; bool enabled = false; };
        void SetClip(const Clip& clip) { m_Clip = clip; }
        void Flush(hearth::CommandList& cmd);

    private:
        struct Instance { float rect[4]; float uv[4]; float color[4]; float params[4]; float clip[4]; float clipParams[4]; float paint[4]; float paint2[4]; };
        struct Run { const AtlasPage* page; std::uint32_t first, count; };

        void Push(const Instance& instance, const AtlasPage* page, const Paint* paint);
        hearth::Ref<hearth::BindGroup> GroupFor(const AtlasPage* page);

        hearth::Device& m_Device;
        hearth::Ref<hearth::Pipeline> m_Pipeline;
        hearth::Ref<hearth::Texture> m_White;
        hearth::Ref<hearth::Texture> m_Ramp;
        std::vector<Instance> m_Instances;
        std::vector<Run> m_Runs;
        std::vector<hearth::Ref<hearth::Buffer>> m_Buffers;   // one per frame in flight
        std::vector<std::pair<const AtlasPage*, hearth::Ref<hearth::BindGroup>>> m_Groups;
        float m_Viewport[2] = { 1, 1 };
        Clip m_Clip;
    };

}
