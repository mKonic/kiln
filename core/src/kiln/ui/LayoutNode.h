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
#include "kiln/ui/Clip.h"
#include "kiln/ui/TextInput.h"
#include "kiln/ui/TextToolbar.h"
#include "kiln/ui/Autofill.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Popup.h"
#include "kiln/ui/Window.h"
#include "kiln/ui/DragAndDrop.h"
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
        int PlaceOrder() const { return m_PlaceOrder; }
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
        // The space the children are placed in -> root, and its bounds in root (bounding box).
        Mat3 InnerToRoot() const;
        Rect InnerRectInRoot() const;
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
        // What `after` (one of this node's draw modifiers) draws as its content, into `scope` in
        // that modifier's space: the rest of the chain and the children (a one-off redraw, such
        // as a drag shadow; reads are not observed). False if `after` is not here.
        bool DrawAfter(const DrawModifier* after, ContentDrawScope& scope);

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
        // Rulers: each coordinator's provided values and who read them.
        Mat3 CoordinatorToRoot(std::size_t index) const;
        void CaptureRulers(std::size_t index);
        float FindRulerValue(std::size_t index, const Ruler& ruler, float defaultValue);
        void InvalidateRulerReaders(LayoutNode& definer, std::size_t index, const RulerData* ruler);
        void CheckLinesReadByParent();
        bool m_Placed = false;
        bool m_PlacedThisPass = false;   // set by the parent's current layout pass
        IntOffset m_Position;
        float m_ZIndex = 0;
        int m_PlaceOrder = 0;
        IntSize m_ReportedSize{ -1, -1 };   // the size last told to NotifyLayoutChange
        int m_NextPlaceOrder = 0;   // for children, reset each layout of this node
        int m_SemanticsId = 0;

        Watcher m_MeasureWatch{ *this, 0 }, m_LayoutWatch{ *this, 1 }, m_DrawWatch{ *this, 2 };
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
        void SetOnContentSizeChanged(std::function<void(IntSize)> fn) { m_OnContentSizeChanged = std::move(fn); }
        // Dialog windows for Dialog composables in this owner's content.
        void SetDialogHost(DialogHost host) { m_DialogHost = std::move(host); }
        const DialogHost& Dialogs() const { return m_DialogHost; }
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
        void SetDensity(Density d) { m_Density = d; RequestFullRemeasure(); }
        Density GetDensity() const { return m_Density; }
        void SetDirection(LayoutDirection d) { m_Direction = d; RequestFullRemeasure(); }
        LayoutDirection Direction() const { return m_Direction; }
        IntSize WindowSize() const { return m_Window; }
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
        // System bar icons (WindowInsetsControllerCompat.isAppearanceLight*Bars): dark icons for
        // light content behind the bars.
        void SetSystemBarsAppearanceHost(std::function<void(bool lightStatusBars, bool lightNavigationBars)> fn) { m_BarsAppearance = std::move(fn); }
        void SetSystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars) {
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
            std::function<int(const MagnifierConfig&)> create;                          // a handle (> 0)
            std::function<void(int, Offset source, Offset magnifierCenter, float zoom)> show;   // NaN centre / zoom: default
            std::function<void(int)> updateContent, dismiss, destroy;
            std::function<IntSize(int)> size;
            bool canUpdateZoom = true;                                                  // API 29+
        };
        void SetMagnifierHost(MagnifierHost host) { m_Magnifier = std::move(host); }
        const MagnifierHost& Magnifier() const { return m_Magnifier; }
        // HapticFeedback (View.performHapticFeedback with HapticFeedbackConstants).
        void SetHapticHost(std::function<void(int)> fn) { m_Haptic = std::move(fn); }
        void PerformHapticFeedback(int constant) { if (m_Haptic) m_Haptic(constant); }
        // Clipboard (AndroidClipboard: ClipboardManager primary clip, plain text).
        struct ClipboardHost {
            std::function<void(const std::string&)> setText;
            std::function<std::optional<std::string>()> getText;
            std::function<bool()> hasText;   // without reading it (no "pasted" toast)
            std::function<std::optional<std::pair<ClipEntry, ClipMetadata>>()> getClip;   // the whole primary clip
            std::function<std::optional<ClipMetadata>()> getMetadata;                      // its description only
            // The first item's text with its span styles (Compose's Annotation encoding).
            std::function<void(const text::AnnotatedString&)> setAnnotatedText;
            std::function<std::optional<text::AnnotatedString>()> getAnnotatedText;
        };
        void SetClipboardHost(ClipboardHost host) { m_Clipboard = std::move(host); }
        void SetClipboardText(const std::string& text);
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
            std::function<std::optional<std::vector<std::uint8_t>>(const std::string& uri)> read;
            std::function<void(int handle)> releasePermission;
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
            std::function<bool(Rect, const std::vector<TextToolbarEntry>&)> show;
            std::function<void(const std::vector<TextToolbarEntry>&)> update;
            std::function<void(Rect)> move;
            std::function<void()> hide;
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
        using OffscreenRenderer = std::function<std::vector<std::uint8_t>(int width, int height, const std::function<void(DrawScope&)>& draw)>;
        void SetOffscreenRenderer(OffscreenRenderer r) { m_Offscreen = std::move(r); }
        std::vector<std::uint8_t> RenderToPixels(int width, int height, const std::function<void(DrawScope&)>& draw) const {
            return m_Offscreen && width > 0 && height > 0 ? m_Offscreen(width, height, draw) : std::vector<std::uint8_t>{};
        }
        // A scope over `target` with this owner's density and direction (for an offscreen draw).
        void DrawWith(const DrawTarget& target, const std::function<void(DrawScope&)>& draw);
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
        void SetViewRectsHost(std::function<void(int kind, const std::vector<IntRect>& rects)> host) { m_ViewRectsHost = std::move(host); }
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
        void SetRecommendedTimeoutHost(std::function<std::int64_t(int originalTimeout, int uiContentFlags)> host) { m_TimeoutHost = std::move(host); }
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
        void SetFlagSecureHost(std::function<void(bool)> host) { m_FlagSecureHost = std::move(host); }
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
        void SetScreenHost(std::function<void(bool)> keepScreenOn, std::function<void(bool)> contentSensitive) {
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
        void SetDeadCharCombiner(std::function<int(int accent, int c)> fn) { m_DeadChar = std::move(fn); }
        int DeadChar(int accent, int c) const { return m_DeadChar ? m_DeadChar(accent, c) : 0; }
        void RequestRectangleOnScreen(Rect r) { if (m_RequestRectangle) m_RequestRectangle(r); }
        // The platform took focus away from (or gave it back to) the host view.
        void OnHostFocusChanged(bool focused);

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
        TextInputSession* ActiveTextInput() const { return m_Input; }
        // SoftwareKeyboardController: show or hide the keyboard without ending the session.
        void ShowSoftKeyboard() { if (m_InputHost.showKeyboard) m_InputHost.showKeyboard(); }
        void HideSoftKeyboard() { if (m_InputHost.hideKeyboard) m_InputHost.hideKeyboard(); }
        // WindowInfo.isWindowFocused (observable).
        bool IsWindowFocused() const { return m_WindowFocused.Get(); }
        void SetWindowFocused(bool focused) { m_WindowFocused.Set(focused); }

        WindowInsetsHolder& Insets() { return m_Insets; }

        // Semantics: rebuilt on demand from the layout tree. Changes bump the version and notify.
        SemanticsNode BuildSemanticsTree();
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
        void SetOnAccessibilityChange(std::function<void(bool layout)> fn) { m_OnAccessibilityChange = std::move(fn); }
        // onLayoutChange: a node was placed somewhere else or at another size.
        void NotifyLayoutChange(LayoutNode& node);
        std::uint64_t SemanticsVersion() const { return m_SemanticsVersion; }
        void SetOnSemanticsChanged(std::function<void()> fn) { m_OnSemanticsChanged = std::move(fn); }
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
        bool m_LaidOut = false;
        std::function<void(int)> m_Haptic;
        std::function<void(bool, bool)> m_BarsAppearance;
        ClipboardHost m_Clipboard;
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
        std::function<void(bool)> m_KeepScreenOnHost, m_ContentSensitiveHost;
        OffscreenRenderer m_Offscreen;
        rt::State<bool> m_ScrollCaptureInProgress{ false };
        std::unique_ptr<ui::ScrollCapture> m_ScrollCapture;
        ActivityResultRegistry m_ActivityResults;
        int m_KeepScreenOnCount = 0, m_SensitiveCount = 0;
        std::function<void(int, const std::vector<IntRect>&)> m_ViewRectsHost;
        std::function<std::int64_t(int, int)> m_TimeoutHost;
        rt::State<AccessibilityServiceState> m_A11yServices{ AccessibilityServiceState{} };
        rt::State<bool> m_FlagSecure{ false };
        std::function<void(bool)> m_FlagSecureHost;
        bool m_AppliedSecure = false;
        std::optional<Constraints> m_ContentConstraints;
        IntSize m_LastContentSize{ -1, -1 };
        std::function<void(IntSize)> m_OnContentSizeChanged;
        DialogHost m_DialogHost;
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
        void HitTest(LayoutNode& n, const LayoutNode::Mat3& parentInner, Offset p, PointerType type, std::vector<HitEntry>& path,
                     std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors);
        // Hit testing with the popups' rules (see Popup.h); `press` lets outside presses dismiss.
        void HitTestWindow(Offset p, PointerType type, bool press, std::vector<HitEntry>& path,
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
        std::unique_ptr<LayoutNode> m_ContentRoot;
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
        rt::State<bool> m_WindowFocused{ true };
        std::function<bool()> m_RequestHostFocus;
        std::function<void(Rect)> m_RequestRectangle;
        std::vector<int> m_KeysDown;
        TextInputHost m_InputHost;
        TextInputSession* m_Input = nullptr;
        WindowInsetsHolder m_Insets;
        std::uint64_t m_SemanticsVersion = 0;
        std::unique_ptr<AutofillManager> m_Autofill;
        std::unique_ptr<DragAndDropManager> m_DragAndDrop;
        std::function<void()> m_OnSemanticsChanged;
        std::unique_ptr<AccessibilityDelegate> m_Accessibility;
        std::function<void(bool)> m_OnAccessibilityChange;
        int m_LastSemanticsId = 0;
    };

}
