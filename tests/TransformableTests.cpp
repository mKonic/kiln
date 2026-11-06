#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Transformable.h"
#include "kiln/ui/Trackpad.h"

#include <cmath>

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
        void Send(RawPointerEvent e) {
            owner.ProcessPointer(e);
            rt::Dispatcher::Main().RunPending();
        }
        // One unpressed mouse pointer at (50, 50), as a reinterpreted trackpad gesture.
        void Cursor(PointerEventType type, std::int64_t t, Offset pan = {}, float scale = 1, std::uint32_t meta = 0,
                    Offset wheel = {}) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.metaState = meta;
            RawPointer p{ 1, { 50, 50 }, false, PointerType::Mouse, wheel };
            p.panOffset = pan;
            p.scaleFactor = scale;
            e.pointers.push_back(p);
            Send(e);
        }
    };

    struct Totals {
        float zoom = 1, rotation = 0;
        Offset pan{};
        int calls = 0;
    };

    void Content(Harness& h, Totals& t, TransformableState*& state) {
        h.Run([&] {
            auto& s = RememberTransformableState([&](Offset, float z, Offset p, float r) {
                t.zoom *= z;
                t.pan = t.pan + p;
                t.rotation += r;
                ++t.calls;
            });
            state = &s;
            Box(Transformable(SizeMod(Modifier{}, Dp(100), Dp(100)), s));
        });
    }
}

TEST(TransformableZoomsWithTwoFingers) {
    Harness h;
    Totals t;
    TransformableState* state = nullptr;
    Content(h, t, state);
    auto two = [&](PointerEventType type, Offset a, Offset b, bool down, std::int64_t at) {
        RawPointerEvent e;
        e.type = type;
        e.uptimeMillis = at;
        e.pointers.push_back({ 1, a, down, PointerType::Touch, {} });
        e.pointers.push_back({ 2, b, down, PointerType::Touch, {} });
        h.Send(e);
    };
    std::int64_t at = 0;
    two(PointerEventType::Press, { 40, 50 }, { 60, 50 }, true, at);
    for (int i = 1; i <= 10; ++i) two(PointerEventType::Move, { 40 - float(i) * 2, 50 }, { 60 + float(i) * 2, 50 }, true, at += 10);
    CHECK(t.zoom > 1.5f);
    CHECK(state->IsTransformInProgress());
    two(PointerEventType::Release, { 20, 50 }, { 80, 50 }, false, at += 10);
    CHECK(!state->IsTransformInProgress());
}

TEST(TransformableTrackpadPinchPanAndCtrlScroll) {
    Harness h;
    Totals t;
    TransformableState* state = nullptr;
    Content(h, t, state);
    // Pinch: the scale factors multiply.
    h.Cursor(PointerEventType::ScaleStart, 0, {}, 1.0f);
    h.Cursor(PointerEventType::ScaleChange, 10, {}, 1.25f);
    h.Cursor(PointerEventType::ScaleChange, 20, {}, 1.2f);
    CHECK(std::abs(t.zoom - 1.5f) < 1e-4f);
    CHECK(state->IsTransformInProgress());
    h.Cursor(PointerEventType::ScaleEnd, 30, {}, 1.0f);
    CHECK(!state->IsTransformInProgress());
    // Two-finger swipe: the pan is the scroll distance, negated.
    h.Cursor(PointerEventType::PanStart, 100, { 0, 0 });
    h.Cursor(PointerEventType::PanMove, 110, { 5, -3 });
    CHECK(std::abs(t.pan.x + 5) < 1e-4f && std::abs(t.pan.y - 3) < 1e-4f);
    h.Cursor(PointerEventType::PanEnd, 120, { 0, 0 });
    // Ctrl + wheel up one notch: 2^(64 / 545) zoom in.
    const float before = t.zoom;
    h.Cursor(PointerEventType::Scroll, 200, {}, 1, 0x1000, { 0, -1 });
    CHECK(std::abs(t.zoom / before - std::pow(2.0f, 64.0f / 545.0f)) < 1e-4f);
    // Without Ctrl the wheel does nothing here.
    const int calls = t.calls;
    h.Cursor(PointerEventType::Scroll, 300, {}, 1, 0, { 0, -1 });
    CHECK_EQ(t.calls, calls);
}

TEST(TransformablePriorities) {
    Totals t;
    TransformableState state([&](Offset, float z, Offset, float) { t.zoom *= z; ++t.calls; });
    // A user gesture holds the state: a programmatic zoom does not start.
    {
        TransformSession user(state, MutatePriority::UserInput, {});
        ZoomBy(state, 2);
        CHECK_EQ(t.calls, 0);
        CHECK(state.IsTransformInProgress());
    }
    CHECK(!state.IsTransformInProgress());
    ZoomBy(state, 2);
    CHECK_EQ(t.calls, 1);
    // A gesture interrupts a running programmatic transformation.
    bool stopped = false;
    TransformSession animation(state, MutatePriority::Default, [&] { stopped = true; });
    TransformSession user(state, MutatePriority::UserInput, {});
    CHECK(stopped);
    CHECK(!animation.Current());
    CHECK(user.Current());
}

TEST(TrackpadPinchBecomesOneCursorPointer) {
    TrackpadGestureTracker tracker;
    using T = TrackpadGestureTracker;
    auto event = [](std::vector<Offset> at) {
        RawPointerEvent e;
        std::int64_t id = 0;
        for (Offset p : at) e.pointers.push_back({ id++, p, true, PointerType::Touch, {} });
        return e;
    };
    // Hovering first gives the cursor position.
    RawPointerEvent hover = event({ { 30, 40 } });
    tracker.Process(hover, T::HoverMove, 0, nullptr, 0);
    CHECK(hover.type == PointerEventType::Move);
    const float scale[] = { 0, 0, 1.0f };
    RawPointerEvent down = event({ { 100, 100 } });
    tracker.Process(down, T::Down, T::Pinch, scale, 3);
    CHECK(down.type == PointerEventType::ScaleStart);
    CHECK_EQ(down.pointers.size(), std::size_t(1));
    CHECK(!down.pointers[0].down);
    CHECK(down.pointers[0].type == PointerType::Mouse);
    CHECK(down.pointers[0].position == (Offset{ 30, 40 }));
    // The second finger: the cursor is their midpoint.
    RawPointerEvent second = event({ { 100, 100 }, { 200, 120 } });
    tracker.Process(second, T::PointerDown, T::Pinch, scale, 3);
    CHECK(second.type == PointerEventType::ScaleChange);
    CHECK(second.pointers[0].position == (Offset{ 150, 110 }));
    const float grow[] = { 0, 0, 1.1f };
    RawPointerEvent move = event({ { 90, 100 }, { 210, 120 } });
    tracker.Process(move, T::Move, T::Pinch, grow, 3);
    CHECK(move.type == PointerEventType::ScaleChange);
    CHECK(std::abs(move.pointers[0].scaleFactor - 1.1f) < 1e-6f);
    CHECK(move.pointers[0].position == (Offset{ 150, 110 }));
    RawPointerEvent up = event({ { 90, 100 } });
    tracker.Process(up, T::Up, T::Pinch, scale, 3);
    CHECK(up.type == PointerEventType::ScaleEnd);
    // A swipe holds Pan until the fingers lift, even on events without the classification.
    const float scroll[] = { 4, 2, 0 };
    RawPointerEvent swipe = event({ { 10, 10 } });
    tracker.Process(swipe, T::Down, T::TwoFingerSwipe, scroll, 3);
    CHECK(swipe.type == PointerEventType::PanStart);
    RawPointerEvent swipeMove = event({ { 12, 10 } });
    tracker.Process(swipeMove, T::Move, 0, scroll, 3);
    CHECK(swipeMove.type == PointerEventType::PanMove);
    CHECK(swipeMove.pointers[0].panOffset == (Offset{ 4, 2 }));
    CHECK(swipeMove.pointers[0].position == (Offset{ 10, 10 }));
    RawPointerEvent swipeUp = event({ { 12, 10 } });
    tracker.Process(swipeUp, T::Up, 0, scroll, 3);
    CHECK(swipeUp.type == PointerEventType::PanEnd);
    // A plain touch afterwards is a press.
    RawPointerEvent touch = event({ { 5, 5 } });
    tracker.Process(touch, T::Down, 0, nullptr, 0);
    CHECK(touch.type == PointerEventType::Press);
    CHECK(touch.pointers[0].down);
}
