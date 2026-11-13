#pragma once

// The layout tree (spec 03 req. 1-3). A LayoutNode is a TreeNode the composition builds; it owns
// a modifier node chain and a measure policy. Layout modifiers each get a coordinator wrapping
// the next; the innermost coordinator runs the policy over the children. Draw modifiers draw in
// the coordinator of the next layout modifier after them (or the inner one).

#include "kiln/runtime/Composition.h"
#include <kiln/Function.h>
#include <stdexcept>
#include <typeinfo>
#include "kiln/runtime/State.h"
#include "kiln/ui/Draw.h"
#include "kiln/graphics/shadow/ShadowContext.h"
#include "kiln/ui/Layout.h"
#include "kiln/ui/Pointer.h"
#include "kiln/ui/SystemBars.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/IndirectPointer.h"
#include "kiln/ui/InputDevices.h"
#include "kiln/ui/Clip.h"
#include "kiln/ui/TextInput.h"
#include "kiln/ui/TextToolbar.h"
#include "kiln/ui/Autofill.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Popup.h"
#include "kiln/ui/RectCallbacks.h"
#include "kiln/ui/Window.h"
#include "kiln/ui/DragAndDrop.h"
#include "kiln/ui/ExternalSurface.h"
#include "kiln/ui/InteropView.h"
#include "kiln/ui/SpellCheck.h"
#include "kiln/ui/ScrollCapture.h"
#include "kiln/ui/ActivityResult.h"
#include "kiln/ui/Insets.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/Semantics.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <array>
#include <map>
#include <optional>

#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace kiln { class TextBackend; }

namespace kiln::ui {

    class Owner;
    class AccessibilityDelegate;

    class LayoutNode final : public rt::TreeNode {
    public:
        LayoutNode();
        ~LayoutNode() override;

        void SetMeasurePolicy(std::shared_ptr<MeasurePolicy> policy);
        const std::shared_ptr<MeasurePolicy>& Policy() const { return m_Policy; }
        void SetModifier(const Modifier& modifier);
        void SetChildren(std::vector<rt::TreeNode*> children) override;
        void OnDeactivate() override;   // LayoutNode.onDeactivate: resets its modifier nodes
        void OnReuse() override;        // LayoutNode.onReuse: measures and draws anew
        // Measure-time child changes (subcomposition): attaches without re-dirtying this node,
        // which is being measured right now.
        void SetChildrenDuringMeasure(std::vector<LayoutNode*> children);
        // Children come from measure (SubcomposeLayout), not from the composition: SetChildren
        // from the composition is ignored.
        void SetMeasureManagedChildren(bool v) { m_MeasureManaged = v; }
        // Remeasurement.forceRemeasure: measure and place now with the last constraints.
        void ForceRemeasure();

        // A virtual node (LayoutNode(isVirtual = true)) takes no part in layout: its children are
        // folded into its parent's (they are the parent's children, in its place). Set before
        // the node is attached.
        void SetVirtual(bool v) { m_Virtual = v; }
        bool IsVirtual() const { return m_Virtual; }
        // The virtual node a child was folded from, else its parent.
        LayoutNode* FoldedParent() const { return m_FoldedParent ? m_FoldedParent : m_Parent; }

        // Lookahead (LookaheadScope.kt, LookaheadPassDelegate.kt). Inside a LookaheadScope every
        // node is measured and placed twice: a lookahead pass (MeasureScope::IsLookingAhead)
        // settles where everything ends up, then the main pass, in which approach modifiers
        // move toward that. The scope's virtual node is the lookahead root of its subtree; a node
        // with an approach modifier outside any scope is one of its own.
        void SetVirtualLookaheadRoot(bool v) { m_IsVirtualLookaheadRoot = v; }
        bool IsVirtualLookaheadRoot() const { return m_IsVirtualLookaheadRoot; }
        LayoutNode* LookaheadRoot() const { return m_LookaheadRoot; }
        // The topmost node of a lookahead subtree: its main measure and placement run its
        // lookahead first.
        bool IsOutMostLookaheadRoot() const {
            return m_LookaheadRoot && !m_Virtual && (!m_Parent || !m_Parent->m_LookaheadRoot || m_DetachedFromParentLookahead);
        }
        // markDetachedFromParentLookaheadPass: a node only the main pass measures (composed for
        // an approach alone) runs its own lookahead, until a lookahead pass measures it again.
        void MarkDetachedFromParentLookaheadPass() { m_DetachedFromParentLookahead = true; }
        Measurable& AsLookaheadMeasurable();
        // The lookahead pass's results, once it measured (placed) this node.
        std::optional<IntSize> LookaheadSize() const;
        std::optional<IntOffset> LookaheadPosition() const;
        // Invalidate both passes (what the composition changed: modifiers, policy, children).
        // RequestRemeasure / RequestRelayout do the same for nodes in a lookahead scope; reads
        // in the main pass only invalidate the main pass.
        void RequestLookaheadRemeasure();
        void RequestLookaheadRelayout();
        // This node's coordinates: its outer bounds, the space its children are placed in, or
        // that of the coordinator a modifier sits in (null: the outer bounds).
        LayoutCoordinates OuterCoordinates() const { return { const_cast<LayoutNode*>(this), 0, false }; }
        LayoutCoordinates InnerCoordinates() const { return { const_cast<LayoutNode*>(this), m_Coordinators.size() - 1, false }; }
        LayoutCoordinates CoordinatesOf(const ModifierNode* at) const;

        LayoutNode* Parent() const { return m_Parent; }
        const std::vector<LayoutNode*>& Children() const { return m_Children; }
        Owner* GetOwner() const { return m_Owner; }
        int Depth() const { return m_Depth; }

        // Measured size and position of the node's outer bounds in its parent's content.
        IntSize Size() const;
        IntOffset Position() const { return m_Position; }
        // Draw, hit-test and focus order among siblings. A pass-through node (a subcompose slot's
        // holder) takes its placed children's highest, so slots stack by their content's zIndex
        // as Compose's direct children do.
        float ZIndex() const;
        void SetZIndexFromChildren(bool fromChildren) { m_ZIndexFromChildren = fromChildren; }
        int PlaceOrder() const { return m_PlaceOrder; }
        // zSortedChildren: by zIndex, then placement order (how they draw, last on top).
        std::vector<LayoutNode*> ZSortedChildren() const;
        bool IsPlaced() const { return m_Placed; }
        // A modifier's coordinator may be unplaced even when the outer node is placed (a
        // layout modifier can hide what it wraps). Null checks the innermost content.
        bool IsModifierPlaced(const ModifierNode* at = nullptr) const;
        Offset PositionInRoot() const;
        // Expires when the node is destroyed.
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
        // This node's local space (its outer bounds) -> root, including graphicsLayer transforms
        // (LayoutCoordinates.localToRoot).
        gfx::Matrix LocalToRoot() const { return ContentToRoot(false); }
        // inner: the space its children are placed in (after every modifier).
        gfx::Matrix ContentToRoot(bool inner) const;
        // Hit testing (NodeCoordinator.hitTest): the pointer modifiers and clipping layers in chain
        // order, each with the space and size of the coordinator it sits in. Row-major 3x3
        // matrices (projective through 3D-rotated layers), local -> root.
        using Mat3 = std::array<float, 9>;
        struct HitStep { PointerInputModifier* mod = nullptr; Mat3 toRoot{}; ui::Size size; const DrawModifier* clip = nullptr; };   // mod null: a clip
        // `parentInner`: the parent's children space; `inner` receives this node's.
        void HitSteps(const Mat3& parentInner, std::vector<HitStep>& out, Mat3& inner) const;
        // Coordinator `index`'s space (0: the outer bounds, the last: the children's) -> root.
        Mat3 CoordinatorToRoot(std::size_t index) const;
        // The space and size a pointer modifier of this node receives events in.
        bool PointerSpace(const PointerInputModifier* mod, Mat3& toRoot, ui::Size& size) const;
        // The bounding box of this node's outer bounds in root coordinates, through every layer
        // above it, 3D ones included (LayoutCoordinates.boundsInRoot without clipping).
        Rect BoundsInRoot() const { return RectInRoot(std::nullopt); }
        // A rect of this node's local space (its outer bounds; nullopt: all of them) in root
        // coordinates, through every layer above it (the bounding box).
        // With `at`, the space is that modifier's: its coordinator, through the layers before it.
        Rect RectInRoot(std::optional<Rect> local, const ModifierNode* at = nullptr) const;
        // The space at a modifier of this node (its coordinator, through the layers before it;
        // null: the outer bounds) -> root, projective, with that space's size
        // (LayoutCoordinates.localToRoot for a modifier node's coordinates).
        Mat3 SpaceToRoot(const ModifierNode* at, ui::Size* size = nullptr) const;
        // The space the children are placed in -> root, and its bounds in root (bounding box).
        Mat3 InnerToRoot() const;
        Rect InnerRectInRoot() const;
        // The size of that space (the innermost coordinator's).
        IntSize InnerSize() const;
        // A point through a local -> root matrix, and back (nullopt behind the camera).
        static Offset MapPoint(const Mat3& toRoot, Offset local);
        static std::optional<Offset> UnmapPoint(const Mat3& toRoot, Offset root);
        // The bounds a layout modifier of this node measured, in root coordinates through the
        // layers above it (bounding box).
        Rect LayoutModifierRectInRoot(const LayoutModifier* modifier) const;
        const ParentData& GetParentData() const { return m_ParentData; }
        std::vector<const ModifierNode*> ModifierNodes() const {
            std::vector<const ModifierNode*> out;
            for (auto& n : m_Nodes) out.push_back(n.get());
            return out;
        }

        void RequestRemeasure();
        void RequestRelayout();
        void InvalidateDraw();
        // Collects the parent data from the chain again (invalidateParentData).
        void UpdateParentData();

        // Traversal of TraversableNodes by key (and, given, by the exact node type) for the
        // free functions below.
        static void TraverseAncestorsImpl(const ModifierNode& from, const void* key, const std::type_info* type,
                                          const kiln::Function<bool(ModifierNode&, TraversableNode&)>& block);
        static void TraverseDescendantsImpl(const ModifierNode& from, const void* key, const std::type_info* type, bool childrenOnly,
                                            const kiln::Function<TraverseDescendantsAction(ModifierNode&, TraversableNode&)>& block);

        // Draws this node and its subtree with scope translated to the node's top-left. The
        // node's own drawing is recorded once and replayed until it is invalidated (a draw read
        // changed, InvalidateDraw, its size or modifiers changed); its children are drawn live
        // from the recording, so a child that moves or changes re-records only itself (a
        // RenderNode per node, with children drawn at their current positions).
        void Draw(ContentDrawScope& scope);
        // Whether the next Draw records again (tests).
        bool DrawDirty() const { return m_DrawDirty; }
        // What `after` (one of this node's draw modifiers) draws as its content, into `scope` in
        // that modifier's space: the rest of the chain and the children (a one-off redraw, such
        // as a drag shadow; reads are not observed). False if `after` is not here.
        bool DrawAfter(const DrawModifier* after, ContentDrawScope& scope);

        Measurable& AsMeasurable();
        // Stable id for semantics / accessibility virtual views.
        int SemanticsId() const { return m_SemanticsId; }
        // The direction this node measures and places with: the LocalLayoutDirection it was
        // composed under when one was provided (a subtree can differ from the window), else the
        // window's.
        LayoutDirection GetLayoutDirection() const;
        void SetLayoutDirection(std::optional<LayoutDirection> direction);
        // The density this node measures and draws with: LocalDensity where a composition
        // provides one, else the owner's.
        Density NodeDensity() const;
        void SetLocalDensity(std::optional<Density> density);

    private:
        friend class Owner;
        friend class ModifierNode;
        friend class NestedScrollNode;
        friend struct LayoutCoordinates;
        friend class LookaheadScope;
        struct Coordinator;
        struct LookaheadDelegate;
        struct Watcher final : rt::Subscriber {
            LayoutNode& node; int phase;
            Watcher(LayoutNode& n, int p) : node(n), phase(p) {}
            void OnStateChanged() override;
        };

        void Attach(Owner& owner, int depth);
        void Detach();
        // Sets the children from the composed list, folding virtual nodes in.
        void Refold();
        void Fold(std::vector<rt::TreeNode*>& composed, LayoutNode* from, std::vector<LayoutNode*>& out);
        // Recomputes the lookahead root here and below.
        void UpdateLookaheadRoot();
        bool HasApproachModifier() const;
        // The main pass's own invalidations (reads in main measure / placement).
        void RequestMainRemeasure();
        void RequestMainRelayout();
        Placeable& LookaheadMeasureNode(const Constraints& c);
        void LookaheadLayoutChain();
        void LookaheadLayoutIfNeeded();
        LookaheadDelegate& La(std::size_t index);
        // The node moved (in either pass): placements that read coordinates below it rerun.
        void OnMoved();
        struct LookaheadOffset { const LayoutNode* root; IntOffset offset; };   // in root's parent's inner space
        LookaheadOffset LookaheadOffsetOf(std::size_t coordinator) const;
        std::vector<PointerInputModifier*> PointerModifiers() const;
        void RebuildChain();
        Placeable& MeasureNode(const Constraints& c);
        void LayoutChain();
        void LayoutIfNeeded() { if (m_LayoutDirty) LayoutChain(); }
        void UpdateZIndex();
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
        bool m_HasLayoutAware = false;
        void DispatchPlaced();
        void DispatchRemeasured();
        void DispatchUnplaced();
        std::vector<std::unique_ptr<Coordinator>> m_Coordinators;   // outer .. inner
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
        ParentData m_ParentData;

        Constraints m_LastConstraints;
        bool m_Measured = false;
        bool m_MeasureManaged = false;
        bool m_MeasureDirty = true;
        bool m_LayoutDirty = true;
        // Alignment-line change tracking (LayoutNodeAlignmentLines): the lines the parent read
        // from this node, their values then, and whether it read any while measuring.
        bool m_InMeasure = false, m_InLayout = false;
        std::vector<std::pair<const AlignmentLine*, int>> m_LinesReadByParent;
        bool m_LinesReadInMeasure = false, m_CheckingLines = false;
        void RecordLineRead(const AlignmentLine& line, int value);
        // Rulers: each coordinator's provided values and who read them.
        void CaptureRulers(std::size_t index);
        float FindRulerValue(std::size_t index, const Ruler& ruler, float defaultValue);
        void InvalidateRulerReaders(LayoutNode& definer, std::size_t index, const RulerData* ruler);
        void CheckLinesReadByParent();
        bool m_Placed = false;
        bool m_PlacedThisPass = false;   // set by the parent's current layout pass
        IntOffset m_Position;
        bool m_MotionFrame = false;   // placed under the parent's motion frame of reference
        int PosX() const;
        int PosY() const;
        static float CoordX(const Coordinator& c);
        static float CoordY(const Coordinator& c);
        float m_ZIndex = 0;
        int m_PlaceOrder = 0;
        IntSize m_ReportedSize{ -1, -1 };   // the size last told to NotifyLayoutChange
        int m_NextPlaceOrder = 0;   // for children, reset each layout of this node
        int m_SemanticsId = 0;
        std::optional<LayoutDirection> m_LocalDirection;
        std::optional<Density> m_LocalDensity;
        bool m_ZIndexFromChildren = false;

        bool m_Virtual = false, m_IsVirtualLookaheadRoot = false;
        LayoutNode* m_FoldedParent = nullptr;
        std::vector<rt::TreeNode*> m_Composed;   // as composed (virtual nodes not folded)
        std::vector<LayoutNode*> m_Virtuals;     // virtual nodes folded into this one
        LayoutNode* m_LookaheadRoot = nullptr;
        bool m_DetachedFromParentLookahead = false;
        // The lookahead pass's state (LookaheadPassDelegate), while in a lookahead scope.
        struct LookaheadState {
            Constraints constraints;
            bool measured = false, measureDirty = true, layoutDirty = true, inMeasure = false;
            bool placed = false, placedThisPass = false, readsCoordinates = false;
            bool motionFrame = false;
            IntOffset position;
            int placeOrder = 0, nextPlaceOrder = 0;
        };
        std::unique_ptr<LookaheadState> m_Lookahead;
        bool m_ReadsCoordinates = false;   // a placement here read PlacementScope::Coordinates

        // The retained recording of this node's drawing (children as one live command).
        DisplayList m_DrawList;
        bool m_DrawDirty = true;
        std::uint64_t m_DrawGeneration = 0;   // the owner's when recorded
        void MarkDrawDirty() { m_DrawDirty = true; }
        void DrawChildren(ContentDrawScope& scope);

        Watcher m_MeasureWatch{ *this, 0 }, m_LayoutWatch{ *this, 1 }, m_DrawWatch{ *this, 2 };
        Watcher m_LookaheadMeasureWatch{ *this, 3 }, m_LookaheadLayoutWatch{ *this, 4 };
    };

    // The root of a window's layout tree (Compose's Owner / MeasureAndLayoutDelegate).
    class Owner {
    public:
        Owner();
        ~Owner();

        // The content root (what the window's composition fills).
        LayoutNode& Root() { return *m_ContentRoot; }
        // The window root above it: the content root, then each shown popup on top.
        LayoutNode& WindowRoot() { return *m_Root; }

        // A dialog window's owner (DialogLayout): the content is measured within these
        // constraints rather than filling the window, and centred in it. The host measures it
        // first (its view's onMeasure) to size the window.
        void SetContentConstraints(std::optional<Constraints> c);
        IntSize MeasureContent(Constraints c);
        // Where the content sits in the window (window pixels).
        IntRect ContentBounds() const;
        // The content's measured size changed (the host re-measures its window).
        void SetOnContentSizeChanged(kiln::Function<void(IntSize)> fn) { m_OnContentSizeChanged = std::move(fn); }
        // Dialog windows for Dialog composables in this owner's content.
        void SetDialogHost(DialogHost host) { m_DialogHost = std::move(host); }
        const DialogHost& Dialogs() const { return m_DialogHost; }
        // Popup windows for Popup composables in this owner's content (unset: the overlay layer).
        void SetPopupHost(PopupHost host) { m_PopupHost = std::move(host); }
        const PopupHost& PopupWindows() const { return m_PopupHost; }
        // The host's window moved on the screen without a layout here (a dialog re-centred, the
        // IME panned the window): the window popups place themselves again.
        void OnWindowMovedOnScreen() { for (auto* f : m_WindowMovedListeners) (*f)(); }
        void AddWindowMovedListener(kiln::Function<void()>* f) { m_WindowMovedListeners.push_back(f); }
        void RemoveWindowMovedListener(kiln::Function<void()>* f) { std::erase(m_WindowMovedListeners, f); }
        const std::optional<Constraints>& ContentConstraintsForRoot() const { return m_ContentConstraints; }

        // Popups (PopupLayout windows) in show order; the last is on top. The layer's node is the
        // caller's and stays theirs; the owner places it and routes input to it.
        void AddPopup(PopupLayer* layer);
        void RemovePopup(PopupLayer* layer);
        // Properties, provider or anchor changed: measured and placed again.
        void UpdatePopup(PopupLayer* layer);
        const std::vector<PopupLayer*>& Popups() const { return m_Popups; }
        // The topmost focusable popup (it has the key focus), if any. Observable.
        PopupLayer* TopFocusablePopup() const;
        // The popup whose node contains `n`, if any.
        PopupLayer* PopupOf(const LayoutNode& n) const;
        // getWindowVisibleDisplayFrame (clipping) or the whole window, window pixels.
        IntRect PopupDisplayBounds(bool clippingEnabled) const;
        // The window's (or a popup's) window focus: the content's window loses it to a focusable
        // popup above it; a popup has it while it is the top focusable one.
        bool IsContentWindowFocused() const { return IsWindowFocused() && !TopFocusablePopup(); }
        // Every node records its drawing again (density, direction, fonts changed).
        void InvalidateAllDraws() { ++m_DrawGeneration; OnDrawRequested(); }
        std::uint64_t DrawGeneration() const { return m_DrawGeneration; }
        // Retained drawing on (the default) or off (every frame re-runs every draw; A/B checks).
        static void SetDrawRetention(bool on);
        static bool DrawRetention();
        void SetDensity(Density d) { m_Density = d; m_ObservedDensity.Set(d); RequestFullRemeasure(); }
        Density GetDensity() const { return m_Density; }
        // The density as observable state: LocalDensity provides it to compositions.
        const Density& ObservedDensity() const { return m_ObservedDensity.Get(); }
        void SetDirection(LayoutDirection d) { m_Direction = d; RequestFullRemeasure(); }
        LayoutDirection Direction() const { return m_Direction; }
        IntSize WindowSize() const { return m_Window; }
        // The window size as observable state (LocalWindowInfo.containerSize): compositions
        // that read it recompose when the window resizes.
        const IntSize& ObservedWindowSize() const { return m_ObservedWindow.Get(); }
        void SetText(TextBackend* text) { m_Text = text; }
        // Fonts arrived (downloadable fonts): text lays out again.
        void OnFontsChanged() { ++m_FontGeneration; RequestFullRemeasure(); }
        int FontGeneration() const { return m_FontGeneration; }
        TextBackend* Text() const { return m_Text; }
        void SetRequestFrame(kiln::Function<void()> fn) { m_RequestFrame = std::move(fn); }

        // Measures dirty nodes (root with the window size) and places what moved.
        void MeasureAndLayout(IntSize window);
        void Draw(const DrawTarget& target);
        // The window's light for elevation shadows (the host sets it from the display and the
        // window's place on it); unset, the framework light over a window at the display's
        // top-left the size of this one.
        void SetShadowLight(std::optional<gfx::ShadowLight> light) { m_ShadowLight = light; }
        gfx::ShadowLight ShadowLight() const;
        std::shared_ptr<gfx::shadow::ShadowContext> Shadows() const { return m_ShadowContext; }

        // Pointer input: hit test on press, dispatch in three passes along the kept path.
        void ProcessPointer(const RawPointerEvent& event);
        ViewConfiguration& Config() { return m_Config; }
        InputDevices& Devices() { return m_InputDevices; }

        // Focus (FocusOwnerImpl). Host hooks: ask the platform to focus the host view.
        FocusTargetNode* ActiveFocus() const { return m_ActiveFocus; }
        bool FocusCaptured() const { return m_FocusCaptured; }
        FocusState StateOf(const FocusTargetNode& t) const;
        bool RequestFocus(FocusTargetNode& t, FocusDirection direction);
        enum class CustomResult : std::uint8_t { None, Cancelled, Redirected, RedirectCancelled };
        CustomResult CustomEnterExit(FocusTargetNode& t, FocusDirection d, bool enter);
        CustomResult CustomClearFocus(FocusTargetNode& t, FocusDirection d);
        CustomResult CustomRequestFocus(FocusTargetNode& t, FocusDirection d);
        // The nearest focus targets below `t` (its focus children).
        std::vector<FocusTargetNode*> FocusChildrenOf(const FocusTargetNode& t) const { return FocusChildren(t); }
        // invalidateFocusProperties: re-read the focus properties of `n`'s targets.
        void InvalidateFocusOf(LayoutNode& n) { ScheduleFocusInvalidation(n); }
        bool ClearFocus(bool force = false);
        // autoClearFocusBehavior (ComposeView's).
        void SetAutoClearFocusBehavior(AutoClearFocusBehavior b) { m_AutoClearFocus = b; }
        // preferredFrameRate: content drawn in a frame votes (a rate in Hz, or a category as a
        // negative value); the host reads the last frame's result after drawing.
        void VoteFrameRate(float rateOrCategory);
        const FrameRateVote& LastFrameRateVote() const { return m_LastFrameRate; }
        AutoClearFocusBehavior GetAutoClearFocusBehavior() const { return m_AutoClearFocus; }
        // wrapAround: 1D search (Next / Previous) starts over from the other end when it runs out.
        bool MoveFocus(FocusDirection direction, bool wrapAround = true);
        // FocusOwner.focusSearch: offers the target a search from the focused item (or from
        // `focusedRect`, root pixels, when nothing is focused) finds to `onFound`, without
        // moving focus itself. nullopt when a focusProperties destination cancels the search.
        std::optional<bool> FocusSearch(FocusDirection direction, const Rect* focusedRect,
                                        const kiln::Function<bool(FocusTargetNode&)>& onFound);
        // resetFocus: clear (unless held), then focus whatever a fresh search finds.
        bool ResetFocus(FocusDirection direction);
        // The focused item's focus rect in root pixels, if anything is focused.
        std::optional<Rect> FocusedRect() const;
        // A target's focus rect (its focusRect, else its bounds) in root pixels.
        Rect FocusRectOf(const FocusTargetNode& t) const { return FocusRect(t); }
        // Whether anything in the tree can take focus.
        bool HasFocusableContent() const;
        bool CaptureFocus();
        bool FreeFocus();
        // Key input: preview tunnel, then bubble; unhandled keys move focus (AndroidComposeView
        // root key handler). Returns whether the event was consumed.
        bool DispatchKeyEvent(const KeyEvent& event);
        // dispatchInterceptedSoftKeyboardEvent / dispatchRotaryEvent: pre-events root to focused,
        // then events focused to root. False when no node took it (or nothing is focused).
        bool DispatchInterceptedSoftKeyboardEvent(const KeyEvent& event);
        bool DispatchRotaryEvent(const RotaryScrollEvent& event);
        // Indirect pointer events go along the focused path in three passes; returns whether any
        // change was consumed. The cancel tells the path the stream ended.
        bool DispatchIndirectPointerEvent(IndirectPointerEvent& event);
        // Interaction barriers (InteractionBarrierNode): a focus target that a barrier drawn
        // above it fully covers cannot take focus, is skipped by search, and loses focus.
        void RegisterInteractionBarrier(const LayoutNode* n) { m_Barriers.push_back(n); }
        void UnregisterInteractionBarrier(const LayoutNode* n) {
            m_Barriers.erase(std::remove(m_Barriers.begin(), m_Barriers.end(), n), m_Barriers.end());
        }
        bool IsOccludedByInteractionBarrier(const FocusTargetNode& t) const;
        void DispatchIndirectPointerCancel();
        // The modifiers of one kind on the focused path, root first (the key dispatch path).
        template <typename T> std::vector<T*> FocusedPath(T* (ModifierNode::*as)());
        InputMode GetInputMode() const { return m_InputMode.Get(); }
        void SetInputMode(InputMode mode) { m_InputMode.Set(mode); }
        void SetRequestHostFocus(kiln::Function<bool()> fn) { m_RequestHostFocus = std::move(fn); }
        // The root's bringIntoView: the rect (root pixels, where the scroll containers will have
        // put it) goes to the host view's requestRectangleOnScreen (its scrolling parents).
        void SetRequestRectangleOnScreen(kiln::Function<void(Rect)> fn) { m_RequestRectangle = std::move(fn); }
        // The host's View.playSoundEffect (SoundEffectConstants: 0 click, 1-4 navigation left, up,
        // right, down, 5-8 their repeating variants).
        // Pointer icons (PointerIconService): nodes under a hovering mouse or stylus ask for an
        // icon (android.view.PointerIcon types); the deepest one wins, and the host shows it.
        // Without any, the default (TYPE_DEFAULT 1000).
        void SetPointerIconHost(kiln::Function<void(int)> fn) { m_PointerIconHost = std::move(fn); }
        // UriHandler (AndroidUriHandler): opens a URI with the platform (ACTION_VIEW); false when
        // nothing can open it.
        void SetUriHandler(kiln::Function<bool(const std::string&)> fn) { m_UriHandler = std::move(fn); }
        bool OpenUri(const std::string& uri) { return m_UriHandler && m_UriHandler(uri); }
        void RequestPointerIcon(const void* requester, int depth, int type);
        void ReleasePointerIcon(const void* requester);
        int CurrentPointerIcon() const { return m_PointerIcon; }
        void SetPlaySoundEffect(kiln::Function<void(int)> fn) { m_PlaySound = std::move(fn); }
        // AudioManager.playSoundEffect(effect, volume) (AudioManager.FX_*: 9 is
        // FX_KEYPRESS_INVALID); volume 0..1, or -1 for the default.
        void SetPlayAudioEffect(kiln::Function<void(int effect, float volume)> fn) { m_PlayAudio = std::move(fn); }
        void PlayAudioEffect(int effect, float volume = -1) { if (m_PlayAudio) m_PlayAudio(effect, volume); }
        // System bar icons (WindowInsetsControllerCompat.isAppearanceLight*Bars): dark icons for
        // light content behind the bars.
        // A request made before the host exists is applied when it is set.
        void SetSystemBarsAppearanceHost(kiln::Function<void(bool lightStatusBars, bool lightNavigationBars)> fn) {
            m_BarsAppearance = std::move(fn);
            if (m_BarsAppearance && m_PendingBars) m_BarsAppearance(m_PendingBars->first, m_PendingBars->second);
        }
        // enableEdgeToEdge(statusBarStyle, navigationBarStyle) (activity EdgeToEdge.kt): bar
        // colours, contrast and icon appearance; the activity starts with the defaults.
        void SetEdgeToEdgeHost(kiln::Function<void(const SystemBarStyle&, const SystemBarStyle&)> fn) { m_EdgeToEdge = std::move(fn); }
        void EnableEdgeToEdge(const SystemBarStyle& statusBarStyle = SystemBarStyle::DefaultStatus(),
                              const SystemBarStyle& navigationBarStyle = SystemBarStyle::DefaultNavigation()) {
            if (m_EdgeToEdge) m_EdgeToEdge(statusBarStyle, navigationBarStyle);
        }
        void SetSystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars) {
            m_PendingBars = std::pair(lightStatusBars, lightNavigationBars);
            if (m_BarsAppearance) m_BarsAppearance(lightStatusBars, lightNavigationBars);
        }
        // Magnifier (android.widget.Magnifier, API 28): created per user, shown over a source
        // point in root pixels (and optionally centred at another), its content refreshed after
        // each frame that changed what it shows.
        struct MagnifierConfig {
            bool useTextDefault = false;   // the platform's text magnifier (Magnifier(view))
            float widthPx = NAN, heightPx = NAN, cornerRadiusPx = NAN, elevationPx = NAN, initialZoom = NAN;
            bool clippingEnabled = true;
            bool operator==(const MagnifierConfig& o) const {
                auto same = [](float a, float b) { return a == b || (a != a && b != b); };
                return useTextDefault == o.useTextDefault && same(widthPx, o.widthPx) && same(heightPx, o.heightPx) &&
                       same(cornerRadiusPx, o.cornerRadiusPx) && same(elevationPx, o.elevationPx) && same(initialZoom, o.initialZoom) &&
                       clippingEnabled == o.clippingEnabled;
            }
        };
        struct MagnifierHost {
            kiln::Function<int(const MagnifierConfig&)> create;                          // a handle (> 0)
            kiln::Function<void(int, Offset source, Offset magnifierCenter, float zoom)> show;   // NaN centre / zoom: default
            kiln::Function<void(int)> updateContent, dismiss, destroy;
            kiln::Function<IntSize(int)> size;
            bool canUpdateZoom = true;                                                  // API 29+
        };
        void SetMagnifierHost(MagnifierHost host) { m_Magnifier = std::move(host); }
        const MagnifierHost& Magnifier() const { return m_Magnifier; }
        // HapticFeedback (View.performHapticFeedback with HapticFeedbackConstants).
        void SetHapticHost(kiln::Function<void(int)> fn) { m_Haptic = std::move(fn); }
        void PerformHapticFeedback(int constant) { if (m_Haptic) m_Haptic(constant); }
        // Clipboard (AndroidClipboard: ClipboardManager primary clip, plain text).
        struct ClipboardHost {
            kiln::Function<void(const std::string&)> setText;
            kiln::Function<std::optional<std::string>()> getText;
            kiln::Function<bool()> hasText;   // without reading it (no "pasted" toast)
            kiln::Function<std::optional<std::pair<ClipEntry, ClipMetadata>>()> getClip;   // the whole primary clip
            kiln::Function<std::optional<ClipMetadata>()> getMetadata;                      // its description only
            // The first item's text with its span styles (Compose's Annotation encoding).
            kiln::Function<void(const text::AnnotatedString&)> setAnnotatedText;
            kiln::Function<std::optional<text::AnnotatedString>()> getAnnotatedText;
            // The whole clip (Clipboard.setClipEntry); null clears the clipboard.
            kiln::Function<void(const std::optional<ClipEntry>&)> setClip;
        };
        void SetClipboardHost(ClipboardHost host) { m_Clipboard = std::move(host); }
        // Layout rect callbacks (onLayoutRectChanged, onVisibilityChanged).
        RectRegistry& Rects() { return m_Rects; }
        // Where the window is on screen (RelativeLayoutBounds.positionInScreen); the host sets it.
        void SetScreenOffset(IntOffset o) { m_Rects.SetScreenOffset(o); }
        // A service object kept for the owner's lifetime under `tag` (the CompositionLocals'
        // owner-backed defaults).
        std::shared_ptr<void>& Service(const std::string& tag) { return m_Services[tag]; }
        void SetClipboardText(const std::string& text);
        // Clipboard.setClipEntry: every item; null clears it. Without a host that takes whole
        // clips, the first item's text.
        void SetClipboardClip(const std::optional<ClipEntry>& entry);
        // ClipboardManager.setText / getText with an AnnotatedString: the span styles travel
        // with the text (other annotations do not).
        void SetClipboardAnnotatedText(const text::AnnotatedString& text);
        std::optional<text::AnnotatedString> ClipboardAnnotatedText() const;
        std::optional<std::string> ClipboardText() const;
        bool ClipboardHasText() const;
        // Clipboard.getClipEntry with its metadata (text items only without a host).
        std::optional<std::pair<ClipEntry, ClipMetadata>> ClipboardClip() const;
        std::optional<ClipMetadata> ClipboardMetadata() const;
        // Content URIs from clips and the keyboard (ContentResolver.openInputStream), and the
        // read permission a keyboard granted with commitContent (InputContentInfo, by handle).
        struct ContentHost {
            kiln::Function<std::optional<std::vector<std::uint8_t>>(const std::string& uri)> read;
            kiln::Function<void(int handle)> releasePermission;
        };
        void SetContentHost(ContentHost host) { m_Content = std::move(host); }
        std::optional<std::vector<std::uint8_t>> ReadContent(const std::string& uri) const { return m_Content.read ? m_Content.read(uri) : std::nullopt; }
        void ReleaseContentPermission(int handle) const { if (m_Content.releasePermission && handle > 0) m_Content.releasePermission(handle); }
        // The text toolbar (AndroidTextContextMenuToolbarProvider): one floating ActionMode for one
        // client at a time; showing another client's ends the previous session (MutatorMutex).
        // The host starts the mode (false: nothing to show), updates its menu, moves it and
        // finishes it; it calls OnTextToolbarItem for a click and OnTextToolbarDestroyed when
        // the mode ends by itself.
        struct TextToolbarHost {
            kiln::Function<bool(Rect, const std::vector<TextToolbarEntry>&)> show;
            kiln::Function<void(const std::vector<TextToolbarEntry>&)> update;
            kiln::Function<void(Rect)> move;
            kiln::Function<void()> hide;
        };
        void SetTextToolbarHost(TextToolbarHost host) { m_ToolbarHost = std::move(host); }
        void ShowTextToolbar(TextToolbarClient* client);
        // Finishes the mode if `client` has it (no OnClosed: the client ended it).
        void HideTextToolbar(TextToolbarClient* client);
        bool TextToolbarShown() const { return m_ToolbarClient != nullptr; }
        TextToolbarClient* CurrentTextToolbarClient() const { return m_ToolbarClient; }
        Rect TextToolbarRect() const { return m_ToolbarRect; }
        const std::vector<TextToolbarEntry>& TextToolbarEntries() const { return m_ToolbarEntries; }
        void OnTextToolbarItem(std::size_t index);
        void OnTextToolbarDestroyed();
        // Draws `draw` into a width x height RGBA image (premultiplied, tightly packed) off screen;
        // blocking, for one-off images such as a drag shadow. Empty without a renderer.
        using OffscreenRenderer = kiln::Function<std::vector<std::uint8_t>(int width, int height, const kiln::Function<void(DrawScope&)>& draw)>;
        void SetOffscreenRenderer(OffscreenRenderer r) { m_Offscreen = std::move(r); }
        std::vector<std::uint8_t> RenderToPixels(int width, int height, const kiln::Function<void(DrawScope&)>& draw) const {
            return m_Offscreen && width > 0 && height > 0 ? m_Offscreen(width, height, draw) : std::vector<std::uint8_t>{};
        }
        // A scope over `target` with this owner's density and direction (for an offscreen draw).
        void DrawWith(const DrawTarget& target, const kiln::Function<void(DrawScope&)>& draw);
        ActivityResultRegistry& ActivityResults() { return m_ActivityResults; }
        ScrollCapture& ScrollCaptureManager() {
            if (!m_ScrollCapture) m_ScrollCapture = std::make_unique<ui::ScrollCapture>(*this);
            return *m_ScrollCapture;
        }
        // A scroll capture (long screenshot) runs: scrollables skip nested scrolling and
        // animations, lazy lists drop sticky headers (LocalScrollCaptureInProgress). Observable.
        bool ScrollCaptureInProgress() const { return m_ScrollCaptureInProgress.Get(); }
        void SetScrollCaptureInProgress(bool v) { if (m_ScrollCaptureInProgress.Peek() != v) m_ScrollCaptureInProgress.Set(v); }
        // RectListNode: rects in root pixels that nodes give the host view (kind 0:
        // systemGestureExclusionRects, 1: preferKeepClearRects). The host gets the whole list.
        void SetViewRectsHost(kiln::Function<void(int kind, const std::vector<IntRect>& rects)> host) { m_ViewRectsHost = std::move(host); }
        void ReplaceViewRect(int kind, const void* key, std::optional<IntRect> rect) {
            auto& list = m_ViewRects[kind];
            if (rect && rect->Empty()) rect.reset();
            const auto same = std::find_if(list.begin(), list.end(), [&](const auto& e) { return e.first == key; });
            if ((same == list.end() && !rect) || (same != list.end() && rect && same->second == *rect)) return;
            std::erase_if(list, [&](const auto& e) { return e.first == key; });
            if (rect && !rect->Empty()) list.emplace_back(key, *rect);
            std::vector<IntRect> rects;
            for (const auto& [k, r] : list) rects.push_back(r);
            if (m_ViewRectsHost) m_ViewRectsHost(kind, rects);
        }
        std::vector<IntRect> ViewRects(int kind) const {
            std::vector<IntRect> out;
            if (auto it = m_ViewRects.find(kind); it != m_ViewRects.end()) for (const auto& [k, r] : it->second) out.push_back(r);
            return out;
        }
        // AccessibilityManager: the recommended timeout (getRecommendedTimeoutMillis with
        // FLAG_CONTENT_ICONS 1 / TEXT 2 / CONTROLS 4) and the services' state (observable).
        void SetRecommendedTimeoutHost(kiln::Function<std::int64_t(int originalTimeout, int uiContentFlags)> host) { m_TimeoutHost = std::move(host); }
        std::int64_t RecommendedTimeoutMillis(int originalTimeout, int flags) const {
            return m_TimeoutHost ? m_TimeoutHost(originalTimeout, flags) : originalTimeout;
        }
        AccessibilityServiceState AccessibilityServices() const { return m_A11yServices.Get(); }
        void SetAccessibilityServices(AccessibilityServiceState s) { m_A11yServices.Set(s); }
        // keepScreenOn / sensitiveContent counts (AndroidComposeView): the host view keeps the
        // screen on while any node asks, and is marked sensitive (API 35) while any node is.
        // The host window's FLAG_SECURE (LocalWindow): observable; the host applies changes.
        bool FlagSecure() const { return m_FlagSecure.Get(); }
        void SetFlagSecure(bool secure) {
            if (m_FlagSecure.Peek() == secure) return;
            m_FlagSecure.Set(secure);
            ApplyFlagSecure();
        }
        void SetFlagSecureHost(kiln::Function<void(bool)> host) { m_FlagSecureHost = std::move(host); }
        void SetContextMenuHost(ContextMenuHost host) { m_ContextMenuHost = std::move(host); }
        const ContextMenuHost& ContextMenu() const { return m_ContextMenuHost; }
        // A popup with SecureFlagPolicy::SecureOn is showing: the shared surface goes secure.
        void ApplyFlagSecure() {
            bool popupSecure = false;
            for (PopupLayer* l : m_Popups) popupSecure = popupSecure || l->properties.securePolicy == SecureFlagPolicy::SecureOn;
            const bool secure = m_FlagSecure.Peek() || popupSecure;
            if (secure != m_AppliedSecure && m_FlagSecureHost) m_FlagSecureHost(secure);
            m_AppliedSecure = secure;
        }
        void SetScreenHost(kiln::Function<void(bool)> keepScreenOn, kiln::Function<void(bool)> contentSensitive) {
            m_KeepScreenOnHost = std::move(keepScreenOn);
            m_ContentSensitiveHost = std::move(contentSensitive);
        }
        void IncrementKeepScreenOnCount() { if (++m_KeepScreenOnCount == 1 && m_KeepScreenOnHost) m_KeepScreenOnHost(true); }
        void DecrementKeepScreenOnCount() { if (--m_KeepScreenOnCount == 0 && m_KeepScreenOnHost) m_KeepScreenOnHost(false); }
        void IncrementSensitiveComponentCount() { if (m_SensitiveCount++ == 0 && m_ContentSensitiveHost) m_ContentSensitiveHost(true); }
        void DecrementSensitiveComponentCount() { if (m_SensitiveCount-- == 1 && m_ContentSensitiveHost) m_ContentSensitiveHost(false); }
        int KeepScreenOnCount() const { return m_KeepScreenOnCount; }
        int SensitiveComponentCount() const { return m_SensitiveCount; }
        // The show-passwords setting (Settings.System.TEXT_SHOW_PASSWORD; ShowSecretsSetting from
        // API 37): bit 1 for touch input, bit 2 for physical keyboards. Observable.
        int PasswordVisibility() const { return m_PasswordVisibility.Get(); }
        void SetPasswordVisibility(int bits) { m_PasswordVisibility.Set(bits); }
        void SetTextClassifierHost(TextClassifierHost host) { m_Classifier = std::move(host); }
        const TextClassifierHost& TextClassifier() const { return m_Classifier; }
        void PlaySoundEffect(int constant) { if (m_PlaySound) m_PlaySound(constant); }
        // KeyCharacterMap.getDeadChar: the character a dead key's accent makes with `c`, or 0.
        void SetDeadCharCombiner(kiln::Function<int(int accent, int c)> fn) { m_DeadChar = std::move(fn); }
        int DeadChar(int accent, int c) const { return m_DeadChar ? m_DeadChar(accent, c) : 0; }
        void RequestRectangleOnScreen(Rect r) { if (m_RequestRectangle) m_RequestRectangle(r); }
        // The platform took focus away from (or gave it back to) the host view.
        void OnHostFocusChanged(bool focused);

        // External surfaces (AndroidExternalSurface): content adds them by client; the host
        // creates them (now, or once it is set) and reports back by id.
        void SetExternalSurfaceHost(ExternalSurfaceHost host) {
            m_SurfaceHost = std::move(host);
            if (m_SurfaceHost.create)
                for (auto& [id, s] : m_ExternalSurfaces) m_SurfaceHost.create(id, s.config);
        }
        int AddExternalSurface(ExternalSurfaceClient* client, const ExternalSurfaceConfig& config) {
            const int id = m_NextExternalSurface++;
            m_ExternalSurfaces[id] = { client, config };
            if (m_SurfaceHost.create) m_SurfaceHost.create(id, config);
            return id;
        }
        void UpdateExternalSurface(int id, const ExternalSurfaceConfig& config) {
            auto it = m_ExternalSurfaces.find(id);
            if (it == m_ExternalSurfaces.end() || it->second.config == config) return;
            it->second.config = config;
            if (m_SurfaceHost.update) m_SurfaceHost.update(id, config);
        }
        void RemoveExternalSurface(int id) {
            if (!m_ExternalSurfaces.erase(id)) return;
            if (m_SurfaceHost.release) m_SurfaceHost.release(id);
        }
        // From the host (a surface it removed after RemoveExternalSurface is no longer known).
        void DispatchExternalSurfaceCreated(int id, PlatformSurface surface, int width, int height) {
            if (auto it = m_ExternalSurfaces.find(id); it != m_ExternalSurfaces.end()) it->second.client->OnSurfaceCreated(surface, width, height);
        }
        void DispatchExternalSurfaceChanged(int id, PlatformSurface surface, int width, int height) {
            if (auto it = m_ExternalSurfaces.find(id); it != m_ExternalSurfaces.end()) it->second.client->OnSurfaceChanged(surface, width, height);
        }
        void DispatchExternalSurfaceFrameAvailable(int id) {
            if (auto it = m_ExternalSurfaces.find(id); it != m_ExternalSurfaces.end()) it->second.client->OnFrameAvailable();
        }
        std::optional<ExternalFrame> ExternalSurfaceFrame(int id) {
            return m_SurfaceHost.frame ? m_SurfaceHost.frame(id) : std::nullopt;
        }
        void DispatchExternalSurfaceDestroyed(int id, PlatformSurface surface) {
            if (auto it = m_ExternalSurfaces.find(id); it != m_ExternalSurfaces.end()) it->second.client->OnSurfaceDestroyed(surface);
        }

        // The platform spell checker (none: fields do not check).
        void SetSpellCheckHost(SpellCheckHost host) { m_SpellCheckHost = std::move(host); }
        const SpellCheckHost& SpellChecker() const { return m_SpellCheckHost; }

        // Native Views (AndroidView): content adds them by client; the host makes and keeps them.
        // Without a host (the desktop dev host) every call does nothing.
        void SetInteropViewHost(InteropViewHost host) { m_InteropHost = std::move(host); }
        const InteropViewHost& InteropViews() const { return m_InteropHost; }
        int AddInteropView(InteropViewClient* client) {
            const int id = m_NextInteropView++;
            m_InteropViews[id] = client;
            return id;
        }
        void RemoveInteropView(int id) {
            if (m_InteropViews.erase(id) && m_InteropHost.release) m_InteropHost.release(id);
        }
        // The View on `node`'s element (0: none), for accessibility.
        int InteropViewOf(const LayoutNode* node) const {
            for (const auto& [id, client] : m_InteropViews)
                if (client->Node() == node) return id;
            return 0;
        }
        // From the host: the client of a View it holds (null once removed).
        InteropViewClient* InteropViewClientOf(int id) const {
            auto it = m_InteropViews.find(id);
            return it == m_InteropViews.end() ? nullptr : it->second;
        }

        // Text input: one active session at a time (TextInputServiceAndroid).
        void SetTextInputHost(TextInputHost host) { m_InputHost = std::move(host); }
        void StartTextInput(TextInputSession& session);
        void StopTextInput(TextInputSession& session);
        // The field's value changed (TextFieldState.NotifyImeListener): restart the IME's
        // connection when asked, else report the new state (and whether the selection moved).
        void NotifyTextInputUpdate(TextInputSession& session, bool restartIme, bool selectionChanged);
        void UpdateCursorAnchorInfo(TextInputSession& session, const CursorAnchorInfo& info);
        void StartStylusHandwriting(TextInputSession& session) {
            if (m_Input == &session && m_InputHost.startStylusHandwriting) m_InputHost.startStylusHandwriting();
        }
        void PrepareStylusHandwritingDelegation() { if (m_InputHost.prepareStylusHandwritingDelegation) m_InputHost.prepareStylusHandwritingDelegation(); }
        void AcceptStylusHandwritingDelegation() { if (m_InputHost.acceptStylusHandwritingDelegation) m_InputHost.acceptStylusHandwritingDelegation(); }
        TextInputSession* ActiveTextInput() const { return m_Input; }
        // SoftwareKeyboardController: show or hide the keyboard without ending the session.
        void ShowSoftKeyboard() { if (m_InputHost.showKeyboard) m_InputHost.showKeyboard(); }
        void HideSoftKeyboard() { if (m_InputHost.hideKeyboard) m_InputHost.hideKeyboard(); }
        // LocalSoftwareKeyboardController's default: Show / HideSoftKeyboard.
        std::shared_ptr<SoftwareKeyboardController> KeyboardController();
        // WindowInfo.isWindowFocused (observable).
        bool IsWindowFocused() const { return m_WindowFocused.Get(); }
        void SetWindowFocused(bool focused) { m_WindowFocused.Set(focused); }

        WindowInsetsHolder& Insets() { return m_Insets; }

        // Semantics: rebuilt on demand from the layout tree. Changes bump the version and notify.
        // uncoveredOnly: the accessibility view (getAllUncoveredSemanticsNodes), each node clipped to
        // what is not covered, covered and scrolled-away ones dropped. False: every placed node
        // (SemanticsOwner.rootSemanticsNode, what Compose's test finders search).
        SemanticsNode BuildSemanticsTree(bool uncoveredOnly = true);
        void InvalidateSemantics();
        // The accessibility delegate (made on first use). The host hears of semantics changes
        // (`layout` false) and layout changes (true) while a service listens.
        AccessibilityDelegate& Accessibility();
        // Drag and drop (AndroidDragAndDropManager); the host's drag listener calls OnDrag.
        DragAndDropManager& DragAndDrop() {
            if (!m_DragAndDrop) m_DragAndDrop = std::make_unique<DragAndDropManager>(*this);
            return *m_DragAndDrop;
        }
        // Autofill (AndroidAutofillManager); none until the host sets one (autofill unsupported).
        void SetAutofillHost(AutofillHost host);
        AutofillManager* Autofill() const { return m_Autofill.get(); }
        // Owner.requestAutofill: shows the autofill UI for the node.
        void RequestAutofill(LayoutNode& node);
        void SetOnAccessibilityChange(kiln::Function<void(bool layout)> fn) { m_OnAccessibilityChange = std::move(fn); }
        // onLayoutChange: a node was placed somewhere else or at another size.
        void NotifyLayoutChange(LayoutNode& node);
        std::uint64_t SemanticsVersion() const { return m_SemanticsVersion; }
        void SetOnSemanticsChanged(kiln::Function<void()> fn) { m_OnSemanticsChanged = std::move(fn); }
        LayoutNode* FindBySemanticsId(int id);
        int NextSemanticsId() { return ++m_LastSemanticsId; }
        // dispatchOnScrollChanged: a scrollable consumed `delta` (pointer coordinates) itself
        // (for TYPE_VIEW_SCROLLED's scroll deltas).
        void DispatchOnScrollChanged(LayoutNode& node, Offset delta);
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
        // FocusInvalidationManager: focus modifiers on `n` were attached, changed or removed.
        // Handled once, after the composition applied (at the next MeasureAndLayout).
        void ScheduleFocusInvalidation(LayoutNode& n) { m_FocusInvalid.insert(&n); }
        void InvalidateFocusNodes();
        FocusState EventStateAt(LayoutNode& n, std::size_t index);
        void ForgetPointerModifier(PointerInputModifier* m);
        // OnPositionedDispatcher: nodes with onGloballyPositioned modifiers, told after layout.
        void TrackPositionAware(LayoutNode& n);
        void DispatchGloballyPositioned();
        std::set<LayoutNode*> m_PositionAware;
        bool m_LaidOutThisPass = false;
        bool m_LaidOut = false;
        kiln::Function<void(int)> m_Haptic;
        kiln::Function<void(bool, bool)> m_BarsAppearance;
        std::optional<std::pair<bool, bool>> m_PendingBars;   // the latest request (light status, light navigation)
        kiln::Function<void(const SystemBarStyle&, const SystemBarStyle&)> m_EdgeToEdge;
        ClipboardHost m_Clipboard;
        std::map<std::string, std::shared_ptr<void>> m_Services;
        RectRegistry m_Rects;
        MagnifierHost m_Magnifier;
        ContentHost m_Content;
        std::optional<std::string> m_LocalClip;   // without a host (tests): an in-process clipboard
        std::optional<text::AnnotatedString> m_LocalStyledClip;
        TextToolbarHost m_ToolbarHost;
        TextToolbarClient* m_ToolbarClient = nullptr;
        Rect m_ToolbarRect;
        std::vector<TextToolbarEntry> m_ToolbarEntries;
        // The provider's snapshot observer: entries and bounds, each re-read when what it read
        // changed.
        struct ToolbarObserver final : rt::Subscriber {
            Owner* owner = nullptr;
            bool bounds = false;
            void OnStateChanged() override;
        };
        ToolbarObserver m_ToolbarDataObserver, m_ToolbarBoundsObserver;
        void ReadToolbarEntries();
        void ReadToolbarBounds();
        TextClassifierHost m_Classifier;
        rt::State<int> m_PasswordVisibility{ 3 };
        kiln::Function<void(bool)> m_KeepScreenOnHost, m_ContentSensitiveHost;
        OffscreenRenderer m_Offscreen;
        rt::State<bool> m_ScrollCaptureInProgress{ false };
        std::unique_ptr<ui::ScrollCapture> m_ScrollCapture;
        ActivityResultRegistry m_ActivityResults;
        int m_KeepScreenOnCount = 0, m_SensitiveCount = 0;
        kiln::Function<void(int, const std::vector<IntRect>&)> m_ViewRectsHost;
        kiln::Function<std::int64_t(int, int)> m_TimeoutHost;
        rt::State<AccessibilityServiceState> m_A11yServices{ AccessibilityServiceState{} };
        rt::State<bool> m_FlagSecure{ false };
        kiln::Function<void(bool)> m_FlagSecureHost;
        bool m_AppliedSecure = false;
        std::optional<Constraints> m_ContentConstraints;
        IntSize m_LastContentSize{ -1, -1 };
        kiln::Function<void(IntSize)> m_OnContentSizeChanged;
        DialogHost m_DialogHost;
        PopupHost m_PopupHost;
        std::vector<kiln::Function<void()>*> m_WindowMovedListeners;
        ContextMenuHost m_ContextMenuHost;
        PopupLayer* m_PopupBackTracking = nullptr;
        // The top focusable popup takes the key focus: the focus under it is parked there.
        void SyncPopupFocus();
        std::vector<PopupLayer*> m_Popups;
        rt::State<int> m_PopupsVersion{ 0 };
        void UpdatePopupChildren();
        // Where focus search starts: the top focusable popup, else the content.
        LayoutNode& FocusScopeRoot() const;
        void ForgetFocusTarget(const FocusTargetNode* t);
        void PlacePopups();
        std::map<int, std::vector<std::pair<const void*, IntRect>>> m_ViewRects;
        friend class FocusTargetNode;
        friend class AccessibilityDelegate;
        LayoutNode* NodeOf(const FocusTargetNode& t) const;
        FocusTargetNode* FocusParent(const FocusTargetNode& t) const;
        std::vector<FocusTargetNode*> FocusChildren(const FocusTargetNode& t) const;
        void FirstTargets(LayoutNode& n, std::vector<FocusTargetNode*>& out) const;
        Rect FocusRect(const FocusTargetNode& t) const;
        bool Eligible(const FocusTargetNode& t) const;
        bool CanFocus(const FocusTargetNode& t) const;
        void SetActive(FocusTargetNode* t);
        void DispatchFocusEvents();
        bool FindChildForEnter(FocusTargetNode& t, FocusDirection dir, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        bool Search2D(FocusTargetNode& t, FocusDirection dir, const Rect* previous, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        bool SearchChildren2D(FocusTargetNode& t, Rect focused, FocusDirection dir, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        bool Forward(FocusTargetNode& t, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        bool Backward(FocusTargetNode& t, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        bool SearchChildren1D(FocusTargetNode& t, FocusTargetNode& focused, FocusDirection dir, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        // generateAndSearchChildren: the search, then again over items laid out beyond bounds.
        bool Generate1D(FocusTargetNode& t, FocusTargetNode& focused, FocusDirection dir, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        bool Generate2D(FocusTargetNode& t, Rect focused, FocusDirection dir, const kiln::Function<bool(FocusTargetNode&)>& onFound);
        BeyondBoundsLayout* BeyondBoundsParent(const FocusTargetNode& t) const;
        bool SearchBeyondBounds(FocusTargetNode& t, FocusDirection dir, const kiln::Function<bool()>& search);
        std::vector<FocusTargetNode*> SortedChildren(const FocusTargetNode& t) const;
        FocusTargetNode* ActiveChild(const FocusTargetNode& t) const;
        struct HitEntry { LayoutNode* node; PointerInputModifier* mod; bool inBounds = true; };
        void HitTest(LayoutNode& n, const LayoutNode::Mat3& parentInner, Offset p, PointerType type, std::vector<HitEntry>& path,
                     std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors);
        // Hit testing with the popups' rules (see Popup.h); `press` lets outside presses dismiss.
        void HitTestWindow(Offset p, PointerType type, bool press, std::vector<HitEntry>& path,
                           std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors);
        void RequestFullRemeasure();

        kiln::Function<void(int)> m_PlaySound;

        kiln::Function<void(int, float)> m_PlayAudio;
        kiln::Function<void(int)> m_PointerIconHost;
        int m_FontGeneration = 0;
        kiln::Function<bool(const std::string&)> m_UriHandler;
        struct IconRequest { const void* requester; int depth; int type; };
        std::vector<IconRequest> m_IconRequests;
        int m_PointerIcon = 1000;
        void UpdatePointerIcon();
        kiln::Function<int(int, int)> m_DeadChar;
        std::unique_ptr<LayoutNode> m_Root;
        std::unique_ptr<LayoutNode> m_ContentRoot;
        Density m_Density;
        rt::State<Density> m_ObservedDensity{ Density{} };
        rt::State<IntSize> m_ObservedWindow{ IntSize{} };
        LayoutDirection m_Direction = LayoutDirection::Ltr;
        TextBackend* m_Text = nullptr;
        kiln::Function<void()> m_RequestFrame;
        std::uint64_t m_DrawGeneration = 1;
        std::set<LayoutNode*, ByDepth> m_MeasureDirty, m_LayoutDirty;
        std::set<LayoutNode*> m_CoordinateReaders;   // nodes whose placement read their coordinates
        IntSize m_Window;
        std::optional<gfx::ShadowLight> m_ShadowLight;
        std::shared_ptr<gfx::shadow::ShadowContext> m_ShadowContext = std::make_shared<gfx::shadow::ShadowContext>();
        ViewConfiguration m_Config;
        InputDevices m_InputDevices;
        AutoClearFocusBehavior m_AutoClearFocus = AutoClearFocusBehavior::Default;
        FrameRateVote m_FrameRate, m_LastFrameRate;
        struct Tracked { std::vector<HitEntry> path; Offset position; bool pressed = false; std::int64_t uptime = 0; PointerType type{}; };
        std::map<std::int64_t, Tracked> m_Pointers;
        FocusTargetNode m_RootFocus;
        FocusTargetNode* m_ActiveFocus = nullptr;
        std::vector<const LayoutNode*> m_Barriers;
        std::set<LayoutNode*> m_FocusInvalid;
        bool m_FocusTargetLost = false;   // the focused node went away: every state changes
        bool m_FocusCaptured = false;
        rt::State<InputMode> m_InputMode{ InputMode::Touch };
        rt::State<bool> m_WindowFocused{ true };
        kiln::Function<bool()> m_RequestHostFocus;
        kiln::Function<void(Rect)> m_RequestRectangle;
        std::vector<int> m_KeysDown;
        TextInputHost m_InputHost;
        struct ExternalSurfaceEntry {
            ExternalSurfaceClient* client = nullptr;
            ExternalSurfaceConfig config;
        };
        ExternalSurfaceHost m_SurfaceHost;
        InteropViewHost m_InteropHost;
        SpellCheckHost m_SpellCheckHost;
        std::map<int, InteropViewClient*> m_InteropViews;
        int m_NextInteropView = 1;
        std::map<int, ExternalSurfaceEntry> m_ExternalSurfaces;
        int m_NextExternalSurface = 1;
        std::shared_ptr<SoftwareKeyboardController> m_KeyboardController;
        TextInputSession* m_Input = nullptr;
        WindowInsetsHolder m_Insets;
        std::uint64_t m_SemanticsVersion = 0;
        std::unique_ptr<AutofillManager> m_Autofill;
        std::unique_ptr<DragAndDropManager> m_DragAndDrop;
        kiln::Function<void()> m_OnSemanticsChanged;
        std::unique_ptr<AccessibilityDelegate> m_Accessibility;
        kiln::Function<void(bool)> m_OnAccessibilityChange;
        int m_LastSemanticsId = 0;
    };


    // --- DelegatableNode helpers ------------------------------------------------------------

    // currentValueOf (CompositionLocalConsumerModifierNode): a CompositionLocal's value where the
    // node's layout was composed (its default outside a composition). Reads are observed.
    template<typename T>
    const T& CurrentValueOf(const ModifierNode& node, const rt::Local<T>& local) {
        LayoutNode* n = node.Owner();
        rt::Scope* scope = n ? n->ContentScope() : nullptr;
        return scope ? rt::Composition::ReadFrom(scope, local) : local.Default();
    }
    // requireLayoutCoordinates / requireDensity / requireLayoutDirection.
    inline LayoutCoordinates RequireLayoutCoordinates(const ModifierNode& node) {
        if (!node.Owner()) throw std::logic_error("requireLayoutCoordinates: the node is not attached");
        return node.Owner()->CoordinatesOf(&node);
    }
    inline Density RequireDensity(const ModifierNode& node) { return node.Owner() ? node.Owner()->NodeDensity() : Density{}; }
    inline LayoutDirection RequireLayoutDirection(const ModifierNode& node) { return node.Owner() ? node.Owner()->GetLayoutDirection() : LayoutDirection::Ltr; }
    // invalidateSubtree / invalidateDrawForSubtree / invalidateMeasurementForSubtree: the node's
    // layout and everything below it.
    void InvalidateSubtree(const ModifierNode& node);
    // remeasureSync: measures and places the node's layout now, with its last constraints.
    inline void RemeasureSync(const ModifierNode& node) { if (node.Owner()) node.Owner()->ForceRemeasure(); }
    // invalidateRulers: the node's rulers are worked out again; layouts that read them place
    // again. (kiln works rulers out when it places the node, so the node's placement re-runs.)
    inline void InvalidateRulers(ModifierNode& node) { if (node.IsAttached()) node.InvalidatePlacement(); }
    void InvalidateDrawForSubtree(const ModifierNode& node);
    void InvalidateMeasurementForSubtree(const ModifierNode& node);

    // TraversableNode traversal. The typed forms match nodes of the same type and key as `self`;
    // the keyed forms match any traversable node with `key`. Ancestors: the nodes before in the
    // chain, then up the tree (nearest first); the block returns false to stop. Children: the
    // nearest matches below (not past one). Descendants: all matches below, depth first, as the
    // block's action says.
    inline void TraverseAncestors(const ModifierNode& from, const void* key, const kiln::Function<bool(TraversableNode&)>& block) {
        LayoutNode::TraverseAncestorsImpl(from, key, nullptr, [&](ModifierNode&, TraversableNode& t) { return block(t); });
    }
    template<typename T>
    void TraverseAncestors(const T& self, const kiln::Function<bool(T&)>& block) {
        LayoutNode::TraverseAncestorsImpl(self, self.TraverseKey(), &typeid(self), [&](ModifierNode& m, TraversableNode&) { return block(static_cast<T&>(m)); });
    }
    template<typename T>
    T* FindNearestAncestor(const T& self) {
        T* found = nullptr;
        TraverseAncestors<T>(self, [&](T& t) { found = &t; return false; });
        return found;
    }
    inline TraversableNode* FindNearestAncestor(const ModifierNode& from, const void* key) {
        TraversableNode* found = nullptr;
        TraverseAncestors(from, key, [&](TraversableNode& t) { found = &t; return false; });
        return found;
    }
    inline void TraverseChildren(const ModifierNode& from, const void* key, const kiln::Function<bool(TraversableNode&)>& block) {
        LayoutNode::TraverseDescendantsImpl(from, key, nullptr, true, [&](ModifierNode&, TraversableNode& t) {
            return block(t) ? TraverseDescendantsAction::ContinueTraversal : TraverseDescendantsAction::CancelTraversal;
        });
    }
    template<typename T>
    void TraverseChildren(const T& self, const kiln::Function<bool(T&)>& block) {
        LayoutNode::TraverseDescendantsImpl(self, self.TraverseKey(), &typeid(self), true, [&](ModifierNode& m, TraversableNode&) {
            return block(static_cast<T&>(m)) ? TraverseDescendantsAction::ContinueTraversal : TraverseDescendantsAction::CancelTraversal;
        });
    }
    inline void TraverseDescendants(const ModifierNode& from, const void* key, const kiln::Function<TraverseDescendantsAction(TraversableNode&)>& block) {
        LayoutNode::TraverseDescendantsImpl(from, key, nullptr, false, [&](ModifierNode&, TraversableNode& t) { return block(t); });
    }
    template<typename T>
    void TraverseDescendants(const T& self, const kiln::Function<TraverseDescendantsAction(T&)>& block) {
        LayoutNode::TraverseDescendantsImpl(self, self.TraverseKey(), &typeid(self), false, [&](ModifierNode& m, TraversableNode&) { return block(static_cast<T&>(m)); });
    }

}
