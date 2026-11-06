#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Snapping.h"

#include <cmath>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t frameNs = 0;
        Harness() { owner.SetDensity({ 1, 1 }); owner.Config().touchSlop = 8; }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { rt::Composition::RecomposeAll(); owner.MeasureAndLayout({ 100, 100 }); }
        void Frames(int n) { for (int i = 0; i < n; ++i) { frameNs += 16'000'000; rt::Dispatcher::Main().RunFrame(frameNs); Pump(); } }
        void Touch(PointerEventType type, float y, bool down, std::int64_t t) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.pointers.push_back({ 1, { 50, y }, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            rt::Dispatcher::Main().RunPending();
            Pump();
        }
        // A vertical drag of dy in `steps` 10 ms moves; `holdMs` still before releasing.
        void Drag(float y0, float dy, int steps, std::int64_t holdMs, std::int64_t& t) {
            Touch(PointerEventType::Press, y0, true, t);
            for (int i = 1; i <= steps; ++i) Touch(PointerEventType::Move, y0 + dy * float(i) / float(steps), true, t += 10);
            if (holdMs) Touch(PointerEventType::Move, y0 + dy, true, t += holdMs);
            Touch(PointerEventType::Release, y0 + dy, false, t += 1);
        }
    };

    // The item whose start is closest to `position` sits exactly there.
    bool SnappedAt(const LazyListState& state, int position) {
        for (const auto& it : state.LayoutInfo().visibleItemsInfo)
            if (it.offset == position) return true;
        return false;
    }
}

TEST(SnapPositionPlacesItemsInTheViewport) {
    CHECK_EQ(SnapPosition::Start().Position(100, 30, 0, 0), 0);
    CHECK_EQ(SnapPosition::Center().Position(100, 30, 0, 0), 35);
    CHECK_EQ(SnapPosition::End().Position(100, 30, 0, 0), 70);
    CHECK_EQ(SnapPosition::End().Position(100, 30, 10, 20), 40);
}

TEST(LazyColumnSnapFlingRestsOnAnItem) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        LazyListOptions o;
        o.flingBehavior = RememberSnapFlingBehavior(state, SnapPosition::Center());
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [](LazyListScope& s) {
            s.Items(100, [](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); });
        }, std::move(o));
    });
    std::int64_t t = 0;
    // A slow drag that leaves an item off-center: it settles to the closest one, centered.
    h.Drag(80, -50, 5, 300, t);
    h.Frames(80);
    CHECK(!state.IsScrollInProgress());
    CHECK(SnappedAt(state, 35));
    const int afterSlow = state.FirstVisibleItemIndex();
    // A flick travels several items and still rests centered.
    h.Drag(90, -60, 3, 0, t);
    h.Frames(200);
    CHECK(!state.IsScrollInProgress());
    CHECK(SnappedAt(state, 35));
    CHECK(state.FirstVisibleItemIndex() > afterSlow + 2);
}

TEST(LazyGridSnapFlingRestsOnALineStart) {
    Harness h;
    LazyGridState state;
    h.Run([&] {
        LazyGridOptions o;
        o.flingBehavior = RememberSnapFlingBehavior(state, SnapPosition::Start());
        LazyVerticalGrid(FillMax(Modifier{}, Axis::Both), GridCells::Fixed(2), state, [](LazyGridScope& s) {
            s.Items(200, [](int) { Box(SizeMod(Modifier{}, Dp(50), Dp(40))); });
        }, std::move(o));
    });
    std::int64_t t = 0;
    h.Drag(90, -70, 7, 0, t);
    h.Frames(200);
    CHECK(!state.IsScrollInProgress());
    CHECK(state.FirstVisibleItemIndex() > 0);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 0);
}
