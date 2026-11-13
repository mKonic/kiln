#pragma once

#include "kiln/Filters.h"
#include <kiln/Function.h>
#include "kiln/Gradients.h"
#include "kiln/ImageCache.h"
#include "kiln/PathCache.h"
#include "kiln/QuadRenderer.h"
#include "kiln/TextBackend.h"
#include "kiln/lifecycle/ViewModelCompose.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/NestedScroll.h"

#include <kiln/kiln.h>

#include <hearth/Device.h>

#include <functional>
#include <memory>
#include <vector>

namespace kiln {

    // Everything a platform host provides to the core.
    struct HostServices {
        TextBackend* text = nullptr;
        void (*requestFrame)(void* host) = nullptr;   // schedule one vsync callback
        void* host = nullptr;
        ui::HostNestedScrollParent* nestedScrollParent = nullptr;   // the host view's parents (interop)
    };

    // The process-wide app object behind kiln_app. Hosts drive it: SurfaceCreated/Destroyed,
    // Frame, configuration. The M0 frame runs drawing and submission on the calling thread.
    class App final : public AtlasSink {
    public:
        struct Window;
        explicit App(HostServices services);
        ~App();

        void SetCallbacks(const kiln_app_callbacks& callbacks);
        void RequestFrame();

        // Runs posted work and due timers. Hosts call it when woken and before each frame.
        void RunIdle();
        // Frame stages run in order after animations, before drawing (recomposition, layout...).
        using FrameStage = kiln::Function<void(double timeSeconds)>;
        void AddFrameStage(FrameStage stage) { m_Stages.push_back(std::move(stage)); }
        rt::Dispatcher& Dispatcher() { return m_Dispatcher; }

        // ANIMATOR_DURATION_SCALE (spec 02 req. 6): 0 means animations jump to their end.
        void SetAnimationScale(float scale);
        float AnimationScale() const { return m_AnimationScale; }

        void SurfaceCreated(hearth::Surface& surface);
        // Where the device keeps its pipeline cache between launches (Android: the app's cache
        // directory). Set before the first surface; SavePipelineCache writes it now.
        void SetPipelineCachePath(std::string path) { m_PipelineCachePath = std::move(path); }
        void SavePipelineCache() { if (m_Device) m_Device->SavePipelineCache(); }
        void SurfaceResized(std::uint32_t width, std::uint32_t height);
        // The host view's size from its own layout, which comes before the surface's (so focus
        // the platform gives on a window's first layout finds laid-out content).
        void ViewResized(Window* window, std::uint32_t width, std::uint32_t height);
        void SurfaceDestroyed();
        void Frame(double timeSeconds);
        // Owner::RenderToPixels: a one-off draw into an offscreen target, read back as RGBA.
        std::vector<std::uint8_t> RenderToPixels(int width, int height, const kiln::Function<void(ui::DrawScope&)>& draw);

        void SetDensity(float density, float fontScale) {
            m_Density = density;
            m_FontScale = fontScale;
            m_Owner.SetDensity({ density, fontScale, m_FontWeightAdjustment });
        }
        void SetFontWeightAdjustment(int adjustment) {
            m_FontWeightAdjustment = adjustment;
            m_Owner.SetDensity({ m_Density, m_FontScale, adjustment });
        }
        // The host view's layout direction (View.getLayoutDirection): layouts and LocalLayoutDirection.
        void SetLayoutDirection(ui::LayoutDirection d);
        // Configuration (LocalConfiguration; its locales also become Locale.current) and the
        // 24-hour time setting (LocalIs24HourFormat).
        void SetConfiguration(ui::Configuration config);
        const ui::Configuration& GetConfiguration() const { return m_Configuration->Peek(); }
        void Set24HourFormat(bool is24Hour);
        // FEATURE_LEANBACK; set before SetContent.
        void SetTelevision(bool tv) { m_Television = tv; }
        // C++ content root: composed into the window's layout tree each frame it changes.
        void SetContent(kiln::Function<void()> content);
        // Saved instance state (rememberSaveable): restore before SetContent, save on demand.
        void RestoreState(std::span<const std::uint8_t> bytes);
        std::vector<std::uint8_t> SaveState() const;
        ui::Owner& GetOwner() { return m_Owner; }
        // The window's lifecycle (LocalLifecycleOwner where content provides none): CREATED from
        // construction; the host sends the activity's events (on desktop, RESUMED while the
        // window is open, DESTROYED before the app goes).
        void HandleLifecycleEvent(lifecycle::Event event) { m_Lifecycle->registry.HandleLifecycleEvent(event); }
        lifecycle::Lifecycle& GetLifecycle() { return m_Lifecycle->registry; }
        // Input from the host, positions in surface pixels. Layout is brought up to date first
        // so hit testing sees what is on screen (AndroidComposeView.handleMotionEvent).
        void ProcessPointer(const ui::RawPointerEvent& event);
        // A key event from the host; false means the platform should handle it (e.g. Back).
        bool ProcessKey(const ui::KeyEvent& event);
        bool ProcessKeyPreIme(const ui::KeyEvent& event);
        bool ProcessRotary(const ui::RotaryScrollEvent& event, Window* window = nullptr);
        // A touch-navigation pad sample (dispatchGenericMotionEvent, SOURCE_TOUCH_NAVIGATION):
        // returns whether a node on the focused path consumed it. A cancel ends the stream.
        bool ProcessIndirectPointer(const ui::RawIndirectPointerEvent& event, Window* window = nullptr);
        // The host's navigation gesture detector saw a fling along the pad's axis: one focus step,
        // without wrapping.
        void MoveFocusFromIndirectFling(bool next, Window* window = nullptr);
        // View focus integration (AndroidComposeView). Directions are View.FOCUS_* values.
        // The focused item's rect, for View.getFocusedRect.
        std::optional<ui::Rect> FocusedRect(Window* window = nullptr);
        bool HasFocusableContent(Window* window = nullptr);
        // focusSearch from `focused` (view pixels): -1 cancelled, 0 nothing, 1 found (its rect
        // in `target`). Focus does not move.
        int HostFocusSearch(int direction, ui::Rect focused, ui::Rect& target, Window* window = nullptr);
        bool IsBetterFocusCandidate(int direction, ui::Rect proposed, ui::Rect current, ui::Rect focused);
        // View.requestFocus(direction, previouslyFocusedRect) reached the host view: focus an
        // item, first near the rect, then anywhere; a 1D search that wrapped back resets focus.
        bool HostRequestFocus(int direction, const ui::Rect* previouslyFocused, bool hostHasFocus, Window* window = nullptr);
        float Density() const { return m_Density; }
        float FontScale() const { return m_FontScale; }
        TextBackend* Text() const { return m_Services.text; }
        bool FramePending() const { return m_FramePending; }

        // Extra windows (dialogs): an owner shown in its own platform window, laid out at that
        // window's size and drawn into its own presentation surface, every frame with the main
        // one. Input for it goes to its owner through the overloads below.
        Window* AddWindow(ui::Owner& owner);
        void RemoveWindow(Window* window);
        void WindowSurfaceCreated(Window* window, hearth::Surface& surface);
        void WindowSurfaceResized(Window* window, std::uint32_t width, std::uint32_t height);
        void WindowSurfaceDestroyed(Window* window);
        ui::IntSize WindowSize(const Window* window) const;
        void ProcessPointer(Window* window, const ui::RawPointerEvent& event);
        bool ProcessKey(Window* window, const ui::KeyEvent& event);
        bool ProcessKeyPreIme(Window* window, const ui::KeyEvent& event);

        // AtlasSink
        void EnsurePage(std::uint32_t index, std::uint32_t width, std::uint32_t height) override;
        void UploadPage(std::uint32_t index, const void* rgba, std::uint32_t strideBytes,
                        std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override;

        // Used by the C frame calls.
        QuadRenderer& Quads() { return *m_Quads; }
        // Embedded external surfaces: a hardware buffer (AHardwareBuffer*) imported as a page
        // (null where the device cannot); a page dropped from every window's renderer before it
        // goes; and waiting for the GPU before freeing what it may still read.
        std::unique_ptr<AtlasPage> ImportHardwareBuffer(void* hardwareBuffer);
        void ForgetPage(const AtlasPage* page);
        void WaitGpuIdle();
        const AtlasPage* Page(std::uint32_t index) const {
            return index < m_Pages.size() ? m_Pages[index].get() : nullptr;
        }
        void SetClearColor(Rgba c) { m_Clear = c; }
        std::uint32_t Width() const { return m_Width; }
        std::uint32_t Height() const { return m_Height; }

    private:
        HostServices m_Services;
        // The window's owners, as a ComponentActivity is: lifecycle, ViewModels (living as long
        // as the app object, which configuration changes keep), and saved state (the app's
        // registry, so SavedStateHandles ride in SaveState).
        struct HostLifecycleOwner final : lifecycle::SavedStateRegistryOwner, lifecycle::ViewModelStoreOwner,
                                          lifecycle::HasDefaultViewModelProviderFactory {
            lifecycle::LifecycleRegistry registry{ *this };
            lifecycle::ViewModelStore store;
            rt::SaveableStateRegistry* saved = nullptr;
            lifecycle::Lifecycle& GetLifecycle() override { return registry; }
            lifecycle::ViewModelStore& GetViewModelStore() override { return store; }
            rt::SaveableStateRegistry& SavedStateRegistry() override { return *saved; }
            lifecycle::ViewModelProvider::FactoryPtr DefaultViewModelProviderFactory() override { return lifecycle::SavedStateViewModelFactory::Instance(); }
            lifecycle::MutableCreationExtras DefaultViewModelCreationExtras() override {
                lifecycle::MutableCreationExtras extras;
                extras.Set(lifecycle::SavedStateRegistryOwnerKey, static_cast<lifecycle::SavedStateRegistryOwner*>(this));
                extras.Set(lifecycle::ViewModelStoreOwnerKey, static_cast<lifecycle::ViewModelStoreOwner*>(this));
                return extras;
            }
        };
        std::shared_ptr<HostLifecycleOwner> m_Lifecycle = std::make_shared<HostLifecycleOwner>();
        ui::Owner m_Owner;
        ui::IndirectPointerAdapter m_IndirectAdapter;
        std::unique_ptr<rt::SaveableStateRegistry> m_Saveable = std::make_unique<rt::SaveableStateRegistry>();
        bool m_Television = false;
        std::unique_ptr<rt::State<ui::Configuration>> m_Configuration = std::make_unique<rt::State<ui::Configuration>>(ui::Configuration{});
        std::unique_ptr<rt::State<bool>> m_Is24Hour = std::make_unique<rt::State<bool>>(false);
        std::unique_ptr<rt::State<ui::LayoutDirection>> m_Direction = std::make_unique<rt::State<ui::LayoutDirection>>(ui::LayoutDirection::Ltr);
        std::unique_ptr<rt::Composition> m_Composition;
        kiln_app_callbacks m_Callbacks{};
        std::unique_ptr<hearth::Device> m_Device;
        std::string m_PipelineCachePath;
        std::unique_ptr<QuadRenderer> m_Quads;
        std::vector<std::unique_ptr<AtlasPage>> m_Pages;
        std::unique_ptr<PathCache> m_Paths;
        std::unique_ptr<GradientRamps> m_Gradients;
        std::unique_ptr<ImageCache> m_Images;
        std::unique_ptr<FilterTable> m_Filters;
        std::uint32_t m_Width = 0, m_Height = 0;
        float m_Density = 1, m_FontScale = 1;
        int m_FontWeightAdjustment = 0;
        Rgba m_Clear{ 1, 1, 1, 1 };
        bool m_FramePending = false;
        rt::Dispatcher& m_Dispatcher = rt::Dispatcher::Main();
        std::vector<FrameStage> m_Stages;
        float m_AnimationScale = 1.0f;
        bool m_SurfaceLost = false;
        std::vector<std::unique_ptr<Window>> m_Windows;
        void MeasureWindows();
        void LayoutAll();
        ui::Owner& OwnerFor(Window* window);
    };

}

struct kiln_app { kiln::App* impl; };
struct kiln_frame { kiln::App* app; };
struct kiln_paragraph { kiln::ParagraphData data; };
