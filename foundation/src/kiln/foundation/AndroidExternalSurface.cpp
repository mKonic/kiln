#include "kiln/foundation/AndroidExternalSurface.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Draw.h"

#include <cmath>
#include <memory>

namespace kiln::foundation {

    namespace {

        // BaseAndroidExternalSurfaceState: the callbacks onInit set, and the task onSurface runs.
        class SurfaceState final : public AndroidExternalSurfaceScope, public ui::ExternalSurfaceClient,
                                   public std::enable_shared_from_this<SurfaceState> {
        public:
            explicit SurfaceState(bool embedded) : m_Embedded(embedded) {}
            bool initialized = false;

            void OnSurface(std::function<rt::Task<>(SurfaceCoroutineScope&, Surface, int, int)> onSurface) override { m_OnSurface = std::move(onSurface); }

            void OnSurfaceCreated(Surface surface, int width, int height) override {
                m_Current = surface;
                m_LastWidth = width;
                m_LastHeight = height;
                ++m_Generation;
                if (!m_OnSurface) return;
                // A new task replaces (cancels) the previous one.
                m_Job = rt::Launch(rt::Dispatcher::Main(), Run(shared_from_this(), surface, width, height));
            }
            void OnSurfaceChanged(Surface surface, int width, int height) override {
                // A separate surface reports only a size change (surfaceChanged also fires for
                // format changes); an embedded one reports each one.
                if (!m_Embedded && width == m_LastWidth && height == m_LastHeight) return;
                m_LastWidth = width;
                m_LastHeight = height;
                if (m_OnChanged) m_OnChanged(surface, width, height);
            }
            void OnSurfaceDestroyed(Surface surface) override {
                m_Current.reset();
                ++m_Generation;
                if (m_OnDestroyed) m_OnDestroyed(surface);
                m_Job.Cancel();
            }
            // The node went away with a surface still alive: it ends as destroyed.
            void EndIfAlive() {
                if (m_Current) OnSurfaceDestroyed(*m_Current);
            }

        private:
            class Receiver final : public SurfaceCoroutineScope {
            public:
                explicit Receiver(std::shared_ptr<SurfaceState> s) : m_State(std::move(s)), m_Generation(m_State->m_Generation) {}
                void OnChanged(std::function<void(Surface, int, int)> f) override { m_State->m_OnChanged = std::move(f); }
                void OnDestroyed(std::function<void(Surface)> f) override { m_State->m_OnDestroyed = std::move(f); }
                // Until the surface goes or a new one replaces it.
                bool IsActive() const override { return m_State->m_Current.has_value() && m_State->m_Generation == m_Generation; }
            private:
                std::shared_ptr<SurfaceState> m_State;
                std::uint64_t m_Generation;
            };
            static rt::Task<> Run(std::shared_ptr<SurfaceState> self, Surface surface, int width, int height) {
                Receiver receiver(self);
                auto onSurface = self->m_OnSurface;
                co_await onSurface(receiver, surface, width, height);
            }

            bool m_Embedded;
            std::function<rt::Task<>(SurfaceCoroutineScope&, Surface, int, int)> m_OnSurface;
            std::function<void(Surface, int, int)> m_OnChanged;
            std::function<void(Surface)> m_OnDestroyed;
            std::optional<Surface> m_Current;
            int m_LastWidth = -1, m_LastHeight = -1;
            std::uint64_t m_Generation = 0;
            rt::Job m_Job;
        };

        struct SurfaceParams {
            std::shared_ptr<SurfaceState> state;
            ui::ExternalSurfaceConfig config;   // all but the bounds and visibility
            bool operator==(const SurfaceParams&) const = default;
        };

        class SurfaceNode final : public ui::ModifierNode, public ui::DrawModifier, public ui::GlobalPositionAwareModifier,
                                  public ui::LayoutAwareModifier {
        public:
            SurfaceParams params;
            explicit SurfaceNode(SurfaceParams p) : params(std::move(p)) {}
            ui::DrawModifier* AsDraw() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            ui::LayoutAwareModifier* AsLayoutAware() override { return this; }

            void OnAttach() override {
                m_Owner = Owner() ? Owner()->GetOwner() : nullptr;
                if (m_Owner) m_Id = m_Owner->AddExternalSurface(params.state.get(), Config());
            }
            void OnDetach() override {
                params.state->EndIfAlive();
                if (m_Owner && m_Id) m_Owner->RemoveExternalSurface(m_Id);
                m_Owner = nullptr;
                m_Id = 0;
            }
            void OnParamsChanged() {
                Push();
                InvalidateDraw();
            }
            void OnGloballyPositioned() override {
                const ui::Rect r = ui::RequireLayoutCoordinates(*this).BoundsInRoot();
                m_Bounds = { int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) };
                m_Visible = true;
                Push();
            }
            void OnUnplaced() override {
                m_Visible = false;
                Push();
            }

            void Draw(ui::ContentDrawScope& s) override {
                // A surface below the content shows through a hole cleared over the element.
                if (!params.config.embedded && params.config.zOrder != int(AndroidExternalSurfaceZOrder::OnTop)) {
                    s.Save();
                    s.SetBlendMode(gfx::BlendMode::Clear);
                    s.DrawRect(ui::Color(0xFF000000u), { 0, 0 }, s.GetSize());
                    s.Restore();
                }
                s.DrawContent();
            }

        private:
            ui::ExternalSurfaceConfig Config() const {
                ui::ExternalSurfaceConfig c = params.config;
                c.bounds = m_Bounds;
                c.visible = m_Visible;
                return c;
            }
            void Push() {
                if (m_Owner && m_Id) m_Owner->UpdateExternalSurface(m_Id, Config());
            }
            ui::Owner* m_Owner = nullptr;
            int m_Id = 0;
            ui::IntRect m_Bounds;
            bool m_Visible = false;
        };

        // AndroidView's default measure for a SurfaceView / TextureView: the incoming maximum
        // where bounded (a View fills an AT_MOST spec), else the minimum.
        class SurfaceMeasurePolicy final : public ui::MeasurePolicy {
        public:
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const>, const ui::Constraints& c) override {
                const int w = c.HasBoundedWidth() ? c.maxWidth : c.minWidth;
                const int h = c.HasBoundedHeight() ? c.maxHeight : c.minHeight;
                return s.Layout(w, h);
            }
        };

        void ExternalSurface(const Modifier& modifier, ui::ExternalSurfaceConfig config, const std::function<void(AndroidExternalSurfaceScope&)>& onInit,
                             rt::Key key) {
            rt::Composition* c = rt::Composition::Current();
            const bool embedded = config.embedded;
            auto& state = c->Remember<std::shared_ptr<SurfaceState>>([&] { return std::make_shared<SurfaceState>(embedded); }, {}, key);
            if (!state->initialized) {
                state->initialized = true;
                if (onInit) onInit(*state);
            }
            static const auto policy = std::make_shared<SurfaceMeasurePolicy>();
            Layout(modifier.With<SurfaceNode>(SurfaceParams{ state, config }), policy, {}, key);
        }

    }

    void AndroidExternalSurface(const Modifier& modifier, bool isOpaque, ui::IntSize surfaceSize, AndroidExternalSurfaceZOrder zOrder, bool isSecure,
                                std::function<void(AndroidExternalSurfaceScope&)> onInit, rt::Key key) {
        ui::ExternalSurfaceConfig config;
        config.fixedSize = surfaceSize;
        config.opaque = isOpaque;
        config.zOrder = int(zOrder);
        config.secure = isSecure;
        ExternalSurface(modifier, config, onInit, key);
    }

    void AndroidEmbeddedExternalSurface(const Modifier& modifier, bool isOpaque, ui::IntSize surfaceSize, std::optional<std::array<float, 9>> transform,
                                        std::function<void(AndroidExternalSurfaceScope&)> onInit, rt::Key key) {
        ui::ExternalSurfaceConfig config;
        config.fixedSize = surfaceSize;
        config.opaque = isOpaque;
        config.embedded = true;
        config.transform = transform;
        ExternalSurface(modifier, config, onInit, key);
    }

}
