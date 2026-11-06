#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Flow.h"
#include "kiln/foundation/Layouts.h"

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
            composition.Recompose();
            owner.MeasureAndLayout({ 400, 400 });
        }
    };
    IntOffset At(LayoutNode* n) { return { int(n->PositionInRoot().x), int(n->PositionInRoot().y) }; }
}

TEST(FlowRowWrapsWithSpacing) {
    Harness h;
    std::vector<LayoutNode*> items;
    LayoutNode* flow = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            FlowRow(Width(Modifier{}, Dp(100)), Arrangement::SpacedBy(Dp(10)), Arrangement::SpacedBy(Dp(5)), AlignmentV::Top(), [&] {
                items.clear();
                for (int i = 0; i < 5; ++i) Box(SizeMod(Modifier{}, Dp(40), Dp(20)));
            });
        });
    });
    flow = h.owner.Root().Children()[0]->Children()[0];
    for (LayoutNode* c : flow->Children()) items.push_back(c);
    CHECK_EQ(At(items[1]).x, 50);
    CHECK_EQ(At(items[2]).x, 0);            // 40 + 10 + 40 + 10 + 40 > 100: wraps
    CHECK_EQ(At(items[2]).y, 25);
    CHECK_EQ(At(items[4]).y, 50);
    CHECK_EQ(flow->Size().height, 70);       // 3 lines of 20 + 2 gaps of 5
}

TEST(FlowRowLimitsItemsAndLines) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            FlowRow(Modifier{}, Arrangement::Start(), Arrangement::Top(), AlignmentV::Top(), [&] {
                for (int i = 0; i < 7; ++i) Box(SizeMod(Modifier{}, Dp(10), Dp(10)));
            }, FlowOptions{ 3, 2 });
        });
    });
    LayoutNode* flow = h.owner.Root().Children()[0]->Children()[0];
    const auto& items = flow->Children();
    CHECK_EQ(At(items[3]).x, 0);             // 3 per line
    CHECK_EQ(At(items[3]).y, 10);
    CHECK(items[5]->IsPlaced());
    CHECK(!items[6]->IsPlaced());            // a third line would exceed maxLines
    CHECK_EQ(flow->Size().height, 20);
}

TEST(FlowRowWeightsFillTheLine) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            FlowRow(Width(Modifier{}, Dp(100)), Arrangement::Start(), Arrangement::Top(), AlignmentV::Top(), [&] {
                Box(SizeMod(Modifier{}, Dp(40), Dp(10)));
                Box(Height(Weight(Modifier{}, 1), Dp(10)));
            });
        });
    });
    LayoutNode* flow = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(flow->Children()[1]->Size().width, 60);
}
