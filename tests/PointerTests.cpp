#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/ui/VelocityTracker.h"

#include <cmath>
#include <cstdio>

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

namespace {
    // Taps (down + up) with mouse type so no touch-target expansion applies.
    void Tap(Harness& h, float x, float y, std::int64_t t) {
        auto send = [&](PointerEventType type, bool down, std::int64_t at) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = at;
            e.buttons = down ? 1u : 0u;
            e.pointers.push_back({ 1, { x, y }, down, PointerType::Mouse, {} });
            h.owner.ProcessPointer(e);
            rt::Dispatcher::Main().RunPending();
        };
        send(PointerEventType::Press, true, t);
        send(PointerEventType::Release, false, t + 10);
    }
}

TEST(HitTestFollowsLayerTranslation) {
    Harness h;
    int clicks = 0;
    h.Run([&] {
        GraphicsLayerParams p;
        p.translationX = 40;
        Box(GraphicsLayer(Modifier{}, p), Alignment::TopStart(), false, [&] {
            Box(Clickable(SizeMod(Modifier{}, Dp(20), Dp(20)), [&] { ++clicks; }));
        });
    });
    Tap(h, 10, 10, 1000);          // where it would be without the layer
    CHECK_EQ(clicks, 0);
    Tap(h, 50, 10, 2000);          // where it is drawn
    CHECK_EQ(clicks, 1);
}

TEST(HitTestFollowsLayerRotation) {
    Harness h;
    int clicks = 0;
    h.Run([&] {
        GraphicsLayerParams p;
        p.rotationZ = 90;           // a 60x10 bar at (20,45) turned upright about its centre (50,50)
        Box(Padding(Modifier{}, PaddingValues{ Dp(20), Dp(45), Dp(0), Dp(0) }), Alignment::TopStart(), false, [&] {
            Box(Clickable(SizeMod(GraphicsLayer(Modifier{}, p), Dp(60), Dp(10)), [&] { ++clicks; }));
        });
    });
    Tap(h, 25, 50, 1000);          // inside the unrotated bar, outside the rotated one
    CHECK_EQ(clicks, 0);
    Tap(h, 50, 25, 2000);          // inside the rotated bar
    CHECK_EQ(clicks, 1);
}

#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"

TEST(ClickableReportsPressInteractions) {
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    std::vector<std::string> log;
    source->Subscribe([&](const Interaction& i) {
        if (i.kind == Interaction::Kind::Press) log.push_back(i.phase == Interaction::Phase::Start ? "press" : i.phase == Interaction::Phase::End ? "release" : "cancel");
    });
    int clicks = 0;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            ClickableOptions o;
            o.interactionSource = source;
            Box(Clickable(SizeMod(Modifier{}, Dp(50), Dp(50)), [&] { ++clicks; }, o));
        });
    });
    h.Touch(PointerEventType::Press, 10, 10, true, 0);
    CHECK(source->IsPressed());
    h.Touch(PointerEventType::Release, 10, 10, false, 50);
    CHECK(!source->IsPressed());
    CHECK_EQ(clicks, 1);
    CHECK_EQ(log.size(), std::size_t(2));
    CHECK_EQ(log[1], std::string("release"));
    // Dragged off: cancel, no click.
    h.Touch(PointerEventType::Press, 10, 10, true, 1000);
    h.Touch(PointerEventType::Move, 90, 90, true, 1050);
    h.Touch(PointerEventType::Release, 90, 90, false, 1100);
    CHECK_EQ(clicks, 1);
    CHECK_EQ(log.back(), std::string("cancel"));
}

TEST(PressInScrollableContainerIsDelayed) {
    ScrollState state;   // outlives the composition
    Harness h;
    auto source = std::make_shared<MutableInteractionSource>();
    h.Run([&] {
        Column(VerticalScroll(FillMax(Modifier{}, Axis::Both), state), [&] {
            ClickableOptions o;
            o.interactionSource = source;
            Box(Clickable(SizeMod(Modifier{}, Dp(50), Dp(50)), [] {}, o));
            Box(SizeMod(Modifier{}, Dp(50), Dp(300)));
        });
    });
    h.Touch(PointerEventType::Press, 10, 10, true, 0);
    CHECK(!source->IsPressed());                       // TapTimeout not yet passed
    h.Touch(PointerEventType::Release, 10, 10, false, 20);
    CHECK(!source->IsPressed());                       // a quick tap shows a brief press, then releases
}

TEST(ToggleableExposesStateAndFlips) {
    Harness h;
    rt::State<bool> on(false);
    h.Run([&] {
        ClickableOptions o;
        o.role = Role::Switch;
        Box(Toggleable(SizeMod(Modifier{}, Dp(50), Dp(50)), on.Get(), [&](bool v) { on.Set(v); }, o));
    });
    auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(int(tree.children.size()), 1);
    CHECK_EQ(*tree.children[0].config.toggleableState, 0);
    CHECK(tree.children[0].config.role == Role::Switch);
    h.Touch(PointerEventType::Press, 10, 10, true, 0);
    h.Touch(PointerEventType::Release, 10, 10, false, 50);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 100, 100 });
    CHECK(on.Peek());
    CHECK_EQ(*h.owner.BuildSemanticsTree().children[0].config.toggleableState, 1);
}

TEST(DisabledClickableKeepsSemanticsButIgnoresTaps) {
    Harness h;
    int clicks = 0;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            ClickableOptions o;
            o.enabled = false;
            o.onClickLabel = "Open";
            Box(Clickable(SizeMod(Modifier{}, Dp(50), Dp(50)), [&] { ++clicks; }, o));
        });
    });
    h.Touch(PointerEventType::Press, 10, 10, true, 0);
    h.Touch(PointerEventType::Release, 10, 10, false, 50);
    CHECK_EQ(clicks, 0);
    const auto tree = h.owner.BuildSemanticsTree();
    CHECK_EQ(int(tree.children.size()), 1);
    CHECK(tree.children[0].config.disabled);
    CHECK(tree.children[0].config.onClickLabel.has_value());
    if (tree.children[0].config.onClickLabel) CHECK_EQ(*tree.children[0].config.onClickLabel, std::string("Open"));
}

TEST(HoverEntersAndExitsAsTheMouseMoves) {
    Harness h;
    auto a = std::make_shared<MutableInteractionSource>(), b = std::make_shared<MutableInteractionSource>();
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Row(Modifier{}, Arrangement::Start(), AlignmentV::Top(), [&] {
                ClickableOptions oa; oa.interactionSource = a;
                ClickableOptions ob; ob.interactionSource = b;
                Box(Clickable(SizeMod(Modifier{}, Dp(40), Dp(40)), [] {}, oa));
                Box(Clickable(SizeMod(Modifier{}, Dp(40), Dp(40)), [] {}, ob));
            });
        });
    });
    auto mouse = [&](PointerEventType type, float x, std::int64_t t) {
        RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t;
        e.pointers.push_back({ 7, { x, 20 }, false, PointerType::Mouse, {} });
        h.owner.ProcessPointer(e);
        rt::Dispatcher::Main().RunPending();
    };
    mouse(PointerEventType::Enter, 10, 0);
    CHECK(a->IsHovered());
    CHECK(!b->IsHovered());
    mouse(PointerEventType::Move, 60, 10);   // into b
    CHECK(!a->IsHovered());
    CHECK(b->IsHovered());
    mouse(PointerEventType::Exit, 60, 20);   // leaves the window
    CHECK(!b->IsHovered());
}

#include "kiln/foundation/Draggable.h"

TEST(DraggableDeliversDeltasAfterSlopAndAVelocity) {
    Harness h;
    h.owner.Config().touchSlop = 8;
    float total = 0, velocity = 0;
    auto src = std::make_shared<MutableInteractionSource>();
    bool sawDrag = false;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            auto& state = RememberDraggableState([&](float d) { total += d; if (src->IsDragged()) sawDrag = true; });
            DraggableOptions o;
            o.interactionSource = src;
            o.onDragStopped = [&](float v) { velocity = v; };
            Box(Draggable(SizeMod(Modifier{}, Dp(100), Dp(100)), state, Orientation::Horizontal, o));
        });
    });
    std::int64_t t = 0;
    h.Touch(PointerEventType::Press, 10, 50, true, t);
    for (int i = 1; i <= 5; ++i) h.Touch(PointerEventType::Move, 10 + float(i) * 10, 50, true, t += 10);
    h.Touch(PointerEventType::Release, 60, 50, false, t += 10);
    CHECK_EQ(total, 42.0f);              // 50 moved, 8 of it slop
    CHECK(sawDrag);
    CHECK(!src->IsDragged());
    CHECK(velocity > 500);               // ~1000 px/s
    // Vertical movement does not drag a horizontal draggable.
    total = 0;
    h.Touch(PointerEventType::Press, 50, 10, true, t += 1000);
    for (int i = 1; i <= 5; ++i) h.Touch(PointerEventType::Move, 50, 10 + float(i) * 10, true, t += 10);
    h.Touch(PointerEventType::Release, 50, 60, false, t += 10);
    CHECK_EQ(total, 0.0f);
}

TEST(TransformGesturesReportZoomAndRotation) {
    Harness h;
    float zoom = 1, rotation = 0;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Box(PointerInput(SizeMod(Modifier{}, Dp(100), Dp(100)), rt::Keys(0), [&](PointerInputScope& s) {
                return DetectTransformGestures(s, false, [&](Offset, Offset, float z, float r) { zoom *= z; rotation += r; });
            }));
        });
    });
    auto two = [&](PointerEventType type, Offset a, Offset b, bool down, std::int64_t t) {
        RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = t;
        e.pointers.push_back({ 1, a, down, PointerType::Touch, {} });
        e.pointers.push_back({ 2, b, down, PointerType::Touch, {} });
        h.owner.ProcessPointer(e);
        rt::Dispatcher::Main().RunPending();
    };
    std::int64_t t = 0;
    two(PointerEventType::Press, { 40, 50 }, { 60, 50 }, true, t);
    for (int i = 1; i <= 10; ++i) two(PointerEventType::Move, { 40 - float(i) * 2, 50 }, { 60 + float(i) * 2, 50 }, true, t += 10);
    CHECK(zoom > 1.5f);                       // 20 apart -> 60 apart (slop eats the first steps)
    two(PointerEventType::Release, { 20, 50 }, { 80, 50 }, false, t += 10);
    // Quarter turn around (50, 50).
    zoom = 1; rotation = 0;
    two(PointerEventType::Press, { 20, 50 }, { 80, 50 }, true, t += 1000);
    for (int i = 1; i <= 18; ++i) {
        const float a = float(i) * 5 * 3.14159265f / 180;
        two(PointerEventType::Move, { 50 - 30 * std::cos(a), 50 - 30 * std::sin(a) }, { 50 + 30 * std::cos(a), 50 + 30 * std::sin(a) }, true, t += 10);
    }
    // 90 degrees minus what the slop absorbed (three 5-degree steps at a 30 px radius), clockwise.
    CHECK(std::abs(rotation - 75) < 0.5f);
}

TEST(CombinedClickableLongAndDoubleClick) {
    Harness h;
    int clicks = 0, longs = 0, doubles = 0;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            ClickableOptions o;
            o.onLongClick = [&] { ++longs; };
            o.onDoubleClick = [&] { ++doubles; };
            Box(Clickable(SizeMod(Modifier{}, Dp(60), Dp(60)), [&] { ++clicks; }, o));
        });
    });
    auto& d = rt::Dispatcher::Main();
    (void)d;
    h.Touch(PointerEventType::Press, 30, 30, true, 0);
    h.Touch(PointerEventType::Release, 30, 30, false, 50);
    h.Touch(PointerEventType::Press, 30, 30, true, 120);
    h.Touch(PointerEventType::Release, 30, 30, false, 170);
    CHECK_EQ(doubles, 1);
    CHECK_EQ(clicks, 0);
    const auto tree = h.owner.BuildSemanticsTree();
    CHECK(bool(tree.children[0].config.onLongClick));
}
