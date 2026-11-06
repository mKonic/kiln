#include "kiln/foundation/Insets.h"

namespace kiln::foundation {

    ui::InsetsValue WindowInsets::Resolve(ui::WindowInsetsHolder& h) const {
        ui::InsetsValue v;
        for (std::uint32_t i = 0; i < std::uint32_t(ui::InsetsType::Count); ++i)
            if (types & (1u << i)) v = v.Union(h.Get(ui::InsetsType(i)));
        if (!left) v.left = 0;
        if (!top) v.top = 0;
        if (!right) v.right = 0;
        if (!bottom) v.bottom = 0;
        return v;
    }

    namespace {
        struct InsetsParams { WindowInsets insets; bool pad; bool operator==(const InsetsParams&) const = default; };

        // InsetsPaddingModifierNode: pads by (insets - consumed by ancestors) and counts its own
        // insets as consumed for descendants. Reading the holder subscribes measure to changes.
        struct InsetsNode final : ModifierNode, LayoutModifier {
            InsetsParams params;
            explicit InsetsNode(InsetsParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }

            ui::InsetsValue Own(ui::WindowInsetsHolder& h) const { return params.insets.Resolve(h); }

            ui::InsetsValue AncestorConsumed(ui::WindowInsetsHolder& h) const {
                ui::InsetsValue consumed;
                LayoutNode* n = Owner();
                if (!n) return consumed;
                // Earlier nodes in this chain, then every ancestor's chain.
                bool before = true;
                for (LayoutNode* cur = n; cur; cur = cur->Parent(), before = false) {
                    for (const auto* m : NodesOf(cur)) {
                        if (before && m == this) break;
                        if (auto* x = dynamic_cast<const InsetsNode*>(m)) consumed = consumed.Union(x->Own(h));
                    }
                }
                return consumed;
            }
            static std::vector<const ModifierNode*> NodesOf(LayoutNode* n) { return n->ModifierNodes(); }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                auto* owner = Owner() ? Owner()->GetOwner() : nullptr;
                ui::InsetsValue pad;
                if (owner && params.pad) {
                    auto& h = owner->Insets();
                    const ui::InsetsValue own = Own(h), done = AncestorConsumed(h);
                    pad = { std::max(0, own.left - done.left), std::max(0, own.top - done.top),
                            std::max(0, own.right - done.right), std::max(0, own.bottom - done.bottom) };
                }
                const int horizontal = pad.left + pad.right, vertical = pad.top + pad.bottom;
                Placeable& p = m.Measure(c.Offset(-horizontal, -vertical));
                const int left = pad.left, top = pad.top;
                return s.Layout(c.ConstrainWidth(p.Width() + horizontal), c.ConstrainHeight(p.Height() + vertical),
                                [&p, left, top](PlacementScope& place) { place.Place(p, left, top); });
            }
        };
    }

    Modifier WindowInsetsPadding(Modifier m, WindowInsets insets) { return m.With<InsetsNode>(InsetsParams{ insets, true }); }
    Modifier ConsumeWindowInsets(Modifier m, WindowInsets insets) { return m.With<InsetsNode>(InsetsParams{ insets, false }); }

}
