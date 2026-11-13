#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/BringIntoView.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Lazy.h"
#include "kiln/runtime/Saveable.h"
#include <chrono>
#include <cstdio>
#include <map>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
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

namespace {
    struct NodeSink { std::map<int, LayoutNode*>* map; int key; bool operator==(const NodeSink&) const = default; };
    struct NodeSinkNode final : ModifierNode {
        NodeSink params;
        explicit NodeSinkNode(NodeSink p) : params(p) {}
        void OnParamsChanged() { (*params.map)[params.key] = Owner(); }
        void OnAttach() override { (*params.map)[params.key] = Owner(); }
        void OnDetach() override { params.map->erase(params.key); }
    };
    Modifier Sink(Modifier m, std::map<int, LayoutNode*>& map, int key) { return m.With<NodeSinkNode>(NodeSink{ &map, key }); }
}

TEST(AnimateItemMovesAndFadesItems) {
    std::map<int, LayoutNode*> nodes;   // outlives the composition (nodes detach into it)
    Harness h;
    LazyListState state;
    rt::State<std::vector<int>> data{ std::vector<int>{ 0, 1, 2, 3, 4 } };
    h.Run([&] {
        const auto& d = data.Get();
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&, d](LazyListScope& s) {
            s.Items(int(d.size()), [&, d](int i) {
                Box(Sink(AnimateItem(SizeMod(Modifier{}, Dp(100), Dp(20))), nodes, d[size_t(i)]));
            }, [d](int i) { return rt::Key(d[size_t(i)] + 1); });
        });
    });
    CHECK_EQ(nodes[0]->PositionInRoot().y, 0.0f);
    // Move item 4 to the front: everything else shifts down 20, animated from where it was.
    data.Set({ 4, 0, 1, 2, 3 });
    h.Pump();
    CHECK_EQ(nodes[0]->PositionInRoot().y, 0.0f);    // still drawn at its old place
    CHECK_EQ(nodes[4]->PositionInRoot().y, 80.0f);
    h.Frames(5);
    const float mid = nodes[0]->PositionInRoot().y;
    CHECK(mid > 0 && mid < 20);
    h.Frames(80);
    CHECK_EQ(nodes[0]->PositionInRoot().y, 20.0f);
    CHECK_EQ(nodes[4]->PositionInRoot().y, 0.0f);
    // Remove item 1: it stays (fading) where it was while the rest close the gap.
    data.Set({ 4, 0, 2, 3 });
    h.Pump();
    CHECK(nodes.count(1) == 1);
    CHECK_EQ(nodes[1]->PositionInRoot().y, 40.0f);
    h.Frames(120);
    CHECK(nodes.count(1) == 0);                       // gone once faded
    CHECK_EQ(nodes[2]->PositionInRoot().y, 40.0f);
    // Scrolling does not animate.
    data.Set({ 4, 0, 2, 3, 5, 6, 7, 8, 9, 10 });
    h.Pump();
    h.Frames(80);
    state.ConsumeScrollDelta(30);
    h.Pump();
    CHECK_EQ(nodes[3]->PositionInRoot().y, 60.0f - 30.0f);
}

TEST(AnimateItemSlidesItemsInAndOutAcrossTheEdges) {
    std::map<int, LayoutNode*> nodes;
    Harness h;
    LazyListState state;
    rt::State<bool> flipped{ false };
    h.Run([&] {
        const bool f = flipped.Get();
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&, f](LazyListScope& s) {
            // A fixed first item keeps the scroll position put (it is the first visible key).
            s.Item([] { Box(SizeMod(Modifier{}, Dp(100), Dp(20))); }, rt::Key(1000));
            s.Items(20, [&, f](int i) {
                const int id = f ? 19 - i : i;
                Box(Sink(AnimateItem(SizeMod(Modifier{}, Dp(100), Dp(20))), nodes, id));
            }, [f](int i) { return rt::Key((f ? 19 - i : i) + 1); });
        });
    });
    flipped.Set(true);
    h.Pump();
    // Incoming (ids 19..16) start below the viewport, stacked in their old order (Compose's
    // movingInFromEndBound sorts by previous index).
    CHECK_EQ(nodes[16]->PositionInRoot().y, 100.0f);
    CHECK_EQ(nodes[19]->PositionInRoot().y, 160.0f);
    // Outgoing (ids 0..3) are still drawn where they were.
    CHECK(nodes.count(0) == 1);
    CHECK_EQ(nodes[0]->PositionInRoot().y, 20.0f);
    h.Frames(5);
    CHECK(nodes[19]->PositionInRoot().y < 160.0f);
    CHECK(nodes[0]->PositionInRoot().y > 20.0f);
    h.Frames(120);
    CHECK_EQ(nodes[19]->PositionInRoot().y, 20.0f);
    CHECK(nodes.count(0) == 0);   // slid out and dropped
}

TEST(AnimateItemSlidesOutUnderAStickyHeader) {
    std::map<int, LayoutNode*> nodes;
    Harness h;
    LazyListState state;
    rt::State<bool> flipped{ false };
    h.Run([&] {
        const bool f = flipped.Get();
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&, f](LazyListScope& s) {
            s.StickyHeader([](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(20))); }, rt::Key(1000));
            s.Items(20, [&, f](int i) {
                const int id = f ? 19 - i : i;
                Box(Sink(AnimateItem(SizeMod(Modifier{}, Dp(100), Dp(20))), nodes, id));
            }, [f](int i) { return rt::Key((f ? 19 - i : i) + 1); });
        });
    });
    flipped.Set(true);
    h.Pump();
    CHECK(nodes.count(0) == 1);
    if (nodes.count(0)) CHECK_EQ(nodes[0]->PositionInRoot().y, 20.0f);
    h.Frames(5);
    CHECK(nodes.count(0) == 1);
}

TEST(StaggeredGridPlacesItemsInTheShortestLane) {
    Harness h;
    LazyStaggeredGridState state;
    const int heights[] = { 30, 50, 20, 40, 10, 30, 30, 30, 30, 30, 30, 30 };
    h.Run([&] {
        LazyVerticalStaggeredGrid(FillMax(Modifier{}, Axis::Both), GridCells::Fixed(2), state, [&](LazyStaggeredGridScope& s) {
            s.Items(12, [&](int i) { Box(Height(Modifier{}, Dp(float(heights[i])))); }, {}, [](int i) { return i == 4; });
        });
    });
    auto item = [&](int index) -> const LazyGridItemInfo* {
        for (const auto& it : state.LayoutInfo().visibleItemsInfo) if (it.index == index) return &it;
        return nullptr;
    };
    CHECK_EQ(item(0)->column, 0);
    CHECK_EQ(item(1)->column, 1);
    CHECK_EQ(item(1)->offset.x, 50);
    CHECK_EQ(item(2)->offset.y, 30);   // under item 0 (lane 0 was shorter)
    CHECK_EQ(item(3)->column, 0);      // lanes tie at 50: the first one
    CHECK_EQ(item(3)->offset.y, 50);
    CHECK_EQ(item(4)->offset.y, 90);   // full line: below every lane
    CHECK_EQ(item(4)->size.width, 100);
    CHECK(item(5) == nullptr);         // starts at 100: the viewport's end
    state.ConsumeScrollDelta(60);
    h.Pump();
    CHECK_EQ(item(3)->offset.y, -10);
    CHECK(item(0) == nullptr);
    state.ScrollToItem(5);
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 5);
    CHECK_EQ(item(5)->offset.y, 0);
    // The end: 12 items end at 190 (two lanes of 30 from 100); a 100 px viewport stops at 90.
    state.ConsumeScrollDelta(500);
    h.Pump();
    CHECK(!state.CanScrollForward());
    int bottom = 0;
    for (const auto& it : state.LayoutInfo().visibleItemsInfo) bottom = std::max(bottom, it.offset.y + it.size.height);
    CHECK_EQ(bottom, 100);
}

TEST(BringIntoViewKeepsItemsClearOfTheStickyHeader) {
    Harness h;
    LazyListState state;
    BringIntoViewRequester requester;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.StickyHeader([](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(20))); });
            s.Items(30, [&](int i) {
                Modifier m = SizeMod(Modifier{}, Dp(100), Dp(30));
                if (i == 2) m = BringIntoViewRequesterMod(std::move(m), requester);
                Box(m);
            });
        });
    });
    state.ScrollToItem(3, 15);   // item 2 (index 3) is under the sticking header, half hidden
    h.Pump();
    requester.BringIntoView();
    h.Frames(60);
    // It ends just below the header (at 20), not at the viewport's top.
    CHECK_EQ(state.FirstVisibleItemIndex(), 2);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 10);
}

TEST(FocusedItemStaysComposedWhenScrolledAway) {
    std::map<int, int> composed, disposed;   // before the harness: its teardown disposes items
    FocusRequester focus;
    LazyListState outer;
    LazyListState inner;
    Harness h;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), outer, [&](LazyListScope& s) {
            s.Items(100, [&](int i) {
                rt::Composition::Current()->Remember<std::shared_ptr<int>>([&, i] {
                    ++composed[i];
                    return std::shared_ptr<int>(new int(i), [&disposed](int* p) { ++disposed[*p]; delete p; });
                });
                if (i != 1) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); return; }
                // Item 1 holds a row whose item 0 is focusable: pinning goes through both lists.
                LazyRow(SizeMod(Modifier{}, Dp(100), Dp(30)), inner, [&](LazyListScope& r) {
                    r.Items(10, [&](int j) {
                        Modifier m = SizeMod(Modifier{}, Dp(30), Dp(30));
                        if (j == 0) m = Focusable(FocusRequesterModifier(std::move(m), focus));
                        Box(m);
                    });
                });
            });
        });
    });
    CHECK(focus.RequestFocus());
    h.Frames(5);
    outer.ScrollToItem(50);
    h.Pump();
    CHECK_EQ(composed[1], 1);
    CHECK_EQ(disposed[1], 0);                 // pinned: still composed far above the viewport
    CHECK_EQ(disposed[2], 1);                 // its neighbours were let go
    CHECK_EQ(outer.LayoutInfo().visibleItemsInfo.front().index, 50);   // not a visible item
    h.owner.ClearFocus(true);
    h.Frames(2);
    CHECK_EQ(disposed[1], 1);                 // released: disposed on the next measure
}

TEST(FocusMovesIntoItemsBeyondTheViewport) {
    int focused = -1;
    FocusRequester first;
    LazyListState state;
    Harness h;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.Items(40, [&](int i) {
                Modifier m = SizeMod(Modifier{}, Dp(100), Dp(30));
                if (i == 0) m = FocusRequesterModifier(std::move(m), first);
                Box(Focusable(std::move(m), Focusability::Always, [&, i](FocusState f) { if (IsFocused(f)) focused = i; }));
            });
        });
    });
    CHECK(first.RequestFocus());
    h.Frames(2);
    CHECK_EQ(focused, 0);
    // Items 0..3 are composed; each move down past them lays out the next one to find it.
    for (int k = 1; k <= 6; ++k) {
        CHECK(h.owner.MoveFocus(FocusDirection::Down));
        h.Frames(30);
        CHECK_EQ(focused, k);
    }
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);   // item 6 (180..210) brought in at the bottom edge
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 20);
    // And with Tab (1D), and back up.
    CHECK(h.owner.MoveFocus(FocusDirection::Next));
    h.Frames(30);
    CHECK_EQ(focused, 7);
    CHECK(h.owner.MoveFocus(FocusDirection::Up));
    h.Frames(30);
    CHECK_EQ(focused, 6);
}

TEST(StickyHeaderSticksToTheStartOfAReversedList) {
    LazyListState state;
    Harness h;
    h.Run([&] {
        LazyListOptions o;
        o.reverseLayout = true;
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            for (int section = 0; section < 3; ++section) {
                s.StickyHeader([](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(20))); });
                s.Items(5, [](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); });
            }
        }, o);
    });
    // Item 0 (the first header) sits at the bottom of a reversed list.
    auto headerAt = [&](int index) {
        for (const auto& it : state.LayoutInfo().visibleItemsInfo) if (it.index == index) return it.offset;
        return -999;
    };
    CHECK_EQ(headerAt(0), 0);
    // Scrolled 50 into the first section: the header stays at the start (the bottom edge).
    state.ScrollToItem(1, 20);
    h.Pump();
    CHECK_EQ(headerAt(0), 0);
    // The next header (index 6, at 170) pushes it out once it is closer than the header's size.
    state.ScrollToItem(5, 20);   // content start at 160: header 6 is 10 from the start
    h.Pump();
    CHECK_EQ(headerAt(6), 10);
    CHECK_EQ(headerAt(0), -10);  // pushed back: it ends where header 6 begins
}

TEST(StaggeredGridFocusSearchAndPinning) {
    int focused = -1;
    std::map<int, int> disposed;
    FocusRequester first;
    LazyStaggeredGridState state;
    Harness h;
    h.Run([&] {
        LazyVerticalStaggeredGrid(FillMax(Modifier{}, Axis::Both), GridCells::Fixed(2), state, [&](LazyStaggeredGridScope& s) {
            s.Items(60, [&](int i) {
                rt::Composition::Current()->Remember<std::shared_ptr<int>>([&, i] {
                    return std::shared_ptr<int>(new int(i), [&disposed](int* p) { ++disposed[*p]; delete p; });
                });
                Modifier m = SizeMod(Modifier{}, Dp(50), Dp(i % 2 ? 40 : 30));
                if (i == 0) m = FocusRequesterModifier(std::move(m), first);
                Box(Focusable(std::move(m), Focusability::Always, [&, i](FocusState f) { if (IsFocused(f)) focused = i; }));
            });
        });
    });
    CHECK(first.RequestFocus());
    h.Frames(2);
    // Down the left lane (items 0, 2, 4, ... at 0, 30, 60, ...): past the viewport and beyond.
    for (int k = 1; k <= 5; ++k) {
        CHECK(h.owner.MoveFocus(FocusDirection::Down));
        h.Frames(30);
    }
    CHECK(focused >= 8);
    const int pinnedItem = focused;
    state.ScrollToItem(50);
    h.Pump();
    CHECK_EQ(disposed[pinnedItem], 0);   // focused: kept while scrolled away
    CHECK(disposed[pinnedItem - 2] >= 1);
}

TEST(AnimateItemInStaggeredGrids) {
    std::map<int, LayoutNode*> nodes;
    LazyStaggeredGridState state;
    rt::State<std::vector<int>> data{ std::vector<int>{ 0, 1, 2, 3 } };
    Harness h;
    h.Run([&] {
        const auto& d = data.Get();
        LazyVerticalStaggeredGrid(FillMax(Modifier{}, Axis::Both), GridCells::Fixed(2), state, [&, d](LazyStaggeredGridScope& s) {
            s.Items(int(d.size()), [&, d](int i) {
                Box(Sink(AnimateItem(SizeMod(Modifier{}, Dp(50), Dp(20))), nodes, d[size_t(i)]));
            }, [d](int i) { return rt::Key(d[size_t(i)] + 1); });
        });
    });
    // 0 | 1 / 2 | 3 in two lanes.
    CHECK_EQ(nodes[2]->PositionInRoot().x, 0.0f);
    CHECK_EQ(nodes[2]->PositionInRoot().y, 20.0f);
    // Remove item 0: the others move up a lane slot, animated from where they were; 0 fades.
    data.Set({ 1, 2, 3 });
    h.Pump();
    CHECK_EQ(nodes[2]->PositionInRoot().y, 20.0f);   // still at its old place
    CHECK(nodes.count(0) == 1);                       // fading out where it was
    h.Frames(5);
    const Offset mid = nodes[2]->PositionInRoot();
    CHECK(mid.y < 20.0f && mid.y > 0.0f);
    h.Frames(120);
    CHECK_EQ(nodes[2]->PositionInRoot().x, 50.0f);   // 1 | 2 / 3
    CHECK_EQ(nodes[2]->PositionInRoot().y, 0.0f);
    CHECK(nodes.count(0) == 0);
}

TEST(StaggeredFarJumpThenScrollBackToTheStart) {
    LazyStaggeredGridState state;
    Harness h;
    h.Run([&] {
        LazyVerticalStaggeredGrid(FillMax(Modifier{}, Axis::Both), GridCells::Fixed(2), state, [&](LazyStaggeredGridScope& s) {
            s.Items(10000, [](int i) { Box(SizeMod(Modifier{}, Dp(50), Dp(20 + (i * 7) % 30))); }, {}, [](int i) { return i % 97 == 50; });
        });
    });
    // A far jump measures only around the target (per-lane offsets), not the items before it.
    const auto t0 = std::chrono::steady_clock::now();
    state.ScrollToItem(5000);
    h.Pump();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 50.0);
    CHECK_EQ(state.FirstVisibleItemIndex(), 5000);
    // Scrolling back discovers the earlier items lane by lane and realigns at the top.
    int guard = 0;
    while (state.CanScrollBackward() && guard++ < 2000) { state.ConsumeScrollDelta(-1000); h.Pump(); }
    CHECK_EQ(state.FirstVisibleItemIndex(), 0);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 0);
    const auto& items = state.LayoutInfo().visibleItemsInfo;
    CHECK(!items.empty() && items.front().index == 0 && items.front().offset.y == 0 && items.front().column == 0);
    // Both lanes start at the top.
    bool lane1AtTop = false;
    for (const auto& it : items) if (it.column == 1 && it.offset.y == 0) lane1AtTop = true;
    CHECK(lane1AtTop);
}

// ScrollIndicatorState estimates: the visible items' average size stands for every item.
TEST(LazyListScrollIndicatorEstimatesFromVisibleItems) {
    for (bool reverse : { false, true }) {
        Harness h;
        LazyListState state;
        h.Run([&, reverse] {
            LazyListOptions o;
            o.reverseLayout = reverse;
            o.arrangement = Arrangement::SpacedBy(Dp(2));
            Box({}, Alignment::TopStart(), false, [&, o] {
                LazyColumn(Height(FillMax({}, Axis::Horizontal), Dp(50)), state, [](LazyListScope& s) {
                    s.Items(20, [](int) { Box(Height(FillMax({}, Axis::Horizontal), Dp(10))); });
                }, o);
            });
        });
        const ScrollIndicatorState* s = state.ScrollIndicator();
        CHECK(s != nullptr);
        if (!s) continue;
        // 20 items of 10 with 19 gaps of 2.
        CHECK_EQ(s->ContentSize(), 20 * 10 + 19 * 2);
        CHECK_EQ(s->ViewportSize(), 50);
        CHECK_EQ(s->ScrollOffset(), reverse ? 238 - 50 : 0);
        state.ScrollToItem(3, 4);
        h.Pump();
        // Each item with its gap: 12.
        CHECK_EQ(s->ScrollOffset(), reverse ? 238 - 50 - (3 * 12 + 4) : 3 * 12 + 4);
    }
}

TEST(LazyGridScrollIndicatorCountsLines) {
    Harness h;
    LazyGridState state;
    h.Run([&] {
        Box({}, Alignment::TopStart(), false, [&] {
            LazyVerticalGrid(Height(FillMax({}, Axis::Horizontal), Dp(50)), GridCells::Fixed(3), state, [](LazyGridScope& s) {
                s.Items(10, [](int) { Box(Height(FillMax({}, Axis::Horizontal), Dp(20))); });
            });
        });
    });
    const ScrollIndicatorState* s = state.ScrollIndicator();
    CHECK(s != nullptr);
    if (!s) return;
    // 10 items in 3 columns: 4 lines of 20.
    CHECK_EQ(s->ContentSize(), 80);
    CHECK_EQ(s->ViewportSize(), 50);
    state.ScrollToItem(3, 5);
    h.Pump();
    CHECK_EQ(s->ScrollOffset(), 20 + 5);
}

// LazyItemScope.fillParentMax*: the list's size less its content padding, times the fraction.
TEST(LazyListItemsFillTheirParent) {
    Harness h;
    LazyListState state;
    std::vector<IntSize> sizes(4);
    h.Run([&] {
        LazyListOptions o;
        o.contentPadding.top = Dp(10);
        o.contentPadding.start = Dp(5);
        o.contentPadding.end = Dp(5);
        LazyColumn(FillMax({}, Axis::Both), state, [&](LazyListScope& s) {
            s.Item([&] { Box(OnSizeChanged(FillParentMaxSize({}, 0.5f), [&](IntSize sz) { sizes[0] = sz; })); });
            s.Item([&] { Box(OnSizeChanged(Height(FillParentMaxWidth({}, 0.5f), Dp(10)), [&](IntSize sz) { sizes[1] = sz; })); });
            s.Item([&] { Box(OnSizeChanged(Width(FillParentMaxHeight({}, 0.25f), Dp(10)), [&](IntSize sz) { sizes[2] = sz; })); });
        }, o);
    });
    // The window is 100 x 100.
    CHECK((sizes[0] == IntSize{ 45, 45 }));
    CHECK((sizes[1] == IntSize{ 45, 10 }));
    CHECK((sizes[2] == IntSize{ 10, 23 }));   // 90 * 0.25 = 22.5, rounded half up
}

// LazyListMeasuredItem / LazyGridMeasuredItem: an item's top-level children follow one
// another in a list (each aligned across it, mirrored with reverseLayout) and overlap in a grid.
TEST(LazyItemsStackTheirChildrenInListsOnly) {
    Harness h;
    LazyListState list;
    LazyGridState grid;
    rt::State<bool> reverse(false);
    Offset a, b, c, d;
    h.Run([&] {
        Row(FillMax(Modifier{}, Axis::Both), Arrangement::Start(), AlignmentV::Top(), [&] {
            LazyListOptions o;
            o.horizontalAlignment = AlignmentH::CenterHorizontally();
            o.reverseLayout = reverse.Get();
            LazyColumn(SizeMod(Modifier{}, Dp(50), Dp(100)), list, [&](LazyListScope& s) {
                s.Item([&] {
                    Box(OnGloballyPositioned(SizeMod(Modifier{}, Dp(10), Dp(10)), [&](const LayoutCoordinates& l) { a = l.PositionInRoot(); }));
                    Box(OnGloballyPositioned(SizeMod(Modifier{}, Dp(30), Dp(20)), [&](const LayoutCoordinates& l) { b = l.PositionInRoot(); }));
                });
            }, o);
            LazyVerticalGrid(SizeMod(Modifier{}, Dp(50), Dp(100)), GridCells::Fixed(1), grid, [&](LazyGridScope& s) {
                s.Item([&] {
                    Box(OnGloballyPositioned(SizeMod(Modifier{}, Dp(10), Dp(10)), [&](const LayoutCoordinates& l) { c = l.PositionInRoot(); }));
                    Box(OnGloballyPositioned(SizeMod(Modifier{}, Dp(30), Dp(20)), [&](const LayoutCoordinates& l) { d = l.PositionInRoot(); }));
                });
            });
        });
    });
    CHECK_EQ(int(a.y), 0);
    CHECK_EQ(int(b.y), 10);
    CHECK_EQ(int(a.x), 20);   // centred in the 50 px column
    CHECK_EQ(int(b.x), 10);
    CHECK_EQ(int(c.y), 0);
    CHECK_EQ(int(d.y), 0);
    reverse.Set(true);
    h.Pump();
    CHECK_EQ(int(a.y), 90);   // item at the bottom, its first child last
    CHECK_EQ(int(b.y), 70);
}

TEST(LazyStateCanScrollFlagsAreObservable) {   // canScrollForward / canScrollBackward are snapshot state
    Harness h;
    LazyListState state;
    std::vector<std::pair<bool, bool>> seen;
    h.Run([&] {
        seen.emplace_back(state.CanScrollBackward(), state.CanScrollForward());
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.Items(10, [&](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); });
        });
    });
    h.Pump();
    CHECK(!seen.back().first && seen.back().second);   // at the start: only forward
    state.ConsumeScrollDelta(1000);                     // 10 * 30 - 100: to the end
    h.Pump();
    h.Pump();
    CHECK(seen.back().first && !seen.back().second);   // the reader recomposed with the new values
}

TEST(LazyColumnInALookaheadScopeScrollsOnce) {
    // The lookahead pass consumes the scroll; the approach pass lays out from there.
    Harness h;
    LazyListState state;
    h.Run([&] {
        foundation::LookaheadScope([&](ui::LookaheadScope&) {
            LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
                s.Items(1000, [&](int) { Box(SizeMod(Modifier{}, Dp(100), Dp(30))); });
            });
        });
    });
    CHECK_EQ(state.ConsumeScrollDelta(95), 95.0f);
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);
    CHECK_EQ(state.FirstVisibleItemScrollOffset(), 5);
    h.Pump();
    CHECK_EQ(state.FirstVisibleItemIndex(), 3);
    LayoutNode* list = h.owner.Root().Children().front();
    // Placed where lookahead put them: item 3 at -5.
    int top = 1000;
    for (LayoutNode* c : list->Children()) if (c->IsPlaced()) top = std::min(top, c->Position().y);
    CHECK_EQ(top, -5);
}
