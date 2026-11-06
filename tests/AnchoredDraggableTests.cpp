#include "Check.h"

#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Compose.h"

#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    enum class Pos { Start, Mid, End };

    struct Harness {
        Owner owner;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        rt::Composition composition{ owner.Root(), d };
        std::int64_t frameNs = 0;
        Harness() { owner.SetDensity({ 1, 1 }); owner.Config().touchSlop = 8; }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { composition.Recompose(); owner.MeasureAndLayout({ 200, 100 }); }
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
        void Content(AnchoredDraggableState<Pos>& state) {
            Run([&] {
                Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
                    Box(AnchoredDraggable(SizeMod(Modifier{}, Dp(200), Dp(100)), state, Orientation::Horizontal));
                });
            });
        }
        // A horizontal drag of `dx` in `steps` moves `stepMs` apart, then a release `holdMs` later.
        void Drag(float x0, float dx, int steps, std::int64_t stepMs, std::int64_t holdMs, std::int64_t& t) {
            Touch(PointerEventType::Press, x0, 50, true, t);
            for (int i = 1; i <= steps; ++i) Touch(PointerEventType::Move, x0 + dx * float(i) / float(steps), 50, true, t += stepMs);
            if (holdMs > 0) Touch(PointerEventType::Move, x0 + dx, 50, true, t += holdMs);
            Touch(PointerEventType::Release, x0 + dx, 50, false, t += 1);
        }
    };

    DraggableAnchors<Pos> StartEnd() { return { { Pos::Start, 0 }, { Pos::End, 100 } }; }
}

TEST(AnchorsFindClosestAndDirectional) {
    DraggableAnchors<Pos> a{ { Pos::Start, 0 }, { Pos::Mid, 50 }, { Pos::End, 100 } };
    CHECK(a.ClosestAnchor(20) == Pos::Start);
    CHECK(a.ClosestAnchor(30) == Pos::Mid);
    CHECK(a.ClosestAnchor(20, true) == Pos::Mid);
    CHECK(a.ClosestAnchor(70, false) == Pos::Mid);
    CHECK(a.ClosestAnchor(100, true) == Pos::End);
    CHECK_EQ(a.MinPosition(), 0.0f);
    CHECK_EQ(a.MaxPosition(), 100.0f);
    CHECK(std::isnan(DraggableAnchors<Pos>{}.MinPosition()));
    CHECK(std::isnan(a.PositionOf(Pos(7))));
}

TEST(AnchoredDragFollowsTheFingerAndSettlesPastHalf) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start, StartEnd());
    h.Content(state);
    CHECK_EQ(state.Offset(), 0.0f);
    std::int64_t t = 0;
    // 70 moved, 8 of it slop; held still before release, so no fling velocity.
    h.Touch(PointerEventType::Press, 10, 50, true, t);
    for (int i = 1; i <= 7; ++i) h.Touch(PointerEventType::Move, 10.0f + 10 * i, 50, true, t += 10);
    CHECK_EQ(state.Offset(), 62.0f);
    CHECK(state.CurrentValue() == Pos::End);   // past half way
    CHECK(state.SettledValue() == Pos::Start);
    CHECK(std::abs(state.Progress(Pos::Start, Pos::End) - 0.62f) < 1e-5f);
    h.Touch(PointerEventType::Move, 80, 50, true, t += 300);
    h.Touch(PointerEventType::Release, 80, 50, false, t += 1);
    h.Frames(30);
    CHECK_EQ(state.Offset(), 100.0f);
    CHECK(state.SettledValue() == Pos::End);
    // Short of half way it goes back.
    h.Drag(150, -40, 4, 10, 300, t);   // 32 past slop: offset 68
    h.Frames(30);
    CHECK_EQ(state.Offset(), 100.0f);
    CHECK(state.SettledValue() == Pos::End);
    // Dragging beyond the last anchor is clamped.
    h.Drag(10, 60, 6, 10, 300, t);
    CHECK_EQ(state.Offset(), 100.0f);
}

TEST(AnchoredFlickPastVelocityThresholdGoesToTheNextAnchor) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start, StartEnd());
    h.Content(state);
    std::int64_t t = 0;
    h.Drag(10, 30, 3, 10, 0, t);   // ~1000 px/s, only 22 px past slop
    CHECK_EQ(state.Offset(), 22.0f);
    h.Frames(30);
    CHECK_EQ(state.Offset(), 100.0f);
    CHECK(state.SettledValue() == Pos::End);
}

TEST(AnchoredAnimateToSnapToAndProgress) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start, StartEnd());
    h.Content(state);
    rt::Job job = rt::Launch(h.d, state.AnimateTo(Pos::End));
    CHECK(state.IsAnimationRunning());
    CHECK(state.TargetValue() == Pos::End);
    h.Frames(8);
    const float p = state.Progress(Pos::Start, Pos::End);
    CHECK(p > 0 && p < 1);
    CHECK(state.SettledValue() == Pos::Start);
    h.Frames(30);
    CHECK(!state.IsAnimationRunning());
    CHECK_EQ(state.Offset(), 100.0f);
    CHECK(state.SettledValue() == Pos::End);
    CHECK_EQ(state.Progress(Pos::Start, Pos::End), 1.0f);
    job = rt::Launch(h.d, state.SnapTo(Pos::Start));
    h.d.RunPending();
    CHECK_EQ(state.Offset(), 0.0f);
    CHECK(state.CurrentValue() == Pos::Start);
}

TEST(AnchoredAnchorChangeRestartsTheAnimation) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start, StartEnd());
    h.Content(state);
    // Idle: new anchors snap to the closest.
    state.UpdateAnchors({ { Pos::Start, 10 }, { Pos::End, 120 } });
    CHECK_EQ(state.Offset(), 10.0f);
    rt::Job job = rt::Launch(h.d, state.AnimateTo(Pos::End));
    h.Frames(5);
    state.UpdateAnchors({ { Pos::Start, 10 }, { Pos::End, 180 } }, Pos::End);
    h.Frames(40);
    CHECK_EQ(state.Offset(), 180.0f);
    CHECK(state.SettledValue() == Pos::End);
}

TEST(AnchoredVetoedValueStaysPut) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start, StartEnd(), [](const Pos& p) { return p != Pos::End; });
    h.Content(state);
    std::int64_t t = 0;
    h.Drag(10, 30, 3, 10, 0, t);   // a flick towards End
    CHECK(state.CurrentValue() == Pos::Start);
    h.Frames(30);
    CHECK_EQ(state.Offset(), 0.0f);
    CHECK(state.SettledValue() == Pos::Start);
}

TEST(AnchoredAnimateToWithDecayStopsAtTheTarget) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start, StartEnd());
    h.Content(state);
    float consumed = -1;
    auto run = [](AnchoredDraggableState<Pos>& s, float& out) -> rt::Task<> { out = co_await s.AnimateToWithDecay(Pos::End, 2000); };
    rt::Job job = rt::Launch(h.d, run(state, consumed));
    h.Frames(60);
    CHECK_EQ(state.Offset(), 100.0f);
    CHECK(state.SettledValue() == Pos::End);
    // Exponential decay from 2000 px/s projects ~1000 px: it reaches 100 with velocity left.
    CHECK(consumed > 0 && consumed < 2000);
}

namespace {
    struct CaptureParams { LayoutNode** out; bool operator==(const CaptureParams&) const = default; };
    struct CaptureNode final : ModifierNode {
        CaptureParams params;
        explicit CaptureNode(CaptureParams p) : params(p) {}
        void OnAttach() override { *params.out = Owner(); }
        void OnParamsChanged() { *params.out = Owner(); }
    };
    Modifier Capture(Modifier m, LayoutNode*& out) { return m.With<CaptureNode>(CaptureParams{ &out }); }
}

TEST(AnchorsFromOnSizeChangedAndAPlacementOffset) {
    Harness h;
    AnchoredDraggableState<Pos> state(Pos::Start);
    LayoutNode* thumb = nullptr;
    int compositions = 0;
    h.Run([&] {
        ++compositions;
        Box(OnSizeChanged(SizeMod(Modifier{}, Dp(200), Dp(40)), [&](IntSize s) {
                state.UpdateAnchors({ { Pos::Start, 0 }, { Pos::End, float(s.width - 40) } });
            }), Alignment::TopStart(), false, [&] {
            Box(SizeMod(OffsetMod(Modifier{}, [&](const Density&) {
                    return IntOffset{ int(std::lround(state.Offset())), 0 };
                }), Dp(40), Dp(40)), Alignment::TopStart(), false, [&] {
                Box(Capture(FillMax(Modifier{}, Axis::Both), thumb));
            });
        });
    });
    h.Pump();
    CHECK_EQ(state.Offset(), 0.0f);
    CHECK_EQ(thumb->PositionInRoot().x, 0.0f);
    const int before = compositions;
    rt::Job job = rt::Launch(h.d, state.AnimateTo(Pos::End));
    h.Frames(40);
    CHECK_EQ(state.Offset(), 160.0f);
    CHECK_EQ(thumb->PositionInRoot().x, 160.0f);
    CHECK_EQ(compositions, before);   // only placement followed the offset
}
