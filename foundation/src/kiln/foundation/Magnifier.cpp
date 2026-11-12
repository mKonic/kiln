#include "kiln/foundation/Magnifier.h"

#include "kiln/foundation/text/Selection.h"

namespace kiln::foundation {

    namespace {
        bool Specified(Offset o) { return !std::isnan(o.x) && !std::isnan(o.y); }
        bool SameFloat(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }

        ui::Owner::MagnifierConfig ConfigOf(const MagnifierParams& p, const ui::Density& d) {
            ui::Owner::MagnifierConfig c;
            c.useTextDefault = p.useTextDefault;
            if (p.width.Specified() && p.height.Specified()) { c.widthPx = d.ToPx(p.width); c.heightPx = d.ToPx(p.height); }
            if (p.cornerRadius.Specified()) c.cornerRadiusPx = d.ToPx(p.cornerRadius);
            if (p.elevation.Specified()) c.elevationPx = d.ToPx(p.elevation);
            c.initialZoom = p.zoom;
            c.clippingEnabled = p.clippingEnabled;
            return c;
        }
    }

    void MagnifierController::Recreate(const MagnifierParams& p, const ui::Density& density) {
        const auto& host = m_Owner->Magnifier();
        if (m_Handle) {
            if (host.dismiss) host.dismiss(m_Handle);
            if (host.destroy) host.destroy(m_Handle);
            m_Handle = 0;
        }
        if (!host.create) return;
        m_Config = ConfigOf(p, density);
        m_Zoom = p.zoom;
        m_Density = density;
        m_Handle = host.create(m_Config);
        UpdateSizeIfNecessary(p, density);
    }

    void MagnifierController::UpdateSizeIfNecessary(const MagnifierParams& p, const ui::Density& density) {
        if (!m_Handle || !m_Owner->Magnifier().size) return;
        const ui::IntSize size = m_Owner->Magnifier().size(m_Handle);
        if (size.width == m_PreviousSize.width && size.height == m_PreviousSize.height) return;
        m_PreviousSize = size;
        if (p.onSizeChanged) p.onSizeChanged({ float(size.width) / density.density, float(size.height) / density.density });
    }

    void MagnifierController::Update(const MagnifierParams& p, ui::Owner* owner, Offset anchorInRoot, const ui::Density& density) {
        if (owner != m_Owner) {
            Detach();
            m_Owner = owner;
        }
        if (!m_Owner || !m_Owner->Magnifier().create) return;
        const auto& host = m_Owner->Magnifier();
        // update(): recreate when what the platform magnifier was built with changed.
        if (m_Handle) {
            const bool zoomChanged = !SameFloat(p.zoom, m_Zoom) && !host.canUpdateZoom;
            ui::Owner::MagnifierConfig config = ConfigOf(p, density);
            config.initialZoom = m_Config.initialZoom;   // the zoom itself updates in place
            if (zoomChanged || !(config == m_Config) || !(density == m_Density)) Recreate(p, density);
        }
        // updateMagnifier
        const Offset source = p.sourceCenter ? p.sourceCenter(density) : Offset{ NAN, NAN };
        if (Specified(source) && Specified(anchorInRoot)) {
            const Offset sourceInRoot = anchorInRoot + source;
            Offset center{ NAN, NAN };
            if (p.magnifierCenter) {
                const Offset c = p.magnifierCenter(density);
                if (Specified(c)) center = anchorInRoot + c;
            }
            if (!m_Handle) Recreate(p, density);
            if (m_Handle && host.show) host.show(m_Handle, sourceInRoot, center, p.zoom);
            m_Zoom = p.zoom;
            UpdateSizeIfNecessary(p, density);
            ScheduleContentUpdate();
            return;
        }
        if (m_Handle && host.dismiss) host.dismiss(m_Handle);
    }

    void MagnifierController::ScheduleContentUpdate() {
        // The draw signal (updateContent from the node's draw in Compose): the magnifier copies
        // the window's content once the frame with the change is presented. Frame waiters run
        // as a frame starts, before its draw, so the copy waits for the frame after. Changes
        // that come in meanwhile get another copy.
        if (!m_Handle) return;
        m_ContentRequested = true;
        if (m_ContentPosted) return;
        m_ContentPosted = true;
        m_ContentJob = rt::Launch(rt::Dispatcher::Main(), [](MagnifierController* self, std::weak_ptr<char> weak) -> rt::Task<> {
            while (!weak.expired() && self->m_ContentRequested) {
                self->m_ContentRequested = false;
                co_await rt::NextFrame{};
                if (weak.expired()) co_return;
                co_await rt::NextFrame{};
                if (weak.expired()) co_return;
                if (self->m_Handle && self->m_Owner && self->m_Owner->Magnifier().updateContent) self->m_Owner->Magnifier().updateContent(self->m_Handle);
            }
            if (!weak.expired()) self->m_ContentPosted = false;
        }(this, m_Alive));
    }

    void MagnifierController::Detach() {
        if (m_Handle && m_Owner) {
            const auto& host = m_Owner->Magnifier();
            if (host.dismiss) host.dismiss(m_Handle);
            if (host.destroy) host.destroy(m_Handle);
        }
        m_Handle = 0;
        m_PreviousSize = { -1, -1 };
    }

    // --- magnifier modifier ----------------------------------------------------------------------

    namespace {
        struct MagnifierModParams {
            std::shared_ptr<const MagnifierParams> p;
            bool operator==(const MagnifierModParams& o) const { return p == o.p; }
        };
        struct MagnifierNode final : ModifierNode, DrawModifier, ui::GlobalPositionAwareModifier {
            MagnifierModParams params;
            explicit MagnifierNode(MagnifierModParams p) : params(std::move(p)) {}
            DrawModifier* AsDraw() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            void OnParamsChanged() { InvalidateDraw(); }
            void OnDetach() override { m_Controller.Detach(); }
            void OnGloballyPositioned() override {
                // layoutCoordinates is state the update reads: the draw reruns.
                InvalidateDraw();
            }
            void Draw(ContentDrawScope& s) override {
                s.DrawContent();
                ui::Owner* owner = Owner() ? Owner()->GetOwner() : nullptr;
                const Offset anchor = Owner() ? selection::PositionInRoot(selection::Coordinates{ Owner(), this, AliveToken() }) : Offset{ NAN, NAN };
                m_Controller.Update(*params.p, owner, anchor, s);
            }
            MagnifierController m_Controller;
        };
    }

    Modifier MagnifierMod(Modifier m, MagnifierParams params) {
        return m.With<MagnifierNode>(MagnifierModParams{ std::make_shared<const MagnifierParams>(std::move(params)) });
    }

    // --- animated selection magnifier ------------------------------------------------------------

    SelectionMagnifier::SelectionMagnifier() : m_Center(anim::Vec{ NAN, NAN, 0, 0 }, 2) {}
    SelectionMagnifier::~SelectionMagnifier() { m_Animation.Cancel(); }

    void SelectionMagnifier::Update(const std::function<Offset(ui::IntSize)>& center, ui::Owner* owner, Offset anchorInRoot, const ui::Density& density) {
        m_Attached = true;
        const Offset target = center(m_MagnifierSize);
        if (!m_Target || !(SameFloat(m_Target->x, target.x) && SameFloat(m_Target->y, target.y))) {
            m_Target = target;
            const anim::Vec value = m_Center.Value();
            const bool valueSpecified = !std::isnan(value[0]) && !std::isnan(value[1]);
            // A move to another line springs; along the line (or from / to hidden) it snaps.
            if (valueSpecified && Specified(target) && value[1] != target.y) {
                m_Animation.Cancel();
                m_Animation = rt::Launch(rt::Dispatcher::Main(), [](anim::Animatable* a, Offset t) -> rt::Task<> {
                    co_await a->AnimateTo(anim::Vec{ t.x, t.y, 0, 0 }, anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessMedium, 0.01f));
                }(&m_Center, target));
            } else {
                m_Animation.Cancel();
                m_Center.SnapTo(anim::Vec{ target.x, target.y, 0, 0 });
            }
        }
        MagnifierParams p;
        anim::Animatable* animatable = &m_Center;
        p.sourceCenter = [animatable](const ui::Density&) { const anim::Vec v = animatable->Value(); return Offset{ v[0], v[1] }; };
        ui::IntSize* size = &m_MagnifierSize;
        p.onSizeChanged = [size, density](ui::Size dp) { *size = { density.RoundToPx(Dp(dp.width)), density.RoundToPx(Dp(dp.height)) }; };
        p.useTextDefault = true;
        m_Controller.Update(p, owner, anchorInRoot, density);
    }

    void SelectionMagnifier::Detach() {
        if (!m_Attached) return;
        m_Attached = false;
        m_Animation.Cancel();
        m_Controller.Detach();
        m_Target.reset();
        m_Center.SnapTo(anim::Vec{ NAN, NAN, 0, 0 });
    }

}
