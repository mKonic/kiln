#pragma once

// BringIntoViewRequester (foundation relocation): ask the scroll containers around a node to show
// it, or a rect of it.

#include "kiln/ui/BringIntoView.h"

#include <vector>

namespace kiln::foundation {

    using ui::Modifier;
    using ui::Rect;

    class BringIntoViewRequester {
    public:
        // `rect` in the node's bounds; the whole node when omitted.
        void BringIntoView(std::optional<Rect> rect = std::nullopt);
        // internal
        void Add(ui::ModifierNode* n) { m_Nodes.push_back(n); }
        void Remove(ui::ModifierNode* n) { std::erase(m_Nodes, n); }
    private:
        std::vector<ui::ModifierNode*> m_Nodes;
    };

    Modifier BringIntoViewRequesterMod(Modifier m, BringIntoViewRequester& requester);

}
