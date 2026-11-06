#pragma once

// The layout tree (spec 03 req. 1-3). A LayoutNode is a TreeNode the composition builds; it owns
// a modifier node chain and a measure policy. Layout modifiers each get a coordinator wrapping
// the next; the innermost coordinator runs the policy over the children. Draw modifiers draw in
// the coordinator of the next layout modifier after them (or the inner one).

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/Layout.h"
#include "kiln/ui/Pointer.h"

#include <map>

#include <memory>
#include <set>
#include <vector>

namespace kiln { class TextBackend; }

namespace kiln::ui {

    class Owner;

    class LayoutNode final : public rt::TreeNode {
    public:
        LayoutNode();
        ~LayoutNode() override;

        void SetMeasurePolicy(std::shared_ptr<MeasurePolicy> policy);
        const std::shared_ptr<MeasurePolicy>& Policy() const { return m_Policy; }
        void SetModifier(const Modifier& modifier);
        void SetChildren(std::vector<rt::TreeNode*> children) override;

        LayoutNode* Parent() const { return m_Parent; }
        const std::vector<LayoutNode*>& Children() const { return m_Children; }
        Owner* GetOwner() const { return m_Owner; }
        int Depth() const { return m_Depth; }

        // Measured size and position of the node's outer bounds in its parent's content.
        IntSize Size() const;
        IntOffset Position() const { return m_Position; }
        float ZIndex() const { return m_ZIndex; }
        bool IsPlaced() const { return m_Placed; }
        Offset PositionInRoot() const;
        const ParentData& GetParentData() const { return m_ParentData; }

        void RequestRemeasure();
        void RequestRelayout();
        void InvalidateDraw();

        // Draws this node and its subtree with scope translated to the node's top-left.
        void Draw(ContentDrawScope& scope);

        Measurable& AsMeasurable();

    private:
        friend class Owner;
        friend class ModifierNode;
        struct Coordinator;
        struct Watcher final : rt::Subscriber {
            LayoutNode& node; int phase;
            Watcher(LayoutNode& n, int p) : node(n), phase(p) {}
            void OnStateChanged() override;
        };

        void Attach(Owner& owner, int depth);
        void Detach();
        std::vector<PointerInputModifier*> PointerModifiers() const;
        void RebuildChain();
        Placeable& MeasureNode(const Constraints& c);
        void LayoutChain();
        void LayoutIfNeeded() { if (m_LayoutDirty) LayoutChain(); }
        void DrawCoordinator(std::size_t index, ContentDrawScope& scope);
        void DrawWrapped(std::size_t index, ContentDrawScope& scope);
        MeasureScope Scope() const;

        Owner* m_Owner = nullptr;
        LayoutNode* m_Parent = nullptr;
        std::vector<LayoutNode*> m_Children;
        int m_Depth = 0;

        std::shared_ptr<MeasurePolicy> m_Policy;
        std::vector<std::shared_ptr<const ModifierElement>> m_Elements;
        std::vector<std::unique_ptr<ModifierNode>> m_Nodes;
        std::vector<std::unique_ptr<Coordinator>> m_Coordinators;   // outer .. inner
        ParentData m_ParentData;

        Constraints m_LastConstraints;
        bool m_Measured = false;
        bool m_MeasureDirty = true;
        bool m_LayoutDirty = true;
        bool m_Placed = false;
        IntOffset m_Position;
        float m_ZIndex = 0;
        int m_PlaceOrder = 0;
        int m_NextPlaceOrder = 0;   // for children, reset each layout of this node

        Watcher m_MeasureWatch{ *this, 0 }, m_LayoutWatch{ *this, 1 }, m_DrawWatch{ *this, 2 };
    };

    // The root of a window's layout tree (Compose's Owner / MeasureAndLayoutDelegate).
    class Owner {
    public:
        Owner();
        ~Owner();

        LayoutNode& Root() { return *m_Root; }
        void SetDensity(Density d) { m_Density = d; RequestFullRemeasure(); }
        Density GetDensity() const { return m_Density; }
        void SetDirection(LayoutDirection d) { m_Direction = d; RequestFullRemeasure(); }
        LayoutDirection Direction() const { return m_Direction; }
        void SetText(TextBackend* text) { m_Text = text; }
        TextBackend* Text() const { return m_Text; }
        void SetRequestFrame(std::function<void()> fn) { m_RequestFrame = std::move(fn); }

        // Measures dirty nodes (root with the window size) and places what moved.
        void MeasureAndLayout(IntSize window);
        void Draw(const DrawTarget& target);

        // Pointer input: hit test on press, dispatch in three passes along the kept path.
        void ProcessPointer(const RawPointerEvent& event);
        ViewConfiguration& Config() { return m_Config; }
        const ViewConfiguration& Config() const { return m_Config; }
        bool NeedsMeasureOrLayout() const { return !m_MeasureDirty.empty() || !m_LayoutDirty.empty(); }

    private:
        friend class LayoutNode;
        struct ByDepth { bool operator()(const LayoutNode* a, const LayoutNode* b) const {
            return a->Depth() != b->Depth() ? a->Depth() < b->Depth() : a < b; } };
        void OnMeasureRequested(LayoutNode& n);
        void OnLayoutRequested(LayoutNode& n);
        void OnDrawRequested() { if (m_RequestFrame) m_RequestFrame(); }
        void Forget(LayoutNode& n);
        void ForgetPointerModifier(PointerInputModifier* m);
        struct HitEntry { LayoutNode* node; PointerInputModifier* mod; };
        void HitTest(LayoutNode& n, Offset p, bool touch, std::vector<HitEntry>& path,
                     std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors);
        void RequestFullRemeasure();

        std::unique_ptr<LayoutNode> m_Root;
        Density m_Density;
        LayoutDirection m_Direction = LayoutDirection::Ltr;
        TextBackend* m_Text = nullptr;
        std::function<void()> m_RequestFrame;
        std::set<LayoutNode*, ByDepth> m_MeasureDirty, m_LayoutDirty;
        IntSize m_Window;
        ViewConfiguration m_Config;
        struct Tracked { std::vector<HitEntry> path; Offset position; bool pressed = false; std::int64_t uptime = 0; PointerType type{}; };
        std::map<std::int64_t, Tracked> m_Pointers;
    };

}
