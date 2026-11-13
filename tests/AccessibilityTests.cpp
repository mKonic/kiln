#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/ui/Accessibility.h"

#include <algorithm>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        rt::Composition composition{ owner.Root(), d };
        AccessibilityDelegate& a11y = owner.Accessibility();
        Harness() {
            owner.SetDensity({ 1, 1 });
            owner.Config().touchSlop = 8;
            a11y.SetEnabled(true);
        }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { composition.Recompose(); owner.MeasureAndLayout({ 300, 300 }); }
        std::int64_t frameNs = 0;
        void Frames(int n) { for (int i = 0; i < n; ++i) { frameNs += 16'000'000; d.RunFrame(frameNs); Pump(); } }
        void Touch(PointerEventType type, float x, float y, bool down, std::int64_t t) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.pointers.push_back({ 1, { x, y }, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            d.RunPending();
            Pump();
        }
        // The node whose config has this text first.
        const AccessibilityNode* ByText(const std::string& t) {
            for (const auto& [id, n] : a11y.Nodes())
                if (!n.config.text.empty() && n.config.text.front() == t) return &n;
            return nullptr;
        }
        const AccessibilityNode* ByDescription(const std::string& t) {
            for (const auto& [id, n] : a11y.Nodes())
                if (!n.config.contentDescription.empty() && n.config.contentDescription.front() == t && !n.fake) return &n;
            return nullptr;
        }
        bool HasAction(const AccessibilityNodeInfo& info, int action) {
            return std::any_of(info.actions.begin(), info.actions.end(), [&](const auto& a) { return a.id == action; });
        }
    };
    Modifier Label(Modifier m, std::string s) {
        return Semantics(std::move(m), [s](SemanticsConfiguration& c) { c.text.push_back(s); });
    }
    bool Has(const std::vector<AccessibilityEvent>& events, int type, std::optional<int> change = std::nullopt, int id = a11y::kInvalidId) {
        return std::any_of(events.begin(), events.end(), [&](const AccessibilityEvent& e) {
            return e.type == type && (!change || e.contentChangeTypes == change) && (id == a11y::kInvalidId || e.id == id);
        });
    }
}

TEST(AccessibilityTreeIsUnmergedWithFakeNodes) {
    Harness h;
    h.Run([&] {
        Box(Semantics(SizeMod({}, Dp(100), Dp(40)), [](SemanticsConfiguration& c) {
            c.contentDescription = { "Send" };
            c.role = Role::Button;
            c.onClick = [] { return true; };
        }, true), Alignment::TopStart(), false, [&] { Box(Label(SizeMod({}, Dp(50), Dp(20)), "label")); });
    });
    const AccessibilityNode* button = h.ByDescription("Send");
    const AccessibilityNode* text = h.ByText("label");
    CHECK(button && text);
    if (!button || !text) return;
    // The text stays its own node under the merging button (Compose exports the unmerged tree).
    CHECK_EQ(text->parent, button->id);
    // Fake nodes: the content description first, the role last.
    CHECK_EQ(int(button->replacedChildren.size()), 3);
    if (button->replacedChildren.size() == 3) {
        CHECK_EQ(button->replacedChildren[0], button->id + a11y::kContentDescriptionFakeNodeIdOffset);
        CHECK_EQ(button->replacedChildren[1], text->id);
        CHECK_EQ(button->replacedChildren[2], button->id + a11y::kRoleFakeNodeIdOffset);
    }
    const auto info = h.a11y.CreateNodeInfo(button->id);
    CHECK(info.has_value());
    if (!info) return;
    CHECK(info->screenReaderFocusable);
    CHECK(info->clickable && h.HasAction(*info, a11y::Click));
    CHECK(!info->contentDescription);              // on the fake child instead
    CHECK_EQ(info->className, std::string("android.view.View"));   // the role is on its fake child
    const auto fake = h.a11y.CreateNodeInfo(button->id + a11y::kRoleFakeNodeIdOffset);
    CHECK(fake && fake->className == "android.widget.Button");
    const auto cd = h.a11y.CreateNodeInfo(button->id + a11y::kContentDescriptionFakeNodeIdOffset);
    CHECK(cd && cd->contentDescription == std::optional<std::string>("Send"));
    const auto t = h.a11y.CreateNodeInfo(text->id);
    CHECK(t && t->className == "android.widget.TextView" && t->text == std::optional<std::string>("label"));
    CHECK(t && !t->screenReaderFocusable);         // inside a merging node
}

TEST(AccessibilityTouchBoundsGrowToTheMinimumTarget) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(Semantics(SizeMod(OffsetMod({}, Dp(100), Dp(100)), Dp(20), Dp(20)), [](SemanticsConfiguration& c) {
                c.contentDescription = { "small" };
                c.onClick = [] { return true; };
            }, true));
        });
    });
    const AccessibilityNode* n = h.ByDescription("small");
    CHECK(n != nullptr);
    if (!n) return;
    CHECK(n->adjustedBounds == (IntRect{ 86, 86, 134, 134 }));   // 20 grown to 48 about its centre
    CHECK(n->boundsInRoot.left == 100 && n->boundsInRoot.right == 120);
}

TEST(AccessibilityDropsCoveredAndHiddenNodes) {
    Harness h;
    h.Run([&] {
        Box(SizeMod({}, Dp(100), Dp(100)), Alignment::TopStart(), false, [&] {
            Box(Label(SizeMod({}, Dp(50), Dp(50)), "under"));
            Box(Semantics(SizeMod({}, Dp(100), Dp(100)), [](SemanticsConfiguration& c) { c.contentDescription = { "cover" }; }));
            Box(Semantics(SizeMod({}, Dp(10), Dp(10)), [](SemanticsConfiguration& c) {
                c.text.push_back("hidden");
                c.hideFromAccessibility = true;
            }));
        });
    });
    CHECK(h.ByText("under") == nullptr);   // fully under the cover
    CHECK(h.ByDescription("cover") != nullptr);
    const AccessibilityNode* hidden = h.ByText("hidden");
    CHECK(hidden != nullptr);
    if (hidden) {
        const auto info = h.a11y.CreateNodeInfo(hidden->id);
        CHECK(info && !info->visibleToUser && !info->screenReaderFocusable && !info->importantForAccessibility);
    }
}

TEST(AccessibilityStateDescriptionsAndRanges) {
    Harness h;
    float progress = 0.25f;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [](SemanticsConfiguration& c) {
                c.text.push_back("wifi");
                c.role = Role::Switch;
                c.toggleableState = 1;
            }));
            Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [&](SemanticsConfiguration& c) {
                c.progressBarRangeInfo = ProgressBarRangeInfo{ progress, 0, 1, 0 };
                c.setProgress = [&](float v) { progress = v; return true; };
            }));
            Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [](SemanticsConfiguration& c) {
                c.progressBarRangeInfo = ProgressBarRangeInfo::Indeterminate();
            }));
        });
    });
    const AccessibilityNode* sw = h.ByText("wifi");
    CHECK(sw != nullptr);
    if (sw) {
        const auto info = h.a11y.CreateNodeInfo(sw->id);
        CHECK(info && info->stateDescription == std::optional<std::string>("On") && info->checkable && info->checked == std::optional(true));
        CHECK(info && info->roleDescription == std::optional<std::string>("Switch"));
    }
    std::optional<AccessibilityNodeInfo> slider, spinner;
    for (const auto& [id, n] : h.a11y.Nodes()) {
        if (!n.config.progressBarRangeInfo) continue;
        if (n.config.setProgress) slider = h.a11y.CreateNodeInfo(id);
        else spinner = h.a11y.CreateNodeInfo(id);
    }
    CHECK(slider && slider->className == "android.widget.SeekBar" && slider->stateDescription == std::optional<std::string>("25 percent."));
    CHECK(slider && slider->rangeInfo && slider->rangeInfo->current == 0.25f);
    CHECK(slider && h.HasAction(*slider, a11y::ScrollForward) && h.HasAction(*slider, a11y::SetProgress));
    CHECK(spinner && spinner->className == "android.widget.ProgressBar" && spinner->stateDescription == std::optional<std::string>("In progress"));
    // Scroll forward steps a slider by a twentieth.
    if (slider) {
        CHECK(h.a11y.PerformAction(slider->id, a11y::ScrollForward));
        CHECK(std::abs(progress - 0.30f) < 1e-5f);
    }
}

TEST(AccessibilitySelectableGroupReportsCollectionAndItemInfo) {
    Harness h;
    h.Run([&] { Column(Modifier{}, [&] {
        Row(SelectableGroup(Modifier{}), [&] {
            for (int i = 0; i < 3; ++i)
                Box(Selectable(SizeMod({}, Dp(40), Dp(40)), i == 1, [] {}));
        });
        Column(Semantics(Modifier{}, [](SemanticsConfiguration& c) {
            c.collectionInfo = std::pair{ 2, 1 };
        }), [&] {
            Box(Semantics(SizeMod({}, Dp(10), Dp(10)), [](SemanticsConfiguration& c) {
                c.text.push_back("x");
                c.collectionItemInfo = CollectionItemInfo{ 1, 1, 0, 1 };
            }));
        });
    }); });
    int group = a11y::kInvalidId;
    std::vector<int> items;
    for (const auto& [id, n] : h.a11y.Nodes()) {
        if (n.config.selectableGroup) group = id;
        if (n.config.selected) items.push_back(id);
    }
    CHECK_EQ(int(items.size()), 3);
    const auto g = h.a11y.CreateNodeInfo(group);
    CHECK(g && g->collectionInfo == (std::pair<int, int>{ 1, 3 }));   // a row of three
    for (int id : items) {
        const auto info = h.a11y.CreateNodeInfo(id);
        CHECK(info && info->collectionItemInfo && info->collectionItemInfo->rowIndex == 0);
        CHECK(info && info->checkable && info->stateDescription);   // "Selected" / "Not selected"
    }
    const AccessibilityNode* x = h.ByText("x");
    CHECK(x != nullptr);
    if (x) {
        const auto info = h.a11y.CreateNodeInfo(x->id);
        CHECK(info && info->collectionItemInfo && info->collectionItemInfo->rowIndex == 1);
    }
}

TEST(AccessibilityTraversalOrderFollowsRowsAndIndices) {
    Harness h;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Row(Modifier{}, [&] {
                Box(Label(SizeMod({}, Dp(40), Dp(20)), "a"));
                Box(Label(SizeMod({}, Dp(40), Dp(20)), "b"));
            });
            Box(Semantics(SizeMod({}, Dp(40), Dp(20)), [](SemanticsConfiguration& c) {
                c.text.push_back("first");
                c.traversalIndex = -1;
            }));
        });
    });
    const auto* a = h.ByText("a");
    const auto* b = h.ByText("b");
    const auto* first = h.ByText("first");
    CHECK(a && b && first);
    if (!a || !b || !first) return;
    const auto& before = h.a11y.TraversalBefore();
    CHECK(before.count(first->id) && before.at(first->id) == a->id);
    CHECK(before.count(a->id) && before.at(a->id) == b->id);
    const auto info = h.a11y.CreateNodeInfo(a->id);
    CHECK(info && info->traversalBefore == b->id && info->traversalAfter == first->id);
}

TEST(AccessibilityTextTraversalMovesByWords) {
    Harness h;
    h.Run([&] { Box(Label(SizeMod({}, Dp(200), Dp(20)), "hello big world")); });
    const auto* n = h.ByText("hello big world");
    CHECK(n != nullptr);
    if (!n) return;
    const int id = n->id;
    const auto info = h.a11y.CreateNodeInfo(id);
    CHECK(info && (info->movementGranularities & a11y::Word) && h.HasAction(*info, a11y::NextAtMovementGranularity));
    h.a11y.TakeEvents();
    AccessibilityActionArgs args;
    args.granularity = a11y::Word;
    CHECK(h.a11y.PerformAction(id, a11y::NextAtMovementGranularity, args));
    auto events = h.a11y.TakeEvents();
    // The cursor moves to the end of "hello"; the traversed event names the word.
    CHECK(std::any_of(events.begin(), events.end(), [](const AccessibilityEvent& e) {
        return e.type == a11y::ViewTextSelectionChanged && e.fromIndex == 5 && e.toIndex == 5;
    }));
    CHECK(std::any_of(events.begin(), events.end(), [](const AccessibilityEvent& e) {
        return e.type == a11y::ViewTextTraversedAtMovementGranularity && e.fromIndex == 0 && e.toIndex == 5 && e.movementGranularity == a11y::Word;
    }));
    CHECK(h.a11y.PerformAction(id, a11y::NextAtMovementGranularity, args));
    events = h.a11y.TakeEvents();
    CHECK(std::any_of(events.begin(), events.end(), [](const AccessibilityEvent& e) {
        return e.type == a11y::ViewTextTraversedAtMovementGranularity && e.fromIndex == 6 && e.toIndex == 9;
    }));
    args.granularity = a11y::Character;
    CHECK(h.a11y.PerformAction(id, a11y::PreviousAtMovementGranularity, args));
    events = h.a11y.TakeEvents();
    CHECK(std::any_of(events.begin(), events.end(), [](const AccessibilityEvent& e) {
        return e.type == a11y::ViewTextTraversedAtMovementGranularity && e.fromIndex == 8 && e.toIndex == 9;
    }));
}

TEST(AccessibilityPropertyChangesSendEvents) {
    Harness h;
    rt::State<bool> onState(false), paneState(false);
    rt::State<int> actionsState(1);
    auto* on = &onState;
    auto* pane = &paneState;
    auto* actions = &actionsState;
    h.Run([&] {
        Column(Modifier{}, [&] {
            const bool checked = on->Get();
            Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [checked](SemanticsConfiguration& c) {
                c.text.push_back("toggle");
                c.toggleableState = checked ? 1 : 0;
            }));
            const int n = actions->Get();
            Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [n](SemanticsConfiguration& c) {
                c.text.push_back("menu");
                if (n >= 1) c.customActions.push_back({ "Archive", [] { return true; } });
                if (n >= 2) c.customActions.insert(c.customActions.begin(), { "Delete", [] { return true; } });
            }));
            if (pane->Get())
                Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [](SemanticsConfiguration& c) { c.paneTitle = "Sheet"; }));
        });
    });
    h.a11y.CheckForSemanticsChanges();
    h.a11y.TakeEvents();
    const int toggle = h.ByText("toggle")->id;
    const int menu = h.ByText("menu")->id;
    auto first = h.a11y.CreateNodeInfo(menu);
    int archiveId = 0;
    for (const auto& a : first->actions) if (a.label == std::optional<std::string>("Archive")) archiveId = a.id;
    CHECK_EQ(archiveId, a11y::kCustomActionIdBase);

    on->Set(true);
    pane->Set(true);
    actions->Set(2);
    h.Pump();
    h.a11y.CheckForSemanticsChanges();
    auto events = h.a11y.TakeEvents();
    CHECK(Has(events, a11y::WindowContentChanged, a11y::Checked, toggle));
    CHECK(Has(events, a11y::WindowStateChanged, a11y::PaneAppeared));
    CHECK(Has(events, a11y::WindowContentChanged, a11y::Undefined, menu));   // the custom actions changed
    // "Archive" keeps its id; "Delete" takes the next free one.
    const auto info = h.a11y.CreateNodeInfo(menu);
    for (const auto& a : info->actions) {
        if (a.label == std::optional<std::string>("Archive")) CHECK_EQ(a.id, a11y::kCustomActionIdBase);
        if (a.label == std::optional<std::string>("Delete")) CHECK_EQ(a.id, a11y::kCustomActionIdBase + 1);
    }

    pane->Set(false);
    h.Pump();
    h.a11y.CheckForSemanticsChanges();
    events = h.a11y.TakeEvents();
    CHECK(Has(events, a11y::WindowStateChanged, a11y::PaneDisappeared));
}

TEST(AccessibilityScrollEventsReportRangesAndForwardDeltas) {
    Harness h;
    ScrollState state;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Column(VerticalScroll(SizeMod({}, Dp(100), Dp(100)), state), [&] {
                for (int i = 0; i < 10; ++i) Box(SizeMod(Modifier{}, Dp(100), Dp(50)), Alignment::TopStart(), false, {}, rt::Key(i));
            });
        });
    });
    h.a11y.CheckForSemanticsChanges();   // the first copy
    h.a11y.CheckForSemanticsChanges();   // observing the scroller
    h.a11y.FlushBoundsUpdates();
    h.a11y.TakeEvents();
    h.Touch(PointerEventType::Press, 50, 90, true, 0);
    h.Touch(PointerEventType::Move, 50, 70, true, 100);
    h.Touch(PointerEventType::Move, 50, 40, true, 200);
    h.Touch(PointerEventType::Release, 50, 40, false, 400);
    CHECK(h.a11y.HasPendingBoundsUpdates());
    h.a11y.FlushBoundsUpdates();
    auto events = h.a11y.TakeEvents();
    const auto scrolled = std::find_if(events.begin(), events.end(), [](const AccessibilityEvent& e) { return e.type == a11y::ViewScrolled; });
    CHECK(scrolled != events.end());
    if (scrolled != events.end()) {
        CHECK(!scrolled->scrollX && scrolled->scrollY == std::optional(42) && scrolled->maxScrollY == std::optional(400));
        CHECK_EQ(scrolled->scrollDeltaY, 42);   // finger up = forward = positive
        CHECK_EQ(scrolled->scrollDeltaX, 0);
    }
    CHECK(Has(events, a11y::WindowContentChanged, a11y::Subtree));
    // A programmatic scroll reports its range without a pixel delta.
    state.ScrollTo(10);
    h.Pump();
    h.a11y.FlushBoundsUpdates();
    events = h.a11y.TakeEvents();
    const auto again = std::find_if(events.begin(), events.end(), [](const AccessibilityEvent& e) { return e.type == a11y::ViewScrolled; });
    CHECK(again != events.end() && again->scrollY == std::optional(10) && again->scrollDeltaY == 0);
    // Scroll actions page by the viewport.
    int scroller = a11y::kInvalidId;
    for (const auto& [id, n] : h.a11y.Nodes()) if (n.config.verticalScrollAxisRange) scroller = id;
    const auto info = h.a11y.CreateNodeInfo(scroller);
    CHECK(info && info->className == "android.widget.ScrollView" && info->scrollable);
    CHECK(info && h.HasAction(*info, a11y::ScrollDown) && h.HasAction(*info, a11y::ScrollUp));
    CHECK(h.a11y.PerformAction(scroller, a11y::ScrollForward));
    h.d.RunPending();
    h.Frames(60);
    CHECK_EQ(state.Value(), 110);
}

TEST(AccessibilityShowOnScreenScrollsTheItemIn) {
    Harness h;
    ScrollState state;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] { Column(VerticalScroll(SizeMod({}, Dp(100), Dp(100)), state), [&] {
            for (int i = 0; i < 10; ++i) Box(Semantics(SizeMod(Modifier{}, Dp(100), Dp(50)), [i](SemanticsConfiguration& c) {
                c.text.push_back("item " + std::to_string(i));
                c.onClick = [] { return true; };
            }, true), Alignment::TopStart(), false, {}, rt::Key(i));
        }); });
    });
    // Item 1 (50..100) partly scrolled off the top: showing it brings its top back.
    state.ScrollTo(70);
    h.Pump();
    const auto* partial = h.ByText("item 1");
    CHECK(partial != nullptr);
    if (!partial) return;
    // Clipped by the scroller; the touch bounds keep 48 px (calculateMinimumTouchTargetOffset).
    CHECK(partial->boundsInRoot.top == 0 && partial->boundsInRoot.bottom == 30);
    CHECK(partial->adjustedBounds == (IntRect{ 0, 0, 100, 30 }));
    CHECK(h.a11y.PerformAction(partial->id, a11y::ShowOnScreen));
    h.d.RunPending();
    h.Frames(60);
    CHECK_EQ(state.Value(), 50);
}

TEST(AccessibilityHoverFindsTheDeepestImportantNode) {
    Harness h;
    h.Run([&] {
        Box(Semantics(SizeMod({}, Dp(100), Dp(100)), [](SemanticsConfiguration& c) {
            c.onClick = [] { return true; };
        }, true), Alignment::TopStart(), false, [&] { Box(Label(SizeMod({}, Dp(50), Dp(50)), "inner")); });
    });
    const auto* inner = h.ByText("inner");
    CHECK(inner && h.a11y.HitTest(10, 10) == inner->id);
    CHECK(h.a11y.HitTest(80, 80) == inner->parent);
    CHECK(h.a11y.DispatchHover(9, 10, 10));
    const auto events = h.a11y.TakeEvents();
    CHECK(inner && Has(events, a11y::ViewHoverEnter, std::nullopt, inner->id));
}

TEST(AccessibilityStringsComeFromTheLocale) {
    const auto de = a11y::Strings::ForLocale("de-DE");
    CHECK_EQ(de.stateOn, std::string("An"));
    const auto en = a11y::Strings::ForLocale("xx");
    CHECK_EQ(en.notSelected, std::string("Not selected"));
}

TEST(AccessibilityTextChangesCarryTheImeChangeTypes) {
    Harness h;
    rt::State<std::string> textState("ab");
    rt::State<bool> composing(false);
    auto* text = &textState;
    auto* comp = &composing;
    h.Run([&] {
        const std::string t = text->Get();
        const bool c = comp->Get();
        Box(Semantics(SizeMod({}, Dp(50), Dp(20)), [t, c](SemanticsConfiguration& s) {
            s.testTag = "field";
            s.editableText = t;
            s.setText = [](const std::string&) { return true; };
            if (c) s.textCompositionRange = std::make_pair(2, 3);
            s.inputTextSuggestionState = InputTextSuggestionState{ true, c };
        }));
    });
    h.a11y.CheckForSemanticsChanges();
    h.a11y.TakeEvents();
    text->Set("abc");
    comp->Set(true);
    h.Pump();
    h.a11y.CheckForSemanticsChanges();
    bool found = false;
    for (const auto& e : h.a11y.TakeEvents())
        if (e.type == a11y::ViewTextChanged) { found = true; CHECK(e.textChangeTypes == std::optional<int>(1 | 2 | 4)); }
    CHECK(found);
}
