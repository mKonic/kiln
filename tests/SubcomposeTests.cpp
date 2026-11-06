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

TEST(PrecomposedSlotsAreClaimedByMeasureOrDisposed) {
    Harness h;
    SubcomposeLayoutState state;
    rt::State<bool> showB(false);
    int composedA = 0, composedB = 0;
    auto slotA = [&] { ++composedA; auto& c = *rt::Composition::Current(); c.Remember<int>([] { return 7; }); Box(SizeMod(Modifier{}, Dp(10), Dp(20))); };
    auto slotB = [&] { ++composedB; Box(SizeMod(Modifier{}, Dp(30), Dp(30))); };
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            SubcomposeLayout(state, Modifier{}, [&](SubcomposeMeasureScope& s, const Constraints& c) {
                std::vector<Measurable*> ms;
                if (showB.Get()) ms = s.Subcompose(rt::Key(1), slotA);
                int w = 0;
                std::vector<Placeable*> ps;
                for (auto* m : ms) { ps.push_back(&m->Measure(Constraints{})); w += ps.back()->Width(); }
                return s.Layout(w, 20, [ps](PlacementScope& p) { for (auto* q : ps) p.Place(*q, 0, 0); });
            });
        });
    });
    // Composed ahead and premeasured, before any measure asks for it.
    auto a = state.Precompose(rt::Key(1), slotA);
    CHECK_EQ(composedA, 1);
    CHECK_EQ(a.PlaceablesCount(), 1);
    a.Premeasure(0, Constraints{});
    auto b = state.Precompose(rt::Key(2), slotB);
    CHECK_EQ(composedB, 1);
    // A frame that does not use them keeps them composed.
    h.Frame();
    CHECK_EQ(b.PlaceablesCount(), 1);
    // The measure claims slot 1: the same composition (recomposed with its content).
    showB.Set(true);
    h.Frame();
    CHECK_EQ(a.PlaceablesCount(), 1);
    // Disposing slot 2 drops it; disposing the claimed slot 1 does nothing.
    b.Dispose();
    CHECK_EQ(b.PlaceablesCount(), 0);
    a.Dispose();
    h.Frame();
    CHECK_EQ(a.PlaceablesCount(), 1);
}
