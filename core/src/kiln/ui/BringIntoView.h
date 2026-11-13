#pragma once

// Bring into view (foundation relocation + ContentInViewNode's contract): a node asks for a rect
// of itself to become visible, and every scrollable ancestor scrolls as needed, the outer ones
// towards where the inner ones will put it.

#include "kiln/ui/LayoutNode.h"
#include <kiln/Function.h>

#include <functional>
#include <optional>

namespace kiln::ui {

    class BringIntoViewResponder {
    public:
        virtual ~BringIntoViewResponder() = default;
        // Rects are in the responder's bounds (its layout modifier). Where `localRect` will be once
        // this responder has scrolled it in (calculateRectForParent).
        virtual Rect CalculateRectForParent(Rect localRect) = 0;
        // Scroll until the rect (re-read every frame; nullopt when gone) is visible.
        virtual void BringChildIntoView(kiln::Function<std::optional<Rect>()> localRect) = 0;
    };

    // bringIntoView(rect): `rect` in the space of modifier `from` (its coordinator, through the
    // layers before it), or of the node's outer bounds without one; all of that space when omitted.
    void BringIntoView(LayoutNode& node, std::optional<Rect> rect = std::nullopt, const ModifierNode* from = nullptr);

}
