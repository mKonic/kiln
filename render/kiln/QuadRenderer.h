#pragma once

#include <hearth/Device.h>

#include <cstdint>
#include <memory>
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
        // Wraps an existing texture (an offscreen layer's colour attachment).
        AtlasPage(hearth::Ref<hearth::Texture> texture, std::uint32_t width, std::uint32_t height)
            : m_Texture(std::move(texture)), m_Width(width), m_Height(height) {}
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
        enum class Mode : std::uint8_t { Solid = 0, TintedCoverage = 1, Color = 2, Stretch = 3, Blur = 4 };

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
        // filterRow: a row of the filter table (FilterTable), or -1 for none.
        void FillRect(const RectF& rect, Rgba color, float cornerRadius = 0.0f, const Paint* paint = nullptr, int filterRow = -1);
        // nearest: Color mode samples the nearest texel instead of filtering (FilterQuality.None).
        void DrawAtlas(const RectF& rect, const RectF& uv, const AtlasPage& page, Rgba color, Mode mode,
                       const Paint* paint = nullptr, int filterRow = -1, bool nearest = false);
        void SetFilterTexture(hearth::Ref<hearth::Texture> t) { m_Filters = std::move(t); m_Groups.clear(); }
        // The gradient ramp texture (bound next to every atlas page).
        // Drops the cached bind group of a page about to be destroyed.
        void ForgetPage(const AtlasPage* page) { std::erase_if(m_Groups, [page](const auto& g) { return g.page == page || g.dst == page; }); }
        void SetRampTexture(hearth::Ref<hearth::Texture> ramp) { m_Ramp = std::move(ramp); m_Groups.clear(); }
        // Affine transform (x' = a*x + c*y + e, y' = b*x + d*y + f) applied to quads submitted
        // after this call: their rects are in its local space.
        // Affine a..f; g, h are the perspective row (x' = (a x + c y + e) / (g x + h y + 1)).
        struct Transform { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0, g = 0, h = 0; };
        void SetTransform(const Transform& t) { m_Transform = t; }
        // Blend mode of quads submitted after this call (gfx::BlendMode order; 3 = SrcOver). Other
        // modes read the target: what it holds so far renders into a copy that the quad samples.
        void SetBlendMode(int mode) { m_BlendMode = mode; }
        // The main target's clear colour (destination copies of it start from the same pixels).
        void SetClearColor(float r, float g, float b, float a) { m_RootClear[0] = r; m_RootClear[1] = g; m_RootClear[2] = b; m_RootClear[3] = a; }
        // Clip applied to quads submitted after this call: a rounded rect (x0, y0, x1, y1, radius)
        // in the space `inverse` maps framebuffer pixels into (identity: framebuffer pixels).
        struct Clip { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; float radii[4] = {}; bool enabled = false; Transform inverse; };   // radii: TL, TR, BR, BL
        void SetClip(const Clip& clip) { m_Clip = clip; }
        // Multiplies what the current target holds by the coverage in `page` (DstIn): masks an
        // offscreen layer to a path before it is composited.
        void DrawMask(const RectF& rect, const RectF& uv, const AtlasPage& page);
        // A layer composited through HWUI's stretch (StretchEffect.cpp): stretchX/Y are the
        // normalised overscroll vector (dampStretchVector of the edge distances).
        void DrawStretched(const RectF& rect, const RectF& uv, const AtlasPage& page, float alpha, float stretchX, float stretchY);
        // One Gaussian pass over a layer: `step` is one texel along the pass axis in uv, `sigma` in
        // texels; outside the layer the edge repeats, or with `decal` is transparent.
        void DrawBlur(const RectF& rect, const RectF& uv, const AtlasPage& page, float alpha, float stepU, float stepV, float sigma, bool decal);
        // Triangles (a list, three vertices each) with per-vertex colour: positions in device
        // pixels, colours premultiplied. Clipped like quads; always SrcOver.
        struct Vertex { float x, y; float r, g, b, a; };
        void DrawTriangles(const Vertex* vertices, std::size_t count);

        // Offscreen layers: quads submitted between BeginLayer and EndLayer render into a target
        // covering the device-space rect `bounds` (whole pixels). EndLayer returns the texture and
        // its uv extent; draw it back with DrawAtlas(Mode::Color). Layers nest.
        void BeginLayer(int x, int y, int width, int height);
        struct LayerResult { const AtlasPage* page = nullptr; RectF uv; };
        LayerResult EndLayer();

        // Records the offscreen layers' passes; call before the main render pass.
        void RenderLayers(hearth::CommandList& cmd);
        // Draws the main batch inside the open (swapchain) render pass.
        void Flush(hearth::CommandList& cmd);

    private:
        struct Instance { float rect[4]; float uv[4]; float color[4]; float params[4]; float clip[4]; float clipParams[4]; float paint[4]; float paint2[4];
                         float xform[4]; float xform2[4]; float clipInv[4]; float clipInv2[4]; };
        struct Run { const AtlasPage* page; std::uint32_t first, count; bool mask = false; bool filtered = false; const AtlasPage* dst = nullptr;
                     bool triangles = false; Clip clip{}; };   // triangles: first/count into m_TriVerts

        void Push(const Instance& instance, const AtlasPage* page, const Paint* paint, bool mask = false);
        struct Target {
            std::vector<Instance> instances;
            std::vector<Run> runs;
            float origin[2] = { 0, 0 }, size[2] = { 1, 1 };
            hearth::RenderTarget* rt = nullptr;   // null: the swapchain
            std::uint32_t base = 0;               // first instance in the shared buffer
            // A destination copy: draws the first runs of target `alias` (its instances),
            // within the scissor (target pixels x, y, w, h).
            bool aliased = false;
            std::size_t alias = 0;
            std::uint32_t scissor[4] = { 0, 0, 0, 0 };
            float clear[4] = { 0, 0, 0, 0 };
        };
        // The current target's content so far, rendered into a layer within the device bounds
        // of `instance` (for a blend mode's destination reads).
        const AtlasPage* DestinationCopy(const Instance& instance);
        struct PooledLayer;
        PooledLayer& AcquireLayer(int width, int height);
        struct PooledLayer {
            hearth::Ref<hearth::RenderTarget> rt;
            std::unique_ptr<AtlasPage> page;
            std::uint32_t width = 0, height = 0;
            bool used = false;
        };
        Target& Current() { return m_Targets[m_Stack.back()]; }
        void Upload();
        void DrawTarget(hearth::CommandList& cmd, const Target& t);
        hearth::Ref<hearth::BindGroup> GroupFor(const AtlasPage* page, const AtlasPage* dst = nullptr);

        hearth::Device& m_Device;
        hearth::Ref<hearth::Pipeline> m_Pipeline;
        hearth::Ref<hearth::Pipeline> m_MaskPipeline;
        hearth::Ref<hearth::Pipeline> m_FilteredPipeline;
        hearth::Ref<hearth::Pipeline> m_BlendPipeline;
        hearth::Ref<hearth::Pipeline> m_TriPipeline;
        std::vector<Vertex> m_TriVerts;
        std::vector<hearth::Ref<hearth::Buffer>> m_TriBuffers;   // one per frame in flight   // filtered shader, no fixed-function blending
        int m_BlendMode = 3;
        float m_RootClear[4] = { 0, 0, 0, 1 };
        hearth::Ref<hearth::Texture> m_White;
        hearth::Ref<hearth::Texture> m_Ramp;
        hearth::Ref<hearth::Texture> m_Filters;
        std::vector<Target> m_Targets;            // [0] is the main batch
        std::vector<std::size_t> m_Stack;         // open targets
        std::vector<std::size_t> m_LayerOrder;    // finished layers, inner ones first
        std::vector<std::vector<PooledLayer>> m_Pool;   // per frame in flight
        bool m_Uploaded = false;
        std::vector<const AtlasPage*> m_LayerPages;   // per offscreen target (index - 1)
        std::vector<Instance> m_Staging;
        std::vector<hearth::Ref<hearth::Buffer>> m_Buffers;   // one per frame in flight
        struct Group { const AtlasPage* page; const AtlasPage* dst; hearth::Ref<hearth::BindGroup> group; };
        std::vector<Group> m_Groups;
        float m_Viewport[2] = { 1, 1 };
        Clip m_Clip;
        Transform m_Transform;
    };

}
