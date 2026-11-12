#include "kiln/QuadRenderer.h"

#include <kiln/Shaders.h>

#include <algorithm>
#include <cmath>
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
        for (u32 i = 0; i < 13; ++i)
            layout.attributes.push_back({ i, Format::RGBA32_SFLOAT, i * 16 });
        desc.vertexBuffers.push_back(layout);
        desc.bindings.push_back({ 0, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.bindings.push_back({ 1, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.bindings.push_back({ 2, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });
        desc.bindings.push_back({ 3, BindingType::SampledTexture, 1, StageBit(ShaderStage::Fragment) });   // blend destination
        desc.pushConstantSize = 16;   // viewport size, device origin of the target
        desc.topology = PrimitiveTopology::TriangleStrip;
        desc.blend = BlendMode::AlphaPremultiplied;
        desc.colorFormats = { device.SwapchainFormat() };
        desc.debugName = "kiln.quad";
        m_Pipeline = device.CreatePipeline(desc);
        desc.fragment = device.CreateShader({ { std::begin(shaders::kQuadSolidFrag), std::end(shaders::kQuadSolidFrag) },
                                              ShaderStage::Fragment, "main", "kiln.quad_solid.frag" });
        desc.debugName = "kiln.quad.solid";
        m_SolidPipeline = device.CreatePipeline(desc);
        desc.fragment = device.CreateShader({ { std::begin(shaders::kQuadCoverageFrag), std::end(shaders::kQuadCoverageFrag) },
                                              ShaderStage::Fragment, "main", "kiln.quad_coverage.frag" });
        desc.debugName = "kiln.quad.coverage";
        m_CoveragePipeline = device.CreatePipeline(desc);
        desc.fragment = device.CreateShader({ { std::begin(shaders::kQuadFlatFrag), std::end(shaders::kQuadFlatFrag) },
                                              ShaderStage::Fragment, "main", "kiln.quad_flat.frag" });
        desc.debugName = "kiln.quad.flat";
        m_FlatPipeline = device.CreatePipeline(desc);
        desc.fragment = device.CreateShader({ { std::begin(shaders::kQuadStretchFrag), std::end(shaders::kQuadStretchFrag) },
                                              ShaderStage::Fragment, "main", "kiln.quad_stretch.frag" });
        desc.debugName = "kiln.quad.stretch";
        m_StretchPipeline = device.CreatePipeline(desc);
        // Embedded external surface frames: built per Y'CbCr conversion on first use.
        m_ExternalDesc = desc;
        m_ExternalDesc.fragment = device.CreateShader({ { std::begin(shaders::kQuadExternalFrag), std::end(shaders::kQuadExternalFrag) },
                                                        ShaderStage::Fragment, "main", "kiln.quad_external.frag" });
        desc.fragment = device.CreateShader({ { std::begin(shaders::kQuadFilteredFrag), std::end(shaders::kQuadFilteredFrag) },
                                              ShaderStage::Fragment, "main", "kiln.quad_filtered.frag" });
        desc.debugName = "kiln.quad.filtered";
        m_FilteredPipeline = device.CreatePipeline(desc);
        // Blend modes compute the result from the destination copy and replace the pixel.
        desc.blend = BlendMode::None;
        desc.debugName = "kiln.quad.blend";
        m_BlendPipeline = device.CreatePipeline(desc);
        desc.fragment = fs;
        // DstIn: dst * src.alpha, for masking a layer to a path.
        desc.blend = BlendMode::Custom;
        desc.blendFactors = { BlendFactor::Zero, BlendFactor::SrcAlpha, BlendOp::Add, BlendFactor::Zero, BlendFactor::SrcAlpha, BlendOp::Add };
        desc.debugName = "kiln.quad.mask";
        m_MaskPipeline = device.CreatePipeline(desc);
        {
            PipelineDesc tri;
            tri.vertex = device.CreateShader({ { std::begin(shaders::kTriVert), std::end(shaders::kTriVert) },
                                               ShaderStage::Vertex, "main", "kiln.tri.vert" });
            tri.fragment = device.CreateShader({ { std::begin(shaders::kTriFrag), std::end(shaders::kTriFrag) },
                                                 ShaderStage::Fragment, "main", "kiln.tri.frag" });
            VertexBufferLayout vl;
            vl.stride = sizeof(Vertex);
            vl.stepMode = VertexStepMode::Vertex;
            vl.attributes.push_back({ 0, Format::RG32_SFLOAT, 0 });
            vl.attributes.push_back({ 1, Format::RGBA32_SFLOAT, 8 });
            vl.attributes.push_back({ 2, Format::RGBA32_SFLOAT, 24 });
            vl.attributes.push_back({ 3, Format::RGBA32_SFLOAT, 40 });
            vl.attributes.push_back({ 4, Format::RGBA32_SFLOAT, 56 });
            vl.attributes.push_back({ 5, Format::RGBA32_SFLOAT, 72 });
            tri.vertexBuffers.push_back(vl);
            tri.bindings = desc.bindings;   // the quads' textures: image, ramp, filters, destination
            tri.pushConstantSize = 96;   // viewport, origin, clip (rect, radii, inverse, enabled, outer rect)
            tri.topology = PrimitiveTopology::TriangleList;
            tri.blend = BlendMode::AlphaPremultiplied;
            tri.colorFormats = { device.SwapchainFormat() };
            tri.debugName = "kiln.tri";
            m_TriPipeline = device.CreatePipeline(tri);
            tri.blend = BlendMode::None;
            tri.debugName = "kiln.tri.blend";
            m_TriBlendPipeline = device.CreatePipeline(tri);
        }
        m_White = device.CreateSolidTexture(0xffffffffu);
        m_Buffers.resize(device.Caps().framesInFlight);
        m_TriBuffers.resize(device.Caps().framesInFlight);
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
        m_BlendMode = 3;
        m_TriVerts.clear();
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
        } else if (instance.params[1] != float(Mode::Ripple) && instance.params[1] != float(Mode::Lighting)) {   // these keep their uniforms there
            for (int i = 0; i < 4; ++i) instance.paint[i] = instance.paint2[i] = 0;
        }
        instance.clip[0] = m_Clip.x0; instance.clip[1] = m_Clip.y0; instance.clip[2] = m_Clip.x1; instance.clip[3] = m_Clip.y1;
        for (int i = 0; i < 4; ++i) instance.clipParams[i] = m_Clip.radii[i];
        const Transform& t = m_Transform;
        instance.xform[0] = t.a; instance.xform[1] = t.b; instance.xform[2] = t.c; instance.xform[3] = t.d;
        instance.xform2[0] = t.e; instance.xform2[1] = t.f; instance.xform2[2] = t.g; instance.xform2[3] = t.h;
        const Transform& ci = m_Clip.inverse;
        instance.clipInv[0] = ci.a; instance.clipInv[1] = ci.b; instance.clipInv[2] = ci.c; instance.clipInv[3] = ci.d;
        instance.clipInv2[0] = ci.e; instance.clipInv2[1] = ci.f; instance.clipInv2[2] = m_Clip.enabled ? 1.0f : 0.0f; instance.clipInv2[3] = 0;
        for (int i = 0; i < 4; ++i) instance.clipOuter[i] = m_Clip.outer[i];
        const AtlasPage* dst = nullptr;
        // Stretch and blur passes composite layers themselves and never blend (nor do external
        // frames: their pipeline has no destination read).
        if (m_BlendMode != 3 && !mask && !m_PushExternal && (instance.params[1] <= float(Mode::Color) || instance.params[1] == float(Mode::MaskShader) ||
                                               instance.params[1] == float(Mode::Ripple) || instance.params[1] == float(Mode::Lighting))) {
            dst = DestinationCopy(instance);
            instance.params[3] = float(m_BlendMode + 1);
        }
        Target& t2 = Current();
        const bool filtered = instance.params[2] >= 0;
        // Flat-colour draws without a filter or blend mode take a small pipeline.
        std::uint8_t lite = 0;
        if (m_PushExternal) {
            lite = 5;
        } else if (!filtered && !dst && !mask && instance.paint2[0] == 0) {
            const float m = instance.params[1];
            if (m == float(Mode::Solid)) {
                lite = 1;
                // Square, axis-aligned, unclipped, edges on whole device pixels: fully covered
                // pixels only, the same result as the anti-aliased edge gives there.
                const float a = instance.xform[0], b = instance.xform[1], c = instance.xform[2], d = instance.xform[3];
                if (instance.params[0] <= 0 && b == 0 && c == 0 && a > 0 && d > 0 && instance.xform2[2] == 0 && instance.xform2[3] == 0 &&
                    instance.clipInv2[2] == 0) {
                    const float x0 = a * instance.rect[0] + instance.xform2[0], y0 = d * instance.rect[1] + instance.xform2[1];
                    const float x1 = x0 + a * instance.rect[2], y1 = y0 + d * instance.rect[3];
                    auto whole = [](float v) { return std::abs(v - std::round(v)) < 1e-3f; };
                    if (whole(x0) && whole(y0) && whole(x1) && whole(y1)) {
                        lite = 3;
                        instance.params[1] = float(Mode::Flat);   // the vertex shader grows only AA shapes
                    }
                }
            }
            else if (m == float(Mode::Stretch)) lite = 4;
            else if (m == float(Mode::TintedCoverage) || m == float(Mode::TextCoverage) || (m == float(Mode::Color) && instance.params[0] < 0.5f)) lite = 2;
        }
        if (t2.runs.empty() || t2.runs.back().page != page || t2.runs.back().mask != mask || t2.runs.back().filtered != filtered ||
            t2.runs.back().dst != dst || dst || t2.runs.back().triangles || t2.runs.back().lite != lite || t2.runs.back().aux != m_PushAux) {
            t2.runs.push_back({ page, std::uint32_t(t2.instances.size()), 0, mask, filtered, dst });
            t2.runs.back().lite = lite;
            t2.runs.back().aux = m_PushAux;
        }
        t2.instances.push_back(instance);
        ++t2.runs.back().count;
    }

    void QuadRenderer::FillRect(const RectF& r, Rgba c, float radius, const Paint* paint, int filterRow) {
        // Solid quads ignore the atlas, so they join whatever run is open instead of splitting it.
        const bool openMask = !Current().runs.empty() && (Current().runs.back().mask || Current().runs.back().dst || Current().runs.back().triangles ||
                                                          Current().runs.back().filtered != (filterRow >= 0));
        const AtlasPage* page = Current().runs.empty() || openMask ? nullptr : Current().runs.back().page;
        Push({ { r.x, r.y, r.w, r.h }, { 0, 0, 0, 0 }, { c.r, c.g, c.b, c.a },
               { radius, float(Mode::Solid), float(filterRow), 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, page, paint);
    }

    void QuadRenderer::DrawAtlas(const RectF& r, const RectF& uv, const AtlasPage& page, Rgba c, Mode mode, const Paint* paint, int filterRow,
                                 bool nearest) {
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { c.r, c.g, c.b, c.a },
               { nearest ? 1.0f : 0.0f, float(mode), float(filterRow), 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, paint);
    }

    void QuadRenderer::DrawExternal(const RectF& r, const RectF& uv, const AtlasPage& page, float a, bool opaque) {
        m_PushExternal = true;
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { a, a, a, a },
               { opaque ? 1.0f : 0.0f, float(Mode::Color), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, nullptr);
        m_PushExternal = false;
    }

    const Ref<Pipeline>& QuadRenderer::ExternalPipeline(const AtlasPage* page) {
        const Ref<Texture>& texture = page->Texture();
        const std::uint64_t key = texture->SamplerConversionKey();
        auto it = m_ExternalPipelines.find(key);
        if (it == m_ExternalPipelines.end()) {
            PipelineDesc desc = m_ExternalDesc;
            if (key) desc.bindings[0].immutableSampler = texture;   // the conversion's sampler
            desc.debugName = "kiln.quad.external";
            it = m_ExternalPipelines.emplace(key, m_Device.CreatePipeline(desc)).first;
        }
        return it->second;
    }

    void QuadRenderer::DrawGlyph(const RectF& r, const RectF& uv, const AtlasPage& page, Rgba c, float maskGamma) {
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { c.r, c.g, c.b, c.a },
               { maskGamma, float(Mode::TextCoverage), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, nullptr);
    }

    void QuadRenderer::DrawRipple(const Ripple& r) {
        if (r.dataRow < 0 || !(r.radius > 0)) return;
        const bool openMask = !Current().runs.empty() && (Current().runs.back().mask || Current().runs.back().dst || Current().runs.back().triangles ||
                                                          Current().runs.back().filtered);
        const AtlasPage* page = Current().runs.empty() || openMask ? nullptr : Current().runs.back().page;
        Instance in{ { r.originX - r.radius, r.originY - r.radius, 2 * r.radius, 2 * r.radius },
                     { r.originX, r.originY, r.touchX, r.touchY }, { r.color.r, r.color.g, r.color.b, r.color.a },
                     { float(r.dataRow), float(Mode::Ripple), -1, 0 }, {}, {},
                     { r.progress, r.maxRadius, r.width, r.height }, { r.noisePhase, r.sparkleAlpha, r.alpha, 0 }, {}, {}, {}, {} };
        Push(in, page, nullptr);
    }

    void QuadRenderer::DrawLighting(const Lighting& l) {
        if (l.dataRow < 0 || !(l.width > 0) || !(l.height > 0)) return;
        const bool openMask = !Current().runs.empty() && (Current().runs.back().mask || Current().runs.back().dst || Current().runs.back().triangles ||
                                                          Current().runs.back().filtered);
        const AtlasPage* page = Current().runs.empty() || openMask ? nullptr : Current().runs.back().page;
        Instance in{ { l.x, l.y, l.width, l.height }, { l.x, l.y, 0, 0 }, { 1, 1, 1, 1 }, { float(l.dataRow), float(Mode::Lighting), -1, 0 }, {}, {},
                     {}, {}, {}, {}, {}, {} };
        Push(in, page, nullptr);
    }

    Ref<BindGroup> QuadRenderer::GroupFor(const AtlasPage* page, const AtlasPage* dst, const Ref<Pipeline>& pipeline) {
        const Ref<Pipeline>& layout = pipeline ? pipeline : m_Pipeline;
        for (auto& g : m_Groups)
            if (g.page == page && g.dst == dst && g.pipeline == layout.get()) return g.group;
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
        BindGroupEntry destination;
        destination.binding = 3;
        destination.type = BindingType::SampledTexture;
        destination.textures = { dst ? dst->Texture() : m_White };
        auto group = m_Device.CreateBindGroup(layout, { entry, ramp, filters, destination });
        m_Groups.push_back({ page, dst, group, layout.get() });
        return group;
    }

    void QuadRenderer::DrawMask(const RectF& r, const RectF& uv, const AtlasPage& page) {
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { 1, 1, 1, 1 },
               { 0, float(Mode::TintedCoverage), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, nullptr, true);
    }

    void QuadRenderer::DrawStretched(const RectF& r, const RectF& uv, const AtlasPage& page, float a, float sx, float sy) {
        Instance in{ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { a, a, a, a },
                     { 0, float(Mode::Stretch), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} };
        Push(in, &page, nullptr);
        // Stretch parameters ride in the paint slots (Push zeroes them for a null paint).
        Instance& last = Current().instances.back();
        last.paint[0] = sx; last.paint[1] = sy; last.paint[2] = r.w; last.paint[3] = r.h;
    }

    void QuadRenderer::DrawBlur(const RectF& r, const RectF& uv, const AtlasPage& page, float a, float su, float sv, float sigma, bool decal) {
        Instance in{ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { a, a, a, a },
                     { 0, float(Mode::Blur), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} };
        Push(in, &page, nullptr);
        Instance& last = Current().instances.back();
        last.paint[0] = su; last.paint[1] = sv; last.paint[2] = sigma; last.paint[3] = decal ? 1.0f : 0.0f;
    }

    void QuadRenderer::DrawProgressiveBlur(const RectF& r, const RectF& uv, const AtlasPage& page, float su, float sv, float maxRadius,
                                           bool decal, const Paint& intensity, const AtlasPage* intensityMask) {
        // Pass parameters ride in the colour; the intensity gradient in the paint slots (kind 4:
        // the mask's alpha, bound in the destination slot).
        m_PushAux = intensityMask;
        Push({ { r.x, r.y, r.w, r.h }, { uv.x, uv.y, uv.x + uv.w, uv.y + uv.h }, { su, sv, maxRadius, 1 },
               { decal ? 1.0f : 0.0f, float(Mode::ProgressiveBlur), -1, 0 }, {}, {}, {}, {}, {}, {}, {}, {} }, &page, &intensity);
        m_PushAux = nullptr;
        if (intensityMask) Current().instances.back().paint2[0] = 4;
    }

    void QuadRenderer::DrawTriangles(const Vertex* v, std::size_t count, const TrianglePaint& paint, int blendMode) {
        if (count < 3) return;
        Target& t = Current();
        const auto first = std::uint32_t(m_TriVerts.size());
        m_TriVerts.insert(m_TriVerts.end(), v, v + count);
        float paintGeometry[4] = {}, paint2[4] = {};
        if (paint.image) {
            paint2[0] = 4;
            paint2[1] = float(paint.tileX);
            paint2[3] = float(paint.tileY);
        } else if (paint.paint && paint.paint->kind != Paint::Kind::Color) {
            std::copy(std::begin(paint.paint->geometry), std::end(paint.paint->geometry), paintGeometry);
            paint2[0] = float(paint.paint->kind);
            paint2[1] = float(paint.paint->tile);
            paint2[2] = paint.paint->rampV;
        }
        Run run{ paint.image, first, std::uint32_t(count) };
        if (blendMode != 3) {
            // Device bounds of the triangles in target pixels, a pixel of slack around them.
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            for (std::size_t i = 0; i < count; ++i) {
                x0 = std::min(x0, v[i].x); y0 = std::min(y0, v[i].y);
                x1 = std::max(x1, v[i].x); y1 = std::max(y1, v[i].y);
            }
            const float ox = t.origin[0], oy = t.origin[1];
            run.dst = DestinationCopy(std::max(0.0f, std::floor(x0 - ox) - 2), std::max(0.0f, std::floor(y0 - oy) - 2),
                                      std::min(t.size[0], std::ceil(x1 - ox) + 2), std::min(t.size[1], std::ceil(y1 - oy) + 2));
        }
        Target& cur = Current();   // DestinationCopy may have grown m_Targets
        for (std::size_t i = first; i < m_TriVerts.size(); ++i) {
            Vertex& out = m_TriVerts[i];
            std::copy(std::begin(paintGeometry), std::end(paintGeometry), out.paint);
            std::copy(std::begin(paint2), std::end(paint2), out.paint2);
            out.paint3[0] = float(paint.filterRow + 1);
            out.paint3[1] = blendMode != 3 ? float(blendMode + 1) : 0.0f;
            out.paint3[2] = paint.imageWidth;
            out.paint3[3] = paint.imageHeight;
        }
        run.triangles = true;
        run.clip = m_Clip;
        cur.runs.push_back(run);
    }

    QuadRenderer::PooledLayer& QuadRenderer::AcquireLayer(int w, int h) {
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
        return *layer;
    }

    void QuadRenderer::BeginLayer(int x, int y, int w, int h) {
        PooledLayer& layer = AcquireLayer(w, h);
        Target t;
        t.origin[0] = float(x);
        t.origin[1] = float(y);
        t.size[0] = float(layer.width);
        t.size[1] = float(layer.height);
        t.rt = layer.rt.get();
        m_Targets.push_back(std::move(t));
        m_Stack.push_back(m_Targets.size() - 1);
        m_LayerPages.push_back(layer.page.get());
    }

    const AtlasPage* QuadRenderer::DestinationCopy(const Instance& in) {
        const std::size_t index = m_Stack.back();
        const Target& cur = m_Targets[index];
        const float ox = cur.origin[0], oy = cur.origin[1], tw = cur.size[0], th = cur.size[1];
        // Device bounds of the quad (with the solid-edge growth), in target pixels.
        float x0 = 0, y0 = 0, x1 = tw, y1 = th;
        if (in.xform2[2] == 0 && in.xform2[3] == 0) {
            x0 = y0 = 1e30f; x1 = y1 = -1e30f;
            for (const float cx : { in.rect[0], in.rect[0] + in.rect[2] })
                for (const float cy : { in.rect[1], in.rect[1] + in.rect[3] }) {
                    const float dx = in.xform[0] * cx + in.xform[2] * cy + in.xform2[0] - ox;
                    const float dy = in.xform[1] * cx + in.xform[3] * cy + in.xform2[1] - oy;
                    x0 = std::min(x0, dx); y0 = std::min(y0, dy); x1 = std::max(x1, dx); y1 = std::max(y1, dy);
                }
            x0 = std::max(0.0f, std::floor(x0) - 2); y0 = std::max(0.0f, std::floor(y0) - 2);
            x1 = std::min(tw, std::ceil(x1) + 2); y1 = std::min(th, std::ceil(y1) + 2);
        }
        return DestinationCopy(x0, y0, x1, y1);
    }

    const AtlasPage* QuadRenderer::DestinationCopy(float x0, float y0, float x1, float y1) {
        const std::size_t index = m_Stack.back();
        const Target& cur = m_Targets[index];
        const float ox = cur.origin[0], oy = cur.origin[1], tw = cur.size[0], th = cur.size[1];
        Target copy;
        copy.origin[0] = ox;
        copy.origin[1] = oy;
        copy.runs = cur.runs;
        copy.aliased = true;
        copy.alias = index;
        if (index == 0) std::copy(std::begin(m_RootClear), std::end(m_RootClear), copy.clear);
        copy.scissor[0] = std::uint32_t(x0);
        copy.scissor[1] = std::uint32_t(y0);
        copy.scissor[2] = std::uint32_t(std::max(0.0f, x1 - x0));
        copy.scissor[3] = std::uint32_t(std::max(0.0f, y1 - y0));
        PooledLayer& layer = AcquireLayer(int(tw), int(th));
        copy.size[0] = float(layer.width);
        copy.size[1] = float(layer.height);
        copy.rt = layer.rt.get();
        m_Targets.push_back(std::move(copy));
        m_LayerPages.push_back(layer.page.get());
        m_LayerOrder.push_back(m_Targets.size() - 1);
        return layer.page.get();
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
        if (!m_TriVerts.empty()) {
            auto& tb = m_TriBuffers[m_Device.FrameIndex() % m_TriBuffers.size()];
            const u64 triBytes = m_TriVerts.size() * sizeof(Vertex);
            if (!tb || tb->Size() < triBytes) {
                BufferDesc desc;
                desc.size = triBytes * 2;
                desc.usage = BufferUsage::Vertex;
                desc.memory = MemoryKind::HostVisible;
                desc.debugName = "kiln.triangles";
                tb = m_Device.CreateBuffer(desc);
            }
            tb->Upload(m_TriVerts.data(), triBytes);
        }
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
        if (t.runs.empty()) return;
        const std::uint32_t base = t.aliased ? m_Targets[t.alias].base : t.base;
        auto& buffer = m_Buffers[m_Device.FrameIndex() % m_Buffers.size()];
        const float push[4] = { t.size[0], t.size[1], t.origin[0], t.origin[1] };
        const ScissorRect scissor = t.aliased ? ScissorRect{ i32(t.scissor[0]), i32(t.scissor[1]), t.scissor[2], t.scissor[3] }
                                              : ScissorRect{ 0, 0, u32(t.size[0]), u32(t.size[1]) };
        if (scissor.width == 0 || scissor.height == 0) return;
        const Pipeline* boundPipeline = nullptr;
        for (const Run& run : t.runs) {
            if (run.triangles) {
                const Clip& c = run.clip;
                const float tp[24] = { t.size[0], t.size[1], t.origin[0], t.origin[1],
                                       c.x0, c.y0, c.x1, c.y1,
                                       c.radii[0], c.radii[1], c.radii[2], c.radii[3],
                                       c.inverse.a, c.inverse.b, c.inverse.c, c.inverse.d,
                                       c.inverse.e, c.inverse.f, c.enabled ? 1.0f : 0.0f, 0,
                                       c.outer[0], c.outer[1], c.outer[2], c.outer[3] };
                const Ref<Pipeline>& tri = run.dst ? m_TriBlendPipeline : m_TriPipeline;
                cmd.BindPipeline(tri);
                cmd.SetViewport({ 0, 0, t.size[0], t.size[1], 0, 1 });
                cmd.SetScissor(scissor);
                cmd.SetPushConstants(tp, sizeof(tp));
                cmd.BindBindGroup(GroupFor(run.page, run.dst, tri));
                cmd.BindVertexBuffer(0, m_TriBuffers[m_Device.FrameIndex() % m_TriBuffers.size()]);
                cmd.Draw(run.count, 1, run.first, 0);
                boundPipeline = nullptr;   // the quad state is set again
                continue;
            }
            const Ref<Pipeline>& want = run.mask ? m_MaskPipeline : run.dst ? m_BlendPipeline : run.filtered ? m_FilteredPipeline
                                      : run.lite == 1 ? m_SolidPipeline : run.lite == 2 ? m_CoveragePipeline
                                      : run.lite == 3 ? m_FlatPipeline : run.lite == 4 ? m_StretchPipeline
                                      : run.lite == 5 ? ExternalPipeline(run.page) : m_Pipeline;
            if (boundPipeline != want.get()) {
                cmd.BindPipeline(want);
                cmd.SetViewport({ 0, 0, t.size[0], t.size[1], 0, 1 });
                cmd.SetScissor(scissor);
                cmd.SetPushConstants(push, sizeof(push));
                cmd.BindVertexBuffer(0, buffer);
                boundPipeline = want.get();
            }
            cmd.BindBindGroup(run.lite == 5 ? GroupFor(run.page, run.dst, want) : GroupFor(run.page, run.dst ? run.dst : run.aux));
            cmd.Draw(4, run.count, 0, base + run.first);
        }
    }

    void QuadRenderer::RenderLayers(CommandList& cmd) {
        Upload();
        for (std::size_t index : m_LayerOrder) {
            const Target& t = m_Targets[index];
            RenderPassDesc pass;
            pass.target = t.rt;
            pass.clearColor = { t.clear[0], t.clear[1], t.clear[2], t.clear[3] };
            cmd.BeginRenderPass(pass);
            DrawTarget(cmd, t);
            cmd.EndRenderPass();
        }
    }

    QuadRenderer::Stats QuadRenderer::ComputeStats() const {
        Stats st;
        st.screenPixels = double(m_Targets[0].size[0]) * m_Targets[0].size[1];
        for (std::size_t ti = 0; ti < m_Targets.size(); ++ti) {
            const Target& t = m_Targets[ti];
            const Target& src = t.aliased ? m_Targets[t.alias] : t;
            float sx0 = 0, sy0 = 0, sx1 = t.size[0], sy1 = t.size[1];
            if (t.aliased) {
                ++st.destinationCopies;
                sx0 = float(t.scissor[0]); sy0 = float(t.scissor[1]);
                sx1 = sx0 + float(t.scissor[2]); sy1 = sy0 + float(t.scissor[3]);
                st.copyPixels += double(t.scissor[2]) * t.scissor[3];
            } else if (ti > 0) {
                ++st.layers;
                st.layerPixels += double(t.size[0]) * t.size[1];
            }
            for (const Run& r : t.runs) {
                if (r.triangles) { st.triangles += r.count / 3; continue; }
                for (std::uint32_t i = r.first; i < r.first + r.count && i < src.instances.size(); ++i) {
                    const Instance& in = src.instances[i];
                    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                    for (const float cx : { in.rect[0], in.rect[0] + in.rect[2] })
                        for (const float cy : { in.rect[1], in.rect[1] + in.rect[3] }) {
                            const float dx = in.xform[0] * cx + in.xform[2] * cy + in.xform2[0] - t.origin[0];
                            const float dy = in.xform[1] * cx + in.xform[3] * cy + in.xform2[1] - t.origin[1];
                            x0 = std::min(x0, dx); y0 = std::min(y0, dy); x1 = std::max(x1, dx); y1 = std::max(y1, dy);
                        }
                    x0 = std::max(x0, sx0); y0 = std::max(y0, sy0); x1 = std::min(x1, sx1); y1 = std::min(y1, sy1);
                    ++st.quads;
                    if (x1 > x0 && y1 > y0) {
                        const double px = double(x1 - x0) * (y1 - y0);
                        st.quadPixels += px;
                        const int pipe = r.mask ? 5 : r.dst ? 4 : r.filtered ? 3 : r.lite == 1 ? 0 : r.lite == 2 ? 1 : r.lite == 3 ? 6 : r.lite == 4 ? 7 : 2;
                        st.pipelinePixels[pipe] += px;
                        if (pipe == 0 && in.params[0] <= 0) st.squarePixels += px;
                        if (pipe == 2) {
                            st.fullByMode[std::clamp(int(in.params[1] + 0.5f), 0, 9)] += px;
                            if (in.paint2[0] != 0) st.fullGradient += px;
                        }
                    }
                }
            }
        }
        return st;
    }

    void QuadRenderer::Flush(CommandList& cmd) {
        Upload();
        DrawTarget(cmd, m_Targets[0]);
    }

}
