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
