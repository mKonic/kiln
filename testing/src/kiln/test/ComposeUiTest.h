#pragma once

// Testing an app's content: Compose's ui-test (ComposeUiTest, finders, SemanticsMatcher filters,
// assertions, actions, touch injection and the main test clock) over kiln's semantics tree. A
// test composes content into a window of its own, finds nodes the way accessibility sees them,
// acts on them and checks the result, on the host and on devices alike (link kiln_ui_test).
//
//   kiln::test::ComposeUiTest t;
//   t.SetContent([] { Counter(); });
//   t.OnNodeWithText("Add").PerformClick();
//   t.OnNodeWithText("Pressed 1 time").AssertIsDisplayed();
//
// A failed assertion throws kiln::test::AssertionError, its message saying what was expected
// and what the matching nodes look like (printToString).

#include "kiln/test/MonospaceText.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Semantics.h"
#include <kiln/Function.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::rt { class Composition; }

namespace kiln::test {

    class AssertionError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // A node in a snapshot of the tree, with its ancestors (root first) for the relational
    // matchers.
    struct NodeRef {
        const ui::SemanticsNode* node = nullptr;
        std::vector<const ui::SemanticsNode*> ancestors;
        bool IsRoot() const { return node && node->id == -1; }
        const ui::SemanticsNode* Parent() const { return ancestors.empty() ? nullptr : ancestors.back(); }
    };

    // SemanticsMatcher: a description and a test on a node.
    class SemanticsMatcher {
    public:
        SemanticsMatcher(std::string description, kiln::Function<bool(const NodeRef&)> matches)
            : m_Description(std::move(description)), m_Matches(std::move(matches)) {}
        bool Matches(const NodeRef& node) const { return m_Matches(node); }
        const std::string& Description() const { return m_Description; }
        friend SemanticsMatcher operator&&(const SemanticsMatcher& a, const SemanticsMatcher& b);
        friend SemanticsMatcher operator||(const SemanticsMatcher& a, const SemanticsMatcher& b);
        friend SemanticsMatcher operator!(const SemanticsMatcher& a);

    private:
        std::string m_Description;
        kiln::Function<bool(const NodeRef&)> m_Matches;
    };

    // --- filters (Filters.kt) -------------------------------------------------------------
    SemanticsMatcher IsEnabled();
    SemanticsMatcher IsNotEnabled();
    SemanticsMatcher IsToggleable();
    SemanticsMatcher IsOn();
    SemanticsMatcher IsOff();
    SemanticsMatcher IsSelectable();
    SemanticsMatcher IsSelected();
    SemanticsMatcher IsNotSelected();
    SemanticsMatcher IsFocusable();
    SemanticsMatcher IsNotFocusable();
    SemanticsMatcher IsFocused();
    SemanticsMatcher IsNotFocused();
    SemanticsMatcher HasClickAction();
    SemanticsMatcher HasNoClickAction();
    SemanticsMatcher HasScrollAction();
    SemanticsMatcher HasNoScrollAction();
    SemanticsMatcher HasContentDescription(std::string value, bool substring = false, bool ignoreCase = false);
    SemanticsMatcher HasContentDescriptionExactly(std::vector<std::string> values);
    // Text, input text or editable text equal to (or containing) the value.
    SemanticsMatcher HasText(std::string text, bool substring = false, bool ignoreCase = false);
    // The text items are exactly these (any order), with the editable text when included.
    SemanticsMatcher HasTextExactly(std::vector<std::string> values, bool includeEditableText = true);
    SemanticsMatcher HasStateDescription(std::string value);
    SemanticsMatcher IsHeading();
    SemanticsMatcher HasTestTag(std::string tag);
    SemanticsMatcher HasRole(ui::Role role);
    SemanticsMatcher IsDialog();
    SemanticsMatcher IsPopup();
    SemanticsMatcher IsHiddenFromAccessibility();
    SemanticsMatcher HasImeAction(int imeAction);   // EditorInfo.IME_ACTION_*
    SemanticsMatcher HasSetTextAction();
    SemanticsMatcher HasInsertTextAtCursorAction();
    SemanticsMatcher HasPerformImeAction();
    SemanticsMatcher HasRequestFocusAction();
    SemanticsMatcher HasScrollToIndexAction();
    SemanticsMatcher IsEditable();
    SemanticsMatcher IsRoot();
    SemanticsMatcher HasParent(SemanticsMatcher matcher);
    SemanticsMatcher HasAnyChild(SemanticsMatcher matcher);
    SemanticsMatcher HasAnySibling(SemanticsMatcher matcher);
    SemanticsMatcher HasAnyAncestor(SemanticsMatcher matcher);
    SemanticsMatcher HasAnyDescendant(SemanticsMatcher matcher);

    class ComposeUiTest;
    class SemanticsNodeInteractionCollection;

    // TouchInjectionScope: gestures on a node, in its own pixels (0,0 its top-left). Events are
    // sent as they are made; the clock moves by eventPeriodMillis between them.
    class TouchInjectionScope {
    public:
        static constexpr std::int64_t eventPeriodMillis = 16;
        float Width() const { return m_Bounds.Width(); }
        float Height() const { return m_Bounds.Height(); }
        ui::Offset Center() const { return { Width() / 2, Height() / 2 }; }
        ui::Offset TopLeft() const { return { 0, 0 }; }
        ui::Offset TopCenter() const { return { Width() / 2, 0 }; }
        ui::Offset BottomCenter() const { return { Width() / 2, Height() - 1 }; }
        ui::Offset CenterLeft() const { return { 0, Height() / 2 }; }
        ui::Offset CenterRight() const { return { Width() - 1, Height() / 2 }; }

        void Down(int pointerId, ui::Offset position);
        void Down(ui::Offset position) { Down(0, position); }
        void MoveTo(int pointerId, ui::Offset position);
        void MoveTo(ui::Offset position) { MoveTo(0, position); }
        void MoveBy(ui::Offset delta) { MoveTo(0, m_Pointers.count(0) ? m_Pointers[0] + delta : delta); }
        void Up(int pointerId = 0);
        void Cancel();
        void AdvanceEventTime(std::int64_t millis = eventPeriodMillis);

        void Click(std::optional<ui::Offset> position = std::nullopt);
        // durationMillis 0: the long-press timeout plus 100 ms.
        void LongClick(std::optional<ui::Offset> position = std::nullopt, std::int64_t durationMillis = 0);
        void DoubleClick(std::optional<ui::Offset> position = std::nullopt);
        void Swipe(ui::Offset start, ui::Offset end, std::int64_t durationMillis = 200);
        void SwipeUp();
        void SwipeDown();
        void SwipeLeft();
        void SwipeRight();

    private:
        friend class SemanticsNodeInteraction;
        TouchInjectionScope(ComposeUiTest& test, ui::Rect bounds) : m_Test(test), m_Bounds(bounds) {}
        void Send(int type);
        ComposeUiTest& m_Test;
        ui::Rect m_Bounds;
        std::map<int, ui::Offset> m_Pointers;   // local positions of the pointers down
        std::int64_t m_Time = 0;
    };

    // SemanticsNodeInteraction: the one node a selector finds (asserted when used).
    class SemanticsNodeInteraction {
    public:
        // The node now (its configuration's actions callable); throws when none or several match.
        ui::SemanticsNode FetchSemanticsNode(const std::string& errorMessageOnFail = {}) const;

        SemanticsNodeInteraction& AssertExists(const std::string& errorMessageOnFail = {});
        void AssertDoesNotExist();
        // Placed, with a visible area inside the window.
        SemanticsNodeInteraction& AssertIsDisplayed();
        SemanticsNodeInteraction& AssertIsNotDisplayed();
        bool IsDisplayed() const;
        bool IsNotDisplayed() const { return !IsDisplayed(); }
        SemanticsNodeInteraction& Assert(const SemanticsMatcher& matcher, const std::string& messagePrefix = {});
        SemanticsNodeInteraction& AssertIsEnabled() { return Assert(IsEnabled()); }
        SemanticsNodeInteraction& AssertIsNotEnabled() { return Assert(IsNotEnabled()); }
        SemanticsNodeInteraction& AssertIsOn() { return Assert(IsOn()); }
        SemanticsNodeInteraction& AssertIsOff() { return Assert(IsOff()); }
        SemanticsNodeInteraction& AssertIsSelected() { return Assert(IsSelected()); }
        SemanticsNodeInteraction& AssertIsNotSelected() { return Assert(IsNotSelected()); }
        SemanticsNodeInteraction& AssertIsToggleable() { return Assert(IsToggleable()); }
        SemanticsNodeInteraction& AssertIsSelectable() { return Assert(IsSelectable()); }
        SemanticsNodeInteraction& AssertIsFocused() { return Assert(IsFocused()); }
        SemanticsNodeInteraction& AssertIsNotFocused() { return Assert(IsNotFocused()); }
        SemanticsNodeInteraction& AssertHasClickAction() { return Assert(HasClickAction()); }
        SemanticsNodeInteraction& AssertHasNoClickAction() { return Assert(HasNoClickAction()); }
        SemanticsNodeInteraction& AssertTextEquals(std::vector<std::string> values, bool includeEditableText = true) {
            return Assert(HasTextExactly(std::move(values), includeEditableText));
        }
        SemanticsNodeInteraction& AssertTextContains(std::string value, bool substring = false, bool ignoreCase = false) {
            return Assert(HasText(std::move(value), substring, ignoreCase));
        }
        SemanticsNodeInteraction& AssertContentDescriptionEquals(std::vector<std::string> values) {
            return Assert(HasContentDescriptionExactly(std::move(values)));
        }
        SemanticsNodeInteraction& AssertContentDescriptionContains(std::string value, bool substring = false, bool ignoreCase = false) {
            return Assert(HasContentDescription(std::move(value), substring, ignoreCase));
        }
        // The state description (assertValueEquals).
        SemanticsNodeInteraction& AssertValueEquals(std::string value) { return Assert(HasStateDescription(std::move(value))); }

        // Bounds (BoundsAssertions.kt), in dp, within half a dp.
        ui::Rect GetBoundsInRoot() const;            // dp, clipped
        ui::Rect GetUnclippedBoundsInRoot() const;   // dp
        SemanticsNodeInteraction& AssertWidthIsEqualTo(float dp);
        SemanticsNodeInteraction& AssertHeightIsEqualTo(float dp);
        SemanticsNodeInteraction& AssertWidthIsAtLeast(float dp);
        SemanticsNodeInteraction& AssertHeightIsAtLeast(float dp);
        SemanticsNodeInteraction& AssertPositionInRootIsEqualTo(float leftDp, float topDp);
        SemanticsNodeInteraction& AssertLeftPositionInRootIsEqualTo(float dp);
        SemanticsNodeInteraction& AssertTopPositionInRootIsEqualTo(float dp);

        // Actions (Actions.kt, TextActions.kt); each waits for idle after.
        SemanticsNodeInteraction& PerformClick();
        SemanticsNodeInteraction& PerformTouchInput(const kiln::Function<void(TouchInjectionScope&)>& block);
        SemanticsNodeInteraction& PerformScrollTo();
        SemanticsNodeInteraction& PerformScrollToIndex(int index);
        SemanticsNodeInteraction& PerformTextInput(const std::string& text);
        SemanticsNodeInteraction& PerformTextReplacement(const std::string& text);
        SemanticsNodeInteraction& PerformTextClearance() { return PerformTextReplacement(""); }
        SemanticsNodeInteraction& PerformTextInputSelection(int start, int end);
        SemanticsNodeInteraction& PerformImeAction();
        SemanticsNodeInteraction& RequestFocus();
        SemanticsNodeInteraction& PerformCustomAccessibilityActionWithLabel(const std::string& label);
        // Any action on the node's configuration (performSemanticsAction).
        SemanticsNodeInteraction& PerformSemanticsAction(const std::string& name, const kiln::Function<bool(const ui::SemanticsConfiguration&)>& invoke);

        // Selectors (Selectors.kt).
        SemanticsNodeInteractionCollection OnChildren() const;
        SemanticsNodeInteraction OnChild() const;
        SemanticsNodeInteraction OnChildAt(int index) const;
        SemanticsNodeInteraction OnParent() const;
        SemanticsNodeInteractionCollection OnSiblings() const;
        SemanticsNodeInteraction OnSibling() const;
        SemanticsNodeInteractionCollection OnAncestors() const;

        // printToString: the node and its descendants up to maxDepth.
        std::string PrintToString(int maxDepth = INT32_MAX) const;

        struct Selector {
            std::string description;
            kiln::Function<std::vector<NodeRef>(const ui::SemanticsNode& root)> select;
        };

    private:
        friend class ComposeUiTest;
        friend class SemanticsNodeInteractionCollection;
        SemanticsNodeInteraction(ComposeUiTest& test, Selector selector, bool unmerged)
            : m_Test(&test), m_Selector(std::make_shared<Selector>(std::move(selector))), m_Unmerged(unmerged) {}
        // The matching nodes, in a fresh snapshot kept until the next fetch.
        std::vector<NodeRef> Fetch() const;
        NodeRef FetchOne(const std::string& errorMessageOnFail) const;
        ComposeUiTest* m_Test;
        std::shared_ptr<Selector> m_Selector;
        bool m_Unmerged;
    };

    class SemanticsNodeInteractionCollection {
    public:
        std::vector<ui::SemanticsNode> FetchSemanticsNodes() const;
        SemanticsNodeInteraction operator[](int index) const;
        SemanticsNodeInteractionCollection& AssertCountEquals(int expected);
        SemanticsNodeInteractionCollection& AssertAny(const SemanticsMatcher& matcher);
        SemanticsNodeInteractionCollection& AssertAll(const SemanticsMatcher& matcher);
        SemanticsNodeInteractionCollection Filter(SemanticsMatcher matcher) const;
        SemanticsNodeInteraction FilterToOne(SemanticsMatcher matcher) const;
        SemanticsNodeInteraction OnFirst() const { return (*this)[0]; }
        SemanticsNodeInteraction OnLast() const;

    private:
        friend class ComposeUiTest;
        friend class SemanticsNodeInteraction;
        SemanticsNodeInteractionCollection(ComposeUiTest& test, SemanticsNodeInteraction::Selector selector, bool unmerged)
            : m_Test(&test), m_Selector(std::make_shared<SemanticsNodeInteraction::Selector>(std::move(selector))), m_Unmerged(unmerged) {}
        std::vector<NodeRef> Fetch() const;
        ComposeUiTest* m_Test;
        std::shared_ptr<SemanticsNodeInteraction::Selector> m_Selector;
        bool m_Unmerged;
    };

    // MainTestClock: the clock frames, animations and delays run on. With autoAdvance (the
    // default) waiting for idle advances it a frame at a time while frames are wanted.
    class MainTestClock {
    public:
        static constexpr std::int64_t frameDelayMillis = 16;
        bool autoAdvance = true;
        std::int64_t CurrentTime() const { return m_Millis; }
        void AdvanceTimeByFrame();
        // Whole frames covering the time (exactly the time with ignoreFrameDuration), each run.
        void AdvanceTimeBy(std::int64_t milliseconds, bool ignoreFrameDuration = false);
        // Frames until the condition holds; throws after timeoutMillis.
        void AdvanceTimeUntil(const kiln::Function<bool()>& condition, std::int64_t timeoutMillis = 1000);

    private:
        friend class ComposeUiTest;
        explicit MainTestClock(ComposeUiTest& test) : m_Test(test) {}
        ComposeUiTest& m_Test;
        std::int64_t m_Millis = 0;
    };

    struct ComposeUiTestOptions {
        ui::IntSize windowSize{ 1080, 1920 };   // px
        float density = 1;
        float fontScale = 1;
        // The text layout; null: MonospaceText (the same on every machine).
        TextBackend* text = nullptr;
    };

    // ComposeUiTest (runComposeUiTest / createComposeRule): one window of content.
    class ComposeUiTest {
    public:
        explicit ComposeUiTest(ComposeUiTestOptions options = {});
        ~ComposeUiTest();
        ComposeUiTest(const ComposeUiTest&) = delete;
        ComposeUiTest& operator=(const ComposeUiTest&) = delete;

        void SetContent(kiln::Function<void()> content);
        ui::Owner& Owner() { return m_Owner; }
        float Density() const { return m_Options.density; }
        MainTestClock& MainClock() { return m_Clock; }

        // Recomposes, lays out and runs posted work until nothing is pending (advancing the clock
        // while frames are wanted, when it auto-advances). Throws if that never ends within a
        // minute of the clock (an endless animation).
        void WaitForIdle();
        // Waits for idle until the condition holds; throws after timeoutMillis of the clock.
        void WaitUntil(const kiln::Function<bool()>& condition, std::int64_t timeoutMillis = 1000, const std::string& description = {});
        void RunOnIdle(const kiln::Function<void()>& action) { WaitForIdle(); action(); }

        // Finders (Finders.kt).
        SemanticsNodeInteraction OnNode(SemanticsMatcher matcher, bool useUnmergedTree = false);
        SemanticsNodeInteractionCollection OnAllNodes(SemanticsMatcher matcher, bool useUnmergedTree = false);
        SemanticsNodeInteraction OnNodeWithText(std::string text, bool substring = false, bool ignoreCase = false, bool useUnmergedTree = false);
        SemanticsNodeInteractionCollection OnAllNodesWithText(std::string text, bool substring = false, bool ignoreCase = false, bool useUnmergedTree = false);
        SemanticsNodeInteraction OnNodeWithContentDescription(std::string label, bool substring = false, bool ignoreCase = false, bool useUnmergedTree = false);
        SemanticsNodeInteractionCollection OnAllNodesWithContentDescription(std::string label, bool substring = false, bool ignoreCase = false, bool useUnmergedTree = false);
        SemanticsNodeInteraction OnNodeWithTag(std::string tag, bool useUnmergedTree = false);
        SemanticsNodeInteractionCollection OnAllNodesWithTag(std::string tag, bool useUnmergedTree = false);
        SemanticsNodeInteraction OnRoot(bool useUnmergedTree = false);

        // The whole tree, printed (printToLog's text).
        std::string PrintTree(bool useUnmergedTree = false);

    private:
        friend class SemanticsNodeInteraction;
        friend class SemanticsNodeInteractionCollection;
        friend class TouchInjectionScope;
        friend class MainTestClock;
        // The tree now, kept alive for the NodeRefs taken from it.
        const ui::SemanticsNode& Snapshot(bool unmerged);
        void RunFrame();
        bool Pump();   // one pass; true when it did something

        ComposeUiTestOptions m_Options;
        MonospaceText m_DefaultText;
        ui::Owner m_Owner;
        std::unique_ptr<rt::Composition> m_Composition;
        MainTestClock m_Clock{ *this };
        std::int64_t m_FrameNanos = 0;
        bool m_WasVirtual = false;
        std::vector<std::unique_ptr<ui::SemanticsNode>> m_Snapshots;
    };

    // printToString for one node (and its children up to maxDepth).
    std::string PrintToString(const ui::SemanticsNode& node, int maxDepth = INT32_MAX);

}
