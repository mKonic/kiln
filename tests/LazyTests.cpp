#include "Check.h"

#include "kiln/foundation/Lazy.h"

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
