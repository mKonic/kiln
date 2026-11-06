#pragma once

#include "kiln/QuadRenderer.h"
#include "kiln/TextBackend.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/LayoutNode.h"

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
    };

    // The process-wide app object behind kiln_app. Hosts drive it: SurfaceCreated/Destroyed,
    // Frame, configuration. The M0 frame runs drawing and submission on the calling thread.
    class App final : public AtlasSink {
    public:
        explicit App(HostServices services);
        ~App();

        void SetCallbacks(const kiln_app_callbacks& callbacks);
        void RequestFrame();

        // Runs posted work and due timers. Hosts call it when woken and before each frame.
        void RunIdle();
        // Frame stages run in order after animations, before drawing (recomposition, layout...).
        using FrameStage = std::function<void(double timeSeconds)>;
        void AddFrameStage(FrameStage stage) { m_Stages.push_back(std::move(stage)); }
        rt::Dispatcher& Dispatcher() { return m_Dispatcher; }

        // ANIMATOR_DURATION_SCALE (spec 02 req. 6): 0 means animations jump to their end.
        void SetAnimationScale(float scale);
        float AnimationScale() const { return m_AnimationScale; }

        void SurfaceCreated(hearth::Surface& surface);
        void SurfaceResized(std::uint32_t width, std::uint32_t height);
        void SurfaceDestroyed();
        void Frame(double timeSeconds);

        void SetDensity(float density, float fontScale) {
            m_Density = density;
            m_FontScale = fontScale;
            m_Owner.SetDensity({ density, fontScale });
        }
        // C++ content root: composed into the window's layout tree each frame it changes.
        void SetContent(std::function<void()> content);
        ui::Owner& GetOwner() { return m_Owner; }
        // Input from the host, positions in surface pixels. Layout is brought up to date first
        // so hit testing sees what is on screen (AndroidComposeView.handleMotionEvent).
        void ProcessPointer(const ui::RawPointerEvent& event);
        // A key event from the host; false means the platform should handle it (e.g. Back).
        bool ProcessKey(const ui::KeyEvent& event);
        float Density() const { return m_Density; }
        float FontScale() const { return m_FontScale; }
        TextBackend* Text() const { return m_Services.text; }
        bool FramePending() const { return m_FramePending; }

        // AtlasSink
        void EnsurePage(std::uint32_t index, std::uint32_t width, std::uint32_t height) override;
        void UploadPage(std::uint32_t index, const void* rgba, std::uint32_t strideBytes,
                        std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override;

        // Used by the C frame calls.
        QuadRenderer& Quads() { return *m_Quads; }
        const AtlasPage* Page(std::uint32_t index) const {
            return index < m_Pages.size() ? m_Pages[index].get() : nullptr;
        }
        void SetClearColor(Rgba c) { m_Clear = c; }
        std::uint32_t Width() const { return m_Width; }
        std::uint32_t Height() const { return m_Height; }

    private:
        HostServices m_Services;
        ui::Owner m_Owner;
        std::unique_ptr<rt::Composition> m_Composition;
        kiln_app_callbacks m_Callbacks{};
        std::unique_ptr<hearth::Device> m_Device;
        std::unique_ptr<QuadRenderer> m_Quads;
        std::vector<std::unique_ptr<AtlasPage>> m_Pages;
        std::uint32_t m_Width = 0, m_Height = 0;
        float m_Density = 1, m_FontScale = 1;
        Rgba m_Clear{ 1, 1, 1, 1 };
        bool m_FramePending = false;
        rt::Dispatcher& m_Dispatcher = rt::Dispatcher::Main();
        std::vector<FrameStage> m_Stages;
        float m_AnimationScale = 1.0f;
        bool m_SurfaceLost = false;
    };

}

struct kiln_app { kiln::App* impl; };
struct kiln_frame { kiln::App* app; };
struct kiln_paragraph { kiln::ParagraphData data; };
