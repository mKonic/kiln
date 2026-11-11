#include "kiln/foundation/Compose.h"
#include "kiln/ui/Locals.h"


#include <cmath>
#include <map>
#include <optional>

namespace kiln::foundation {

    void TakeLocalLayoutDirection(ui::LayoutNode& n) {
        const auto* provided = rt::Composition::Current()->ReadIfProvided(ui::LocalLayoutDirection);
        n.SetLayoutDirection(provided ? std::optional<ui::LayoutDirection>(*provided) : std::nullopt);
        // The density too (a subtree given its own LocalDensity measures and draws with it).
        const auto* density = rt::Composition::Current()->ReadIfProvided(ui::LocalDensity);
        n.SetLocalDensity(density ? std::optional<ui::Density>(*density) : std::nullopt);
    }

    LayoutNode& Layout(const Modifier& modifier, std::shared_ptr<MeasurePolicy> policy, std::function<void()> content, Key key) {
        auto* c = rt::Composition::Current();
        return c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                   [&](LayoutNode& n) { n.SetModifier(modifier); TakeLocalLayoutDirection(n); n.SetMeasurePolicy(policy); },
                                   std::move(content), key);
    }

    namespace {
        // LookaheadScopeImpl: the scope's coordinates are its parent's children space (or, at
        // the root, its first child's).
        struct LookaheadScopeImpl final : ui::LookaheadScope {
            LayoutNode* node = nullptr;
            ui::LayoutCoordinates LookaheadScopeCoordinates(const PlacementScope&) const override {
                if (!node) return {};
                if (const LayoutNode* p = node->Parent()) return p->InnerCoordinates();
                return node->Children().empty() ? ui::LayoutCoordinates{} : node->Children().front()->OuterCoordinates();
            }
        };
    }

    void LookaheadScope(std::function<void(ui::LookaheadScope&)> content, Key key) {
        auto* c = rt::Composition::Current();
        auto& scope = c->Remember<std::shared_ptr<LookaheadScopeImpl>>([] { return std::make_shared<LookaheadScopeImpl>(); }, {}, key);
        auto impl = scope;
        c->Node<LayoutNode>([] {
                                auto n = std::make_unique<LayoutNode>();
                                n->SetVirtual(true);
                                n->SetVirtualLookaheadRoot(true);
                                return n;
                            },
                            [&](LayoutNode& n) { impl->node = &n; },
                            [impl, content = std::move(content)] { if (content) content(*impl); }, key);
    }

    namespace {
        // A layout whose policy is a value type: keep the node's current policy if equal.
        template<typename P, typename... A>
        void ValueLayout(const Modifier& modifier, std::function<void()> content, Key key, A&&... args) {
            auto* c = rt::Composition::Current();
            P next(std::forward<A>(args)...);
            c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                [&](LayoutNode& n) {
                                    n.SetModifier(modifier);
                                    TakeLocalLayoutDirection(n);
                                    if (auto* cur = dynamic_cast<P*>(n.Policy().get()); !cur || !(*cur == next))
                                        n.SetMeasurePolicy(std::make_shared<P>(next));
                                },
                                std::move(content), key);
        }
    }

    void Box(const Modifier& m, Alignment a, bool propagateMin, std::function<void()> content, Key key) {
        ValueLayout<BoxPolicy>(m, std::move(content), key, a, propagateMin);
    }
    void Row(const Modifier& m, Arrangement h, AlignmentV v, std::function<void()> content, Key key) {
        ValueLayout<RowColumnPolicy>(m, std::move(content), key, true, h, v, AlignmentH::Start());
    }
    void Column(const Modifier& m, Arrangement v, AlignmentH h, std::function<void()> content, Key key) {
        ValueLayout<RowColumnPolicy>(m, std::move(content), key, false, v, AlignmentV::Top(), h);
    }
    namespace {
        // SpacerMeasurePolicy (Spacer.kt:51-53): fixed constraints give the size, else 0.
        struct SpacerPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                return s.Layout(c.HasFixedWidth() ? c.maxWidth : 0, c.HasFixedHeight() ? c.maxHeight : 0);
            }
            bool operator==(const SpacerPolicy&) const { return true; }
        };
    }

    void Spacer(const Modifier& m, Key key) {
        ValueLayout<SpacerPolicy>(m, {}, key);
    }

}
