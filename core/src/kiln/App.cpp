#include "kiln/App.h"
#include "kiln/ui/Locals.h"
#include "kiln/runtime/State.h"
#include "kiln/animation/Animatable.h"

#include <hearth/Log.h>

#include <cstring>

namespace kiln {

    using namespace hearth;

    App::App(HostServices services) : m_Services(services) {
        m_Dispatcher.requestFrame = [this] { RequestFrame(); };
        m_Owner.SetText(m_Services.text);
        m_Owner.SetRequestFrame([this] { RequestFrame(); });
        // Any state write schedules a frame, like Compose's global apply observer (14 §Recomposer).
        rt::Observation::SetChangeListener([this] { RequestFrame(); });
    }

    void App::RunIdle() { m_Dispatcher.RunPending(); }

    void App::SetAnimationScale(float scale) {
        m_AnimationScale = scale;
        anim::SetDurationScale(scale);
    }

    void App::ProcessPointer(const ui::RawPointerEvent& event) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        // Touch input switches to touch mode (focus rings and SystemDefined focusability off).
        for (const auto& p : event.pointers)
            if (p.type == ui::PointerType::Touch && p.down) { m_Owner.SetInputMode(ui::InputMode::Touch); break; }
        m_Owner.ProcessPointer(event);
        m_Dispatcher.RunPending();
    }

    bool App::ProcessKey(const ui::KeyEvent& event) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        const bool handled = m_Owner.DispatchKeyEvent(event);
        m_Dispatcher.RunPending();
        return handled;
    }

    void App::SetLayoutDirection(ui::LayoutDirection d) {
        if (m_Direction->Peek() == d) return;
        m_Direction->Set(d);
        m_Owner.SetDirection(d);
        RequestFrame();
    }

    void App::SetContent(std::function<void()> content) {
        if (!m_Composition) m_Composition = std::make_unique<rt::Composition>(m_Owner.Root(), m_Dispatcher);
        rt::SaveableStateRegistry* registry = m_Saveable.get();
        rt::State<ui::LayoutDirection>* dir = m_Direction.get();
        ui::HostNestedScrollParent* nested = m_Services.nestedScrollParent;
        const bool tv = m_Television;
        m_Composition->SetContent([registry, dir, nested, tv, content = std::move(content)] {
            auto* c = rt::Composition::Current();
            c->Provide(ui::LocalLayoutDirection, dir->Get(), [c, registry, nested, tv, content] {
                c->Provide(ui::LocalHostNestedScrollParent, nested, [c, registry, tv, content] {
                    c->Provide(ui::LocalIsTelevision, tv, [c, registry, content] {
                        c->Provide(rt::LocalSaveableStateRegistry, registry, content);
                    });
                });
            });
        });
        RequestFrame();
    }

    void App::RestoreState(std::span<const std::uint8_t> bytes) {
        *m_Saveable = rt::SaveableStateRegistry(rt::SaveableStateRegistry::Deserialize(bytes));
    }

    std::vector<std::uint8_t> App::SaveState() const {
        return rt::SaveableStateRegistry::Serialize(m_Saveable->PerformSave());
    }
    App::~App() {
        if (m_Device) m_Device->WaitIdle();
    }

    void App::SetCallbacks(const kiln_app_callbacks& callbacks) {
        m_Callbacks = {};
        // Copy only what the caller's struct version has.
        const size_t n = callbacks.size < sizeof(m_Callbacks) ? callbacks.size : sizeof(m_Callbacks);
        std::memcpy(&m_Callbacks, &callbacks, n);
        m_Callbacks.size = sizeof(m_Callbacks);
    }

    void App::RequestFrame() {
        // Always ask the host: a vsync may have fired while nothing could draw (no surface yet),
        // leaving the flag set with no callback queued. The host de-duplicates.
        m_FramePending = true;
        if (m_Services.requestFrame) m_Services.requestFrame(m_Services.host);
    }

    void App::SurfaceCreated(Surface& surface) {
        if (m_Device && m_SurfaceLost) {
            m_Device->OnSurfaceRecreated(surface);
            m_SurfaceLost = false;
        } else if (!m_Device) {
            DeviceDesc desc;
            desc.surface = &surface;
            desc.appName = "kiln";
            desc.engineName = "kiln";
            DeviceError error;
            m_Device = CreateDevice(desc, &error);
            if (!m_Device) {
                HEARTH_ERROR("kiln: no Vulkan device: {} ({})", DeviceErrorName(error.code), error.message);
                return;
            }
            HEARTH_INFO("kiln: {} / {}", m_Device->Caps().deviceName, m_Device->Caps().driverInfo);
            m_Quads = std::make_unique<QuadRenderer>(*m_Device);
        }
        u32 w = 0, h = 0;
        surface.FramebufferSize(w, h);
        m_Width = w;
        m_Height = h;
        RequestFrame();
    }

    void App::SurfaceResized(std::uint32_t width, std::uint32_t height) {
        m_Width = width;
        m_Height = height;
        if (m_Device && !m_SurfaceLost) m_Device->OnWindowResize(width, height);
        RequestFrame();
    }

    void App::SurfaceDestroyed() {
        if (m_Device && !m_SurfaceLost) {
            m_Device->OnSurfaceLost();
            m_SurfaceLost = true;
        }
    }

    void App::EnsurePage(std::uint32_t index, std::uint32_t width, std::uint32_t height) {
        if (!m_Device) return;
        if (m_Pages.size() <= index) m_Pages.resize(index + 1);
        if (!m_Pages[index]) m_Pages[index] = std::make_unique<AtlasPage>(*m_Device, width, height);
    }

    void App::UploadPage(std::uint32_t index, const void* rgba, std::uint32_t stride, std::uint32_t x,
                         std::uint32_t y, std::uint32_t w, std::uint32_t h) {
        if (index < m_Pages.size() && m_Pages[index]) m_Pages[index]->Upload(rgba, stride, x, y, w, h);
    }

    void App::Frame(double timeSeconds) {
        m_FramePending = false;
        // Frame order (architecture 3.3): posted work, animation frame waiters with the shared
        // vsync time, then stages (state apply + recomposition, measure, layout), then drawing.
        m_Dispatcher.RunPending();
        m_Dispatcher.RunFrame(std::int64_t(timeSeconds * 1e9));
        for (auto& stage : m_Stages) stage(timeSeconds);
        rt::Composition::RecomposeAll();
        if (!m_Device || m_SurfaceLost || m_Width == 0 || m_Height == 0) return;
        m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });

        m_Quads->Begin(m_Width, m_Height);
        m_Quads->SetClearColor(m_Clear.r, m_Clear.g, m_Clear.b, m_Clear.a);
        if (!m_Paths) m_Paths = std::make_unique<PathCache>(*m_Device);
        m_Paths->BeginFrame();
        if (!m_Gradients) { m_Gradients = std::make_unique<GradientRamps>(*m_Device); m_Quads->SetRampTexture(m_Gradients->Texture()); }
        m_Gradients->BeginFrame();
        if (!m_Images) m_Images = std::make_unique<ImageCache>(*m_Device, *m_Quads);
        m_Images->BeginFrame();
        if (!m_Filters) { m_Filters = std::make_unique<FilterTable>(*m_Device); m_Quads->SetFilterTexture(m_Filters->Texture()); }
        m_Filters->BeginFrame();
        if (m_Services.text) m_Services.text->SyncAtlas(*this);
        m_Owner.Draw({ m_Quads.get(), [this](std::uint32_t i) { return Page(i); }, m_Paths.get(), m_Gradients.get(), m_Images.get(), m_Filters.get(), float(m_Width), float(m_Height) });
        kiln_frame frame{ this };
        if (m_Callbacks.on_frame) m_Callbacks.on_frame(m_Callbacks.user, &frame, timeSeconds);
        if (m_Services.text) m_Services.text->SyncAtlas(*this);
        m_Paths->Sync();
        m_Gradients->Sync();
        m_Filters->Sync();

        CommandList* cmd = m_Device->BeginFrame();
        if (!cmd) return;   // swapchain out of date; hearth rebuilds it, try next vsync
        m_Quads->RenderLayers(*cmd);   // offscreen layers first, inner ones before outer
        RenderPassDesc pass;
        pass.clearColor = { m_Clear.r, m_Clear.g, m_Clear.b, m_Clear.a };
        cmd->BeginRenderPass(pass);
        m_Quads->Flush(*cmd);
        cmd->EndRenderPass();
        m_Device->EndFrame();
    }

}
