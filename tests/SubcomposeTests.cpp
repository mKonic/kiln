#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Subcompose.h"

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
            Frame();
        }
        void Frame() {
            rt::Composition::RecomposeAll();
            owner.MeasureAndLayout({ 400, 400 });
            rt::Composition::RecomposeAll();
            owner.MeasureAndLayout({ 400, 400 });
        }
    };
    rt::Local<int> LocalNumber{ 0 };
}

TEST(BoxWithConstraintsChoosesByMaxWidth) {
    Harness h;
    rt::State<int> width(300);
    std::string chosen;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            BoxWithConstraints(Width(Modifier{}, Dp(float(width.Get()))), [&](const BoxWithConstraintsScope& s) {
                chosen = s.MaxWidth().value > 200 ? "wide" : "narrow";
                Box(SizeMod(Modifier{}, Dp(10), Dp(10)));
            });
        });
    });
    CHECK_EQ(chosen, std::string("wide"));
    width.Set(150);
    h.Frame();
    CHECK_EQ(chosen, std::string("narrow"));
}

TEST(SubcompositionsSeeLocals) {
    Harness h;
    int seen = -1;
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalNumber, 42, [&] {
            BoxWithConstraints(Modifier{}, [&](const BoxWithConstraintsScope&) {
                seen = rt::Composition::Current()->Read(LocalNumber);
            });
        });
    });
    CHECK_EQ(seen, 42);
}

TEST(SlotsMeasuredFromEachOther) {
    // Slot b is as wide as slot a measured (the classic dependent measurement).
    Harness h;
    LayoutNode* bNode = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            SubcomposeLayout(Modifier{}, [&](SubcomposeMeasureScope& s, const Constraints& c) {
                auto a = s.Subcompose(rt::Key(1), [] { Box(SizeMod(Modifier{}, Dp(70), Dp(20))); });
                Placeable& pa = a[0]->Measure(Constraints{ 0, c.maxWidth, 0, c.maxHeight });
                const int w = pa.Width();
                auto b = s.Subcompose(rt::Key(2), [&bNode] {
                    Box(FillMax(Modifier{}, Axis::Horizontal), Alignment::TopStart(), false, [] {});
                    bNode = nullptr;
                });
                Placeable& pb = b[0]->Measure(Constraints{ w, w, 0, 10 });
                return s.Layout(w, pa.Height() + pb.Height(), [&pa, &pb](PlacementScope& place) {
                    place.Place(pa, 0, 0);
                    place.Place(pb, 0, pa.Height());
                });
            });
        });
    });
    LayoutNode* sub = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(sub->Size().width, 70);
    CHECK_EQ(int(sub->Children().size()), 2);                         // two slot holders
    LayoutNode* b = sub->Children()[1]->Children()[0];
    CHECK_EQ(b->Size().width, 70);
    CHECK_EQ(int(b->PositionInRoot().y), 20);
}

TEST(LazyItemsSeeLocals) {
    Harness h;
    int seen = -1;
    rt::State<int> dummy(0);
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalNumber, 7, [&] {
            auto& state = rt::Composition::Current()->RememberNew<LazyListState>({}, {});
            LazyColumn(SizeMod(Modifier{}, Dp(100), Dp(100)), state, [&](LazyListScope& scope) {
                scope.Item([&] { seen = rt::Composition::Current()->Read(LocalNumber); });
            });
        });
    });
    CHECK_EQ(seen, 7);
}
