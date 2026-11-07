#pragma once

// The accessibility delegate (spec 01 req. 5-10; AndroidComposeViewAccessibilityDelegateCompat):
// the unmerged semantics tree as virtual views, what each one reports (node info), the actions
// services perform on them, and the events that tell services what changed. Host-independent: the
// Android host turns NodeInfo / Event into AccessibilityNodeInfo / AccessibilityEvent and maps
// root coordinates to the screen. Action, event and granularity numbers are Android's.

#include "kiln/ui/Semantics.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kiln::ui {

    class Owner;
    class LayoutNode;
    class ContentCaptureManager;

    namespace a11y {
        constexpr int kHostViewId = -1;          // AccessibilityNodeProvider.HOST_VIEW_ID
        constexpr int kInvalidId = INT_MIN;
        constexpr int kRoleFakeNodeIdOffset = 1'000'000'000;
        constexpr int kContentDescriptionFakeNodeIdOffset = 2'000'000'000;
        constexpr int kParcelSafeTextLength = 100000;
        constexpr int kSliderStepsCount = 20;           // AccessibilitySliderStepsCount
        constexpr std::int64_t kTextTraversedEventTimeoutMillis = 1000;
        constexpr std::int64_t kSendRecurringEventsIntervalMillis = 100;

        // AccessibilityNodeInfo actions and android.R.id action ids.
        enum Action : int {
            Focus = 1, ClearFocus = 2, Click = 16, LongClick = 32, AccessibilityFocus = 64,
            ClearAccessibilityFocus = 128, NextAtMovementGranularity = 256, PreviousAtMovementGranularity = 512,
            ScrollForward = 4096, ScrollBackward = 8192, Copy = 16384, Paste = 32768, Cut = 65536,
            SetSelection = 131072, Expand = 262144, Collapse = 524288, Dismiss = 1048576, SetText = 2097152,
            ShowOnScreen = 16908342, ScrollUp = 16908344, ScrollLeft = 16908345, ScrollDown = 16908346,
            ScrollRight = 16908347, SetProgress = 16908349, PageUp = 16908358, PageDown = 16908359,
            PageLeft = 16908360, PageRight = 16908361, ImeEnter = 16908372,
        };
        // Custom actions take these ids in order (Compose's R.id.accessibility_custom_action_0..31).
        constexpr int kCustomActionIdBase = 0x7f0f0000;
        constexpr int kCustomActionCount = 32;

        enum Granularity : int { Character = 1, Word = 2, Line = 4, Paragraph = 8, Page = 16 };

        enum EventType : int {
            ViewClicked = 1, ViewSelected = 4, ViewFocused = 8, ViewTextChanged = 16, WindowStateChanged = 32,
            ViewHoverEnter = 128, ViewHoverExit = 256, WindowContentChanged = 2048, ViewScrolled = 4096,
            ViewTextSelectionChanged = 8192, ViewAccessibilityFocused = 32768, ViewAccessibilityFocusCleared = 65536,
            ViewTextTraversedAtMovementGranularity = 131072,
        };
        enum ContentChange : int {
            Undefined = 0, Subtree = 1, Text = 2, ContentDescription = 4, PaneTitle = 8, PaneAppeared = 16,
            PaneDisappeared = 32, StateDescription = 64, ContentInvalid = 1024, Error = 2048, Checked = 8192,
        };
        enum LiveRegion : int { LiveRegionNone = 0, LiveRegionPolite = 1, LiveRegionAssertive = 2 };

        // Extra data keys.
        inline constexpr const char* kExtraDataIdKey = "androidx.compose.ui.semantics.id";
        inline constexpr const char* kExtraDataTestTagKey = "androidx.compose.ui.semantics.testTag";
        inline constexpr const char* kExtraDataShapeTypeKey = "androidx.compose.ui.semantics.shapeType";
        inline constexpr const char* kExtraDataShapeRectKey = "androidx.compose.ui.semantics.shapeRect";
        inline constexpr const char* kExtraDataShapeRectCornersKey = "androidx.compose.ui.semantics.shapeCorners";
        inline constexpr const char* kExtraDataShapeRegionKey = "androidx.compose.ui.semantics.shapeRegion";
        inline constexpr const char* kExtraDataTextCharacterLocationKey = "android.view.accessibility.extra.DATA_TEXT_CHARACTER_LOCATION_KEY";
        inline constexpr const char* kExtraDataTraversalBeforeKey = "android.view.accessibility.extra.EXTRA_DATA_TEST_TRAVERSALBEFORE_VAL";
        inline constexpr const char* kExtraDataTraversalAfterKey = "android.view.accessibility.extra.EXTRA_DATA_TEST_TRAVERSALAFTER_VAL";

        // The strings the delegate speaks (Compose ui's R.string), for one locale.
        struct Strings {
            std::string stateOn = "On", stateOff = "Off", indeterminate = "Partially checked";
            std::string selected = "Selected", notSelected = "Not selected";
            std::string templatePercent = "%1$d percent.";
            std::string inProgress = "In progress", stateEmpty = "Empty", tab = "Tab", switchRole = "Switch";
            std::string locale;   // the tag they were read for (formats numbers in its digits)
            // The table generated from Compose's values-* resources: `tag` like "de", "pt-BR".
            static Strings ForLocale(const std::string& tag);
        };
    }

    struct IntRect {
        int left = 0, top = 0, right = 0, bottom = 0;
        bool operator==(const IntRect&) const = default;
        bool Empty() const { return right <= left || bottom <= top; }
    };

    // A virtual view: Compose's unmerged SemanticsNode with the bounds of its uncovered part.
    struct AccessibilityNode {
        int id = 0;                       // the root is kHostViewId
        int parent = a11y::kInvalidId;    // its semantics parent (the root's: none)
        LayoutNode* layout = nullptr;     // a fake node's: its parent's
        bool fake = false;
        SemanticsConfiguration config;    // unmerged
        IntRect adjustedBounds;           // the uncovered part, in root pixels
        Rect boundsInRoot;                // SemanticsNode.boundsInRoot (clipped)
        Rect unclippedBoundsInRoot;
        Rect touchBoundsInRoot;
        bool isInMergingHiddenSubtree = false;
        bool hidden = false;              // isHidden
        bool important = false;           // isImportantForAccessibility
        bool hasMergingAncestor = false;
        bool rtl = false;
        std::vector<int> replacedChildren;   // unmerged children and fake nodes, in order
        std::vector<int> children;           // unmerged children (no fakes), for traversal order
        float zIndex = 0;
        int placeOrder = 0;
    };

    struct AccessibilityNodeInfo {
        struct ActionEntry { int id = 0; std::optional<std::string> label; };
        int id = 0;
        int parent = a11y::kHostViewId;
        IntRect boundsInRoot;
        std::string className = "android.view.View";
        std::optional<std::string> roleDescription, text, contentDescription, stateDescription, hintText, error, paneTitle,
            viewIdResourceName;
        std::vector<TextSpanSemantics> textSpans;   // on `text`; textIndex 0
        std::vector<int> children;
        std::vector<ActionEntry> actions;
        bool importantForAccessibility = false, accessibilityFocused = false, checkable = false, selected = false,
             heading = false, textEntryKey = false, password = false, editable = false, enabled = true, focusable = false,
             focused = false, visibleToUser = true, clickable = false, longClickable = false, scrollable = false,
             screenReaderFocusable = false, contentInvalid = false, dataSensitive = false;
        std::optional<bool> checked;
        int drawingOrder = -1, maxTextLength = -1, liveRegion = a11y::LiveRegionNone, movementGranularities = 0;
        std::optional<std::pair<int, int>> textSelection;
        struct Range { float min, max, current; };
        std::optional<Range> rangeInfo;
        std::optional<std::pair<int, int>> collectionInfo;   // rows, columns
        struct Item { int rowIndex, rowSpan, columnIndex, columnSpan; bool selected; };
        std::optional<Item> collectionItemInfo;
        int traversalBefore = a11y::kInvalidId, traversalAfter = a11y::kInvalidId;
        std::vector<std::string> extraDataKeys;
    };

    // What a service asked of addExtraDataToAccessibilityNodeInfo.
    struct AccessibilityExtraData {
        std::optional<int> intValue;
        std::optional<std::string> stringValue;
        std::vector<std::optional<Rect>> rects;     // character locations, in root pixels
        // Shape: 0 rectangle, 1 rounded (rect + 8 corner radii), 2 generic (region rects).
        std::optional<int> shapeType;
        std::optional<Rect> shapeRect;
        std::vector<float> shapeCorners;
        std::vector<Rect> shapeRegion;
    };

    struct AccessibilityEvent {
        int type = 0;
        int id = a11y::kHostViewId;
        std::optional<int> contentChangeTypes;
        std::string className = "android.view.View";
        bool password = false, dataSensitive = false;
        std::optional<std::string> contentDescription, beforeText;
        std::vector<std::string> text;
        std::optional<int> fromIndex, toIndex, itemCount, addedCount, removedCount;
        std::optional<int> scrollX, maxScrollX, scrollY, maxScrollY;
        int scrollDeltaX = 0, scrollDeltaY = 0;
        std::optional<int> action, movementGranularity;
    };

    struct AccessibilityActionArgs {
        std::optional<int> granularity, selectionStart, selectionEnd;
        bool extendSelection = false;
        std::optional<std::string> text;
        std::optional<float> progress;
        std::optional<int> characterStart, characterLength;   // extra data arguments
    };

    // The host side the delegate needs (all optional).
    struct AccessibilityHost {
        std::function<bool()> touchExplorationEnabled;
        std::function<bool()> requestFromAccessibilityTool;   // isRequestFromAccessibilityTool
        std::function<bool(const IntRect&)> requestRectangleOnScreen;
        std::function<void()> invalidate;                     // view.invalidate (focus highlight)
        std::function<void()> requestFocusFromTouch;          // ACTION_FOCUS in touch mode
        std::function<std::int64_t()> uptimeMillis;
    };

    class AccessibilityDelegate {
    public:
        explicit AccessibilityDelegate(Owner& owner);
        ~AccessibilityDelegate();

        void SetHost(AccessibilityHost host) { m_Host = std::move(host); }
        void SetStrings(a11y::Strings s) { m_Strings = std::move(s); }
        // Whether a service listens (isEnabled): no events are made otherwise.
        void SetEnabled(bool enabled);
        bool Enabled() const { return m_Enabled; }
        // A service or a content capture session listens: the host is told of changes.
        bool Listening() const;

        // currentSemanticsNodes: rebuilt after a semantics or layout change.
        const std::unordered_map<int, AccessibilityNode>& Nodes();
        const AccessibilityNode* Find(int id) { const auto& n = Nodes(); auto it = n.find(id); return it == n.end() ? nullptr : &it->second; }

        // createAccessibilityNodeInfo (null for an unknown or hidden-sensitive node).
        std::optional<AccessibilityNodeInfo> CreateNodeInfo(int id);
        bool PerformAction(int id, int action, const AccessibilityActionArgs& args = {});
        AccessibilityExtraData ExtraData(int id, const std::string& key, const AccessibilityActionArgs& args);
        // findFocus: the accessibility-focused (true) or input-focused node, kInvalidId for none.
        int FindFocus(bool accessibility) const { return accessibility ? m_AccessibilityFocused : m_Focused; }
        int AccessibilityFocused() const { return m_AccessibilityFocused; }
        // dispatchHoverEvent: true when it handled the event (a node is under the pointer).
        bool DispatchHover(int action /* MotionEvent ACTION_HOVER_* */, float x, float y);
        int HitTest(float x, float y);
        // canScroll (for the host's View.canScrollHorizontally / Vertically at a position).
        bool CanScroll(bool vertical, int direction, Offset position);

        // Change tracking. OnSemanticsChange / OnLayoutChange come from the Owner; the host then
        // calls CheckForSemanticsChanges soon (its semanticsChangeChecker), and FlushBoundsUpdates
        // at most every kSendRecurringEventsIntervalMillis while HasPendingBoundsUpdates.
        void OnSemanticsChange();
        // True when this made the first pending bounds update (the host then schedules a flush).
        bool OnLayoutChange(LayoutNode& node);
        void OnScrollChanged(LayoutNode& node, Offset delta);
        void CheckForSemanticsChanges();
        bool HasPendingBoundsUpdates() const { return !m_SubtreeChangedLayoutNodes.empty(); }
        void FlushBoundsUpdates();
        // Events made since the last call.
        std::vector<AccessibilityEvent> TakeEvents() { return std::exchange(m_Events, {}); }

        // A node's configuration with its merged descendants' (mergedConfig).
        SemanticsConfiguration MergedConfig(const AccessibilityNode& node) const;
        // Any semantics node of the last rebuild (shown or not).
        const AccessibilityNode* FindAnyNode(int id) { Nodes(); return FindAny(id); }
        ContentCaptureManager& ContentCapture() { return *m_ContentCapture; }

        // The traversal order (setTraversalValues): before / after per node.
        const std::unordered_map<int, int>& TraversalBefore() { Nodes(); return m_IdToBefore; }
        const std::unordered_map<int, int>& TraversalAfter() { Nodes(); return m_IdToAfter; }

    private:
        struct NodeCopy { SemanticsConfiguration config; std::unordered_set<int> children; };
        struct ScrollScope {
            int id = 0;
            std::optional<float> oldX, oldY;
            std::optional<ScrollAxisRange> x, y;
            Offset pixelDelta;
        };
        struct PendingTraversal { int id, action, granularity, from, to; std::int64_t time; };

        void Rebuild();
        void SetTraversalValues();
        const AccessibilityNode* FindAny(int id) const { auto it = m_All.find(id); return it == m_All.end() ? nullptr : &it->second; }
        bool IsRequestFromAccessibilityTool() const;
        bool TouchExplorationEnabled() const;
        std::int64_t Now() const;
        bool RequestAccessibilityFocus(int id);
        bool ClearA11yFocus(int id);
        AccessibilityEvent CreateEvent(int id, int type) const;
        void Send(AccessibilityEvent e);
        void SendEventForVirtualView(int id, int type, std::optional<int> contentChange = std::nullopt,
                                     const std::vector<std::string>* contentDescription = nullptr);
        void SendPaneChangeEvents(int id, int contentChangeType, const std::optional<std::string>& title);
        void SendSemanticsPropertyChangeEvents();
        void SendStructureChangeEvents(int id);
        void UpdateSemanticsNodesCopyAndPanes();
        void ScheduleScrollEventIfNeeded(ScrollScope& scope);
        void NotifySubtreeChanged(LayoutNode* layout);
        void SendSubtreeChangeEvents(LayoutNode& layout, std::unordered_set<int>& sent);
        void SendScrolledEvent(LayoutNode& layout);
        bool TraverseAtGranularity(const AccessibilityNode& node, int granularity, bool forward, bool extend);
        bool SetAccessibilitySelection(const AccessibilityNode& node, int start, int end, bool traversalMode);
        void SendPendingTextTraversedEvent(int id);
        int SelectionStart(const AccessibilityNode& node) const;
        int SelectionEnd(const AccessibilityNode& node) const;
        bool ScrollOntoScreen(const AccessibilityNode& node);
        std::optional<std::string> InfoStateDescription(const AccessibilityNode& node);
        bool ScreenReaderFocusable(const AccessibilityNode& node);
        bool UnmergedLeaf(const AccessibilityNode& node) const;
        bool Enabled(const AccessibilityNode& node) const;   // SemanticsNode.enabled() (merged config)

        Owner& m_Owner;
        AccessibilityHost m_Host;
        a11y::Strings m_Strings;
        bool m_Enabled = false;
        bool m_Invalid = true;
        std::unordered_map<int, AccessibilityNode> m_Nodes;   // currentSemanticsNodes
        std::unordered_map<int, AccessibilityNode> m_All;     // every semantics node
        std::unordered_map<int, int> m_IdToBefore, m_IdToAfter;
        std::unordered_map<int, int> m_DrawingOrder;
        int m_AccessibilityFocused = a11y::kInvalidId;
        int m_Focused = a11y::kInvalidId;
        int m_ReportedFocused = a11y::kInvalidId;
        int m_Hovered = a11y::kInvalidId;
        int m_CursorPosition = -1;   // AccessibilityCursorPositionUndefined
        std::optional<int> m_PreviousTraversedNode;
        std::optional<PendingTraversal> m_PendingTraversal;
        std::unordered_map<int, std::unordered_map<int, std::string>> m_ActionIdToLabel;
        std::unordered_map<int, std::unordered_map<std::string, int>> m_LabelToActionId;
        std::unordered_map<int, NodeCopy> m_Previous;
        NodeCopy m_PreviousRoot;
        std::unordered_set<int> m_PaneDisplayed;
        std::vector<ScrollScope> m_ScrollScopes;
        std::unordered_map<int, ScrollAxisRange> m_PendingHorizontalScroll, m_PendingVerticalScroll;
        std::vector<LayoutNode*> m_SubtreeChangedLayoutNodes;
        std::unordered_set<LayoutNode*> m_SubtreeChangedSet;
        std::vector<std::weak_ptr<const char>> m_SubtreeChangedAlive;
        std::vector<AccessibilityEvent> m_Events;
        std::unique_ptr<ContentCaptureManager> m_ContentCapture;
    };

    // Content capture and translation (AndroidContentCaptureManager): the visible semantics
    // nodes as virtual view structures for the session, appearing and disappearing as the tree
    // changes, text changes, and showing translated text.
    struct ContentCaptureEvent {
        enum class Kind : std::uint8_t { Appear, Disappear, TextChanged, Flush };
        Kind kind = Kind::Appear;
        int id = 0;
        int parent = a11y::kInvalidId;    // Appear: the parent's id (kInvalidId: the view itself)
        int index = -1;                   // Appear: among the parent's visible children
        std::optional<std::string> className, text, contentDescription, testTag;
        std::optional<float> textSizePx;
        int left = 0, top = 0, width = 0, height = 0;   // in the parent
        std::int64_t timestamp = 0;
    };

    class ContentCaptureManager {
    public:
        explicit ContentCaptureManager(AccessibilityDelegate& delegate) : m_Delegate(delegate) {}
        // A session exists (the view's content capture session; false without a service).
        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool Enabled() const { return m_Enabled; }
        void SetClock(std::function<std::int64_t()> currentTimeMillis) { m_Clock = std::move(currentTimeMillis); }
        // Lifecycle onStart / onStop: the whole tree appears / disappears.
        void OnStart();
        void OnStop();
        // contentCaptureChangeChecker: disappeared nodes, appeared ones, text changes.
        void CheckForChanges();
        // notifyContentCaptureChanges: the buffered appear / disappear events, then a flush.
        void FlushBuffered();
        bool HasBuffered() const { return !m_Buffered.empty(); }
        std::vector<ContentCaptureEvent> TakeEvents() { return std::exchange(m_Events, {}); }
        // ViewTranslationCallback and the translation requests / responses (Android 12+).
        void OnShowTranslation();
        void OnHideTranslation();
        void OnClearTranslation();
        std::vector<std::pair<int, std::string>> TranslationRequests(const std::vector<int>& ids);
        void TranslationResponses(const std::vector<std::pair<int, std::string>>& responses);

    private:
        struct Copy { SemanticsConfiguration config; std::unordered_set<int> children; };
        void Appeared(int index, const AccessibilityNode& node);
        void Disappeared(const AccessibilityNode& node);
        void SendAppearEvents(const AccessibilityNode& node, const Copy& old);
        void UpdateCopy();
        void UpdateTranslationOnAppeared(const AccessibilityNode& node);
        std::optional<ContentCaptureEvent> ToViewStructure(const AccessibilityNode& node, int index);
        std::int64_t Now() const { return m_Clock ? m_Clock() : 0; }

        AccessibilityDelegate& m_Delegate;
        std::function<std::int64_t()> m_Clock;
        bool m_Enabled = false;
        bool m_ShowTranslated = false;
        std::unordered_map<int, Copy> m_Previous;
        Copy m_PreviousRoot;
        std::vector<ContentCaptureEvent> m_Buffered, m_Events;
    };

    // Text segment iterators for granularity movement (AccessibilityIterators): [start, end) of
    // the segment after / before `current`, nullopt at the end.
    namespace a11y {
        using Segment = std::optional<std::pair<int, int>>;
        Segment CharacterFollowing(const std::u16string& text, int current, const std::function<int(int, bool)>& brk);
        Segment CharacterPreceding(const std::u16string& text, int current, const std::function<int(int, bool)>& brk);
        Segment WordFollowing(const std::u16string& text, int current, const std::function<int(int, bool)>& brk);
        Segment WordPreceding(const std::u16string& text, int current, const std::function<int(int, bool)>& brk);
        Segment ParagraphFollowing(const std::u16string& text, int current);
        Segment ParagraphPreceding(const std::u16string& text, int current);
        Segment LineFollowing(const std::u16string& text, const text::TextLayoutResult& layout, int current);
        Segment LinePreceding(const std::u16string& text, const text::TextLayoutResult& layout, int current);
        Segment PageFollowing(const std::u16string& text, const text::TextLayoutResult& layout, int pageHeight, int current);
        Segment PagePreceding(const std::u16string& text, const text::TextLayoutResult& layout, int pageHeight, int current);
    }

}
