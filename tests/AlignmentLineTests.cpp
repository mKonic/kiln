#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    // A leaf of a fixed size that reports FirstBaseline (and LastBaseline = height - 2).
    struct BaselinePolicy final : MeasurePolicy {
        int w, h, baseline;
        BaselinePolicy(int w_, int h_, int b) : w(w_), h(h_), baseline(b) {}
        MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
            return s.Layout(c.ConstrainWidth(w), c.ConstrainHeight(h), {}, { { &FirstBaseline, baseline }, { &LastBaseline, h - 2 } });
        }
    };
    LayoutNode* Leaf(Modifier m, int w, int h, int b) { return &Layout(m, std::make_shared<BaselinePolicy>(w, h, b)); }

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
    int Top(LayoutNode* n) { return int(n->PositionInRoot().y); }
}

TEST(RowAlignsChildrenByBaseline) {
    Harness h;
    LayoutNode *a = nullptr, *b = nullptr, *row = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Row(Modifier{}, Arrangement::Start(), AlignmentV::Top(), [&] {
                a = Leaf(AlignByBaseline({}), 10, 40, 30);
                b = Leaf(AlignByBaseline({}), 10, 20, 10);
            });
        });
    });
    row = h.owner.Root().Children()[0]->Children()[0];
    CHECK_EQ(Top(a), 0);
    CHECK_EQ(Top(b), 20);                    // baselines meet at 30
    CHECK_EQ(row->Size().height, 40);        // 30 above the line + 10 below
    // The row reports the merged line from its children.
    const int merged = row->AsMeasurable().Measure(Constraints{ 0, 400, 0, 400 }).Get(FirstBaseline);
    CHECK_EQ(merged, 30);
}

TEST(LinesAreInheritedThroughModifiers) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(Padding(Modifier{}, PaddingValues{ Dp(0), Dp(5), Dp(0), Dp(0) }), Alignment::TopStart(), false, [&] { Leaf({}, 10, 40, 30); });
        });
    });
    LayoutNode* wrapper = h.owner.Root().Children()[0]->Children()[0];
    Placeable& p = wrapper->AsMeasurable().Measure(Constraints{ 0, 400, 0, 400 });
    CHECK_EQ(p.Get(FirstBaseline), 35);
    CHECK_EQ(p.Get(LastBaseline), 43);
}

TEST(PaddingFromBaselineAddsSpace) {
    Harness h;
    LayoutNode* leaf = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            leaf = Leaf(PaddingFromBaseline({}, Dp(50), Dp(20)), 10, 40, 30);
        });
    });
    // 20 above (50 - 30) and 18 below (20 - (40 - 38)).
    CHECK_EQ(leaf->Size().height, 20 + 40 + 18);
}
