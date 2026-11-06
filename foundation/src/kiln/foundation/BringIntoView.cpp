#include "kiln/foundation/BringIntoView.h"

namespace kiln::foundation {

    void BringIntoViewRequester::BringIntoView(std::optional<Rect> rect) {
        for (ui::ModifierNode* n : m_Nodes)
            if (n->Owner()) ui::BringIntoView(*n->Owner(), rect);
    }

    namespace {
        struct RequesterParams { BringIntoViewRequester* r; bool operator==(const RequesterParams&) const = default; };
        struct RequesterNode final : ui::ModifierNode {
            RequesterParams params;
            explicit RequesterNode(RequesterParams p) : params(p) {}
            void OnParamsChanged() {}
            void OnAttach() override { params.r->Add(this); }
            void OnDetach() override { params.r->Remove(this); }
        };
    }

    Modifier BringIntoViewRequesterMod(Modifier m, BringIntoViewRequester& r) { return m.With<RequesterNode>(RequesterParams{ &r }); }

}
