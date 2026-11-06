#pragma once

// BringIntoViewRequester (foundation relocation): ask the scroll containers around a node to show
// it, or a rect of it.

#include "kiln/runtime/Composition.h"
#include "kiln/ui/BringIntoView.h"

#include <memory>
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
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
    private:
        std::vector<ui::ModifierNode*> m_Nodes;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

    Modifier BringIntoViewRequesterMod(Modifier m, BringIntoViewRequester& requester);

    // BringIntoViewSpec: how far a scroll container scrolls to show a child, asked every frame
    // of the scroll until it returns 0. offset: the child's leading edge from the container's
    // start; all in pixels along the scroll axis.
    class BringIntoViewSpec {
    public:
        virtual ~BringIntoViewSpec() = default;
        virtual float CalculateScrollDistance(float offset, float size, float containerSize) const {
            return DefaultCalculateScrollDistance(offset, size, containerSize);
        }
        // The least scroll that lines one edge up; nothing when visible or when it covers the container.
        static float DefaultCalculateScrollDistance(float offset, float size, float containerSize);
    };
    std::shared_ptr<const BringIntoViewSpec> DefaultBringIntoViewSpec();
    // TVs: the child's leading edge goes to 30 % of the container (or as far as it fits).
    std::shared_ptr<const BringIntoViewSpec> PivotBringIntoViewSpec();

    // LocalBringIntoViewSpec: unset (null) means the platform default: the pivot on a TV, else
    // the default.
    inline rt::Local<std::shared_ptr<const BringIntoViewSpec>> LocalBringIntoViewSpec{ nullptr };
    // The spec in effect here: LocalBringIntoViewSpec or the platform default.
    std::shared_ptr<const BringIntoViewSpec> CurrentBringIntoViewSpec();

}
