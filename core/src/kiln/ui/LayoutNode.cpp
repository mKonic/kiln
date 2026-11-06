#include "kiln/ui/LayoutNode.h"

#include <algorithm>

namespace kiln::ui {

    // --- ModifierNode ---------------------------------------------------------------------

    void ModifierNode::InvalidateMeasure() { if (m_Owner) m_Owner->RequestRemeasure(); }
    void ModifierNode::InvalidateDraw() { if (m_Owner) m_Owner->InvalidateDraw(); }

    // --- Coordinators -------------------------------------------------------------------------

    struct LayoutNode::Coordinator final : Measurable, Placeable {
        LayoutNode& node;
        std::size_t index;
        LayoutModifier* mod;                 // null for the inner coordinator
        std::vector<DrawModifier*> draws;    // drawn within this coordinator's size
        MeasureResult result;
        IntOffset position;                  // within the enclosing coordinator (0: in parent)

        // The list handed to the policy. It lives until the next measure, so a policy's placement
        // lambda may keep the span it was given (they usually do).
        std::vector<Measurable*> children;

        Coordinator(LayoutNode& n, std::size_t i, LayoutModifier* m) : node(n), index(i), mod(m) {}

        Coordinator* Next() { return index + 1 < node.m_Coordinators.size() ? node.m_Coordinators[index + 1].get() : nullptr; }

        std::vector<Measurable*> ChildMeasurables() {
            std::vector<Measurable*> out;
            out.reserve(node.m_Children.size());
            for (LayoutNode* c : node.m_Children) out.push_back(&c->AsMeasurable());
            return out;
        }

        Placeable& Measure(const Constraints& c) override {
            if (index == 0) return node.MeasureNode(c);
            return MeasureHere(c);
        }

        Placeable& MeasureHere(const Constraints& c) {
            MeasureScope scope = node.Scope();
            if (mod) {
                result = mod->Measure(scope, *Next(), c);
            } else {
                children = ChildMeasurables();
                result = node.m_Policy ? node.m_Policy->Measure(scope, children, c) : MeasureResult{ c.minWidth, c.minHeight, {} };
            }
            SetMeasured({ result.width, result.height }, c);
            return *this;
        }

        void PlaceAt(IntOffset p, float z) override {
            if (index == 0) {
                LayoutNode* parent = node.m_Parent;
                const bool moved = !node.m_Placed || node.m_Position != p || node.m_ZIndex != z;
                node.m_Position = p;
                node.m_ZIndex = z;
                node.m_PlaceOrder = parent ? parent->m_NextPlaceOrder++ : 0;
                node.m_Placed = true;
                if (moved) node.InvalidateDraw();
                node.LayoutIfNeeded();
            } else {
                position = p;
            }
        }

        int MinIntrinsicWidth(int h) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MinIntrinsicWidth(s, *Next(), h);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MinIntrinsicWidth(s, ch, h) : 0;
        }
        int MaxIntrinsicWidth(int h) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MaxIntrinsicWidth(s, *Next(), h);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MaxIntrinsicWidth(s, ch, h) : 0;
        }
        int MinIntrinsicHeight(int w) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MinIntrinsicHeight(s, *Next(), w);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MinIntrinsicHeight(s, ch, w) : 0;
        }
        int MaxIntrinsicHeight(int w) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MaxIntrinsicHeight(s, *Next(), w);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MaxIntrinsicHeight(s, ch, w) : 0;
        }
        const ParentData& GetParentData() const override { return node.m_ParentData; }
    };

    // --- LayoutNode ---------------------------------------------------------------------------

    LayoutNode::LayoutNode() { RebuildChain(); }

    LayoutNode::~LayoutNode() {
        // Children may already be destroyed (the composition frees a removed subtree bottom-up),
        // so only unhook this node itself.
        if (m_Owner) m_Owner->Forget(*this);
        for (auto& n : m_Nodes) if (n->m_Owner) { n->OnDetach(); n->m_Owner = nullptr; }
    }

    void LayoutNode::Watcher::OnStateChanged() {
        if (phase == 0) node.RequestRemeasure();
        else if (phase == 1) node.RequestRelayout();
        else node.InvalidateDraw();
    }

    MeasureScope LayoutNode::Scope() const {
        return MeasureScope(m_Owner ? m_Owner->GetDensity() : Density{}, m_Owner ? m_Owner->Direction() : LayoutDirection::Ltr);
    }

    Measurable& LayoutNode::AsMeasurable() { return *m_Coordinators.front(); }

    IntSize LayoutNode::Size() const {
        const auto& c = *m_Coordinators.front();
        return { c.Width(), c.Height() };
    }

    Offset LayoutNode::PositionInRoot() const {
        Offset o;
        for (const LayoutNode* n = this; n; n = n->m_Parent) {
            o = o + Offset{ float(n->m_Position.x), float(n->m_Position.y) };
            if (n->m_Parent)
                for (std::size_t i = 1; i < n->m_Parent->m_Coordinators.size(); ++i) {
                    const auto& c = *n->m_Parent->m_Coordinators[i];
                    o = o + Offset{ float(c.position.x), float(c.position.y) };
                }
        }
        return o;
    }

    void LayoutNode::SetMeasurePolicy(std::shared_ptr<MeasurePolicy> policy) {
        if (policy == m_Policy) return;
        m_Policy = std::move(policy);
        RequestRemeasure();
    }

    void LayoutNode::SetModifier(const Modifier& modifier) {
        const auto& next = modifier.Elements();
        bool structural = next.size() != m_Elements.size();
        std::vector<std::unique_ptr<ModifierNode>> nodes;
        nodes.reserve(next.size());
        for (std::size_t i = 0; i < next.size(); ++i) {
            const ModifierElement& e = *next[i];
            if (i < m_Elements.size() && m_Nodes[i] && m_Elements[i]->Type() == e.Type()) {
                if (!m_Elements[i]->Equals(e)) e.Update(*m_Nodes[i]);
                nodes.push_back(std::move(m_Nodes[i]));
            } else {
                structural = true;
                auto n = e.Create();
                n->m_Owner = this;
                nodes.push_back(std::move(n));
                if (m_Owner) nodes.back()->OnAttach();
            }
        }
        for (auto& old : m_Nodes)
            if (old) {
                if (m_Owner) {
                    if (auto* p = old->AsPointerInput()) m_Owner->ForgetPointerModifier(p);
                    old->OnDetach();
                }
                old->m_Owner = nullptr;
            }
        m_Nodes = std::move(nodes);
        m_Elements = next;
        if (structural) RebuildChain();
        // Parent data may have changed: the parent measures with it.
        ParentData pd;
        for (auto& n : m_Nodes) if (auto* p = n->AsParentData()) p->ModifyParentData(pd);
        m_ParentData = std::move(pd);
        if (m_Parent) m_Parent->RequestRemeasure();
        RequestRemeasure();
        InvalidateDraw();
    }

    void LayoutNode::RebuildChain() {
        std::vector<std::unique_ptr<Coordinator>> chain;
        std::vector<DrawModifier*> pending;
        (void)0;
        for (auto& n : m_Nodes) {
            if (auto* d = n->AsDraw()) pending.push_back(d);
            if (auto* l = n->AsLayout()) {
                chain.push_back(std::make_unique<Coordinator>(*this, chain.size(), l));
                chain.back()->draws = std::move(pending);
                pending.clear();
            }
        }
        chain.push_back(std::make_unique<Coordinator>(*this, chain.size(), nullptr));
        chain.back()->draws = std::move(pending);
        m_Coordinators = std::move(chain);
        m_Measured = false;
        m_MeasureDirty = true;
    }

    void LayoutNode::SetChildren(std::vector<rt::TreeNode*> children) {
        std::vector<LayoutNode*> next;
        next.reserve(children.size());
        for (auto* c : children) next.push_back(static_cast<LayoutNode*>(c));
        if (next == m_Children) return;
        for (LayoutNode* old : m_Children)
            if (std::find(next.begin(), next.end(), old) == next.end()) {
                old->Detach();
                old->m_Parent = nullptr;
            }
        for (LayoutNode* c : next) {
            if (c->m_Parent != this) {
                c->m_Parent = this;
                if (m_Owner) c->Attach(*m_Owner, m_Depth + 1);
            }
        }
        m_Children = std::move(next);
        RequestRemeasure();
    }

    void LayoutNode::Attach(Owner& owner, int depth) {
        if (m_Owner) owner.Forget(*this);
        const bool fresh = m_Owner == nullptr;
        m_Owner = &owner;
        m_Depth = depth;
        if (fresh) for (auto& n : m_Nodes) n->OnAttach();
        for (LayoutNode* c : m_Children) c->Attach(owner, depth + 1);
        m_MeasureDirty = true;
        owner.OnMeasureRequested(*this);
    }

    void LayoutNode::Detach() {
        for (LayoutNode* c : m_Children) c->Detach();
        if (m_Owner) {
            m_Owner->Forget(*this);
            for (auto& n : m_Nodes) n->OnDetach();
        }
        m_MeasureWatch.Unsubscribe();
        m_LayoutWatch.Unsubscribe();
        m_DrawWatch.Unsubscribe();
        m_Owner = nullptr;
        m_Placed = false;
    }

    void LayoutNode::RequestRemeasure() {
        m_MeasureDirty = true;
        m_LayoutDirty = true;
        if (m_Owner) m_Owner->OnMeasureRequested(*this);
    }

    void LayoutNode::RequestRelayout() {
        m_LayoutDirty = true;
        if (m_Owner) m_Owner->OnLayoutRequested(*this);
    }

    void LayoutNode::InvalidateDraw() {
        if (m_Owner) m_Owner->OnDrawRequested();
    }

    Placeable& LayoutNode::MeasureNode(const Constraints& c) {
        Coordinator& outer = *m_Coordinators.front();
        if (m_Measured && !m_MeasureDirty && c == m_LastConstraints) return outer;
        const IntSize before = Size();
        rt::Observation::Track(m_MeasureWatch, [&] { outer.MeasureHere(c); });
        m_LastConstraints = c;
        m_Measured = true;
        m_MeasureDirty = false;
        m_LayoutDirty = true;
        if (m_Owner) {
            m_Owner->m_MeasureDirty.erase(this);
            m_Owner->m_LayoutDirty.insert(this);
        }
        if (before != Size()) InvalidateDraw();
        return outer;
    }

    void LayoutNode::LayoutChain() {
        m_LayoutDirty = false;
        if (m_Owner) m_Owner->m_LayoutDirty.erase(this);
        m_NextPlaceOrder = 0;
        const LayoutDirection dir = m_Owner ? m_Owner->Direction() : LayoutDirection::Ltr;
        rt::Observation::Track(m_LayoutWatch, [&] {
            for (auto& c : m_Coordinators) {
                PlacementScope scope(dir, c->Width());
                if (c->result.place) c->result.place(scope);
            }
        });
        InvalidateDraw();
    }

    void LayoutNode::Draw(ContentDrawScope& scope) {
        if (!m_Placed && m_Parent) return;
        DrawCoordinator(0, scope);
    }

    void LayoutNode::DrawCoordinator(std::size_t index, ContentDrawScope& scope) {
        Coordinator& c = *m_Coordinators[index];
        const ui::Size size{ float(c.Width()), float(c.Height()) };
        // Draw modifiers nest: each one's DrawContent runs the next, the last draws the wrapped
        // content. Their reads subscribe this node's draw watcher.
        std::function<void(std::size_t)> step = [&](std::size_t d) {
            if (d == c.draws.size()) { DrawWrapped(index, scope); return; }
            scope.SetSize(size);
            scope.SetContent([&, d] { step(d + 1); });
            rt::Observation::Track(m_DrawWatch, [&] { c.draws[d]->Draw(scope); });
        };
        step(0);
    }

    void LayoutNode::DrawWrapped(std::size_t index, ContentDrawScope& scope) {
        if (index + 1 < m_Coordinators.size()) {
            const IntOffset p = m_Coordinators[index + 1]->position;
            scope.Translate({ float(p.x), float(p.y) });
            DrawCoordinator(index + 1, scope);
            scope.Translate({ -float(p.x), -float(p.y) });
            return;
        }
        // Children in zIndex order, ties by placement order (spec 03 §layout).
        std::vector<LayoutNode*> order;
        for (LayoutNode* ch : m_Children) if (ch->m_Placed) order.push_back(ch);
        std::stable_sort(order.begin(), order.end(), [](const LayoutNode* a, const LayoutNode* b) {
            return a->m_ZIndex != b->m_ZIndex ? a->m_ZIndex < b->m_ZIndex : a->m_PlaceOrder < b->m_PlaceOrder;
        });
        for (LayoutNode* ch : order) {
            const Offset p{ float(ch->m_Position.x), float(ch->m_Position.y) };
            scope.Translate(p);
            ch->Draw(scope);
            scope.Translate({ -p.x, -p.y });
        }
    }

    // --- Owner --------------------------------------------------------------------------------

    namespace {
        // RootMeasurePolicy (ui/layout/RootMeasurePolicy.kt): children get the window's
        // constraints as they are and sit at the origin.
        struct RootMeasurePolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                if (ch.empty()) return s.Layout(c.minWidth, c.minHeight);
                std::vector<Placeable*> ps;
                int w = 0, h = 0;
                for (Measurable* m : ch) {
                    Placeable& p = m->Measure(c);
                    w = std::max(w, p.Width());
                    h = std::max(h, p.Height());
                    ps.push_back(&p);
                }
                return s.Layout(c.ConstrainWidth(w), c.ConstrainHeight(h), [ps](PlacementScope& place) {
                    for (Placeable* p : ps) place.PlaceRelative(*p, 0, 0);
                });
            }
        };
    }

    Owner::Owner() : m_Root(std::make_unique<LayoutNode>()) {
        m_RootFocus.m_IsRoot = true;
        m_RootFocus.focusability = Focusability::Never;
        m_Root->SetMeasurePolicy(std::make_shared<RootMeasurePolicy>());
        m_Root->Attach(*this, 0);
    }

    Owner::~Owner() {
        m_Root->Detach();
    }

    void Owner::OnMeasureRequested(LayoutNode& n) {
        m_MeasureDirty.insert(&n);
        if (m_RequestFrame) m_RequestFrame();
    }

    void Owner::OnLayoutRequested(LayoutNode& n) {
        m_LayoutDirty.insert(&n);
        if (m_RequestFrame) m_RequestFrame();
    }

    void Owner::RequestFullRemeasure() {
        std::vector<LayoutNode*> stack{ m_Root.get() };
        while (!stack.empty()) {
            LayoutNode* n = stack.back();
            stack.pop_back();
            n->RequestRemeasure();
            for (LayoutNode* c : n->m_Children) stack.push_back(c);
        }
    }

    void Owner::MeasureAndLayout(IntSize window) {
        if (window != m_Window) {
            m_Window = window;
            m_Root->RequestRemeasure();
        }
        // Shallowest first. A node whose size changed makes its parent remeasure, which the
        // ordered set then visits next (it is shallower).
        while (!m_MeasureDirty.empty()) {
            LayoutNode* n = *m_MeasureDirty.begin();
            m_MeasureDirty.erase(m_MeasureDirty.begin());
            if (!n->m_MeasureDirty) continue;
            if (n == m_Root.get()) {
                n->MeasureNode(Constraints::Fixed(window.width, window.height));
                n->m_Coordinators.front()->PlaceAt({ 0, 0 }, 0);
                continue;
            }
            if (!n->m_Measured || !n->m_Parent) {
                // Never measured: its parent has to measure it.
                if (n->m_Parent) n->m_Parent->RequestRemeasure();
                continue;
            }
            const IntSize before = n->Size();
            n->MeasureNode(n->m_LastConstraints);
            if (n->Size() != before) n->m_Parent->RequestRemeasure();
            else n->RequestRelayout();
        }
        while (!m_LayoutDirty.empty()) {
            LayoutNode* n = *m_LayoutDirty.begin();
            m_LayoutDirty.erase(m_LayoutDirty.begin());
            if (n->m_Placed || n == m_Root.get()) n->LayoutIfNeeded();
        }
    }

    std::vector<PointerInputModifier*> LayoutNode::PointerModifiers() const {
        std::vector<PointerInputModifier*> out;
        for (auto& n : m_Nodes) if (auto* p = n->AsPointerInput()) out.push_back(p);
        return out;
    }

    void Owner::Forget(LayoutNode& n) {
        if (m_ActiveFocus && NodeOf(*m_ActiveFocus) == &n) { m_ActiveFocus = nullptr; m_FocusCaptured = false; }
        m_MeasureDirty.erase(&n);
        m_LayoutDirty.erase(&n);
        for (auto& [id, t] : m_Pointers)
            std::erase_if(t.path, [&](const HitEntry& e) { return e.node == &n; });
    }

    void Owner::ForgetPointerModifier(PointerInputModifier* m) {
        for (auto& [id, t] : m_Pointers)
            std::erase_if(t.path, [&](const HitEntry& e) { return e.mod == m; });
    }

    void Owner::Draw(const DrawTarget& target) {
        ContentDrawScope scope(m_Density, m_Direction, target);
        m_Root->Draw(scope);
    }

}
