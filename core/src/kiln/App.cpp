#include "kiln/App.h"

#include <hearth/Log.h>

#include <cstring>

namespace kiln {

    using namespace hearth;

    App::App(HostServices services) : m_Services(services) {}
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
        if (!m_Device || m_SurfaceLost || m_Width == 0 || m_Height == 0) return;

        m_Quads->Begin(m_Width, m_Height);
        kiln_frame frame{ this };
        if (m_Callbacks.on_frame) m_Callbacks.on_frame(m_Callbacks.user, &frame, timeSeconds);
        if (m_Services.text) m_Services.text->SyncAtlas(*this);

        CommandList* cmd = m_Device->BeginFrame();
        if (!cmd) return;   // swapchain out of date; hearth rebuilds it, try next vsync
        RenderPassDesc pass;
        pass.clearColor = { m_Clear.r, m_Clear.g, m_Clear.b, m_Clear.a };
        cmd->BeginRenderPass(pass);
        m_Quads->Flush(*cmd);
        cmd->EndRenderPass();
        m_Device->EndFrame();
    }

}
