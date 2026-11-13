#include "Check.h"
#include <kiln/Function.h>

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
        void Run(kiln::Function<void()> content) {
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

TEST(IntrinsicHeightMatchesTheTallestSibling) {
    // The classic: a Row of height(IntrinsicSize.Min) whose divider fills the height.
    Harness h;
    LayoutNode* divider = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Row(Height(Modifier{}, IntrinsicSize::Min), Arrangement::Start(), AlignmentV::Top(), [&] {
                Leaf({}, 10, 40, 30);
                divider = &Layout(FillMax(Width(Modifier{}, Dp(1)), Axis::Vertical), std::make_shared<BaselinePolicy>(1, 0, 0));
                Leaf({}, 10, 25, 20);
            });
        });
    });
    CHECK_EQ(divider->Size().height, 40);
}

TEST(IntrinsicWidthOfAColumnIsItsWidestChild) {
    Harness h;
    LayoutNode *a = nullptr, *b = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Column(Width(Modifier{}, IntrinsicSize::Max), Arrangement::Top(), AlignmentH::Start(), [&] {
                a = Leaf(FillMax(Modifier{}, Axis::Horizontal), 30, 10, 5);
                b = Leaf(FillMax(Modifier{}, Axis::Horizontal), 70, 10, 5);
            });
        });
    });
    CHECK_EQ(a->Size().width, 70);
    CHECK_EQ(b->Size().width, 70);
}

TEST(AlignByBlockUsesTheMeasuredChild) {
    Harness h;
    LayoutNode *a = nullptr, *b = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Row(Modifier{}, Arrangement::Start(), AlignmentV::Top(), [&] {
                // Centres on one line: 20 into a, 40 into b.
                a = Leaf(AlignBy(Modifier{}, [](Placeable& p) { return p.Height() / 2; }), 10, 40, 0);
                b = Leaf(AlignBy(Modifier{}, [](Placeable& p) { return p.Height() / 2; }), 10, 80, 0);
            });
        });
    });
    // The shared line sits 40 down: a starts at 20, b at 0; the row is 40 + 40 tall.
    CHECK_EQ(Top(a), 20);
    CHECK_EQ(Top(b), 0);
    CHECK_EQ(a->Parent()->Size().height, 80);
}

namespace {
    // A fixed-size leaf whose baseline comes from state read during its own measure.
    struct StateBaselinePolicy final : MeasurePolicy {
        rt::State<int>* baseline;
        explicit StateBaselinePolicy(rt::State<int>* b) : baseline(b) {}
        MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
            return s.Layout(c.ConstrainWidth(10), c.ConstrainHeight(40), {}, { { &FirstBaseline, baseline->Get() } });
        }
    };
}

TEST(ParentFollowsAMovedLineOfAnUnresizedChild) {
    Harness h;
    rt::State<int> base(10);
    LayoutNode *fixed = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Row(Modifier{}, Arrangement::Start(), AlignmentV::Top(), [&] {
                Layout(AlignByBaseline(Modifier{}), std::make_shared<StateBaselinePolicy>(&base));
                fixed = Leaf(AlignByBaseline(Modifier{}), 10, 40, 30);
            });
        });
    });
    // Line at max(10, 30) = 30: the fixed leaf at the top.
    CHECK_EQ(Top(fixed), 0);
    // Only the first child re-measures (same size); its baseline at 35 moves the shared line.
    base.Set(35);
    rt::Composition::RecomposeAll();
    h.owner.MeasureAndLayout({ 400, 400 });
    h.owner.MeasureAndLayout({ 400, 400 });
    CHECK_EQ(Top(fixed), 5);
}

TEST(PaddingFromBaselineReadsTheBaselineOfItsContent) {
    // The line comes from a child of the padded node: it is laid out early to read it.
    Harness h;
    LayoutNode* leaf = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(PaddingFromBaseline({}, Dp(50), Dp::Unspecified()), Alignment::TopStart(), false, [&] {
                leaf = Leaf(Padding({}, PaddingValues{ Dp(0), Dp(4), Dp(0), Dp(0) }), 10, 40, 30);
            });
        });
    });
    // The baseline sits 34 into the box (its padding is part of the leaf): 16 above it.
    CHECK_EQ(Top(leaf), 16);
}
