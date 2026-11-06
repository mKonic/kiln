#include "kiln/QuadRenderer.h"

#include <kiln/Shaders.h>

#include <algorithm>
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

    AtlasPage::AtlasPage(Device& device, std::uint32_t width, std::uint32_t height, const void* rgba, bool mipmaps)
        : m_Width(width), m_Height(height) {
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = Format::RGBA8_UNORM;
        desc.generateMipmaps = mipmaps;
        desc.debugName = "kiln.image";
        m_Texture = device.CreateTexture(desc);
        m_Texture->Upload(rgba, std::size_t(width) * height * 4);
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
        for (u32 i = 0; i < 12; ++i)
            layout.attributes.push_back({ i, Format::RGBA32_SFLOAT, i * 16 });
        desc.vertexBuffers.push_back(layout);
        desc.bindings.push_back({ 0, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.bindings.push_back({ 1, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.bindings.push_back({ 2, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.pushConstantSize = 16;   // viewport size, device origin of the target
        desc.topology = PrimitiveTopology::TriangleStrip;
        desc.blend = BlendMode::AlphaPremultiplied;
        desc.colorFormats = { device.SwapchainFormat() };
        desc.debugName = "kiln.quad";
        m_Pipeline = device.CreatePipeline(desc);
        desc.fragment = device.CreateShader({ { std::begin(shaders::kQuadFilteredFrag), std::end(shaders::kQuadFilteredFrag) },
                                              ShaderStage::Fragment, "main", "kiln.quad_filtered.frag" });
        desc.debugName = "kiln.quad.filtered";
        m_FilteredPipeline = device.CreatePipeline(desc);
        desc.fragment = fs;
        // DstIn: dst * src.alpha, for masking a layer to a path.
        desc.blend = BlendMode::Custom;
        desc.blendFactors = { BlendFactor::Zero, BlendFactor::SrcAlpha, BlendOp::Add, BlendFactor::Zero, BlendFactor::SrcAlpha, BlendOp::Add };
        desc.debugName = "kiln.quad.mask";
        m_MaskPipeline = device.CreatePipeline(desc);
        m_White = device.CreateSolidTexture(0xffffffffu);
        m_Buffers.resize(device.Caps().framesInFlight);
        m_Pool.resize(device.Caps().framesInFlight);
    }

    void QuadRenderer::Begin(std::uint32_t w, std::uint32_t h) {
        m_Viewport[0] = float(w);
        m_Viewport[1] = float(h);
        m_Targets.clear();
        m_Targets.emplace_back();
        m_Targets[0].size[0] = float(w);
        m_Targets[0].size[1] = float(h);
        m_Stack = { 0 };
        m_LayerOrder.clear();
        m_LayerPages.clear();
        m_Uploaded = false;
        for (auto& l : m_Pool[m_Device.FrameIndex() % m_Pool.size()]) l.used = false;
        m_Clip = {};
        m_Transform = {};
    }

    void QuadRenderer::Push(const Instance& in, const AtlasPage* page, const Paint* paint, bool mask) {
        Instance instance = in;
        if (paint && paint->kind != Paint::Kind::Color) {
            for (int i = 0; i < 4; ++i) instance.paint[i] = paint->geometry[i];
            instance.paint2[0] = float(paint->kind);
            instance.paint2[1] = float(paint->tile);
            instance.paint2[2] = paint->rampV;
            instance.paint2[3] = 0;
        } else {
            for (int i = 0; i < 4; ++i) instance.paint[i] = instance.paint2[i] = 0;
        }
        instance.clip[0] = m_Clip.x0; instance.clip[1] = m_Clip.y0; instance.clip[2] = m_Clip.x1; instance.clip[3] = m_Clip.y1;
        instance.clipParams[0] = m_Clip.radius; instance.clipParams[1] = m_Clip.enabled ? 1.0f : 0.0f;
        instance.clipParams[2] = instance.clipParams[3] = 0;
        const Transform& t = m_Transform;
        instance.xform[0] = t.a; instance.xform[1] = t.b; instance.xform[2] = t.c; instance.xform[3] = t.d;
        instance.xform2[0] = t.e; instance.xform2[1] = t.f; instance.xform2[2] = instance.xform2[3] = 0;
        const Transform& ci = m_Clip.inverse;
        instance.clipInv[0] = ci.a; instance.clipInv[1] = ci.b; instance.clipInv[2] = ci.c; instance.clipInv[3] = ci.d;
        instance.clipInv2[0] = ci.e; instance.clipInv2[1] = ci.f; instance.clipInv2[2] = instance.clipInv2[3] = 0;
        Target& t2 = Current();
        const bool filtered = instance.params[2] >= 0;
        if (t2.runs.empty() || t2.runs.back().page != page || t2.runs.back().mask != mask || t2.runs.back().filtered != filtered)
            t2.runs.push_back({ page, std::uint32_t(t2.instances.size()), 0, mask, filtered });
        t2.instances.push_back(instance);
        ++t2.runs.back().count;
    }

    void QuadRenderer::FillRect(const RectF& r, Rgba c, float radius, const Paint* paint, int filterRow) {
        // Solid quads ignore the atlas, so they join whatever run is open instead of splitting it.
        const bool openMask = !Current().runs.empty() && (Current().runs.back().mask || Current().runs.back().filtered != (filterRow >= 0));
        const AtlasPage* page = Current().runs.empty() || openMask ? nullptr : Current().runs.back().page;
        Push({ { r.x, r.y, r.w, r.h }, { 0, 0, 0, 0 }, { c.r, c.g, c.b, c.a },
               { radius, float(Mode::Solid), float(filterRow), 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, page, paint);
    }

    void QuadRenderer::DrawAtlas(const RectF& r, const RectF& uv, const AtlasPage& page, Rgba c, Mode mode, const Paint* paint, int filterRow) {
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { c.r, c.g, c.b, c.a },
               { 0, float(mode), float(filterRow), 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, paint);
    }

    Ref<BindGroup> QuadRenderer::GroupFor(const AtlasPage* page) {
        for (auto& [p, g] : m_Groups)
            if (p == page) return g;
        BindGroupEntry entry;
        entry.binding = 0;
        entry.type = BindingType::SampledTexture;
        entry.textures = { page ? page->Texture() : m_White };
        BindGroupEntry ramp;
        ramp.binding = 1;
        ramp.type = BindingType::SampledTexture;
        ramp.textures = { m_Ramp ? m_Ramp : m_White };
        BindGroupEntry filters;
        filters.binding = 2;
        filters.type = BindingType::SampledTexture;
        filters.textures = { m_Filters ? m_Filters : m_White };
        auto group = m_Device.CreateBindGroup(m_Pipeline, { entry, ramp, filters });
        m_Groups.emplace_back(page, group);
        return group;
    }

    void QuadRenderer::DrawMask(const RectF& r, const RectF& uv, const AtlasPage& page) {
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { 1, 1, 1, 1 },
               { 0, float(Mode::TintedCoverage), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, nullptr, true);
    }

    void QuadRenderer::BeginLayer(int x, int y, int w, int h) {
        // Pooled targets come in 64 px steps so animated sizes reuse them.
        const std::uint32_t tw = std::uint32_t((std::max(w, 1) + 63) / 64 * 64), th = std::uint32_t((std::max(h, 1) + 63) / 64 * 64);
        auto& pool = m_Pool[m_Device.FrameIndex() % m_Pool.size()];
        PooledLayer* layer = nullptr;
        for (auto& l : pool) if (!l.used && l.width == tw && l.height == th) { layer = &l; break; }
        if (!layer) {
            RenderTargetDesc d;
            d.width = tw;
            d.height = th;
            d.colorFormats = { m_Device.SwapchainFormat() };
            d.debugName = "kiln.layer";
            PooledLayer l;
            l.rt = m_Device.CreateRenderTarget(d);
            l.page = std::make_unique<AtlasPage>(l.rt->ColorTexture(0), tw, th);
            l.width = tw;
            l.height = th;
            pool.push_back(std::move(l));
            layer = &pool.back();
        }
        layer->used = true;
        Target t;
        t.origin[0] = float(x);
        t.origin[1] = float(y);
        t.size[0] = float(tw);
        t.size[1] = float(th);
        t.rt = layer->rt.get();
        m_Targets.push_back(std::move(t));
        m_Stack.push_back(m_Targets.size() - 1);
        m_LayerPages.push_back(layer->page.get());
    }

    QuadRenderer::LayerResult QuadRenderer::EndLayer() {
        if (m_Stack.size() <= 1) return {};
        const std::size_t index = m_Stack.back();
        m_Stack.pop_back();
        m_LayerOrder.push_back(index);
        const AtlasPage* page = m_LayerPages[index - 1];
        return { page, { 0, 0, 1, 1 } };
    }

    void QuadRenderer::Upload() {
        if (m_Uploaded) return;
        m_Uploaded = true;
        std::size_t total = 0;
        for (auto& t : m_Targets) { t.base = std::uint32_t(total); total += t.instances.size(); }
        if (total == 0) return;
        auto& buffer = m_Buffers[m_Device.FrameIndex() % m_Buffers.size()];
        const u64 bytes = total * sizeof(Instance);
        if (!buffer || buffer->Size() < bytes) {
            BufferDesc desc;
            desc.size = bytes * 2;
            desc.usage = BufferUsage::Vertex;
            desc.memory = MemoryKind::HostVisible;
            desc.debugName = "kiln.quads";
            buffer = m_Device.CreateBuffer(desc);
        }
        m_Staging.resize(total);
        for (auto& t : m_Targets) std::copy(t.instances.begin(), t.instances.end(), m_Staging.begin() + t.base);
        buffer->Upload(m_Staging.data(), bytes);
    }

    void QuadRenderer::DrawTarget(CommandList& cmd, const Target& t) {
        if (t.instances.empty()) return;
        auto& buffer = m_Buffers[m_Device.FrameIndex() % m_Buffers.size()];
        const float push[4] = { t.size[0], t.size[1], t.origin[0], t.origin[1] };
        const Pipeline* boundPipeline = nullptr;
        for (const Run& run : t.runs) {
            const Ref<Pipeline>& want = run.mask ? m_MaskPipeline : run.filtered ? m_FilteredPipeline : m_Pipeline;
            if (boundPipeline != want.get()) {
                cmd.BindPipeline(want);
                cmd.SetViewport({ 0, 0, t.size[0], t.size[1], 0, 1 });
                cmd.SetScissor({ 0, 0, u32(t.size[0]), u32(t.size[1]) });
                cmd.SetPushConstants(push, sizeof(push));
                cmd.BindVertexBuffer(0, buffer);
                boundPipeline = want.get();
            }
            cmd.BindBindGroup(GroupFor(run.page));
            cmd.Draw(4, run.count, 0, t.base + run.first);
        }
    }

    void QuadRenderer::RenderLayers(CommandList& cmd) {
        Upload();
        for (std::size_t index : m_LayerOrder) {
            const Target& t = m_Targets[index];
            RenderPassDesc pass;
            pass.target = t.rt;
            pass.clearColor = { 0, 0, 0, 0 };
            cmd.BeginRenderPass(pass);
            DrawTarget(cmd, t);
            cmd.EndRenderPass();
        }
    }

    void QuadRenderer::Flush(CommandList& cmd) {
        Upload();
        DrawTarget(cmd, m_Targets[0]);
    }

}
