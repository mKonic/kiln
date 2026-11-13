#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/BringIntoView.h"
#include "kiln/foundation/Pager.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t frameNs = 0;
        Harness() { owner.SetDensity({ 1, 1 }); owner.Config().touchSlop = 8; }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
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
    // layoutInfo: pages 60 apart plus the 10 spacing, from the content start.
    const PagerLayoutInfo& info = state.LayoutInfo();
    CHECK_EQ(info.pageSize, 60);
    CHECK_EQ(info.pageSpacing, 10);
    CHECK(info.orientation == Orientation::Horizontal);
    CHECK(info.visiblePagesInfo.size() >= 2);
    CHECK_EQ(info.visiblePagesInfo[0].index, 0);
    CHECK_EQ(info.visiblePagesInfo[1].offset, 70);
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToPage(3));
    h.Frames(120);
    CHECK_EQ(state.CurrentPage(), 3);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 0);
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);
}

TEST(PagerPagesForAccessibility) {
    Harness h;
    PagerState state(0, 0, [] { return 3; });
    h.Run([&] {
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); });
    });
    kiln::Function<const SemanticsNode*(const SemanticsNode&)> find = [&](const SemanticsNode& n) -> const SemanticsNode* {
        if (n.config.pageRight) return &n;
        for (const auto& c : n.children) if (const auto* f = find(c)) return f;
        return nullptr;
    };
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    const SemanticsNode* n = find(root);
    CHECK(n != nullptr);
    if (!n) return;
    CHECK(!n->config.pageLeft());   // at the first page
    CHECK(n->config.pageRight());
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 1);
    CHECK(n->config.pageLeft());
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 0);
}

namespace {
    // x positions of the placed 100 x 100 nodes (the pages) under `n`.
    void PageXs(LayoutNode& n, std::multiset<int>& out) {
        for (LayoutNode* c : n.Children()) {
            if (!c->IsPlaced()) continue;
            if (c->Size() == IntSize{ 100, 100 } && c->Children().empty()) out.insert(int(c->PositionInRoot().x));
            PageXs(*c, out);
        }
    }
}

TEST(PagerComposesBeyondViewportPages) {
    Harness h;
    PagerState state(0, 0, [] { return 6; });
    std::map<int, int> compositions;
    h.Run([&] {
        PagerOptions o;
        o.beyondViewportPageCount = 1;
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [&](int page) {
            rt::Composition::Current()->Remember<int>([&, page] { return ++compositions[page]; });
            Box(FillMax(Modifier{}, Axis::Both));
        }, o);
    });
    CHECK(compositions == (std::map<int, int>{ { 0, 1 }, { 1, 1 } }));   // page 1 is ready to the right
    std::multiset<int> xs;
    PageXs(h.owner.Root(), xs);
    CHECK(xs == (std::multiset<int>{ 0, 100 }));
    CHECK_EQ(state.CurrentPage(), 0);
    state.ScrollToPage(1);
    h.Pump();
    // Page 1 was kept (not composed again); page 2 joins, page 0 stays as the one before.
    CHECK(compositions == (std::map<int, int>{ { 0, 1 }, { 1, 1 }, { 2, 1 } }));
    xs.clear();
    PageXs(h.owner.Root(), xs);
    CHECK(xs == (std::multiset<int>{ -100, 0, 100 }));
    CHECK_EQ(state.CurrentPage(), 1);
}

TEST(PagerCenterSnapPosition) {
    Harness h;
    PagerState state(0, 0, [] { return 6; });
    h.Run([&] {
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(60));
        o.snapPosition = SnapPosition::Center();
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); }, o);
    });
    // At the start page 0 cannot reach the center (20): it is the closest page, a third away.
    CHECK_EQ(state.CurrentPage(), 0);
    CHECK(std::abs(state.CurrentPageOffsetFraction() - 20.0f / 60.0f) < 1e-4f);
    state.ScrollToPage(2);
    h.Pump();
    CHECK_EQ(state.CurrentPage(), 2);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    CHECK_EQ(state.FirstVisiblePage(), 1);   // page 1 shows to the left of the centered page
    // A slow drag of 40 px (past the 30 px threshold) settles on the next page, centered.
    std::int64_t t = 0;
    h.Drag(90, -48, 6, 300, t);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 3);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
}

TEST(PagerStartsWithItsInitialPageAtTheSnapPosition) {
    Harness h;
    PagerState state(2, 0, [] { return 6; });
    h.Run([&] {
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(60));
        o.snapPosition = SnapPosition::Center();
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); }, o);
    });
    CHECK_EQ(state.CurrentPage(), 2);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    CHECK_EQ(state.FirstVisiblePage(), 1);
}

TEST(PagerSnapPositionAndPageSizeMayDependOnThePage) {
    // Pages rest 10 px further in for each page index; the size comes from the available space.
    Harness h;
    PagerState state(0, 0, [] { return 6; });
    h.Run([&] {
        PagerOptions o;
        o.pageSize = PageSize::Custom([](const Density&, int available, int) { return available / 2; });
        o.snapPosition = SnapPosition::Custom([](int, int, int, int, int index, int) { return index * 10; });
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); }, o);
    });
    CHECK_EQ(state.PageSize(), 50);
    state.ScrollToPage(3);
    h.Pump();
    CHECK_EQ(state.CurrentPage(), 3);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    // Page 3 rests 30 px in: page 2 shows its last 30 px before it (scrolled 20 px past).
    CHECK_EQ(state.FirstVisiblePage(), 2);
    CHECK_EQ(state.FirstVisiblePageOffset(), 20);
}

TEST(PagerSnapDistanceLimitsAFling) {
    auto flick = [](int pages) {
        Harness h;
        PagerState state(0, 0, [] { return 50; });
        h.Run([&] {
            PagerOptions o;
            o.flingBehavior = PagerFlingBehavior(state, PagerSnapDistance::AtMost(pages));
            HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); }, o);
        });
        std::int64_t t = 0;
        h.Drag(95, -90, 3, 0, t);   // 3000 px/s
        h.Frames(150);
        CHECK(!state.IsScrollInProgress());
        CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
        return state.CurrentPage();
    };
    CHECK_EQ(flick(1), 1);
    const int far = flick(10);
    CHECK(far > 1 && far <= 10);   // the decay carries it several pages, within the limit
    CHECK_EQ(flick(3), 3);
}

TEST(PageNestedScrollConnectionSettlesThePagerFirst) {
    Harness h;
    PagerState state(0, 0, [] { return 5; });
    std::shared_ptr<NestedScrollConnection> connection;
    h.Run([&] {
        connection = PageNestedScrollConnection(state, Orientation::Horizontal);
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [](int) { Box(FillMax(Modifier{}, Axis::Both)); });
    });
    state.ScrollToPage(1, 0.3f);
    h.Pump();
    CHECK(std::abs(state.CurrentPageOffsetFraction() - 0.3f) < 1e-4f);
    // A child dragged right by 50: the pager takes 30 to settle on page 1, the child the rest.
    const Offset pre = connection->OnPreScroll({ 50, 0 }, NestedScrollSource::UserInput);
    h.Pump();
    CHECK(std::abs(pre.x - 30.0f) < 1e-3f);
    CHECK_EQ(state.CurrentPage(), 1);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    // Settled: nothing more is taken; a child's fling leftovers never reach the pager.
    CHECK_EQ(connection->OnPreScroll({ 50, 0 }, NestedScrollSource::UserInput).x, 0.0f);
    CHECK_EQ(connection->OnPostScroll({}, { 20, 5 }, NestedScrollSource::SideEffect).x, 20.0f);
    Offset consumed{ -1, -1 };
    const auto post = [&]() -> rt::Task<> { consumed = co_await connection->OnPostFling({}, { 900, 40 }); };
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), post());
    rt::Dispatcher::Main().RunPending();
    CHECK_EQ(consumed.x, 900.0f);
    CHECK_EQ(consumed.y, 0.0f);
}

TEST(PagerBringIntoViewLandsOnWholePages) {
    Harness h;
    PagerState state(0, 0, [] { return 6; });
    BringIntoViewRequester requester;
    h.Run([&] {
        PagerOptions o;
        o.beyondViewportPageCount = 2;   // page 2 is composed while page 0 shows
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [&](int page) {
            Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&, page] {
                Modifier m = SizeMod(Modifier{}, Dp(30), Dp(30));
                if (page == 2) m = BringIntoViewRequesterMod(OffsetMod(std::move(m), Dp(60), Dp(0)), requester);
                Box(m);
            });
        }, o);
    });
    requester.BringIntoView();
    h.Frames(60);
    // The default spec would stop once the box shows; the pager's lands on its page.
    CHECK_EQ(state.CurrentPage(), 2);
    CHECK_EQ(state.CurrentPageOffsetFraction(), 0.0f);
    CHECK(!state.IsScrollInProgress());
}

// PagerScrollIndicatorTest (density 1: px are dp).
namespace {
    void IndicatorPager(Harness& h, PagerState& state, bool vertical, int size, PagerOptions o = {}) {
        h.Run([&, vertical, size, o] {
            const auto content = [](int) { Box(FillMax(Modifier{}, Axis::Both)); };
            if (vertical) VerticalPager(RequiredSize({}, Dp(float(size)), Dp(float(size))), state, content, o);
            else HorizontalPager(RequiredSize({}, Dp(float(size)), Dp(float(size))), state, content, o);
        });
    }
    void CheckIndicator(const PagerState& state, int offset, int content, int viewport) {
        const ScrollIndicatorState* s = state.ScrollIndicator();
        CHECK(s != nullptr);
        if (!s) return;
        CHECK_EQ(s->ScrollOffset(), offset);
        CHECK_EQ(s->ContentSize(), content);
        CHECK_EQ(s->ViewportSize(), viewport);
    }
}

TEST(PagerScrollIndicatorEmptyPager) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 0; });
        IndicatorPager(h, state, vertical, 100);
        CheckIndicator(state, 0, 0, 100);
    }
}

TEST(PagerScrollIndicatorFixedPagesThatFit) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 2; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        IndicatorPager(h, state, vertical, 200, o);
        CheckIndicator(state, 0, 100, 200);
    }
}

TEST(PagerScrollIndicatorFillPages) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 2; });
        IndicatorPager(h, state, vertical, 100);
        CheckIndicator(state, 0, 200, 100);
    }
}

TEST(PagerScrollIndicatorFixedPagesThatDoNotFit) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 4; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        IndicatorPager(h, state, vertical, 100, o);
        CheckIndicator(state, 0, 200, 100);
    }
}

TEST(PagerScrollIndicatorPageSpacing) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 4; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        o.pageSpacing = Dp(20);
        IndicatorPager(h, state, vertical, 100, o);
        CheckIndicator(state, 0, (50 + 20) * 4 - 20, 100);
    }
}

TEST(PagerScrollIndicatorContentPadding) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 4; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        if (vertical) { o.contentPadding.top = Dp(20); o.contentPadding.bottom = Dp(30); }
        else { o.contentPadding.start = Dp(20); o.contentPadding.end = Dp(30); }
        IndicatorPager(h, state, vertical, 100, o);
        CheckIndicator(state, 0, 50 * 4 + 20 + 30, 100);
    }
}

TEST(PagerScrollIndicatorFollowsScrollToPage) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 6; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        IndicatorPager(h, state, vertical, 100, o);
        state.ScrollToPage(3);
        h.Pump();
        CheckIndicator(state, 3 * 50, 300, 100);
    }
}

TEST(PagerScrollIndicatorFollowsScrollBy) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 6; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        IndicatorPager(h, state, vertical, 100, o);
        state.ConsumeScrollDelta(70);
        h.Pump();
        CheckIndicator(state, 70, 300, 100);
    }
}

TEST(PagerScrollIndicatorCoercesToIntMax) {
    Harness h;
    const int count = std::numeric_limits<int>::max();
    PagerState state(count - 10, 0, [count] { return count; });
    PagerOptions o;
    o.pageSize = PageSize::Fixed(Dp(50));
    IndicatorPager(h, state, false, 100, o);
    CheckIndicator(state, std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 100);
}

TEST(PagerScrollIndicatorReverseLayout) {
    for (bool vertical : { true, false }) {
        Harness h;
        PagerState state(0, 0, [] { return 4; });
        PagerOptions o;
        o.pageSize = PageSize::Fixed(Dp(50));
        o.reverseLayout = true;
        IndicatorPager(h, state, vertical, 100, o);
        CheckIndicator(state, 100, 200, 100);
    }
}

TEST(PagerReverseLayoutPagesTheOtherWay) {
    // Page 0 at the end: dragging right (towards the start edge) brings the next page.
    Harness h;
    PagerState state(0, 0, [] { return 5; });
    std::vector<int> placedLeft(5, -1);
    h.Run([&] {
        PagerOptions o;
        o.reverseLayout = true;
        o.pageSize = PageSize::Fixed(Dp(50));
        HorizontalPager(FillMax(Modifier{}, Axis::Both), state, [&](int page) {
            Box(OnGloballyPositioned(FillMax(Modifier{}, Axis::Both), [&, page](const LayoutCoordinates& c) { placedLeft[std::size_t(page)] = int(c.PositionInRoot().x); }));
        }, o);
    });
    CHECK_EQ(placedLeft[0], 50);   // at the right edge of the 100 px pager
    CHECK_EQ(placedLeft[1], 0);
    std::int64_t t = 0;
    h.Drag(10, 40, 4, 300, t);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 1);
    h.Drag(90, -40, 4, 300, t);
    h.Frames(60);
    CHECK_EQ(state.CurrentPage(), 0);
}
