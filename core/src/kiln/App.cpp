#include "kiln/App.h"
#include "kiln/ui/Locals.h"
#include "kiln/runtime/State.h"
#include "kiln/animation/Animatable.h"

#include <hearth/Log.h>

#include <algorithm>
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

    bool App::ProcessKeyPreIme(const ui::KeyEvent& event) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        const bool handled = m_Owner.DispatchInterceptedSoftKeyboardEvent(event);
        m_Dispatcher.RunPending();
        return handled;
    }

    bool App::ProcessRotary(const ui::RotaryScrollEvent& event) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        const bool handled = m_Owner.DispatchRotaryEvent(event);
        m_Dispatcher.RunPending();
        return handled;
    }

    bool App::ProcessIndirectPointer(const ui::RawIndirectPointerEvent& raw) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        ui::IndirectPointerEvent event;
        bool consumed = false;
        if (m_IndirectAdapter.Convert(raw, event)) consumed = m_Owner.DispatchIndirectPointerEvent(event);
        else m_Owner.DispatchIndirectPointerCancel();
        m_Dispatcher.RunPending();
        return consumed;
    }

    void App::MoveFocusFromIndirectFling(bool next) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        m_Owner.MoveFocus(next ? ui::FocusDirection::Next : ui::FocusDirection::Previous, false);
        m_Dispatcher.RunPending();
    }

    namespace {
        // View.FOCUS_* to a focus direction (AndroidComposeView toFocusDirection).
        std::optional<ui::FocusDirection> ToFocusDirection(int d) {
            switch (d) {
                case 1: return ui::FocusDirection::Previous;   // FOCUS_BACKWARD
                case 2: return ui::FocusDirection::Next;       // FOCUS_FORWARD
                case 17: return ui::FocusDirection::Left;
                case 33: return ui::FocusDirection::Up;
                case 66: return ui::FocusDirection::Right;
                case 130: return ui::FocusDirection::Down;
                default: return std::nullopt;
            }
        }
    }

    std::optional<ui::Rect> App::FocusedRect() {
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        return m_Owner.FocusedRect();
    }

    bool App::HasFocusableContent() {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        return m_Owner.HasFocusableContent();
    }

    int App::HostFocusSearch(int direction, ui::Rect focused, ui::Rect& target) {
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        const auto d = ToFocusDirection(direction).value_or(ui::FocusDirection::Down);
        bool found = false;
        const auto r = m_Owner.FocusSearch(d, &focused, [&](ui::FocusTargetNode& n) {
            target = m_Owner.FocusRectOf(n);
            found = true;
            return true;
        });
        if (!r) return -1;
        return found ? 1 : 0;
    }

    bool App::IsBetterFocusCandidate(int direction, ui::Rect proposed, ui::Rect current, ui::Rect focused) {
        const auto d = ToFocusDirection(direction).value_or(ui::FocusDirection::Down);
        return ui::IsBetterFocusCandidate(proposed, current, focused, d);
    }

    bool App::HostRequestFocus(int direction, const ui::Rect* previouslyFocused, bool hostHasFocus) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        const auto d = ToFocusDirection(direction).value_or(ui::FocusDirection::Enter);
        auto request = [&](ui::FocusTargetNode& n) { return n.RequestFocus(d); };
        bool done = m_Owner.FocusSearch(d, previouslyFocused, request).value_or(false);
        if (!done) done = m_Owner.FocusSearch(d, nullptr, request).value_or(false);
        // FocusFinder wrapped back to this view with an item already focused (1D only).
        if (!done && hostHasFocus && (d == ui::FocusDirection::Next || d == ui::FocusDirection::Previous))
            done = m_Owner.ResetFocus(d);
        m_Dispatcher.RunPending();
        return done;
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
        // AndroidSoundEffect: clicks go to the host view.
        struct HostSound final : ui::SoundEffect {
            ui::Owner* owner;
            explicit HostSound(ui::Owner* o) : owner(o) {}
            void PlayClickSound() override { owner->PlaySoundEffect(0); }
        };
        std::shared_ptr<ui::SoundEffect> sound = std::make_shared<HostSound>(&m_Owner);
        m_Composition->SetContent([registry, dir, nested, tv, sound, content = std::move(content)] {
            auto* c = rt::Composition::Current();
            c->Provide(ui::LocalLayoutDirection, dir->Get(), [c, registry, nested, tv, sound, content] {
                c->Provide(ui::LocalHostNestedScrollParent, nested, [c, registry, tv, sound, content] {
                    c->Provide(ui::LocalIsTelevision, tv, [c, registry, sound, content] {
                        c->Provide(ui::LocalSoundEffect, sound, [c, registry, content] {
                            c->Provide(rt::LocalSaveableStateRegistry, registry, content);
                        });
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
        const auto values = m_Saveable->PerformSave();
        auto bytes = rt::SaveableStateRegistry::Serialize(values);
        // The saved blob rides in the Activity bundle, and a Binder transaction holds about
        // 1 MB in total: past half of that, name the biggest entries.
        constexpr std::size_t kWarnBytes = 512 * 1024;
        if (bytes.size() > kWarnBytes) {
            std::vector<std::pair<std::size_t, std::string>> sizes;
            for (const auto& [key, list] : values)
                sizes.emplace_back(rt::SaveableStateRegistry::Serialize({ { key, list } }).size(), key);
            std::sort(sizes.rbegin(), sizes.rend());
            std::string message = "saved state is " + std::to_string(bytes.size() / 1024) +
                                  " KB, near the ~1 MB Binder limit (TransactionTooLargeException); largest keys:";
            for (std::size_t i = 0; i < sizes.size() && i < 5; ++i)
                message += " " + sizes[i].second + " (" + std::to_string(sizes[i].first / 1024) + " KB)";
            hearth::LogMessage(hearth::LogLevel::Warn, message);
        }
        return bytes;
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
