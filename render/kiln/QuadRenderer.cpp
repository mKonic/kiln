#include "kiln/QuadRenderer.h"

#include <kiln/Shaders.h>

#include <cstring>

namespace kiln {

    using namespace hearth;

    AtlasPage::AtlasPage(Device& device, std::uint32_t width, std::uint32_t height)
        : m_Width(width), m_Height(height) {
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = Format::RGBA8_UNORM;
        desc.debugName = "kiln.atlas";
        m_Texture = device.CreateTexture(desc);
        std::vector<std::uint8_t> clear(std::size_t(width) * height * 4, 0);
        m_Texture->Upload(clear.data(), clear.size());
    }

    void AtlasPage::Upload(const void* rgba, std::uint32_t strideBytes, std::uint32_t x,
                           std::uint32_t y, std::uint32_t w, std::uint32_t h) {
        if (w == 0 || h == 0) return;
        const auto* src = static_cast<const std::uint8_t*>(rgba);
        if (strideBytes == w * 4) {
            m_Texture->UploadRegion(src, x, y, w, h);
            return;
        }
        m_Scratch.resize(std::size_t(w) * h * 4);
        for (std::uint32_t row = 0; row < h; ++row)
            std::memcpy(&m_Scratch[std::size_t(row) * w * 4], src + std::size_t(row) * strideBytes, w * 4);
        m_Texture->UploadRegion(m_Scratch.data(), x, y, w, h);
    }

    QuadRenderer::QuadRenderer(Device& device) : m_Device(device) {
        auto vs = device.CreateShader({ { std::begin(shaders::kQuadVert), std::end(shaders::kQuadVert) },
                                        ShaderStage::Vertex, "main", "kiln.quad.vert" });
        auto fs = device.CreateShader({ { std::begin(shaders::kQuadFrag), std::end(shaders::kQuadFrag) },
                                        ShaderStage::Fragment, "main", "kiln.quad.frag" });
        PipelineDesc desc;
        desc.vertex = vs;
        desc.fragment = fs;
        VertexBufferLayout layout;
        layout.stride = sizeof(Instance);
        layout.stepMode = VertexStepMode::Instance;
        for (u32 i = 0; i < 6; ++i)
            layout.attributes.push_back({ i, Format::RGBA32_SFLOAT, i * 16 });
        desc.vertexBuffers.push_back(layout);
        desc.bindings.push_back({ 0, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.pushConstantSize = 8;
        desc.topology = PrimitiveTopology::TriangleStrip;
        desc.blend = BlendMode::AlphaPremultiplied;
        desc.colorFormats = { device.SwapchainFormat() };
        desc.debugName = "kiln.quad";
        m_Pipeline = device.CreatePipeline(desc);
        m_White = device.CreateSolidTexture(0xffffffffu);
        m_Buffers.resize(device.Caps().framesInFlight);
    }

    void QuadRenderer::Begin(std::uint32_t w, std::uint32_t h) {
        m_Viewport[0] = float(w);
        m_Viewport[1] = float(h);
        m_Instances.clear();
        m_Runs.clear();
        m_Clip = {};
    }

    void QuadRenderer::Push(const Instance& in, const AtlasPage* page) {
        Instance instance = in;
        instance.clip[0] = m_Clip.x0; instance.clip[1] = m_Clip.y0; instance.clip[2] = m_Clip.x1; instance.clip[3] = m_Clip.y1;
        instance.clipParams[0] = m_Clip.radius; instance.clipParams[1] = m_Clip.enabled ? 1.0f : 0.0f;
        instance.clipParams[2] = instance.clipParams[3] = 0;
        if (m_Runs.empty() || m_Runs.back().page != page)
            m_Runs.push_back({ page, std::uint32_t(m_Instances.size()), 0 });
        m_Instances.push_back(instance);
        ++m_Runs.back().count;
    }

    void QuadRenderer::FillRect(const RectF& r, Rgba c, float radius) {
        // Solid quads ignore the atlas, so they join whatever run is open instead of splitting it.
        const AtlasPage* page = m_Runs.empty() ? nullptr : m_Runs.back().page;
        Push({ { r.x, r.y, r.w, r.h }, { 0, 0, 0, 0 }, { c.r, c.g, c.b, c.a },
               { radius, float(Mode::Solid), 0, 0 }, {}, {} }, page);
    }

    void QuadRenderer::DrawAtlas(const RectF& r, const RectF& uv, const AtlasPage& page, Rgba c, Mode mode) {
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { c.r, c.g, c.b, c.a },
               { 0, float(mode), 0, 0 }, {}, {} }, &page);
    }

    Ref<BindGroup> QuadRenderer::GroupFor(const AtlasPage* page) {
        for (auto& [p, g] : m_Groups)
            if (p == page) return g;
        BindGroupEntry entry;
        entry.binding = 0;
        entry.type = BindingType::SampledTexture;
        entry.textures = { page ? page->Texture() : m_White };
        auto group = m_Device.CreateBindGroup(m_Pipeline, { entry });
        m_Groups.emplace_back(page, group);
        return group;
    }

    void QuadRenderer::Flush(CommandList& cmd) {
        if (m_Instances.empty()) return;
        auto& buffer = m_Buffers[m_Device.FrameIndex() % m_Buffers.size()];
        const u64 bytes = m_Instances.size() * sizeof(Instance);
        if (!buffer || buffer->Size() < bytes) {
            BufferDesc desc;
            desc.size = bytes * 2;
            desc.usage = BufferUsage::Vertex;
            desc.memory = MemoryKind::HostVisible;
            desc.debugName = "kiln.quads";
            buffer = m_Device.CreateBuffer(desc);
        }
        buffer->Upload(m_Instances.data(), bytes);
        cmd.BindPipeline(m_Pipeline);
        cmd.SetViewport({ 0, 0, m_Viewport[0], m_Viewport[1], 0, 1 });
        cmd.SetScissor({ 0, 0, u32(m_Viewport[0]), u32(m_Viewport[1]) });
        cmd.SetPushConstants(m_Viewport, sizeof(m_Viewport));
        cmd.BindVertexBuffer(0, buffer);
        for (const Run& run : m_Runs) {
            cmd.BindBindGroup(GroupFor(run.page));
            cmd.Draw(4, run.count, 0, run.first);
        }
    }

}
