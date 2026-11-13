#include "kiln/App.h"
#include <kiln/Function.h>
#include "kiln/ui/Locals.h"
#include "kiln/runtime/State.h"
#include "kiln/animation/Animatable.h"

#include <hearth/Log.h>

#include <algorithm>
#include <cstring>

namespace kiln {

    // An extra window (see App.h).
    struct App::Window {
        ui::Owner* owner = nullptr;
        hearth::PresentSurface* present = nullptr;
        std::unique_ptr<QuadRenderer> quads;
        std::uint32_t width = 0, height = 0;
        bool drawn = false;   // drawn this frame (its batch is ready to record)
        bool textures = false;   // the shared ramp and filter textures are bound
    };

    using namespace hearth;

    App::App(HostServices services) : m_Services(services) {
        m_Dispatcher.requestFrame = [this] { RequestFrame(); };
        m_Owner.SetText(m_Services.text);
        m_Owner.SetRequestFrame([this] { RequestFrame(); });
        m_Owner.SetOffscreenRenderer([this](int w, int h, const kiln::Function<void(ui::DrawScope&)>& draw) { return RenderToPixels(w, h, draw); });
        // Any state write schedules a frame, like Compose's global apply observer (14 §Recomposer).
        rt::Observation::SetChangeListener([this] { RequestFrame(); });
        // The window's owners (after the listener: moving the lifecycle writes state).
        m_Lifecycle->registry.SetCurrentState(lifecycle::State::Created);
        m_Lifecycle->saved = m_Saveable.get();
        lifecycle::EnableSavedStateHandles(*m_Lifecycle);
        lifecycle::SetHostLifecycleOwner(m_Owner, m_Lifecycle);
        lifecycle::SetHostViewModelStoreOwner(m_Owner, m_Lifecycle);
        lifecycle::SetHostSavedStateRegistryOwner(m_Owner, m_Lifecycle);
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

    bool App::ProcessRotary(const ui::RotaryScrollEvent& event, Window* window) {
        rt::Composition::RecomposeAll();
        LayoutAll();
        const bool handled = OwnerFor(window).DispatchRotaryEvent(event);
        m_Dispatcher.RunPending();
        return handled;
    }

    bool App::ProcessIndirectPointer(const ui::RawIndirectPointerEvent& raw, Window* window) {
        rt::Composition::RecomposeAll();
        LayoutAll();
        ui::IndirectPointerEvent event;
        bool consumed = false;
        if (m_IndirectAdapter.Convert(raw, event)) consumed = OwnerFor(window).DispatchIndirectPointerEvent(event);
        else OwnerFor(window).DispatchIndirectPointerCancel();
        m_Dispatcher.RunPending();
        return consumed;
    }

    void App::MoveFocusFromIndirectFling(bool next, Window* window) {
        rt::Composition::RecomposeAll();
        LayoutAll();
        OwnerFor(window).MoveFocus(next ? ui::FocusDirection::Next : ui::FocusDirection::Previous, false);
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

    std::optional<ui::Rect> App::FocusedRect(Window* window) {
        LayoutAll();
        return OwnerFor(window).FocusedRect();
    }

    bool App::HasFocusableContent(Window* window) {
        rt::Composition::RecomposeAll();
        LayoutAll();
        return OwnerFor(window).HasFocusableContent();
    }

    int App::HostFocusSearch(int direction, ui::Rect focused, ui::Rect& target, Window* window) {
        LayoutAll();
        const auto d = ToFocusDirection(direction).value_or(ui::FocusDirection::Down);
        bool found = false;
        const auto r = OwnerFor(window).FocusSearch(d, &focused, [&](ui::FocusTargetNode& n) {
            target = OwnerFor(window).FocusRectOf(n);
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

    bool App::HostRequestFocus(int direction, const ui::Rect* previouslyFocused, bool hostHasFocus, Window* window) {
        // As AndroidComposeView.requestFocus: the tree as composed (pending recompositions wait
        // for the frame), laid out.
        LayoutAll();
        const auto d = ToFocusDirection(direction).value_or(ui::FocusDirection::Enter);
        auto request = [&](ui::FocusTargetNode& n) { return n.RequestFocus(d); };
        bool done = OwnerFor(window).FocusSearch(d, previouslyFocused, request).value_or(false);
        if (!done) done = OwnerFor(window).FocusSearch(d, nullptr, request).value_or(false);
        // FocusFinder wrapped back to this view with an item already focused (1D only).
        if (!done && hostHasFocus && (d == ui::FocusDirection::Next || d == ui::FocusDirection::Previous))
            done = OwnerFor(window).ResetFocus(d);
        m_Dispatcher.RunPending();
        return done;
    }

    void App::SetLayoutDirection(ui::LayoutDirection d) {
        if (m_Direction->Peek() == d) return;
        m_Direction->Set(d);
        m_Owner.SetDirection(d);
        RequestFrame();
    }

    void App::SetConfiguration(ui::Configuration config) {
        if (!config.locales.empty()) ui::SetPlatformLocaleList(config.locales);
        if (m_Configuration->Peek() == config) return;
        m_Configuration->Set(std::move(config));
        RequestFrame();
    }

    void App::Set24HourFormat(bool is24Hour) {
        if (m_Is24Hour->Peek() == is24Hour) return;
        m_Is24Hour->Set(is24Hour);
        RequestFrame();
    }

    void App::SetContent(kiln::Function<void()> content) {
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
        struct HostHaptics final : ui::HapticFeedback {
            ui::Owner* owner;
            explicit HostHaptics(ui::Owner* o) : owner(o) {}
            void PerformHapticFeedback(ui::HapticFeedbackType type) override { owner->PerformHapticFeedback(int(type)); }
        };
        std::shared_ptr<ui::HapticFeedback> haptics = std::make_shared<HostHaptics>(&m_Owner);
        // AndroidAccessibilityManager.
        struct HostA11yManager final : ui::AccessibilityManager {
            ui::Owner* owner;
            explicit HostA11yManager(ui::Owner* o) : owner(o) {}
            std::int64_t CalculateRecommendedTimeoutMillis(std::int64_t original, bool icons, bool text, bool controls) const override {
                if (original >= INT32_MAX) return original;
                const int flags = (icons ? 1 : 0) | (text ? 2 : 0) | (controls ? 4 : 0);
                const std::int64_t r = owner->RecommendedTimeoutMillis(int(original), flags);
                return r == INT32_MAX ? INT64_MAX : r;
            }
            ui::AccessibilityServiceState ServiceState() const override { return owner->AccessibilityServices(); }
        };
        std::shared_ptr<const ui::AccessibilityManager> a11yManager = std::make_shared<HostA11yManager>(&m_Owner);
        ui::WindowInsetsHolder* insets = &m_Owner.Insets();
        struct HostWindowInfo final : ui::WindowInfo {
            ui::Owner* owner;
            explicit HostWindowInfo(ui::Owner* o) : owner(o) {}
            bool IsWindowFocused() const override { return owner->IsContentWindowFocused(); }
            ui::IntSize ContainerSize() const override { return owner->ObservedWindowSize(); }
        };
        std::shared_ptr<const ui::WindowInfo> windowInfo = std::make_shared<HostWindowInfo>(&m_Owner);
        ui::Owner* owner = &m_Owner;
        struct HostWindow final : ui::Window {
            ui::Owner* owner;
            explicit HostWindow(ui::Owner* o) : owner(o) {}
            bool IsFlagSecure() const override { return owner->FlagSecure(); }
            void SetFlagSecure(bool secure) override { owner->SetFlagSecure(secure); }
        };
        std::shared_ptr<ui::Window> window = std::make_shared<HostWindow>(&m_Owner);
        rt::State<ui::Configuration>* config = m_Configuration.get();
        rt::State<bool>* is24Hour = m_Is24Hour.get();
        auto hostOwner = m_Lifecycle;
        content = [config, is24Hour, window, owner, hostOwner, content = std::move(content)] {
            auto* c = rt::Composition::Current();
            c->Provide(lifecycle::LocalViewModelStoreOwner, lifecycle::ViewModelStoreOwnerPtr(hostOwner), [c, hostOwner, config, is24Hour, window, owner, content] {
            c->Provide(lifecycle::LocalSavedStateRegistryOwner, lifecycle::SavedStateRegistryOwnerPtr(hostOwner), [c, hostOwner, config, is24Hour, window, owner, content] {
            c->Provide(lifecycle::LocalLifecycleOwner, lifecycle::LifecycleOwnerPtr(hostOwner), [c, config, is24Hour, window, owner, content] {
            c->Provide(ui::LocalOwner, owner, [c, config, is24Hour, window, owner, content] {
            c->Provide(ui::LocalDensity, owner->ObservedDensity(), [c, config, is24Hour, window, content] {
            c->Provide(ui::LocalConfiguration, config->Get(), [c, is24Hour, window, content] {
                c->Provide(ui::LocalIs24HourFormat, is24Hour->Get(), [c, window, content] {
                    c->Provide(ui::LocalWindow, window.get(), content);
                });
            });
            });
            });
            });
            });
            });
        };
        m_Composition->SetContent([registry, dir, nested, tv, sound, insets, windowInfo, haptics, owner, a11yManager, content = std::move(content)] {
            auto* c = rt::Composition::Current();
            c->Provide(ui::LocalActivityResultRegistry, &owner->ActivityResults(), [c, registry, dir, nested, tv, sound, insets, windowInfo, haptics, owner, a11yManager, content] {
            c->Provide(ui::LocalAccessibilityManager, a11yManager, [c, registry, dir, nested, tv, sound, insets, windowInfo, haptics, owner, content] {
            c->Provide(ui::LocalHapticFeedback, haptics, [c, registry, dir, nested, tv, sound, insets, windowInfo, owner, content] {
            c->Provide(ui::LocalScrollCaptureInProgress, owner->ScrollCaptureInProgress(), [c, registry, dir, nested, tv, sound, insets, windowInfo, content] {
            c->Provide(ui::LocalWindowInfo, windowInfo.get(), [c, registry, dir, nested, tv, sound, insets, content] {
            c->Provide(ui::LocalWindowInsets, insets, [c, registry, dir, nested, tv, sound, content] {
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
            });
            });
            });
            });
            });
        });
        RequestFrame();
    }

    void App::RestoreState(std::span<const std::uint8_t> bytes) {
        *m_Saveable = rt::SaveableStateRegistry(rt::SaveableStateRegistry::Deserialize(bytes));
        // The new registry has no providers: the window's SavedStateHandles register again
        // (and restore from it).
        lifecycle::EnableSavedStateHandles(*m_Lifecycle);
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
        // The window is gone for good: its ViewModels are cleared (the content leaves after).
        m_Lifecycle->store.Clear();
        // The thread's dispatcher and the global change listener outlive this app (a relaunched
        // activity makes a new one): they must not call back into it.
        rt::Observation::SetChangeListener(nullptr);
        m_Dispatcher.requestFrame = nullptr;
        m_Dispatcher.wake = nullptr;
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

    std::unique_ptr<AtlasPage> App::ImportHardwareBuffer(void* hardwareBuffer) {
        if (!m_Device) return nullptr;
        Ref<Texture> texture = m_Device->ImportHardwareBuffer(hardwareBuffer);
        if (!texture) return nullptr;
        const std::uint32_t w = texture->Width(), h = texture->Height();
        return std::make_unique<AtlasPage>(std::move(texture), w, h);
    }

    void App::ForgetPage(const AtlasPage* page) {
        if (m_Quads) m_Quads->ForgetPage(page);
        for (auto& w : m_Windows) if (w->quads) w->quads->ForgetPage(page);
    }

    void App::WaitGpuIdle() {
        if (m_Device) m_Device->WaitIdle();
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
            desc.pipelineCachePath = m_PipelineCachePath;
#ifdef __ANDROID__
            // External surfaces below kiln's frame show through where content clears it; the
            // view marks its surface opaque while there are none, which makes alpha moot.
            desc.swapchainTransparent = true;
#endif
            DeviceError error;
            m_Device = CreateDevice(desc, &error);
            if (!m_Device) {
                HEARTH_ERROR("kiln: no Vulkan device: {} ({})", DeviceErrorName(error.code), error.message);
                return;
            }
            HEARTH_INFO("kiln: {} / {}", m_Device->Caps().deviceName, m_Device->Caps().driverInfo);
            m_Quads = std::make_unique<QuadRenderer>(*m_Device);
            // The renderer built its pipelines: keep them for the next cold start (the driver's
            // shader compiler is most of a cold launch on Adreno).
            m_Device->SavePipelineCache();
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

    void App::ViewResized(Window* window, std::uint32_t width, std::uint32_t height) {
        std::uint32_t& w = window ? window->width : m_Width;
        std::uint32_t& h = window ? window->height : m_Height;
        if (w == width && h == height) return;
        w = width;
        h = height;
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

    std::vector<std::uint8_t> App::RenderToPixels(int width, int height, const kiln::Function<void(ui::DrawScope&)>& draw) {
        std::vector<std::uint8_t> out;
        if (!m_Device || m_SurfaceLost || !m_Quads) return out;
        RenderTargetDesc d;
        d.width = std::uint32_t(width);
        d.height = std::uint32_t(height);
        d.colorFormats = { m_Device->SwapchainFormat() };
        d.debugName = "kiln.offscreen";
        auto rt = m_Device->CreateRenderTarget(d);
        if (!rt) return out;
        // The frame's caches are reused: everything is synced before the immediate submit, and
        // the next frame starts them over.
        m_Quads->Begin(std::uint32_t(width), std::uint32_t(height));
        m_Quads->SetClearColor(0, 0, 0, 0);
        if (!m_Paths) m_Paths = std::make_unique<PathCache>(*m_Device);
        if (!m_Gradients) { m_Gradients = std::make_unique<GradientRamps>(*m_Device); m_Quads->SetRampTexture(m_Gradients->Texture()); }
        if (!m_Images) m_Images = std::make_unique<ImageCache>(*m_Device, *m_Quads);
        if (!m_Filters) { m_Filters = std::make_unique<FilterTable>(*m_Device); m_Quads->SetFilterTexture(m_Filters->Texture()); }
        if (m_Services.text) m_Services.text->SyncAtlas(*this);
        m_Owner.DrawWith({ m_Quads.get(), [this](std::uint32_t i) { return Page(i); }, m_Paths.get(), m_Gradients.get(), m_Images.get(), m_Filters.get(),
                           float(width), float(height) },
                         draw);
        if (m_Services.text) m_Services.text->SyncAtlas(*this);
        m_Paths->Sync();
        m_Gradients->Sync();
        m_Filters->Sync();
        m_Device->SubmitImmediate([&](CommandList& cmd) {
            m_Quads->RenderLayers(cmd);
            RenderPassDesc pass;
            pass.target = rt.get();
            pass.clearColor = { 0, 0, 0, 0 };
            cmd.BeginRenderPass(pass);
            m_Quads->Flush(cmd);
            cmd.EndRenderPass();
        });
        const std::size_t bytes = std::size_t(width) * std::size_t(height) * 4;
        out.resize(bytes);
        rt->ReadPixels(out.data(), bytes);
        // The swapchain may be BGRA: hand out RGBA.
        const Format f = m_Device->SwapchainFormat();
        if (f == Format::BGRA8_UNORM || f == Format::BGRA8_SRGB)
            for (std::size_t i = 0; i < bytes; i += 4) std::swap(out[i], out[i + 2]);
        RequestFrame();   // the frame's renderer state was used
        return out;
    }

    // --- extra windows -------------------------------------------------------------------------


    App::Window* App::AddWindow(ui::Owner& owner) {
        auto w = std::make_unique<Window>();
        w->owner = &owner;
        owner.SetText(m_Services.text);
        owner.SetRequestFrame([this] { RequestFrame(); });
        m_Windows.push_back(std::move(w));
        return m_Windows.back().get();
    }

    void App::RemoveWindow(Window* window) {
        WindowSurfaceDestroyed(window);
        std::erase_if(m_Windows, [&](const auto& w) { return w.get() == window; });
    }

    void App::WindowSurfaceCreated(Window* window, hearth::Surface& surface) {
        if (!window || !m_Device || window->present) return;
        window->present = m_Device->AddPresentSurface(surface, /*transparent*/ true);
        if (!window->present) return;
        window->quads = std::make_unique<QuadRenderer>(*m_Device);
        window->textures = false;
        if (m_Images) m_Images->AddRenderer(*window->quads);
        hearth::u32 w = 0, h = 0;
        surface.FramebufferSize(w, h);
        window->width = w;
        window->height = h;
        RequestFrame();
    }

    void App::WindowSurfaceResized(Window* window, std::uint32_t width, std::uint32_t height) {
        if (!window) return;
        window->width = width;
        window->height = height;
        if (m_Device && window->present) m_Device->ResizePresentSurface(window->present, width, height);
        RequestFrame();
    }

    void App::WindowSurfaceDestroyed(Window* window) {
        if (!window || !window->present) return;
        if (m_Images && window->quads) m_Images->RemoveRenderer(*window->quads);
        m_Device->RemovePresentSurface(window->present);
        window->present = nullptr;
        window->quads.reset();
    }

    ui::IntSize App::WindowSize(const Window* window) const {
        return window ? ui::IntSize{ int(window->width), int(window->height) } : ui::IntSize{};
    }

    ui::Owner& App::OwnerFor(Window* window) { return window ? *window->owner : m_Owner; }

    void App::LayoutAll() {
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        MeasureWindows();
    }

    void App::MeasureWindows() {
        for (auto& w : m_Windows)
            if (w->width && w->height) w->owner->MeasureAndLayout({ int(w->width), int(w->height) });
    }

    void App::ProcessPointer(Window* window, const ui::RawPointerEvent& event) {
        rt::Composition::RecomposeAll();
        if (m_Width && m_Height) m_Owner.MeasureAndLayout({ int(m_Width), int(m_Height) });
        MeasureWindows();
        for (const auto& p : event.pointers)
            if (p.type == ui::PointerType::Touch && p.down) { window->owner->SetInputMode(ui::InputMode::Touch); break; }
        window->owner->ProcessPointer(event);
        m_Dispatcher.RunPending();
    }

    bool App::ProcessKey(Window* window, const ui::KeyEvent& event) {
        rt::Composition::RecomposeAll();
        MeasureWindows();
        const bool handled = window->owner->DispatchKeyEvent(event);
        m_Dispatcher.RunPending();
        return handled;
    }

    bool App::ProcessKeyPreIme(Window* window, const ui::KeyEvent& event) {
        rt::Composition::RecomposeAll();
        MeasureWindows();
        const bool handled = window->owner->DispatchInterceptedSoftKeyboardEvent(event);
        m_Dispatcher.RunPending();
        return handled;
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
        MeasureWindows();

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
        // The extra windows into their own batches (transparent where they draw nothing).
        for (auto& w : m_Windows) {
            w->drawn = false;
            if (!w->present || !w->quads || !w->width || !w->height) continue;
            if (!w->textures) {
                w->quads->SetRampTexture(m_Gradients->Texture());
                w->quads->SetFilterTexture(m_Filters->Texture());
                w->textures = true;
            }
            w->quads->Begin(w->width, w->height);
            w->quads->SetClearColor(0, 0, 0, 0);
            w->owner->Draw({ w->quads.get(), [this](std::uint32_t i) { return Page(i); }, m_Paths.get(), m_Gradients.get(), m_Images.get(), m_Filters.get(), float(w->width), float(w->height) });
            w->drawn = true;
        }
        kiln_frame frame{ this };
        if (m_Callbacks.on_frame) m_Callbacks.on_frame(m_Callbacks.user, &frame, timeSeconds);
        if (m_Services.text) m_Services.text->SyncAtlas(*this);
        m_Paths->Sync();
        m_Gradients->Sync();
        m_Filters->Sync();

        CommandList* cmd = m_Device->BeginFrame();
        if (!cmd) {
            // The swapchain was out of date (hearth rebuilds it): draw again next vsync. Nothing
            // else may ask for that frame, and until one is presented a Wayland window is not
            // even mapped.
            if (!m_SurfaceLost) RequestFrame();
            return;
        }
        m_Quads->RenderLayers(*cmd);   // offscreen layers first, inner ones before outer
        RenderPassDesc pass;
        pass.clearColor = { m_Clear.r, m_Clear.g, m_Clear.b, m_Clear.a };
        cmd->BeginRenderPass(pass);
        m_Quads->Flush(*cmd);
        cmd->EndRenderPass();
        for (auto& w : m_Windows) {
            if (!w->drawn || !w->present->Ready()) continue;
            w->quads->RenderLayers(*cmd);
            RenderPassDesc wp;
            wp.present = w->present;
            wp.clearColor = { 0, 0, 0, 0 };
            cmd->BeginRenderPass(wp);
            w->quads->Flush(*cmd);
            cmd->EndRenderPass();
        }
        m_Device->EndFrame();
    }

}
