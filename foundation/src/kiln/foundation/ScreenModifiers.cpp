#include "kiln/foundation/ScreenModifiers.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

#include <cmath>
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

    namespace {
        // RectListNode.
        struct RectListParams {
            int kind = 0;
            std::shared_ptr<const kiln::Function<Rect(ui::Size)>> rect;
            bool operator==(const RectListParams&) const = default;
        };
        struct RectListNode final : ModifierNode, ui::GlobalPositionAwareModifier {
            RectListParams params;
            explicit RectListNode(RectListParams p) : params(std::move(p)) {}
            // The rect provider changed: recompute once the chain is rebuilt.
            void OnParamsChanged() {
                rt::Dispatcher::Main().Post([this, alive = AliveToken()] { if (!alive.expired()) OnGloballyPositioned(); });
            }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            void OnGloballyPositioned() override {
                LayoutNode* n = Owner();
                ui::Owner* o = OwnerOf(*this);
                if (!n || !o || !n->IsPlaced()) return;
                ui::Size size;
                n->SpaceToRoot(this, &size);
                const Rect local = params.rect && *params.rect ? (*params.rect)(size) : Rect{ 0, 0, size.width, size.height };
                // calcBounds: the bounding box of the rect's corners in the root.
                const Rect r = n->RectInRoot(local, this);
                const ui::IntRect ir{ int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) };
                m_Owner = o;
                o->ReplaceViewRect(params.kind, this, ir);
            }
            void OnDetach() override {
                if (m_Owner) m_Owner->ReplaceViewRect(params.kind, this, std::nullopt);
                m_Owner = nullptr;
            }
            ui::Owner* m_Owner = nullptr;
        };
    }

    Modifier SystemGestureExclusion(Modifier m, kiln::Function<Rect(ui::Size)> rect) {
        return m.With<RectListNode>(RectListParams{ 0, rect ? std::make_shared<const kiln::Function<Rect(ui::Size)>>(std::move(rect)) : nullptr });
    }
    Modifier PreferKeepClear(Modifier m, kiln::Function<Rect(ui::Size)> rect) {
        return m.With<RectListNode>(RectListParams{ 1, rect ? std::make_shared<const kiln::Function<Rect(ui::Size)>>(std::move(rect)) : nullptr });
    }

    Modifier KeepScreenOn(Modifier m) { return m.With<KeepScreenOnNode>(KeepParams{}); }
    Modifier SensitiveContent(Modifier m, bool isContentSensitive) { return m.With<SensitiveContentNode>(SensitiveParams{ isContentSensitive }); }

}
