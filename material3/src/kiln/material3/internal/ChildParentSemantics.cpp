#include "kiln/material3/internal/ChildParentSemantics.h"

namespace kiln::material3::internal {

    namespace {
        struct Params {
            std::shared_ptr<const SemanticsProperties> properties;
            bool operator==(const Params& o) const { return properties == o.properties; }
        };

        struct ParentNode final : ui::ModifierNode, ui::SemanticsModifier {
            Params params;
            bool semanticsConsumed = false;
            explicit ParentNode(Params p) : params(std::move(p)) {}
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void OnParamsChanged() { InvalidateSemantics(); }
            void ApplySemantics(ui::SemanticsConfiguration& c) override {
                c.mergeDescendants = true;   // shouldMergeDescendantSemantics
                if (!semanticsConsumed && params.properties && *params.properties) (*params.properties)(c);
            }
            // Invalidated only when the hand-over changes, so collecting semantics settles.
            void Obtain(ui::SemanticsConfiguration& c) {
                if (params.properties && *params.properties) (*params.properties)(c);
                if (!semanticsConsumed) {
                    semanticsConsumed = true;
                    InvalidateSemantics();
                }
            }
            void Release() {
                if (!semanticsConsumed) return;
                semanticsConsumed = false;
                InvalidateSemantics();
            }
        };

        // traverseAncestors(ParentSemanticsNodeKey): the nodes before this one in its chain,
        // nearest first, then each ancestor layout's chain.
        template<typename F> void ForEachAncestorParent(const ui::ModifierNode* self, F&& f) {
            ui::LayoutNode* layout = self->Owner();
            bool past = false;
            while (layout) {
                auto nodes = layout->ModifierNodes();
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
                    if (!past) { if (*it == self) past = true; continue; }
                    if (auto* p = dynamic_cast<const ParentNode*>(*it)) f(const_cast<ParentNode&>(*p));
                }
                past = true;
                layout = layout->Parent();
            }
        }

        struct ChildNode final : ui::ModifierNode, ui::SemanticsModifier {
            Params params;
            explicit ChildNode(Params p) : params(std::move(p)) {}
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void OnParamsChanged() { InvalidateSemantics(); }
            void ApplySemantics(ui::SemanticsConfiguration& c) override {
                ForEachAncestorParent(this, [&c](ParentNode& p) { p.Obtain(c); });
                if (params.properties && *params.properties) (*params.properties)(c);
            }
            void OnDetach() override { ForEachAncestorParent(this, [](ParentNode& p) { p.Release(); }); }
        };
    }

    ui::Modifier ChildSemantics(ui::Modifier m, SemanticsProperties properties) {
        return m.With<ChildNode>(Params{ std::make_shared<const SemanticsProperties>(std::move(properties)) });
    }

    ui::Modifier ParentSemantics(ui::Modifier m, SemanticsProperties properties) {
        return m.With<ParentNode>(Params{ std::make_shared<const SemanticsProperties>(std::move(properties)) });
    }

}
