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
#include "kiln/ui/Focus.h"
#include "kiln/ui/TextInput.h"
#include "kiln/ui/Insets.h"
#include "kiln/ui/Semantics.h"

#include <map>

#include <memory>
#include <set>
#include <unordered_map>
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
        // Measure-time child changes (subcomposition): attaches without re-dirtying this node,
        // which is being measured right now.
        void SetChildrenDuringMeasure(std::vector<LayoutNode*> children);
        // Children come from measure (SubcomposeLayout), not from the composition: SetChildren
        // from the composition is ignored.
        void SetMeasureManagedChildren(bool v) { m_MeasureManaged = v; }
        // Remeasurement.forceRemeasure: measure and place now with the last constraints.
        void ForceRemeasure();

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
        // The bounds a layout modifier of this node measured, in root coordinates (no layers).
        Rect LayoutModifierBoundsInRoot(const LayoutModifier* modifier) const;
        // Expires when the node is destroyed.
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
        // This node's local space (its outer bounds) -> root, including graphicsLayer transforms
        // (LayoutCoordinates.localToRoot).
        gfx::Matrix LocalToRoot() const { return ContentToRoot(false); }
        // inner: the space its children are placed in (after every modifier).
        gfx::Matrix ContentToRoot(bool inner) const;
        const ParentData& GetParentData() const { return m_ParentData; }
        std::vector<const ModifierNode*> ModifierNodes() const {
            std::vector<const ModifierNode*> out;
            for (auto& n : m_Nodes) out.push_back(n.get());
            return out;
        }

        void RequestRemeasure();
        void RequestRelayout();
        void InvalidateDraw();

        // Draws this node and its subtree with scope translated to the node's top-left.
        void Draw(ContentDrawScope& scope);

        Measurable& AsMeasurable();
        // Stable id for semantics / accessibility virtual views.
        int SemanticsId() const { return m_SemanticsId; }

    private:
        friend class Owner;
        friend class ModifierNode;
        friend class NestedScrollNode;
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
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
        ParentData m_ParentData;

        Constraints m_LastConstraints;
        bool m_Measured = false;
        bool m_MeasureManaged = false;
        bool m_MeasureDirty = true;
        bool m_LayoutDirty = true;
        bool m_Placed = false;
        bool m_PlacedThisPass = false;   // set by the parent's current layout pass
        IntOffset m_Position;
        float m_ZIndex = 0;
        int m_PlaceOrder = 0;
        int m_NextPlaceOrder = 0;   // for children, reset each layout of this node
        int m_SemanticsId = 0;

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

        // Focus (FocusOwnerImpl). Host hooks: ask the platform to focus the host view.
        FocusTargetNode* ActiveFocus() const { return m_ActiveFocus; }
        bool FocusCaptured() const { return m_FocusCaptured; }
        FocusState StateOf(const FocusTargetNode& t) const;
        bool RequestFocus(FocusTargetNode& t, FocusDirection direction);
        bool ClearFocus(bool force = false);
        bool MoveFocus(FocusDirection direction);
        bool CaptureFocus();
        bool FreeFocus();
        // Key input: preview tunnel, then bubble; unhandled keys move focus (AndroidComposeView
        // root key handler). Returns whether the event was consumed.
        bool DispatchKeyEvent(const KeyEvent& event);
        InputMode GetInputMode() const { return m_InputMode.Get(); }
        void SetInputMode(InputMode mode) { m_InputMode.Set(mode); }
        void SetRequestHostFocus(std::function<bool()> fn) { m_RequestHostFocus = std::move(fn); }
        // The platform took focus away from (or gave it back to) the host view.
        void OnHostFocusChanged(bool focused);

        // Text input: one active session at a time (TextInputServiceAndroid).
        void SetTextInputHost(TextInputHost host) { m_InputHost = std::move(host); }
        void StartTextInput(TextInputSession& session);
        void StopTextInput(TextInputSession& session);
        // The field's state changed outside the IME (programmatic edit, key, gesture).
        void NotifyTextInputChanged(TextInputSession& session, bool contentChanged);
        TextInputSession* ActiveTextInput() const { return m_Input; }

        WindowInsetsHolder& Insets() { return m_Insets; }

        // Semantics: rebuilt on demand from the layout tree. Changes bump the version and notify.
        SemanticsNode BuildSemanticsTree();
        void InvalidateSemantics() { ++m_SemanticsVersion; if (m_OnSemanticsChanged) m_OnSemanticsChanged(); }
        std::uint64_t SemanticsVersion() const { return m_SemanticsVersion; }
        void SetOnSemanticsChanged(std::function<void()> fn) { m_OnSemanticsChanged = std::move(fn); }
        LayoutNode* FindBySemanticsId(int id);
        int NextSemanticsId() { return ++m_LastSemanticsId; }
        // dispatchOnScrollChanged: a scrollable consumed `delta` (pointer coordinates) itself.
        // Kept per semantics id, in the direction of ACTION_SCROLL_FORWARD (down; the layout end),
        // for the host's next TYPE_VIEW_SCROLLED; TakeScrollDelta returns and clears it.
        void DispatchOnScrollChanged(LayoutNode& node, Offset delta);
        Offset TakeScrollDelta(int semanticsId);
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
        friend class FocusTargetNode;
        LayoutNode* NodeOf(const FocusTargetNode& t) const;
        FocusTargetNode* FocusParent(const FocusTargetNode& t) const;
        std::vector<FocusTargetNode*> FocusChildren(const FocusTargetNode& t) const;
        void FirstTargets(LayoutNode& n, std::vector<FocusTargetNode*>& out) const;
        Rect FocusRect(const FocusTargetNode& t) const;
        bool Eligible(const FocusTargetNode& t) const;
        bool CanFocus(const FocusTargetNode& t) const;
        void SetActive(FocusTargetNode* t);
        void DispatchFocusEvents();
        bool FindChildForEnter(FocusTargetNode& t, FocusDirection dir, const std::function<bool(FocusTargetNode&)>& onFound);
        bool Search2D(FocusTargetNode& t, FocusDirection dir, const Rect* previous, const std::function<bool(FocusTargetNode&)>& onFound);
        bool SearchChildren2D(FocusTargetNode& t, Rect focused, FocusDirection dir, const std::function<bool(FocusTargetNode&)>& onFound);
        bool Forward(FocusTargetNode& t, const std::function<bool(FocusTargetNode&)>& onFound);
        bool Backward(FocusTargetNode& t, const std::function<bool(FocusTargetNode&)>& onFound);
        bool SearchChildren1D(FocusTargetNode& t, FocusTargetNode& focused, FocusDirection dir, const std::function<bool(FocusTargetNode&)>& onFound);
        std::vector<FocusTargetNode*> SortedChildren(const FocusTargetNode& t) const;
        FocusTargetNode* ActiveChild(const FocusTargetNode& t) const;
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
        FocusTargetNode m_RootFocus;
        FocusTargetNode* m_ActiveFocus = nullptr;
        bool m_FocusCaptured = false;
        rt::State<InputMode> m_InputMode{ InputMode::Touch };
        std::function<bool()> m_RequestHostFocus;
        std::vector<int> m_KeysDown;
        TextInputHost m_InputHost;
        TextInputSession* m_Input = nullptr;
        WindowInsetsHolder m_Insets;
        std::uint64_t m_SemanticsVersion = 0;
        std::function<void()> m_OnSemanticsChanged;
        int m_LastSemanticsId = 0;
        std::unordered_map<int, Offset> m_ScrollDeltas;
    };

}
