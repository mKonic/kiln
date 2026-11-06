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
#include "kiln/ui/IndirectPointer.h"
#include "kiln/ui/TextInput.h"
#include "kiln/ui/Insets.h"
#include "kiln/ui/Semantics.h"

#include <algorithm>
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
        // The space the children are placed in -> root.
        Mat3 InnerToRoot() const;
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
        void CheckLinesReadByParent();
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
        // Fonts arrived (downloadable fonts): text lays out again.
        void OnFontsChanged() { ++m_FontGeneration; RequestFullRemeasure(); }
        int FontGeneration() const { return m_FontGeneration; }
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
        enum class CustomResult : std::uint8_t { None, Cancelled, Redirected, RedirectCancelled };
        CustomResult CustomEnterExit(FocusTargetNode& t, FocusDirection d, bool enter);
        CustomResult CustomClearFocus(FocusTargetNode& t, FocusDirection d);
        CustomResult CustomRequestFocus(FocusTargetNode& t, FocusDirection d);
        // The nearest focus targets below `t` (its focus children).
        std::vector<FocusTargetNode*> FocusChildrenOf(const FocusTargetNode& t) const { return FocusChildren(t); }
        bool ClearFocus(bool force = false);
        // wrapAround: 1D search (Next / Previous) starts over from the other end when it runs out.
        bool MoveFocus(FocusDirection direction, bool wrapAround = true);
        // FocusOwner.focusSearch: offers the target a search from the focused item (or from
        // `focusedRect`, root pixels, when nothing is focused) finds to `onFound`, without
        // moving focus itself. nullopt when a focusProperties destination cancels the search.
        std::optional<bool> FocusSearch(FocusDirection direction, const Rect* focusedRect,
                                        const std::function<bool(FocusTargetNode&)>& onFound);
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
        void SetRequestHostFocus(std::function<bool()> fn) { m_RequestHostFocus = std::move(fn); }
        // The root's bringIntoView: the rect (root pixels, where the scroll containers will have
        // put it) goes to the host view's requestRectangleOnScreen (its scrolling parents).
        void SetRequestRectangleOnScreen(std::function<void(Rect)> fn) { m_RequestRectangle = std::move(fn); }
        // The host's View.playSoundEffect (SoundEffectConstants: 0 click, 1-4 navigation left, up,
        // right, down, 5-8 their repeating variants).
        // Pointer icons (PointerIconService): nodes under a hovering mouse or stylus ask for an
        // icon (android.view.PointerIcon types); the deepest one wins, and the host shows it.
        // Without any, the default (TYPE_DEFAULT 1000).
        void SetPointerIconHost(std::function<void(int)> fn) { m_PointerIconHost = std::move(fn); }
        // UriHandler (AndroidUriHandler): opens a URI with the platform (ACTION_VIEW); false when
        // nothing can open it.
        void SetUriHandler(std::function<bool(const std::string&)> fn) { m_UriHandler = std::move(fn); }
        bool OpenUri(const std::string& uri) { return m_UriHandler && m_UriHandler(uri); }
        void RequestPointerIcon(const void* requester, int depth, int type);
        void ReleasePointerIcon(const void* requester);
        int CurrentPointerIcon() const { return m_PointerIcon; }
        void SetPlaySoundEffect(std::function<void(int)> fn) { m_PlaySound = std::move(fn); }
        // HapticFeedback (View.performHapticFeedback with HapticFeedbackConstants).
        void SetHapticHost(std::function<void(int)> fn) { m_Haptic = std::move(fn); }
        void PerformHapticFeedback(int constant) { if (m_Haptic) m_Haptic(constant); }
        // Clipboard (AndroidClipboard: ClipboardManager primary clip, plain text).
        struct ClipboardHost {
            std::function<void(const std::string&)> setText;
            std::function<std::optional<std::string>()> getText;
            std::function<bool()> hasText;   // without reading it (no "pasted" toast)
        };
        void SetClipboardHost(ClipboardHost host) { m_Clipboard = std::move(host); }
        void SetClipboardText(const std::string& text);
        std::optional<std::string> ClipboardText() const;
        bool ClipboardHasText() const;
        // TextToolbar (AndroidTextToolbar): a floating ActionMode over a rect in root pixels with
        // the actions that are set (copy, paste, cut, select all). The host calls
        // OnTextToolbarItem when one is tapped (it then closes) and OnTextToolbarDestroyed when
        // the action mode ends; the status stays Shown until HideTextToolbar, like Compose.
        struct TextToolbarActions { std::function<void()> copy, paste, cut, selectAll; };
        enum TextToolbarItem : unsigned { kToolbarCopy = 1, kToolbarPaste = 2, kToolbarCut = 4, kToolbarSelectAll = 8 };
        struct TextToolbarHost {
            std::function<void(Rect, unsigned items)> show;   // starts the action mode, or updates it
            std::function<void()> hide;
        };
        void SetTextToolbarHost(TextToolbarHost host) { m_ToolbarHost = std::move(host); }
        void ShowTextToolbar(Rect rect, TextToolbarActions actions);
        void HideTextToolbar();
        bool TextToolbarShown() const { return m_ToolbarShown; }
        Rect TextToolbarRect() const { return m_ToolbarRect; }
        unsigned TextToolbarItems() const;
        void OnTextToolbarItem(unsigned item);
        void OnTextToolbarDestroyed() { m_ToolbarActive = false; }
        void PlaySoundEffect(int constant) { if (m_PlaySound) m_PlaySound(constant); }
        // KeyCharacterMap.getDeadChar: the character a dead key's accent makes with `c`, or 0.
        void SetDeadCharCombiner(std::function<int(int accent, int c)> fn) { m_DeadChar = std::move(fn); }
        int DeadChar(int accent, int c) const { return m_DeadChar ? m_DeadChar(accent, c) : 0; }
        void RequestRectangleOnScreen(Rect r) { if (m_RequestRectangle) m_RequestRectangle(r); }
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
        bool m_LaidOut = false;
        std::function<void(int)> m_Haptic;
        ClipboardHost m_Clipboard;
        std::optional<std::string> m_LocalClip;   // without a host (tests): an in-process clipboard
        TextToolbarHost m_ToolbarHost;
        TextToolbarActions m_ToolbarActions;
        Rect m_ToolbarRect;
        bool m_ToolbarShown = false, m_ToolbarActive = false;
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
        // generateAndSearchChildren: the search, then again over items laid out beyond bounds.
        bool Generate1D(FocusTargetNode& t, FocusTargetNode& focused, FocusDirection dir, const std::function<bool(FocusTargetNode&)>& onFound);
        bool Generate2D(FocusTargetNode& t, Rect focused, FocusDirection dir, const std::function<bool(FocusTargetNode&)>& onFound);
        BeyondBoundsLayout* BeyondBoundsParent(const FocusTargetNode& t) const;
        bool SearchBeyondBounds(FocusTargetNode& t, FocusDirection dir, const std::function<bool()>& search);
        std::vector<FocusTargetNode*> SortedChildren(const FocusTargetNode& t) const;
        FocusTargetNode* ActiveChild(const FocusTargetNode& t) const;
        struct HitEntry { LayoutNode* node; PointerInputModifier* mod; bool inBounds = true; };
        void HitTest(LayoutNode& n, const LayoutNode::Mat3& parentInner, Offset p, bool touch, std::vector<HitEntry>& path,
                     std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors);
        void RequestFullRemeasure();

        std::function<void(int)> m_PlaySound;
        std::function<void(int)> m_PointerIconHost;
        int m_FontGeneration = 0;
        std::function<bool(const std::string&)> m_UriHandler;
        struct IconRequest { const void* requester; int depth; int type; };
        std::vector<IconRequest> m_IconRequests;
        int m_PointerIcon = 1000;
        void UpdatePointerIcon();
        std::function<int(int, int)> m_DeadChar;
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
        std::vector<const LayoutNode*> m_Barriers;
        std::set<LayoutNode*> m_FocusInvalid;
        bool m_FocusTargetLost = false;   // the focused node went away: every state changes
        bool m_FocusCaptured = false;
        rt::State<InputMode> m_InputMode{ InputMode::Touch };
        std::function<bool()> m_RequestHostFocus;
        std::function<void(Rect)> m_RequestRectangle;
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
