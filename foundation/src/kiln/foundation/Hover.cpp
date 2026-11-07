#include "kiln/foundation/Hover.h"

#include "kiln/ui/Pointer.h"

namespace kiln::foundation {

    using ui::PointerEvent;
    using ui::PointerEventPass;
    using ui::PointerEventType;

    namespace {

        struct HoverParams {
            std::shared_ptr<MutableInteractionSource> source;
            bool enabled = true;
            bool operator==(const HoverParams&) const = default;
        };

        struct HoverNode final : ui::ModifierNode, ui::PointerInputModifier {
            HoverParams params;
            explicit HoverNode(HoverParams p) : params(std::move(p)) {}
            ui::PointerInputModifier* AsPointerInput() override { return this; }
            void OnParamsChanged() {
                if (m_Source != params.source || !params.enabled) Exit();
            }
            void OnDetach() override { Exit(); }
            void OnPointerEvent(PointerEvent& e, PointerEventPass pass, ui::IntSize) override {
                if (pass != PointerEventPass::Main || !params.enabled) return;
                if (e.type == PointerEventType::Enter) Enter();
                else if (e.type == PointerEventType::Exit) Exit();
            }
            void OnCancelPointerInput() override { Exit(); }

            void Enter() {
                if (m_Id || !params.source) return;
                m_Source = params.source;
                m_Id = m_Source->Start(Interaction::Kind::Hover);
            }
            void Exit() {
                if (!m_Id) return;
                m_Source->End(Interaction::Kind::Hover, m_Id);
                m_Id = 0;
                m_Source.reset();
            }
            std::shared_ptr<MutableInteractionSource> m_Source;
            std::uint64_t m_Id = 0;
        };

        struct IconParams {
            PointerIcon icon;
            bool overrideDescendants;
            bool stylus = false;   // stylusHoverIcon: stylus and eraser; otherwise mouse
            std::optional<DpTouchBoundsExpansion> expansion;
            bool operator==(const IconParams&) const = default;
        };

        struct IconNode final : ui::ModifierNode, ui::PointerInputModifier {
            IconParams params;
            explicit IconNode(IconParams p) : params(p) {}
            ui::PointerInputModifier* AsPointerInput() override { return this; }
            void OnParamsChanged() { if (m_Hovered) Request(); }
            void OnDetach() override { Release(); }
            // isRelevantPointerType
            bool Relevant(const PointerEvent& e) const {
                for (const auto& c : e.changes) {
                    const bool stylus = c.type == ui::PointerType::Stylus || c.type == ui::PointerType::Eraser;
                    if (params.stylus ? stylus : c.type == ui::PointerType::Mouse) return true;
                }
                return false;
            }
            std::optional<std::array<float, 4>> TouchBoundsExpansion(const ui::Density& d, ui::LayoutDirection dir) const override {
                if (!params.expansion) return std::nullopt;
                return params.expansion->Resolve(d, dir);
            }
            void OnPointerEvent(PointerEvent& e, PointerEventPass pass, ui::IntSize) override {
                if (pass != PointerEventPass::Main) return;
                if (!Relevant(e)) return;
                if (e.type == PointerEventType::Enter || (e.type == PointerEventType::Move && !m_Hovered)) Request();
                else if (e.type == PointerEventType::Exit) Release();
            }
            void OnCancelPointerInput() override { Release(); }

            void Request() {
                ui::Owner* o = Owner() ? Owner()->GetOwner() : nullptr;
                if (!o) return;
                m_Hovered = true;
                // An overriding ancestor ranks above any descendant.
                const int depth = params.overrideDescendants ? 1'000'000 - Owner()->Depth() : Owner()->Depth();
                o->RequestPointerIcon(this, depth, int(params.icon));
            }
            void Release() {
                if (!m_Hovered) return;
                m_Hovered = false;
                if (ui::Owner* o = Owner() ? Owner()->GetOwner() : nullptr) o->ReleasePointerIcon(this);
            }
            bool m_Hovered = false;
        };

    }

    Modifier Hoverable(Modifier m, std::shared_ptr<MutableInteractionSource> source, bool enabled) {
        return m.With<HoverNode>(HoverParams{ std::move(source), enabled });
    }

    Modifier PointerHoverIcon(Modifier m, PointerIcon icon, bool overrideDescendants) {
        return m.With<IconNode>(IconParams{ icon, overrideDescendants, false, std::nullopt });
    }
    Modifier StylusHoverIcon(Modifier m, PointerIcon icon, bool overrideDescendants, std::optional<DpTouchBoundsExpansion> expansion) {
        return m.With<IconNode>(IconParams{ icon, overrideDescendants, true, expansion });
    }

}
