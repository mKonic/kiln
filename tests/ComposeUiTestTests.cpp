// The public UI testing library (kiln_ui_test): finders, actions, asserts, the clock and the
// failure messages, the way an app's tests use it.
#include "Check.h"

#include "kiln/test/ComposeUiTest.h"

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/Checkbox.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/TextField.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::test;
using ui::Modifier;

namespace {
    // The message an assertion throws, or empty when it passes.
    template <typename F>
    std::string Failure(F&& f) {
        try {
            f();
        } catch (const AssertionError& e) {
            return e.what();
        }
        return {};
    }
    bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
}

TEST(UiTestClicksAButtonAndSeesTheText) {
    ComposeUiTest t;
    rt::State<int> count{ 0 };
    t.SetContent([&] {
        MaterialTheme([&] {
            Column({}, [&] {
                Text(count.Get() == 1 ? "Pressed 1 time" : "Pressed " + std::to_string(count.Get()) + " times", TestTag({}, "label"));
                Button([&] { count.Set(count.Get() + 1); }, TestTag({}, "add"), {}, [] { Text("Add"); });
            });
        });
    });
    t.OnNodeWithText("Pressed 0 times").AssertIsDisplayed();
    // The button merges its label: found by its text, it is the clickable node.
    t.OnNodeWithText("Add").AssertHasClickAction().AssertIsEnabled().PerformClick();
    CHECK_EQ(count.Peek(), 1);
    t.OnNodeWithText("Pressed 1 time").AssertExists();
    t.OnNodeWithTag("label").AssertTextEquals({ "Pressed 1 time" });
    t.OnNodeWithTag("add").Assert(HasRole(ui::Role::Button)).AssertHeightIsAtLeast(40);
    // Unmerged, the label is its own node under the button.
    t.OnNodeWithText("Add", false, false, true).OnParent().Assert(HasTestTag("add"));
    t.OnAllNodes(HasClickAction()).AssertCountEquals(1);
    t.OnNode(HasText("pressed", true, true)).AssertExists();
    t.OnNode(HasAnyChild(HasText("Add")), true).Assert(HasTestTag("add"));
    t.OnNodeWithText("Add").PerformClick().PerformClick();
    t.OnNodeWithTag("label").AssertTextContains("3 times", true);
}

TEST(UiTestReportsWhatFailed) {
    ComposeUiTest t;
    t.SetContent([] {
        Column({}, [] {
            Box(TestTag(Clickable(SizeMod({}, 40_dp, 20_dp), [] {}), "one"));
            Box(TestTag(SizeMod({}, 40_dp, 20_dp), "two"));
            Box(TestTag(SizeMod({}, 40_dp, 20_dp), "two"));
        });
    });
    std::string m = Failure([&] { t.OnNodeWithTag("missing").AssertExists(); });
    CHECK(Has(m, "could not find any node") && Has(m, "TestTag = 'missing'"));
    m = Failure([&] { t.OnNodeWithTag("two").AssertExists(); });
    CHECK(Has(m, "found '2' nodes") && Has(m, "Tag: 'two'"));
    m = Failure([&] { t.OnNodeWithTag("one").AssertHasNoClickAction(); });
    CHECK(Has(m, "Failed to assert the following") && Has(m, "Actions = [OnClick"));
    m = Failure([&] { t.OnNodeWithTag("one").AssertDoesNotExist(); });
    CHECK(Has(m, "Did not expect any node"));
    m = Failure([&] { t.OnNodeWithTag("one").AssertWidthIsEqualTo(41); });
    CHECK(Has(m, "Actual width is 40.dp"));
    CHECK(Failure([&] { t.OnNodeWithTag("one").AssertWidthIsEqualTo(40).AssertPositionInRootIsEqualTo(0, 0); }).empty());
    t.OnNodeWithTag("missing").AssertDoesNotExist();
    t.OnAllNodesWithTag("two").AssertCountEquals(2).AssertAll(HasNoClickAction());
    t.OnAllNodesWithTag("two").OnLast().AssertTopPositionInRootIsEqualTo(40);
    m = Failure([&] { t.OnAllNodesWithTag("two").AssertCountEquals(3); });
    CHECK(Has(m, "Expected '3' nodes but found '2'"));
    m = Failure([&] { t.WaitUntil([] { return false; }, 100, "never"); });
    CHECK(Has(m, "(never)"));
    CHECK(Has(t.PrintTree(), "Tag: 'one'"));
}

TEST(UiTestEntersText) {
    ComposeUiTest t;
    TextFieldState name("");
    int submitted = 0;
    t.SetContent([&] {
        MaterialTheme([&] {
            TextFieldOptions o;
            o.keyboardOptions.imeAction = ui::ImeAction::Done;
            o.onKeyboardAction = [&](kiln::Function<void()>) { ++submitted; };
            TextField(name, TestTag({}, "name"), o);
        });
    });
    t.OnNodeWithTag("name").Assert(IsEditable()).PerformTextInput("Ada");
    CHECK_EQ(name.TextUtf8(), std::string("Ada"));
    t.OnNodeWithTag("name").AssertIsFocused().PerformTextInput(" L.");
    CHECK_EQ(name.TextUtf8(), std::string("Ada L."));
    t.OnNodeWithTag("name").PerformTextReplacement("Grace");
    CHECK_EQ(name.TextUtf8(), std::string("Grace"));
    t.OnNodeWithTag("name").PerformTextInputSelection(0, 2).PerformTextInput("T");
    CHECK_EQ(name.TextUtf8(), std::string("Tace"));
    t.OnNodeWithTag("name").PerformImeAction();
    CHECK_EQ(submitted, 1);
    t.OnNodeWithTag("name").PerformTextClearance();
    CHECK(name.TextUtf8().empty());
}

TEST(UiTestTogglesAndScrolls) {
    ComposeUiTest t({ .windowSize = { 400, 300 } });
    rt::State<bool> checked{ false };
    ScrollState scroll;
    t.SetContent([&] {
        MaterialTheme([&] {
            Column(VerticalScroll(FillMax({}, Axis::Both), scroll), [&] {
                Checkbox(checked.Get(), [&](bool v) { checked.Set(v); }, TestTag({}, "check"));
                for (int i = 0; i < 20; ++i) Text("Row " + std::to_string(i), SizeMod({}, 100_dp, 40_dp));
            });
        });
    });
    t.OnNodeWithTag("check").AssertIsToggleable().AssertIsOff().PerformClick().AssertIsOn();
    CHECK(checked.Peek());
    t.OnNodeWithText("Row 15").AssertIsNotDisplayed().PerformScrollTo().AssertIsDisplayed();
    CHECK(scroll.Value() > 0);
    // Whole, at the viewport's bottom edge.
    const ui::Rect b = t.OnNodeWithText("Row 15").GetBoundsInRoot();
    CHECK(std::abs(b.bottom - 300) < 1);
    t.OnNodeWithText("Row 0").PerformScrollTo();
    CHECK_EQ(scroll.Value(), 48);   // the checkbox's 48 dp stay above it
    t.OnRoot().OnChild().PerformTouchInput([](TouchInjectionScope& s) { s.SwipeUp(); });
    CHECK(scroll.Value() > 48);
}

TEST(UiTestScrollsALazyListToAnIndex) {
    ComposeUiTest t({ .windowSize = { 300, 300 } });
    LazyListState state;
    t.SetContent([&] {
        LazyColumn(TestTag(FillMax({}, Axis::Both), "list"), state, [](LazyListScope& s) {
            s.Items(100, [](int i) { Box(TestTag(SizeMod({}, 100_dp, 30_dp), "item" + std::to_string(i))); });
        });
    });
    t.OnNodeWithTag("item50").AssertDoesNotExist();
    t.OnNodeWithTag("list").Assert(HasScrollToIndexAction()).PerformScrollToIndex(50);
    t.OnNodeWithTag("item50").AssertIsDisplayed().AssertTopPositionInRootIsEqualTo(0);
    t.OnNodeWithTag("list").OnChildren().AssertCountEquals(10).OnFirst().Assert(HasTestTag("item50"));
}

TEST(UiTestDrivesTheClock) {
    ComposeUiTest t;
    rt::State<bool> big{ false };
    float seen = 0;
    t.SetContent([&] {
        seen = AnimateFloatAsState(big.Get() ? 100.f : 0.f, anim::Tween(320, 0, anim::Linear()));
    });
    // Auto-advancing: waiting for idle runs the animation to its end.
    big.Set(true);
    t.WaitForIdle();
    CHECK_EQ(seen, 100.f);
    CHECK(t.MainClock().CurrentTime() >= 320);
    // Paused: the test steps it.
    t.MainClock().autoAdvance = false;
    big.Set(false);
    t.WaitForIdle();
    t.MainClock().AdvanceTimeByFrame();
    t.MainClock().AdvanceTimeBy(160);
    CHECK(seen > 30 && seen < 70);
    t.MainClock().AdvanceTimeUntil([&] { return seen == 0; });
    CHECK_EQ(seen, 0.f);
    t.MainClock().autoAdvance = true;
}

TEST(UiTestLongAndDoubleClicks) {
    ComposeUiTest t;
    int clicks = 0, longs = 0, doubles = 0;
    t.SetContent([&] {
        ClickableOptions o;
        o.onLongClick = [&] { ++longs; };
        o.onDoubleClick = [&] { ++doubles; };
        Box(TestTag(Clickable(SizeMod({}, 100_dp, 100_dp), [&] { ++clicks; }, o), "target"));
    });
    t.OnNodeWithTag("target").PerformTouchInput([](TouchInjectionScope& s) { s.LongClick(); });
    CHECK_EQ(longs, 1);
    CHECK_EQ(clicks, 0);
    t.OnNodeWithTag("target").PerformTouchInput([](TouchInjectionScope& s) { s.DoubleClick(); });
    CHECK_EQ(doubles, 1);
    t.OnNodeWithTag("target").PerformClick();
    t.MainClock().AdvanceTimeBy(400);
    CHECK_EQ(clicks, 1);
}
