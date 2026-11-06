#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Pager.h"

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
        void Touch(PointerEventType type, float x, float y, bool down, std::int64_t t) {
            RawPointerEvent e;
            e.type = type;
            e.uptimeMillis = t;
            e.pointers.push_back({ 1, { x, y }, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            rt::Dispatcher::Main().RunPending();
            Pump();
        }
        // A horizontal drag of dx in `steps` 10 ms moves; `holdMs` still before releasing.
        void Drag(float x0, float dx, int steps, std::int64_t holdMs, std::int64_t& t) {
            Touch(PointerEventType::Press, x0, 50, true, t);
            for (int i = 1; i <= steps; ++i) Touch(PointerEventType::Move, x0 + dx * float(i) / float(steps), 50, true, t += 10);
            if (holdMs) Touch(PointerEventType::Move, x0 + dx, 50, true, t += holdMs);
            Touch(PointerEventType::Release, x0 + dx, 50, false, t += 1);
        }
    };
}

TEST(PagerSettlesOnPagesByDistanceAndVelocity) {
    Harness h;
    PagerState state(0, 0, [] { return 5; });
    std::vector<int> composedPages;
    h.Run([&] {
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [&](int page) {
            composedPages.push_back(page);
            Box(FillMax(Modifier{}, Axis::Both));
        });
    });
    CHECK_EQ(state.CurrentPage(), 0);
    CHECK_EQ(state.PageSize(), 100);
    std::int64_t t = 0;
    // Slowly past half a page: the next page.
    h.Drag(90, -70, 7, 300, t);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 1);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    CHECK_EQ(state.SettledPage(), 1);
    CHECK(!state.IsScrollInProgress());
    // Slowly less than half: back.
    h.Drag(90, -30, 3, 300, t);
    CHECK(state.CurrentPageOffsetFraction() > 0.2f);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 1);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    // A flick (1000 px/s) of the same distance: the next page.
    h.Drag(90, -30, 3, 0, t);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 2);
    // And back with a flick the other way.
    h.Drag(10, 30, 3, 0, t);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 1);
}

TEST(PagerProgrammaticScrolling) {
    Harness h;
    PagerState state(0, 0, [] { return 20; });
    h.Run([&] {
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); });
    });
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToPage(10));
    h.Frames(2);
    CHECK(state.IsScrollInProgress());
    CHECK_EQ(state.TargetPage(), 10);
    CHECK_EQ(state.SettledPage(), 0);
    h.Frames(120);
    CHECK_EQ(state.CurrentPage(), 10);
    CHECK_EQ(state.SettledPage(), 10);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    state.ScrollToPage(2, 0.25f);
    h.Pump();
    CHECK_EQ(state.CurrentPage(), 2);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.25f);
    state.ScrollToPage(2, -0.25f);
    h.Pump();
    CHECK_EQ(state.CurrentPage(), 2);
    CHECK_EQ(state.CurrentPageOffsetFraction(), -0.25f);
    CHECK_EQ(state.FirstVisibleItemIndex(), 1);
}

TEST(PagerFixedPageSizeAndSpacing) {
    Harness h;
    PagerState state(0, 0, [] { return 10; });
    h.Run([&] {
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(60));
        o.pageSpacing = Dp(10);
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); }, o);
    });
    CHECK_EQ(state.PageSize(), 60);
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToPage(3));
    h.Frames(120);
    CHECK_EQ(state.CurrentPage(), 3);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 0);
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);
}
