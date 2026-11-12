// draggable2D, scrollable2D and rememberScrollableState: drags in any direction after the slop,
// flings that decay on both axes.
#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Scrollable2D.h"

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
