#include "kiln/foundation/ScreenModifiers.h"

#include "kiln/ui/LayoutNode.h"

#include <stdexcept>

namespace kiln::foundation {

    namespace {
        ui::Owner* OwnerOf(const ModifierNode& n) { return n.Owner() ? n.Owner()->GetOwner() : nullptr; }

        struct KeepParams { bool operator==(const KeepParams&) const = default; };
        struct KeepScreenOnNode final : ModifierNode {
            KeepParams params;
            explicit KeepScreenOnNode(KeepParams p) : params(p) {}
            void OnParamsChanged() {}
            void OnAttach() override { if ((m_Owner = OwnerOf(*this))) m_Owner->IncrementKeepScreenOnCount(); }
            void OnDetach() override { if (m_Owner) m_Owner->DecrementKeepScreenOnCount(); m_Owner = nullptr; }
            ui::Owner* m_Owner = nullptr;
        };

        struct SensitiveParams {
            bool isContentSensitive = true;
            bool operator==(const SensitiveParams&) const = default;
        };
        struct SensitiveContentNode final : ModifierNode {
            SensitiveParams params;
            explicit SensitiveContentNode(SensitiveParams p) : params(p) {}
            void OnParamsChanged() {
                if (!m_Owner) return;
                if (params.isContentSensitive && !m_Counted) {
                    m_Owner->IncrementSensitiveComponentCount();
                    m_Counted = true;
                } else if (!params.isContentSensitive && m_Counted) {
                    m_Owner->DecrementSensitiveComponentCount();
                    m_Counted = false;
                }
            }
            void OnAttach() override {
                m_Owner = OwnerOf(*this);
                if (params.isContentSensitive && m_Owner) {
                    if (m_Counted) throw std::logic_error("invalid sensitive content state");
                    m_Owner->IncrementSensitiveComponentCount();
                    m_Counted = true;
                }
            }
            void OnDetach() override {
                if (m_Counted && m_Owner) m_Owner->DecrementSensitiveComponentCount();
                m_Counted = false;
                m_Owner = nullptr;
            }
            ui::Owner* m_Owner = nullptr;
            bool m_Counted = false;
        };
    }

    Modifier KeepScreenOn(Modifier m) { return m.With<KeepScreenOnNode>(KeepParams{}); }
    Modifier SensitiveContent(Modifier m, bool isContentSensitive) { return m.With<SensitiveContentNode>(SensitiveParams{ isContentSensitive }); }

}
