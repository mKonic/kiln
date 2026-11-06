#include "kiln/ui/BringIntoView.h"

namespace kiln::ui {

    void BringIntoView(LayoutNode& node, std::optional<Rect> rect, const ModifierNode* from) {
        // The rect in root coordinates, re-read each time (the content moves while scrolling).
        std::function<std::optional<Rect>()> inRoot = [alive = node.AliveToken(), n = &node, rect,
                                                        fromAlive = from ? from->AliveToken() : std::weak_ptr<const char>{}, from]() -> std::optional<Rect> {
            if (alive.expired() || !n->IsPlaced()) return std::nullopt;
            if (from && fromAlive.expired()) return std::nullopt;
            // Through layer transforms (localBoundingBoxOf), like the container's bounds below.
            return n->RectInRoot(rect, from);
        };
        for (LayoutNode* a = node.Parent(); a; a = a->Parent()) {
            const auto mods = a->ModifierNodes();
            // Innermost first: the responder closest to the content.
            for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
                auto* m = const_cast<ModifierNode*>(*it);
                BringIntoViewResponder* r = m->AsBringIntoViewResponder();
                const LayoutModifier* lm = m->AsLayout();
                if (!r || !lm) continue;
                auto bounds = [a, lm, aliveA = a->AliveToken(), aliveM = m->AliveToken()]() -> std::optional<Rect> {
                    if (aliveA.expired() || aliveM.expired()) return std::nullopt;
                    return a->LayoutModifierRectInRoot(lm);
                };
                auto local = [inRoot, bounds]() -> std::optional<Rect> {
                    auto rr = inRoot();
                    auto b = bounds();
                    if (!rr || !b) return std::nullopt;
                    return Rect{ rr->left - b->left, rr->top - b->top, rr->right - b->left, rr->bottom - b->top };
                };
                r->BringChildIntoView(local);
                // For the next ancestor: where this responder will have put it.
                inRoot = [local, bounds, r, aliveM = m->AliveToken()]() -> std::optional<Rect> {
                    auto l = local();
                    auto b = bounds();
                    if (!l || !b || aliveM.expired()) return std::nullopt;
                    const Rect d = r->CalculateRectForParent(*l);
                    return Rect{ d.left + b->left, d.top + b->top, d.right + b->left, d.bottom + b->top };
                };
            }
        }
        // The root responder (AndroidComposeView): the host's own scrolling parents.
        if (Owner* o = node.GetOwner())
            if (auto r = inRoot()) o->RequestRectangleOnScreen(*r);
    }

}
