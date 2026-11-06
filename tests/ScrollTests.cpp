#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Gestures.h"

#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        rt::Composition composition{ owner.Root(), d };
        std::int64_t frameNs = 0;
        Harness() { owner.SetDensity({ 1, 1 }); owner.Config().touchSlop = 8; }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { composition.Recompose(); owner.MeasureAndLayout({ 100, 100 }); }
        void Touch(PointerEventType type, float x, float y, bool down, std::int64_t t) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.pointers.push_back({ 1, { x, y }, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            d.RunPending();
            Pump();
        }
        void Frames(int n) { for (int i = 0; i < n; ++i) { frameNs += 16'000'000; d.RunFrame(frameNs); Pump(); } }
    };

    void Tall(ScrollState& state, int items) {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&, items] {
            for (int i = 0; i < items; ++i) Box(SizeMod(Modifier{}, Dp(100), Dp(50)), Alignment::TopStart(), false, {}, rt::Key(i));
        });
    }
}

TEST(DragScrollsByFingerMinusSlop) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 10); });
    CHECK_EQ(state.MaxValue(), 400);   // 500 content - 100 viewport
    h.Touch(PointerEventType::Press, 50, 90, true, 0);
    h.Touch(PointerEventType::Move, 50, 70, true, 100);   // 20 up: 12 past slop
    h.Touch(PointerEventType::Move, 50, 40, true, 200);   // 30 more
    CHECK_EQ(state.Value(), 42);
    h.Touch(PointerEventType::Release, 50, 40, false, 400);   // slow: no fling
    h.Frames(5);
    CHECK_EQ(state.Value(), 42);
}

TEST(FlingContinuesAndStopsAtTheEdge) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 10); });
    h.Touch(PointerEventType::Press, 50, 95, true, 0);
    for (int i = 1; i <= 6; ++i) h.Touch(PointerEventType::Move, 50, 95.0f - 10 * i, true, 10 * i);
    h.Touch(PointerEventType::Release, 50, 35, false, 70);
    const int afterDrag = state.Value();
    CHECK(state.IsScrollInProgress());
    h.Frames(10);
    CHECK(state.Value() > afterDrag);
    h.Frames(200);
    // 1000 px/s at density 1 flies ~194 px with the platform spline (drag was 60 - 8 slop).
    CHECK(std::abs(state.Value() - (afterDrag + 194)) <= 2);
    CHECK(!state.IsScrollInProgress());
    // A faster fling from near the end stops at the edge.
    h.Touch(PointerEventType::Press, 50, 95, true, 5000);
    for (int i = 1; i <= 6; ++i) h.Touch(PointerEventType::Move, 50, 95.0f - 15 * i, true, 5000 + 10 * i);
    h.Touch(PointerEventType::Release, 50, 5, false, 5070);
    h.Frames(200);
    CHECK_EQ(state.Value(), 400);
}

TEST(PressCatchesAFling) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 40); });
    h.Touch(PointerEventType::Press, 50, 95, true, 0);
    for (int i = 1; i <= 6; ++i) h.Touch(PointerEventType::Move, 50, 95.0f - 10 * i, true, 10 * i);
    h.Touch(PointerEventType::Release, 50, 35, false, 70);
    h.Frames(5);
    h.Touch(PointerEventType::Press, 50, 50, true, 2000);
    const int caught = state.Value();
    h.Frames(20);
    CHECK_EQ(state.Value(), caught);
    CHECK(!state.IsScrollInProgress());
    h.Touch(PointerEventType::Release, 50, 50, false, 2100);
}

TEST(NestedScrollPassesLeftoverToParent) {
    Harness h;
    ScrollState outer, inner;
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), outer), [&] {
            // The height goes before verticalScroll, or the scrolled content itself is 60 tall.
            Box(VerticalScroll(Height(Modifier{}, Dp(60)), inner), Alignment::TopStart(), false, [&] {
                Box(SizeMod(Modifier{}, Dp(100), Dp(80)));   // inner can scroll 20
            });
            Box(SizeMod(Modifier{}, Dp(100), Dp(200)));
        });
    });
    CHECK_EQ(inner.MaxValue(), 20);
    h.Touch(PointerEventType::Press, 50, 50, true, 0);
    h.Touch(PointerEventType::Move, 50, 42, true, 100);    // slop
    h.Touch(PointerEventType::Move, 50, 2, true, 300);     // 40 more
    CHECK_EQ(inner.Value(), 20);
    CHECK_EQ(outer.Value(), 20);                           // the leftover went to the parent
    h.Touch(PointerEventType::Release, 50, 2, false, 600);
}

TEST(DragOverClickableChildrenScrollsAndCancelsTheClick) {
    Harness h;
    ScrollState state;
    int clicks = 0;
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&] {
            for (int i = 0; i < 10; ++i)
                Box(Clickable(SizeMod(Modifier{}, Dp(100), Dp(50)), [&] { ++clicks; }), Alignment::TopStart(), false, {}, rt::Key(i));
        });
    });
    h.Touch(PointerEventType::Press, 50, 90, true, 0);
    h.Touch(PointerEventType::Move, 50, 60, true, 100);
    h.Touch(PointerEventType::Move, 50, 40, true, 200);
    h.Touch(PointerEventType::Release, 50, 40, false, 600);
    CHECK_EQ(state.Value(), 42);
    CHECK_EQ(clicks, 0);
}

TEST(DragScrollsWhileContentRecomposesEveryFrame) {
    Harness h;
    ScrollState state;
    rt::State<int> tick(0);
    h.Run([&] {
        auto& c = *rt::Composition::Current();
        (void)c;
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&] {
            Box(SizeMod(Modifier{}, Dp(100), Dp(float(50 + tick.Get() % 2))));   // read: recomposes
            for (int i = 0; i < 9; ++i) Box(SizeMod(Modifier{}, Dp(100), Dp(50)), Alignment::TopStart(), false, {}, rt::Key(i));
        });
    });
    std::int64_t t = 0;
    h.Touch(PointerEventType::Press, 50, 90, true, t);
    for (int i = 1; i <= 10; ++i) {
        tick.Set(i);
        h.Pump();
        h.Touch(PointerEventType::Move, 50, 90 - float(i) * 5, true, t += 16);
    }
    h.Touch(PointerEventType::Release, 50, 40, false, t += 16);
    CHECK(state.Value() > 30);
}

#include "kiln/foundation/Overscroll.h"

TEST(OverscrollPullsAtTheEdgeAndRelaxesFirst) {
    OverscrollEffect e;
    e.UpdateSize({ 100, 400 });
    float scrolled = 0;
    auto perform = [&](Offset d) {
        // Content at the top: it can only move towards negative y (scroll down).
        const float can = d.y > 0 ? 0.0f : d.y;
        scrolled += can;
        return Offset{ 0, can };
    };
    // Dragging down at the top: nothing scrolls, the top edge stretches.
    Offset c = e.ApplyToScroll({ 0, 40 }, NestedScrollSource::UserInput, perform);
    CHECK_EQ(scrolled, 0.0f);
    CHECK(e.IsInProgress());
    CHECK_EQ(c.y, 0.0f);
    float sx = 0, sy = 0;
    e.Advance(0, sx, sy);
    CHECK(sy > 0);                                   // top stretch is positive
    // Dragging back up relaxes the stretch before the content scrolls.
    c = e.ApplyToScroll({ 0, -20 }, NestedScrollSource::UserInput, perform);
    CHECK_EQ(c.y, -20.0f);
    CHECK_EQ(scrolled, 0.0f);
    c = e.ApplyToScroll({ 0, -40 }, NestedScrollSource::UserInput, perform);
    CHECK(!e.IsInProgress());
    CHECK(scrolled < 0);                             // the rest scrolled the content
}

TEST(OverscrollSpringsBackAfterRelease) {
    OverscrollEffect e;
    std::int64_t t = 1'000'000;
    e.clock = [&t] { return t; };
    e.UpdateSize({ 100, 400 });
    auto none = [](Offset) { return Offset{}; };
    e.ApplyToScroll({ 0, 60 }, NestedScrollSource::UserInput, none);
    e.AbsorbPostFling({});                           // finger up, no velocity: release
    float sx = 0, sy = 0;
    bool animating = true;
    for (int i = 0; i < 200 && animating; ++i) animating = e.Advance(t += 16, sx, sy);
    CHECK(!animating);
    CHECK(!e.IsInProgress());
    CHECK_EQ(sy, 0.0f);
}

#include "kiln/foundation/BringIntoView.h"
#include "kiln/foundation/Focus.h"

TEST(FocusAndRequestersBringContentIntoView) {
    Harness h;
    ScrollState state;
    FocusRequester focus;
    BringIntoViewRequester requester;
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&] {
            for (int i = 0; i < 10; ++i) {
                Modifier m = SizeMod(Modifier{}, Dp(100), Dp(50));
                if (i == 7) m = Focusable(FocusRequesterModifier(std::move(m), focus));
                if (i == 2) m = BringIntoViewRequesterMod(std::move(m), requester);
                Box(m, Alignment::TopStart(), false, {}, rt::Key(i));
            }
        });
    });
    CHECK(focus.RequestFocus());
    h.Frames(60);
    CHECK_EQ(state.Value(), 300);   // item 7 (350..400) lines up with the bottom edge
    requester.BringIntoView();
    h.Frames(60);
    CHECK_EQ(state.Value(), 100);   // item 2 (100..150) lines up with the top edge
    // A rect within a node: the part asked for, not the whole node.
    requester.BringIntoView(Rect{ 0, 40, 100, 50 });
    h.Frames(60);
    CHECK_EQ(state.Value(), 100);   // already visible
}

TEST(ShrinkingViewportKeepsTheFocusedChildVisible) {
    Harness h;
    ScrollState state;
    FocusRequester focus;
    rt::State<int> height{ 100 };
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Column(VerticalScroll(Height(Modifier{}, Dp(float(height.Get()))), state), [&] {
                for (int i = 0; i < 10; ++i) {
                    Modifier m = SizeMod(Modifier{}, Dp(100), Dp(50));
                    if (i == 1) m = Focusable(FocusRequesterModifier(std::move(m), focus));
                    Box(m, Alignment::TopStart(), false, {}, rt::Key(i));
                }
            });
        });
    });
    CHECK(focus.RequestFocus());
    h.Frames(10);
    CHECK_EQ(state.Value(), 0);     // visible already
    height.Set(60);                 // an IME takes 40 px
    h.Pump();
    h.Frames(60);
    CHECK_EQ(state.Value(), 40);    // item 1 (50..100) back above the new bottom edge
}

#include "kiln/ui/Locals.h"

TEST(HorizontalScrollReversesItsDragInRtl) {
    Harness h;
    h.owner.SetDirection(LayoutDirection::Rtl);
    ScrollState state;
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalLayoutDirection, LayoutDirection::Rtl, [&] {
            Row(HorizontalScroll(FillMax(Modifier{}, Axis::Both), state), Arrangement::Start(), AlignmentV::Top(), [] {
                for (int i = 0; i < 6; ++i) Box(SizeMod(Modifier{}, Dp(50), Dp(50)), Alignment::TopStart(), false, {}, rt::Key(i));
            });
        });
    });
    // The start is on the right; dragging right (towards the start side's opposite) scrolls forward.
    h.Touch(PointerEventType::Press, 10, 50, true, 0);
    h.Touch(PointerEventType::Move, 30, 50, true, 100);
    h.Touch(PointerEventType::Move, 50, 50, true, 200);
    CHECK_EQ(state.Value(), 32);   // 40 moved, 8 of it slop
    h.Touch(PointerEventType::Release, 50, 50, false, 500);
}

TEST(PageKeysScrollByAViewport) {
    Harness h;
    ScrollState state;
    FocusRequester focus;
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&] {
            for (int i = 0; i < 10; ++i) {
                Modifier m = SizeMod(Modifier{}, Dp(100), Dp(50));
                if (i == 0) m = Focusable(FocusRequesterModifier(std::move(m), focus));
                Box(m, Alignment::TopStart(), false, {}, rt::Key(i));
            }
        });
    });
    CHECK(focus.RequestFocus());
    h.Frames(5);
    KeyEvent down{ keys::PageDown, KeyEventType::KeyDown };
    CHECK(h.owner.DispatchKeyEvent(down));
    h.Pump();
    CHECK_EQ(state.Value(), 100);
    KeyEvent up{ keys::PageUp, KeyEventType::KeyDown };
    CHECK(h.owner.DispatchKeyEvent(up));
    h.Pump();
    CHECK_EQ(state.Value(), 0);
    KeyEvent ctrl{ keys::PageDown, KeyEventType::KeyDown, kMetaCtrl };
    CHECK(!h.owner.DispatchKeyEvent(ctrl) || state.Value() == 0);
    h.Pump();
    CHECK_EQ(state.Value(), 0);
}

#include "kiln/foundation/Overscroll.h"

TEST(OverscrollFactoryCanTurnOverscrollOff) {
    Harness h;
    std::shared_ptr<OverscrollEffect> withDefault, withNone;
    h.Run([&] {
        withDefault = RememberOverscrollEffect();
        rt::Composition::Current()->Provide(LocalOverscrollFactory, std::shared_ptr<const OverscrollFactory>{}, [&] {
            withNone = RememberOverscrollEffect();
        });
    });
    CHECK(withDefault != nullptr);
    CHECK(withNone == nullptr);
}

TEST(ChildOverscrollStretchesOnlyWhatTheParentLeaves) {
    Harness h;
    ScrollState outer, inner;
    auto innerEffect = std::make_shared<OverscrollEffect>();
    auto outerEffect = std::make_shared<OverscrollEffect>();
    innerEffect->UpdateSize({ 100, 60 });    // normally learnt while drawing
    outerEffect->UpdateSize({ 100, 100 });
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), outer, true, false, outerEffect), [&] {
            Box(VerticalScroll(Height(Modifier{}, Dp(60)), inner, true, false, innerEffect), Alignment::TopStart(), false, [&] {
                Box(SizeMod(Modifier{}, Dp(100), Dp(80)));
            });
            Box(SizeMod(Modifier{}, Dp(100), Dp(200)));
        });
    });
    // Inner at its end, outer can take the rest: no stretch anywhere.
    h.Touch(PointerEventType::Press, 50, 50, true, 0);
    h.Touch(PointerEventType::Move, 50, 42, true, 100);
    h.Touch(PointerEventType::Move, 50, 2, true, 300);
    CHECK(!innerEffect->IsInProgress());
    CHECK(!outerEffect->IsInProgress());
    h.Touch(PointerEventType::Release, 50, 2, false, 600);
    h.Frames(60);
    // Both at their start: pulling down stretches the inner one (the leftover after the parent).
    outer.ScrollTo(0);
    inner.ScrollTo(0);
    h.Pump();
    h.Touch(PointerEventType::Press, 50, 10, true, 1000);
    h.Touch(PointerEventType::Move, 50, 20, true, 1100);
    h.Touch(PointerEventType::Move, 50, 50, true, 1200);
    CHECK(innerEffect->IsInProgress());
    CHECK(!outerEffect->IsInProgress());
    h.Touch(PointerEventType::Release, 50, 50, false, 1500);
}

TEST(ScrollEventsReportRangesAndForwardDeltas) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 10); });
    ScrollEventTracker tracker;
    CHECK(tracker.Collect(h.owner.BuildSemanticsTree(), h.owner).empty());   // first sight: observing
    h.Touch(PointerEventType::Press, 50, 90, true, 0);
    h.Touch(PointerEventType::Move, 50, 70, true, 100);
    h.Touch(PointerEventType::Move, 50, 40, true, 200);
    h.Touch(PointerEventType::Release, 50, 40, false, 400);
    auto events = tracker.Collect(h.owner.BuildSemanticsTree(), h.owner);
    CHECK_EQ(int(events.size()), 1);
    if (events.size() == 1) {
        CHECK(!events[0].x);
        CHECK(events[0].y == (std::pair<int, int>{ 42, 400 }));
        CHECK_EQ(events[0].deltaY, 42);   // finger up = forward = positive
        CHECK_EQ(events[0].deltaX, 0);
    }
    CHECK(tracker.Collect(h.owner.BuildSemanticsTree(), h.owner).empty());   // nothing moved since
    // A programmatic scroll reports its range without a pixel delta (like Compose).
    state.ScrollTo(10);
    h.Pump();
    events = tracker.Collect(h.owner.BuildSemanticsTree(), h.owner);
    CHECK(events.size() == 1 && events[0].y == (std::pair<int, int>{ 10, 400 }) && events[0].deltaY == 0);
}

namespace {
    // One wheel notch towards the end (AXIS_VSCROLL -1; the host flips it to scrollDelta +1).
    void Wheel(Harness& h, float notches, std::int64_t t) {
        RawPointerEvent e;
        e.type = PointerEventType::Scroll;
        e.uptimeMillis = t;
        RawPointer p{ 1, { 50, 50 }, false, PointerType::Mouse, {} };
        p.scrollDelta = { 0, notches };
        e.pointers.push_back(p);
        h.owner.ProcessPointer(e);
        h.d.RunPending();
        h.Pump();
    }
}

TEST(MouseWheelScrollsSmoothly) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 10); });
    Wheel(h, 1, 0);
    CHECK_EQ(state.Value(), 6);              // the 6 dp threshold at once
    CHECK(state.IsScrollInProgress());
    h.Frames(2);
    CHECK(state.Value() > 6 && state.Value() < 64);   // then animated (1 dp/ms)
    h.Frames(4);
    CHECK_EQ(state.Value(), 64);             // one notch = the scroll factor
    // The scroll waits 50 ms minus the animation's 58 ms for more notches: none here.
    CHECK(!state.IsScrollInProgress());
    // Notches during the animation retarget it: two notches land 128 further.
    Wheel(h, 1, 300);
    h.Frames(1);
    Wheel(h, 1, 316);
    h.Frames(20);
    CHECK_EQ(state.Value(), 192);
    // Wheel up scrolls back.
    Wheel(h, -1, 900);
    h.Frames(20);
    CHECK_EQ(state.Value(), 128);
}

TEST(MouseWheelAtTheEdgeIsLeftForOthers) {
    Harness h;
    ScrollState inner;
    int outerScrolled = 0;
    ScrollState outer;
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), outer), [&] {
            Column(VerticalScroll(Height(Modifier{}, Dp(60)), inner), [&] {
                Box(SizeMod(Modifier{}, Dp(100), Dp(80)));
            });
            Box(SizeMod(Modifier{}, Dp(100), Dp(200)));
        });
    });
    // Wheel up with both at the top: neither can take it.
    Wheel(h, -1, 0);
    h.Frames(10);
    CHECK_EQ(inner.Value(), 0);
    CHECK_EQ(outer.Value(), 0);
    // Down: the inner one scrolls to its end (20 px) and the rest goes up to the outer one.
    Wheel(h, 1, 100);
    h.Frames(20);
    CHECK_EQ(inner.Value(), 20);
    outerScrolled = outer.Value();
    CHECK_EQ(outerScrolled, 44);
    // The inner one is at its end now: the next notch goes straight to the outer one.
    Wheel(h, 1, 400);
    h.Frames(20);
    CHECK_EQ(outer.Value(), 108);
}

namespace {
    void Pan(Harness& h, PointerEventType type, float panY, std::int64_t t) {
        RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t;
        RawPointer p{ 1, { 50, 50 }, false, PointerType::Mouse, {} };
        p.panOffset = { 0, panY };
        e.pointers.push_back(p);
        h.owner.ProcessPointer(e);
        h.d.RunPending();
        h.Pump();
    }
}

TEST(TrackpadPanScrollsWithoutSlopAndFlings) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 10); });
    Pan(h, PointerEventType::PanStart, 0, 0);
    for (int i = 1; i <= 3; ++i) { Pan(h, PointerEventType::PanMove, 10, 10 * i); h.Frames(1); }
    CHECK_EQ(state.Value(), 30);             // offsets apply directly: no touch slop
    CHECK(state.IsScrollInProgress());
    Pan(h, PointerEventType::PanEnd, 0, 31);
    h.Frames(60);
    CHECK(state.Value() > 45);               // ~1000 px/s carries on as a fling
    CHECK(!state.IsScrollInProgress());
}

TEST(MouseDragDoesNotScroll) {
    Harness h;
    ScrollState state;
    h.Run([&] { Tall(state, 10); });
    auto mouse = [&](PointerEventType type, float y, bool down, std::int64_t t) {
        RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t;
        e.buttons = down ? 1 : 0;
        e.pointers.push_back({ 1, { 50, y }, down, PointerType::Mouse, {} });
        h.owner.ProcessPointer(e);
        h.d.RunPending();
        h.Pump();
    };
    mouse(PointerEventType::Press, 90, true, 0);
    mouse(PointerEventType::Move, 50, true, 50);
    mouse(PointerEventType::Move, 10, true, 100);
    mouse(PointerEventType::Release, 10, false, 150);
    h.Frames(10);
    CHECK_EQ(state.Value(), 0);
}

namespace {
    // A View parent (an app bar) that takes half of every pre-scroll, in View signs.
    struct FakeViewParent final : HostNestedScrollParent {
        int started = 0, stopped = 0, preScrolled = 0, scrolledUnconsumed = 0;
        bool Start(int axes, bool) override { started |= axes; return true; }
        void PreScroll(int, int dy, bool, int consumed[2]) override { consumed[1] = dy / 2; preScrolled += dy / 2; }
        void Scroll(int, int, int, int dyUnconsumed, bool, int consumed[2]) override { scrolledUnconsumed += dyUnconsumed; consumed[1] = 0; }
        bool PreFling(float, float) override { return false; }
        bool Fling(float, float, bool) override { return false; }
        void StopAll() override { ++stopped; }
    };
}

TEST(NestedScrollInteropSharesScrollWithTheHostViewParent) {
    Harness h;
    ScrollState state;
    FakeViewParent parent;
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalHostNestedScrollParent, static_cast<HostNestedScrollParent*>(&parent), [&] {
            auto interop = RememberNestedScrollInteropConnection();
            Column(VerticalScroll(NestedScroll(FillMax(Modifier{}, Axis::Both), interop), state), [&] {
                for (int i = 0; i < 10; ++i) Box(SizeMod(Modifier{}, Dp(100), Dp(50)), Alignment::TopStart(), false, {}, rt::Key(i));
            });
        });
    });
    h.Touch(PointerEventType::Press, 50, 90, true, 0);
    h.Touch(PointerEventType::Move, 50, 70, true, 100);   // 12 past slop
    h.Touch(PointerEventType::Move, 50, 40, true, 200);   // 30 more
    CHECK_EQ(parent.started, 2);                          // vertical
    CHECK_EQ(parent.preScrolled, 6 + 15);                 // the View side scrolled content up (+)
    CHECK_EQ(state.Value(), 42 - 21);
    h.Touch(PointerEventType::Release, 50, 40, false, 400);
    h.Frames(5);
    CHECK(parent.stopped >= 1);                           // the fling's end stops nested scrolling
}

TEST(BringIntoViewReachesTheHostWithTheScrolledRect) {
    Harness h;
    ScrollState state;
    BringIntoViewRequester requester;
    std::vector<Rect> host;
    h.owner.SetRequestRectangleOnScreen([&](Rect r) { host.push_back(r); });
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&] {
            for (int i = 0; i < 10; ++i) {
                Modifier m = SizeMod(Modifier{}, Dp(100), Dp(50));
                if (i == 7) m = BringIntoViewRequesterMod(std::move(m), requester);
                Box(m, Alignment::TopStart(), false, {}, rt::Key(i));
            }
        });
    });
    requester.BringIntoView();
    // At once, before the scroll animates: where the container will have put it (the bottom edge).
    CHECK_EQ(int(host.size()), 1);
    if (!host.empty()) {
        CHECK_EQ(host[0].top, 50.0f);
        CHECK_EQ(host[0].bottom, 100.0f);
    }
    h.Frames(60);
    CHECK_EQ(state.Value(), 300);
}
