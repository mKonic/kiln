#include "kiln/ui/DragAndDrop.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        enum class Traverse : std::uint8_t { Continue, SkipSubtree, Cancel };

        // The drag and drop nodes below `node` in `layout`'s chain and its children, nearest
        // first: a node's own descendants are found through it.
        void CollectBelow(const LayoutNode& layout, std::size_t fromIndex, std::vector<DragAndDropNode*>& out) {
            const auto nodes = layout.ModifierNodes();
            for (std::size_t i = fromIndex; i < nodes.size(); ++i)
                if (auto* d = const_cast<ModifierNode*>(nodes[i])->AsDragAndDrop()) { out.push_back(d); return; }
            for (const LayoutNode* c : layout.Children()) CollectBelow(*c, 0, out);
        }

        bool Unspecified(Offset o) { return std::isnan(o.x) || std::isnan(o.y); }
    }

    ui::Owner* DragAndDropNode::OwnerOf() const { return ModifierNode::Owner() ? ModifierNode::Owner()->GetOwner() : nullptr; }

    std::vector<DragAndDropNode*> DragAndDropNode::Children() const {
        std::vector<DragAndDropNode*> out;
        const LayoutNode* layout = ModifierNode::Owner();
        if (!layout) return out;
        const auto nodes = layout->ModifierNodes();
        const auto it = std::find(nodes.begin(), nodes.end(), this);
        CollectBelow(*layout, std::size_t(it - nodes.begin()) + 1, out);
        return out;
    }

    // traverseSelfAndDescendants: the block on this node, then (unless it stops) depth first on
    // the nodes below.
    template<typename F>
    void DragAndDropNode::TraverseSelfAndDescendants(F&& block) {
        bool cancelled = false;
        kiln::Function<void(DragAndDropNode&)> visit = [&](DragAndDropNode& n) {
            if (cancelled) return;
            const Traverse t = block(n);
            if (t == Traverse::Cancel) { cancelled = true; return; }
            if (t == Traverse::SkipSubtree) return;
            for (DragAndDropNode* c : n.Children()) visit(*c);
        };
        visit(*this);
    }

    DragAndDropNode* DragAndDropNode::FirstDescendant(const kiln::Function<bool(DragAndDropNode&)>& predicate) {
        if (!Attached()) return nullptr;
        DragAndDropNode* match = nullptr;
        kiln::Function<bool(DragAndDropNode&)> visit = [&](DragAndDropNode& n) {
            for (DragAndDropNode* c : n.Children()) {
                if (predicate(*c)) { match = c; return true; }
                if (visit(*c)) return true;
            }
            return false;
        };
        visit(*this);
        return match;
    }

    std::optional<Rect> DragAndDropNode::BoundsInRoot() const {
        const LayoutNode* layout = ModifierNode::Owner();
        if (!layout || !layout->IsPlaced()) return std::nullopt;
        return layout->RectInRoot(std::nullopt, this);
    }

    bool DragAndDropNode::Contains(Offset p) const {
        if (!Attached()) return false;
        const auto r = BoundsInRoot();
        return r && p.x >= r->left && p.x <= r->right && p.y >= r->top && p.y <= r->bottom;
    }

    void DragAndDropNode::OnDetach() {
        m_ThisTarget = nullptr;
        m_LastChild = nullptr;
    }

    void DragAndDropNode::RequestDragAndDropTransfer(Offset offset) {
        if (!onStartTransfer) throw std::logic_error("requestDragAndDropTransfer needs onStartTransfer");
        if (ui::Owner* o = OwnerOf()) o->DragAndDrop().RequestDragAndDropTransfer(*this, offset);
    }

    void DragAndDropNode::StartDragAndDropTransfer(DragAndDropStartTransferScope& scope, Offset offset, const kiln::Function<bool()>& isTransferStarted) {
        const auto origin = BoundsInRoot();
        TraverseSelfAndDescendants([&](DragAndDropNode& n) {
            if (!n.Attached()) return Traverse::SkipSubtree;
            if (!n.onStartTransfer) return Traverse::Continue;
            if (!Unspecified(offset)) {
                const auto r = n.BoundsInRoot();
                if (!origin || !r) return Traverse::Continue;
                const Offset local{ origin->left + offset.x - r->left, origin->top + offset.y - r->top };
                if (!(local.x >= 0 && local.y >= 0 && local.x < r->Width() && local.y < r->Height())) return Traverse::Continue;
                n.onStartTransfer(scope, local);
            } else {
                n.onStartTransfer(scope, offset);
            }
            return isTransferStarted() ? Traverse::Cancel : Traverse::Continue;
        });
    }

    bool DragAndDropNode::AcceptDragAndDropTransfer(const DragAndDropEvent& start) {
        bool handled = false;
        ui::Owner* owner = OwnerOf();
        TraverseSelfAndDescendants([&](DragAndDropNode& n) {
            if (!n.Attached()) return Traverse::SkipSubtree;
            if (n.m_ThisTarget) throw std::logic_error("DragAndDropTarget self reference must be null at the start of a drag and drop session");
            n.m_ThisTarget = n.onDropTargetValidate ? n.onDropTargetValidate(start) : nullptr;
            const bool accepted = n.m_ThisTarget != nullptr;
            if (accepted && owner) owner->DragAndDrop().RegisterTargetInterest(&n);
            handled = handled || accepted;
            return Traverse::Continue;
        });
        return handled;
    }

    namespace {
        void DispatchEntered(DragAndDropTarget& t, const DragAndDropEvent& e) {
            t.OnEntered(e);
            t.OnMoved(e);
        }
    }

    void DragAndDropNode::OnStarted(const DragAndDropEvent& e) {
        if (m_ThisTarget) m_ThisTarget->OnStarted(e);
        else if (m_LastChild) m_LastChild->OnStarted(e);
    }

    void DragAndDropNode::OnEntered(const DragAndDropEvent& e) {
        if (m_ThisTarget) m_ThisTarget->OnEntered(e);
        else if (m_LastChild) m_LastChild->OnEntered(e);
    }

    void DragAndDropNode::OnMoved(const DragAndDropEvent& e) {
        ui::Owner* owner = OwnerOf();
        DragAndDropNode* current = m_LastChild;
        DragAndDropNode* next = nullptr;
        if (current && current->Contains(e.positionInRoot)) {
            next = current;
        } else {
            next = FirstDescendant([&](DragAndDropNode& child) {
                return owner && owner->DragAndDrop().IsInterestedTarget(&child) && child.Contains(e.positionInRoot);
            });
        }
        if (next && !current) {
            DispatchEntered(*next, e);
            if (m_ThisTarget) m_ThisTarget->OnExited(e);
        } else if (!next && current) {
            if (m_ThisTarget) DispatchEntered(*m_ThisTarget, e);
            current->OnExited(e);
        } else if (next != current) {
            if (next) DispatchEntered(*next, e);
            if (current) current->OnExited(e);
        } else if (next) {
            next->OnMoved(e);
        } else if (m_ThisTarget) {
            m_ThisTarget->OnMoved(e);
        }
        m_LastChild = next;
    }

    void DragAndDropNode::OnChanged(const DragAndDropEvent& e) {
        if (m_ThisTarget) m_ThisTarget->OnChanged(e);
        else if (m_LastChild) m_LastChild->OnChanged(e);
    }

    void DragAndDropNode::OnExited(const DragAndDropEvent& e) {
        if (m_ThisTarget) m_ThisTarget->OnExited(e);
        if (m_LastChild) m_LastChild->OnExited(e);
        m_LastChild = nullptr;
    }

    bool DragAndDropNode::OnDrop(const DragAndDropEvent& e) {
        if (m_LastChild) return m_LastChild->OnDrop(e);
        return m_ThisTarget ? m_ThisTarget->OnDrop(e) : false;
    }

    void DragAndDropNode::OnEnded(const DragAndDropEvent& e) {
        TraverseSelfAndDescendants([&](DragAndDropNode& n) {
            if (!n.Attached()) return Traverse::SkipSubtree;
            if (n.m_ThisTarget) n.m_ThisTarget->OnEnded(e);
            n.m_ThisTarget = nullptr;
            n.m_LastChild = nullptr;
            return Traverse::Continue;
        });
    }

    // --- the manager ---------------------------------------------------------------------------

    // The root DragAndDropNode: not in a chain; everything in the tree is below it.
    class DragAndDropManager::Root final : public DragAndDropNode {
    public:
        explicit Root(ui::Owner& o) : m_RootOwner(o) {}
    protected:
        std::vector<DragAndDropNode*> Children() const override {
            std::vector<DragAndDropNode*> out;
            CollectBelow(m_RootOwner.WindowRoot(), 0, out);
            return out;
        }
        bool Attached() const override { return true; }
        ui::Owner* OwnerOf() const override { return &m_RootOwner; }
    public:
        ui::Owner& m_RootOwner;
    };

    DragAndDropManager::DragAndDropManager(Owner& owner) : m_Owner(owner), m_Root(std::make_unique<Root>(owner)) {}
    DragAndDropManager::~DragAndDropManager() = default;

    void DragAndDropManager::RequestDragAndDropTransfer(DragAndDropNode& node, Offset offset) {
        struct Scope final : DragAndDropStartTransferScope {
            DragAndDropManager* m;
            bool started = false;
            bool StartDragAndDropTransfer(const DragAndDropTransferData& data, Size size, kiln::Function<void(DrawScope&)> draw) override {
                if (!m->m_Host.startDrag) return false;
                const int w = std::max(1, int(std::ceil(size.width))), h = std::max(1, int(std::ceil(size.height)));
                std::vector<std::uint8_t> pixels = draw ? m->m_Owner.RenderToPixels(w, h, draw) : std::vector<std::uint8_t>{};
                started = m->m_Host.startDrag(data, w, h, std::move(pixels));
                return started;
            }
        } scope;
        scope.m = this;
        node.StartDragAndDropTransfer(scope, offset, [&] { return scope.started; });
    }

    void DragAndDropManager::RegisterTargetInterest(DragAndDropTarget* target) {
        if (!IsInterestedTarget(target)) m_InterestedTargets.push_back(target);
    }
    bool DragAndDropManager::IsInterestedTarget(DragAndDropTarget* target) const {
        return std::find(m_InterestedTargets.begin(), m_InterestedTargets.end(), target) != m_InterestedTargets.end();
    }

    bool DragAndDropManager::OnDrag(const DragAndDropEvent& e) {
        using A = DragAndDropEvent::Action;
        switch (e.action) {
            case A::Started: {
                const bool accepted = m_Root->AcceptDragAndDropTransfer(e);
                const auto targets = m_InterestedTargets;
                for (DragAndDropTarget* t : targets) t->OnStarted(e);
                return accepted;
            }
            case A::Drop: return m_Root->OnDrop(e);
            case A::Entered: m_Root->OnEntered(e); return false;
            case A::Location: m_Root->OnMoved(e); return false;
            case A::Exited: m_Root->OnExited(e); return false;
            case A::Ended:
                m_Root->OnEnded(e);
                m_InterestedTargets.clear();
                return false;
        }
        return false;
    }

}
