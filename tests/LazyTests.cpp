#include "Check.h"

#include "kiln/foundation/Lazy.h"
#include "kiln/runtime/Saveable.h"
#include <map>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() { rt::Composition::RecomposeAll(); owner.MeasureAndLayout({ 100, 100 }); }
        std::int64_t frameNs = 0;
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
    };
}

TEST(LazyColumnComposesOnlyVisibleItems) {
    Harness h;
    LazyListState state;
    int composed = 0;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.Items(1000, [&](int) { ++composed; Box(SizeMod(Modifier{}, Dp(100), Dp(30))); });
        });
    });
    // 100 px viewport, 30 px items: 4 items (0..3) cover it.
    CHECK(composed >= 4 && composed <= 6);
    CHECK_EQ(state.FirstVisibleItemIndex(), 0);
    composed = 0;
    const float consumed = state.ConsumeScrollDelta(95);
    CHECK_EQ(consumed, 95.0f);
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);        // 95 = 3 * 30 + 5
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 5);
    CHECK(composed < 10);
}

TEST(LazyColumnStopsAtBothEnds) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.Items(5, [&](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); });
        });
    });
    // 150 content, 100 viewport: at most 50 forward.
    CHECK_EQ(state.ConsumeScrollDelta(80), 50.0f);
    CHECK(!state.CanScrollForward());
    CHECK_EQ(state.ConsumeScrollDelta(-200), -50.0f);
    CHECK_EQ(state.FirstVisibleItemIndex(), 0);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 0);
}

TEST(LazyColumnKeepsPositionByKey) {
    Harness h;
    LazyListState state;
    rt::State<std::vector<int>> data(std::vector<int>{ 10, 11, 12, 13, 14, 15, 16, 17 });
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            const auto& d = data.Get();
            s.Items(int(d.size()), [&](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); },
                    [&d](int i) { return rt::Key(d[i]); });
        });
    });
    state.ConsumeScrollDelta(65);            // first visible: index 2 (key 12), offset 5
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 2);
    data.Set({ 1, 2, 10, 11, 12, 13, 14, 15, 16, 17 });   // two items inserted at the top
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 4);  // still key 12
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 5);
}

TEST(LazyLayoutInfoAndAnimateScrollToItem) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [](LazyListScope& s) {
            s.Items(1000, [](int i) { Box(SizeMod(Modifier{}, Dp(100), Dp(20)), Alignment::TopStart(), false, {}); },
                    [](int i) { return rt::Key(i + 5000); });
        });
    });
    const auto& info = state.LayoutInfo();
    CHECK_EQ(info.totalItemsCount, 1000);
    CHECK_EQ(int(info.visibleItemsInfo.size()), 5);   // 100 px viewport, 20 px items
    CHECK_EQ(info.visibleItemsInfo[1].offset, 20);
    CHECK_EQ(info.visibleItemsInfo[1].size, 20);
    CHECK(info.visibleItemsInfo[1].key == rt::Key(5001));
    // A visible target: one seek.
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToItem(3));
    CHECK(state.IsScrollInProgress());
    h.Frames(120);
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 0);
    CHECK(!state.IsScrollInProgress());
    // Far away: teleports and lands exactly, with an offset.
    job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToItem(700, 7));
    h.Frames(400);
    CHECK_EQ(state.FirstVisibleItemIndex(), 700);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 7);
    // Backward.
    job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToItem(10));
    h.Frames(400);
    CHECK_EQ(state.FirstVisibleItemIndex(), 10);
    // Near the end the list stops at its last position.
    job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToItem(999));
    h.Frames(400);
    CHECK_EQ(state.FirstVisibleItemIndex(), 995);
    CHECK(!state.IsScrollInProgress());
}

TEST(PressStopsAProgrammaticScroll) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [](LazyListScope& s) {
            s.Items(1000, [](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(20)), Alignment::TopStart(), false, {}); });
        });
    });
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToItem(500));
    h.Frames(3);
    CHECK(state.IsScrollInProgress());
    h.Touch(PointerEventType::Press, 50, 50, true, 0);
    const int at = state.FirstVisibleItemIndex();
    h.Frames(30);
    CHECK(!state.IsScrollInProgress());
    CHECK_EQ(state.FirstVisibleItemIndex(), at);
    h.Touch(PointerEventType::Release, 50, 50, false, 10);
}

TEST(StickyHeadersPinAndArePushedByTheNext) {
    Harness h;
    LazyListState state;
    std::vector<int> headerIndices;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            for (int section = 0; section < 10; ++section) {
                s.StickyHeader([&](int index) {
                    headerIndices.push_back(index);
                    Box(SizeMod(Modifier{}, Dp(100), Dp(20)));
                });
                s.Items(4, [](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(20))); });
            }
        });
    });
    auto offsetOf = [&](int index) {
        for (const auto& it : state.LayoutInfo().visibleItemsInfo) if (it.index == index) return it.offset;
        return -9999;
    };
    CHECK_EQ(offsetOf(0), 0);
    CHECK_EQ(offsetOf(5), -9999);     // section 1's header starts at the viewport end: not composed
    state.ConsumeScrollDelta(30);
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 1);
    CHECK_EQ(offsetOf(0), 0);         // pinned
    CHECK_EQ(offsetOf(1), -10);
    state.ConsumeScrollDelta(60);     // section 1's header at 10: pushes header 0 up
    h.Pump();
    CHECK_EQ(offsetOf(5), 10);
    CHECK_EQ(offsetOf(0), -10);
    state.ConsumeScrollDelta(40);     // header 5 now pinned, header 0 gone
    h.Pump();
    CHECK_EQ(offsetOf(5), 0);
    CHECK_EQ(offsetOf(0), -9999);
    bool sawFive = false;
    for (int i : headerIndices) { CHECK(i % 5 == 0); if (i == 5) sawFive = true; }
    CHECK(sawFive);
}

TEST(ItemSaveableStateSurvivesScrollingAwayAndRecreation) {
    std::vector<std::uint8_t> saved;
    // One content function for both "activities" so the composite keys match.
    auto run = [](Harness& h, rt::SaveableStateRegistry& registry, LazyListState& state, std::map<int, rt::State<int>*>& counters) {
        h.Run([&] { rt::Composition::Current()->Provide(rt::LocalSaveableStateRegistry, &registry, [&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.Items(100, [&](int i) {
                counters[i] = &rt::RememberSaveableState(0);
                Box(SizeMod(Modifier{}, Dp(100), Dp(20)));
            }, [](int i) { return rt::Key(i + 1); });
        });
        }); });
    };
    {
        rt::SaveableStateRegistry registry;   // outlives the composition, which unregisters from it
        LazyListState state;
        Harness h;
        std::map<int, rt::State<int>*> counters;
        run(h, registry, state, counters);
        counters[1]->Set(11);
        state.ScrollToItem(50);
        h.Pump();
        counters.erase(1);
        state.ScrollToItem(0);
        h.Pump();
        CHECK(counters.count(1) == 1);
        CHECK_EQ(counters[1]->Peek(), 11);      // came back with its value
        counters[2]->Set(22);
        state.ScrollToItem(50);                 // items 1 and 2 off-screen when saving
        h.Pump();
        saved = rt::SaveableStateRegistry::Serialize(registry.PerformSave());
    }
    rt::SaveableStateRegistry registry(rt::SaveableStateRegistry::Deserialize(saved));
    LazyListState state;
    Harness h;
    std::map<int, rt::State<int>*> counters;
    run(h, registry, state, counters);
    CHECK_EQ(counters[1]->Peek(), 11);
    CHECK_EQ(counters[2]->Peek(), 22);
    CHECK_EQ(counters[3]->Peek(), 0);
}

TEST(GridCellSizesSplitLikeCompose) {
    Density d{ 1, 1 };
    CHECK(GridCells::Fixed(3).CellSizes(d, 100, 0) == (std::vector<int>{ 34, 33, 33 }));
    CHECK(GridCells::Fixed(3).CellSizes(d, 100, 5) == (std::vector<int>{ 30, 30, 30 }));
    CHECK_EQ(int(GridCells::Adaptive(Dp(30)).CellSizes(d, 100, 0).size()), 3);
    CHECK_EQ(int(GridCells::Adaptive(Dp(200)).CellSizes(d, 100, 0).size()), 1);
    CHECK(GridCells::FixedSize(Dp(40)).CellSizes(d, 100, 10) == (std::vector<int>{ 40, 40 }));
}

TEST(LazyVerticalGridPlacesCellsInLinesWithSpans) {
    Harness h;
    LazyGridState state;
    h.Run([&] {
        LazyGridOptions o;
        o.crossAxisArrangement = Arrangement::SpacedBy(Dp(5));
        o.mainAxisArrangement = Arrangement::SpacedBy(Dp(2));
        LazyVerticalGrid(FillMax(Modifier{}, Axis::Both), GridCells::Fixed(3), state, [](LazyGridScope& s) {
            s.StickyHeader([](int) { Box(SizeMod(Modifier{}, Dp(10), Dp(10))); });   // index 0, full line
            s.Items(100, [](int) { Box(Height(Modifier{}, Dp(20))); }, {},
                    [](const GridItemSpanScope& sc, int i) { return i == 1 ? 3 : i == 4 ? sc.maxLineSpan : 1; });
        }, o);
    });
    const auto& info = state.LayoutInfo();
    auto item = [&](int index) -> const LazyGridItemInfo* {
        for (const auto& it : info.visibleItemsInfo) if (it.index == index) return &it;
        return nullptr;
    };
    // Header line 0 (10 tall, full width), then items: [1 2 _] -> 2's span 3 wraps: [1][2 full]...
    CHECK_EQ(item(0)->size.width, 100);
    CHECK_EQ(item(0)->row, 0);
    CHECK_EQ(item(1)->row, 1);          // local 0: span 1
    CHECK_EQ(item(1)->size.width, 30);
    CHECK_EQ(item(2)->row, 2);          // local 1: span 3, does not fit after one cell
    CHECK_EQ(item(2)->size.width, 100);
    CHECK_EQ(item(3)->row, 3);
    CHECK_EQ(item(4)->offset.x, 35);    // second slot: 30 + 5 spacing
    CHECK_EQ(item(4)->row, 3);
    CHECK_EQ(item(5)->row, 4);          // local 4: full line
    CHECK_EQ(item(1)->offset.y, 12);    // header 10 + spacing 2
    CHECK_EQ(item(3)->offset.y, 12 + 22 + 22);
    // Scrolling moves whole lines; the first visible index is the first item of a line.
    state.ConsumeScrollDelta(60);
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);
    CHECK_EQ(item(0)->offset.y, 0);     // header pinned
    // Animated scroll to a far item lands on its line.
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), state.AnimateScrollToItem(60));
    h.Frames(300);
    CHECK(state.FirstVisibleItemIndex() <= 60);
    const auto* far = item(60);
    CHECK(far != nullptr);
    if (far) CHECK_EQ(far->offset.y, 0);
}

TEST(LazyHorizontalGridAdaptiveRows) {
    Harness h;
    LazyGridState state;
    h.Run([&] {
        LazyHorizontalGrid(FillMax(Modifier{}, Axis::Both), GridCells::Adaptive(Dp(45)), state, [](LazyGridScope& s) {
            s.Items(50, [](int) { Box(Width(Modifier{}, Dp(30))); });
        });
    });
    const auto& info = state.LayoutInfo();
    CHECK_EQ(info.visibleItemsInfo[0].size.height, 50);   // two rows of 50
    CHECK_EQ(info.visibleItemsInfo[1].offset.y, 50);
    CHECK_EQ(info.visibleItemsInfo[1].column, 0);
    CHECK_EQ(info.visibleItemsInfo[2].offset.x, 30);
    CHECK_EQ(int(info.visibleItemsInfo.size()), 8);       // 4 columns of 30 cover 100
}

TEST(LazyArrangementForShortContentAndReverseLayout) {
    Harness h;
    LazyListState centered, reversed, longReversed;
    std::vector<LayoutNode*> nodes(3, nullptr);
    h.Run([&] {
        Row(FillMax(Modifier{}, Axis::Both), Arrangement::Start(), AlignmentV::Top(), [&] {
            LazyListOptions c;
            c.arrangement = Arrangement::Center();
            LazyColumn(Width(FillMax(Modifier{}, Axis::Vertical), Dp(30)), centered, [](LazyListScope& s) {
                s.Items(3, [](int) { Box(SizeMod(Modifier{}, Dp(30), Dp(20))); });
            }, c);
            LazyListOptions r;
            r.reverseLayout = true;
            r.arrangement = Arrangement::Bottom();
            LazyColumn(Width(FillMax(Modifier{}, Axis::Vertical), Dp(30)), reversed, [](LazyListScope& s) {
                s.Items(3, [](int) { Box(SizeMod(Modifier{}, Dp(30), Dp(20))); });
            }, r);
            LazyListOptions lr;
            lr.reverseLayout = true;
            LazyColumn(Width(FillMax(Modifier{}, Axis::Vertical), Dp(30)), longReversed, [](LazyListScope& s) {
                s.Items(50, [](int) { Box(SizeMod(Modifier{}, Dp(30), Dp(20))); });
            }, lr);
        });
    });
    // Centered: 60 px of items in 100: 20, 40, 60.
    CHECK_EQ(centered.LayoutInfo().visibleItemsInfo[0].offset, 20);
    CHECK_EQ(centered.LayoutInfo().visibleItemsInfo[2].offset, 60);
    // Reverse + Bottom: item 0 at the bottom, offsets along the reversed axis from 0.
    CHECK_EQ(reversed.LayoutInfo().visibleItemsInfo[0].offset, 0);
    CHECK_EQ(reversed.LayoutInfo().visibleItemsInfo[2].offset, 40);
    // A long reversed list scrolls towards higher indices with a positive delta, as usual.
    longReversed.ConsumeScrollDelta(30);
    h.Pump();
    CHECK_EQ(longReversed.FirstVisibleItemIndex(), 1);
    CHECK_EQ(longReversed.FirstVisibleItemScrollOffset(), 10);
}

namespace { rt::Task<> ScrollBy2000(LazyListState* s) { co_await AnimateScrollBy(*s, 2000); } }

TEST(RequestScrollToItemKeepsAFlingRunning) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [](LazyListScope& s) {
            s.Items(500, [](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(20))); });
        });
    });
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), ScrollBy2000(&state));
    h.Frames(2);
    CHECK(state.IsScrollInProgress());
    state.RequestScrollToItem(200);
    h.Pump();
    CHECK(state.FirstVisibleItemIndex() >= 200);
    CHECK(state.IsScrollInProgress());
    state.ScrollToItem(10);   // scrollToItem stops it
    CHECK(!state.IsScrollInProgress());
}
