// draggable2D, scrollable2D and rememberScrollableState: drags in any direction after the slop,
// flings that decay on both axes.
#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Scrollable2D.h"
#include "kiln/foundation/Snapping.h"
#include "kiln/animation/Decay.h"

#include <chrono>
#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t frameNanos = 0;
        std::int64_t t = 0;
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            rt::Dispatcher::Main().RunPending();
            rt::Composition::RecomposeAll();
            owner.MeasureAndLayout({ 400, 400 });
        }
        void Touch(PointerEventType type, Offset at, bool down) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.pointers.push_back({ 1, at, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            Pump();
        }
        // A drag from `from` by `by` in `steps` moves `stepMs` apart.
        void Drag(Offset from, Offset by, int steps, std::int64_t stepMs) {
            Touch(PointerEventType::Press, from, true);
            for (int i = 1; i <= steps; ++i) {
                t += stepMs;
                Touch(PointerEventType::Move, { from.x + by.x * float(i) / float(steps), from.y + by.y * float(i) / float(steps) }, true);
            }
            t += 1;
            Touch(PointerEventType::Release, from + by, false);
        }
        void Frames(int n) {
            auto& d = rt::Dispatcher::Main();
            for (int i = 0; i < n; ++i) {
                d.AdvanceTimeBy(std::chrono::milliseconds(16));
                frameNanos += 16'000'000;
                d.RunPending();
                d.RunFrame(frameNanos);
                Pump();
            }
        }
    };
}

TEST(Draggable2DDragsDiagonallyPastTheSlop) {
    Harness h;
    Offset total{};
    Offset velocity{};
    bool started = false;
    h.Run([&] {
        auto& state = RememberDraggable2DState([&](Offset d) { total = total + d; });
        Draggable2DOptions o;
        o.onDragStarted = [&](Offset) { started = true; };
        o.onDragStopped = [&](Offset v) { velocity = v; };
        Box(Draggable2D(SizeMod(Modifier{}, Dp(300), Dp(300)), state, o), Alignment::TopStart(), false, [] {});
    });
    // 60 right and 80 down (100 along the drag): everything past the slop arrives, along the drag.
    h.Drag({ 50, 50 }, { 60, 80 }, 10, 10);
    CHECK(started);
    const float slop = h.owner.Config().touchSlop;
    CHECK(std::abs(std::hypot(total.x, total.y) - (100 - slop)) < 0.5f);
    CHECK(std::abs(total.x / total.y - 0.75f) < 0.01f);
    CHECK(velocity.x > 0 && velocity.y > 0);
}

TEST(Scrollable2DScrollsAndFlingsOnBothAxes) {
    Harness h;
    Offset position{};
    bool inProgress = false;
    Scrollable2DState* st = nullptr;
    h.Run([&] {
        // Content that moves with the finger, bounded to +-1000 on each axis.
        auto& state = RememberScrollable2DState([&](Offset d) {
            const Offset next{ std::clamp(position.x + d.x, -1000.0f, 1000.0f), std::clamp(position.y + d.y, -1000.0f, 1000.0f) };
            const Offset used = next - position;
            position = next;
            return used;
        });
        st = &state;
        inProgress = state.IsScrollInProgress();
        Box(Scrollable2D(SizeMod(Modifier{}, Dp(300), Dp(300)), state), Alignment::TopStart(), false, [] {});
    });
    h.Drag({ 150, 150 }, { -60, -40 }, 6, 16);
    // Content follows the finger: dragging up-left scrolls the state forward (positive).
    const Offset afterDrag = position;
    CHECK(afterDrag.x > 0 && afterDrag.y > 0);
    CHECK(st->IsScrollInProgress());   // flinging
    h.Frames(200);
    CHECK(position.x > afterDrag.x && position.y > afterDrag.y);
    CHECK(!st->IsScrollInProgress());
    (void)inProgress;
}

TEST(RememberScrollableStateUsesTheLatestLambda) {
    Harness h;
    float consumedBy = 0;
    int which = 1;
    rt::State<int> version(1);
    LambdaScrollableState* st = nullptr;
    h.Run([&] {
        const int v = version.Get();
        auto& s = RememberScrollableState([&, v](float d) { which = v; consumedBy += d; return d; });
        st = &s;
    });
    CHECK_EQ(st->ConsumeScrollDelta(5), 5.0f);
    CHECK_EQ(which, 1);
    version.Set(2);
    h.Pump();
    st->ConsumeScrollDelta(3);
    CHECK_EQ(which, 2);
    CHECK_EQ(consumedBy, 8.0f);
}

TEST(DragBlocksMoveTheStateOneAtATime) {
    float total = 0;
    DraggableState state([&](float d) { total += d; });
    // A drag that waits a frame between steps; a second drag takes over in between.
    auto first = rt::Launch(rt::Dispatcher::Main(), state.Drag([](const DragScope& s) -> rt::Task<> {
        s.DragBy(10);
        co_await rt::NextFrame();
        s.DragBy(100);   // stopped by then: ignored
    }));
    CHECK_EQ(total, 10.0f);
    auto second = rt::Launch(rt::Dispatcher::Main(), state.Drag([](const DragScope& s) -> rt::Task<> { s.DragBy(1); co_return; }));
    rt::Dispatcher::Main().RunFrame(16'000'000);
    CHECK_EQ(total, 11.0f);
}

TEST(ScrollContainerHelpers) {
    auto throws = [](auto f) { try { f(); } catch (const std::logic_error&) { return true; } return false; };
    CHECK(throws([] { CheckScrollableContainerConstraints(Constraints{ 0, 100, 0, Constraints::Infinity }, Orientation::Vertical); }));
    CHECK(!throws([] { CheckScrollableContainerConstraints(Constraints{ 0, 100, 0, Constraints::Infinity }, Orientation::Horizontal); }));
    // scroll { }: in progress while the block runs.
    Offset moved{};
    Scrollable2DState state([&](Offset d) { moved = moved + d; return d; });
    bool during = false;
    auto job = rt::Launch(rt::Dispatcher::Main(), state.Scroll([&](const Scrollable2DState::Scroll2DScope& s) -> rt::Task<> {
        s.ScrollBy({ 3, 4 });
        during = state.IsScrollInProgress();
        co_return;
    }));
    CHECK(during);
    CHECK(!state.IsScrollInProgress());
    CHECK(moved == (Offset{ 3, 4 }));
}

TEST(ScrollableAreaScrollsWithTheFingerAndReportsDrags) {
    Harness h;
    float value = 0;
    std::shared_ptr<MutableInteractionSource> source;
    std::vector<Interaction::Phase> phases;
    h.Run([&] {
        source = RememberInteractionSource();
        auto& state = RememberScrollableState([&](float d) { value += d; return d; });
        ScrollableAreaOptions o;
        o.interactionSource = source;
        o.overscroll = std::shared_ptr<OverscrollEffect>();   // none
        Box(ScrollableArea(SizeMod(Modifier{}, Dp(300), Dp(300)), state, Orientation::Vertical, o), Alignment::TopStart(), false, [] {});
    });
    source->Subscribe([&](const Interaction& i) { if (i.kind == Interaction::Kind::Drag) phases.push_back(i.phase); });
    // Dragging up scrolls forward (the content follows the finger).
    h.Drag({ 150, 200 }, { 0, -80 }, 8, 16);
    CHECK(value > 0);
    CHECK((phases == std::vector{ Interaction::Phase::Start, Interaction::Phase::End }));
}

TEST(AxisDragHelpersAndPressScope) {
    Harness h;
    float horizontal = 0;
    int released = -1;
    h.Run([&] {
        Column(Modifier{}, [&] {
            // horizontalTouchSlop + horizontalDrag: only x movement counts.
            Box(PointerInput(SizeMod(Modifier{}, Dp(300), Dp(100)), rt::Keys(1), [&](PointerInputScope& s) -> rt::Task<> {
                for (;;) {
                    auto down = co_await AwaitFirstDown(s);
                    float over = 0;
                    auto drag = co_await AwaitHorizontalTouchSlopOrCancellation(s, down->id, [&](PointerInputChange& c, float o) { c.Consume(); over = o; });
                    if (!drag) continue;
                    horizontal += over;
                    co_await HorizontalDrag(s, drag->id, [&](PointerInputChange& c) { horizontal += c.PositionChange().x; c.Consume(); });
                }
            }), Alignment::TopStart(), false, [] {});
            // A scoped onPress learns how the press ended.
            Box(PointerInput(SizeMod(Modifier{}, Dp(300), Dp(100)), rt::Keys(2), [&](PointerInputScope& s) {
                TapCallbacks cb;
                cb.onPressScoped = [&](PressGestureScope& p, Offset) -> rt::Task<> { released = (co_await p.TryAwaitRelease()) ? 1 : 0; };
                return DetectTapGestures(s, cb);
            }), Alignment::TopStart(), false, [] {});
        });
    });
    h.Drag({ 20, 50 }, { 100, 30 }, 10, 10);
    const float slop = h.owner.Config().touchSlop;
    CHECK(std::abs(horizontal - (100 - slop)) < 1.0f);
    h.Touch(PointerEventType::Press, { 50, 150 }, true);
    CHECK_EQ(released, -1);   // still pressed
    h.t += 30;
    h.Touch(PointerEventType::Release, { 50, 150 }, false);
    CHECK_EQ(released, 1);
}

TEST(TargetedSnapFlingReportsTheDistanceLeft) {
    struct Provider final : SnapLayoutInfoProvider {
        float CalculateApproachOffset(float, float) override { return 0; }
        float CalculateSnapOffset(float) override { return 100; }
    };
    auto fling = TargetedSnapFlingBehavior(std::make_shared<Provider>(), [] { return std::make_shared<anim::SplineDecay>(1.0f); });
    float scrolled = 0;
    std::vector<float> reports;
    auto job = rt::Launch(rt::Dispatcher::Main(), [](TargetedFlingBehavior f, float* scrolled, std::vector<float>* reports) -> rt::Task<> {
        co_await f([scrolled](float d) { *scrolled += d; return d; }, 0, [reports](float r) { reports->push_back(r); });
    }(fling, &scrolled, &reports));
    std::int64_t t = 0;
    for (int i = 0; i < 200; ++i) rt::Dispatcher::Main().RunFrame(t += 16'000'000);
    CHECK(std::abs(scrolled - 100) < 0.5f);
    CHECK(!reports.empty());
    CHECK(std::abs(reports.back()) < 0.5f);
    // The distance left only shrinks during the snap.
    bool shrinking = true;
    for (std::size_t i = 1; i < reports.size(); ++i) if (reports[i] > reports[i - 1] + 0.01f && reports[i - 1] != 0) shrinking = false;
    CHECK(shrinking);
}
