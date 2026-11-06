#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/ui/VelocityTracker.h"

#include <cmath>

#include <string>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) {
            composition.SetContent(std::move(content));
            composition.Recompose();
            owner.MeasureAndLayout({ 100, 100 });
        }
        void Touch(PointerEventType type, float x, float y, bool down, std::int64_t t) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.pointers.push_back({ 1, { x, y }, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            rt::Dispatcher::Main().RunPending();
        }
    };

    rt::Task<> LogPasses(PointerInputScope& s, std::string name, std::vector<std::string>& log, bool consumeMain) {
        for (;;) {
            auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
            log.push_back(name + ":I");
            auto m = co_await s.AwaitPointerEvent(PointerEventPass::Main);
            log.push_back(name + ":M" + (m->changes[0].IsConsumed() ? "c" : ""));
            if (consumeMain) m->changes[0].Consume();
            auto f = co_await s.AwaitPointerEvent(PointerEventPass::Final);
            log.push_back(name + ":F");
            (void)e; (void)f;
        }
    }
}

TEST(PointerPassesRunOuterInnerOuter) {
    Harness h;
    std::vector<std::string> log;
    h.Run([&] {
        Box(PointerInput(SizeMod(Modifier{}, Dp(100), Dp(100)), rt::Keys(1),
                         [&](PointerInputScope& s) { return LogPasses(s, "outer", log, false); }),
            Alignment::TopStart(), false, [&] {
            Box(PointerInput(SizeMod(Modifier{}, Dp(50), Dp(50)), rt::Keys(1),
                             [&](PointerInputScope& s) { return LogPasses(s, "inner", log, true); }));
        });
    });
    h.Touch(PointerEventType::Press, 10, 10, true, 0);
    std::vector<std::string> expect{ "outer:I", "inner:I", "inner:M", "outer:Mc", "outer:F", "inner:F" };
    CHECK(log == expect);
    // Outside the inner box only the outer handler sees the press.
    log.clear();
    h.Touch(PointerEventType::Release, 10, 10, false, 10);
    h.Touch(PointerEventType::Press, 80, 80, true, 20);
    std::vector<std::string> outerOnly{ "outer:I", "outer:M", "outer:F" };
    CHECK(std::vector<std::string>(log.end() - 3, log.end()) == outerOnly);
}

TEST(ClickableTapsAndCancelsWhenMovedOut) {
    Harness h;
    int clicks = 0;
    std::vector<bool> pressed;
    h.Run([&] {
        // Not the root's direct child: the root hands its children the window size as fixed.
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Box(Clickable(SizeMod(Modifier{}, Dp(60), Dp(60)), [&] { ++clicks; }, [&](bool p) { pressed.push_back(p); }));
        });
    });
    h.Touch(PointerEventType::Press, 30, 30, true, 0);
    h.Touch(PointerEventType::Release, 30, 30, false, 50);
    CHECK_EQ(clicks, 1);
    CHECK(pressed == std::vector<bool>({ true, false }));
    h.Touch(PointerEventType::Press, 30, 30, true, 1000);
    h.Touch(PointerEventType::Move, 90, 30, true, 1050);   // out of bounds: cancelled
    h.Touch(PointerEventType::Release, 90, 30, false, 1100);
    CHECK_EQ(clicks, 1);
}

TEST(TouchTargetExpandsSmallNodes) {
    Harness h;
    int clicks = 0;
    h.Run([&] {
        Box(SizeMod(Modifier{}, Dp(100), Dp(100)), Alignment::Center(), false, [&] {
            Box(Clickable(SizeMod(Modifier{}, Dp(20), Dp(20)), [&] { ++clicks; }));
        });
    });
    // The 20x20 box sits at 40..60; 48dp minimum target reaches 26..74.
    h.Touch(PointerEventType::Press, 30, 50, true, 0);
    h.Touch(PointerEventType::Release, 30, 50, false, 30);
    CHECK_EQ(clicks, 1);
}

TEST(DragCrossesSlopThenFollowsAndReportsVelocity) {
    Harness h;
    h.owner.Config().touchSlop = 8;
    Offset total;
    bool started = false, ended = false;
    VelocityTracker tracker;
    Offset velocity;
    h.Run([&] {
        Box(PointerInput(FillMax(Modifier{}, Axis::Both), rt::Keys(1), [&](PointerInputScope& s) {
            DragCallbacks cb;
            cb.onDragStart = [&](const PointerInputChange&, const PointerInputChange&, Offset) { started = true; };
            cb.onDrag = [&](const PointerInputChange& c, Offset d) { total = total + d; tracker.AddPointerInputChange(c); };
            cb.onDragEnd = [&](const PointerInputChange& up) { ended = true; tracker.AddPointerInputChange(up); velocity = tracker.CalculateVelocity(); };
            return DetectDragGestures(s, std::nullopt, cb);
        }));
    });
    h.Touch(PointerEventType::Press, 10, 50, true, 0);
    h.Touch(PointerEventType::Move, 15, 50, true, 10);     // 5 px: under slop
    CHECK(!started);
    for (int i = 1; i <= 8; ++i) h.Touch(PointerEventType::Move, 15.0f + 10 * i, 50, true, 10 + 10 * i);
    CHECK(started);
    h.Touch(PointerEventType::Release, 95, 50, false, 95);
    CHECK(ended);
    // Total drag = movement after the slop: 85 px moved, 8 px of slop removed.
    CHECK(std::abs(total.x - 77.0f) < 0.01f);
    CHECK(std::abs(total.y) < 0.01f);
    // 10 px every 10 ms = 1000 px/s.
    CHECK(std::abs(velocity.x - 1000.0f) < 1.0f);
}
