#include "Check.h"

#include "kiln/foundation/Compose.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    // Captures the LayoutNode a modifier lands on.
    struct CaptureParams { LayoutNode** out; bool operator==(const CaptureParams&) const = default; };
    struct CaptureNode final : ModifierNode {
        CaptureParams params;
        explicit CaptureNode(CaptureParams p) : params(p) {}
        void OnParamsChanged() {}
        void OnAttach() override { *params.out = Owner(); }
    };
    Modifier Capture(Modifier m, LayoutNode*& out) { return m.With<CaptureNode>(CaptureParams{ &out }); }

    struct Harness {
        Owner owner;
        rt::Dispatcher dispatcher;
        rt::Composition composition{ owner.Root(), dispatcher };
        explicit Harness(LayoutDirection dir = LayoutDirection::Ltr) { owner.SetDensity({ 1, 1 }); owner.SetDirection(dir); }
        void Run(std::function<void()> content, IntSize window = { 100, 100 }) {
            composition.SetContent(std::move(content));
            composition.Recompose();
            owner.MeasureAndLayout(window);
        }
    };

    std::vector<int> Arrange(Arrangement a, std::vector<int> sizes, int total, LayoutDirection dir = LayoutDirection::Ltr) {
        std::vector<int> out(sizes.size());
        a.Arrange(Density{ 1, 1 }, total, sizes, dir, out);
        return out;
    }
    using V = std::vector<int>;
}

TEST(ArrangementPositions) {
    CHECK(Arrange(Arrangement::SpaceEvenly(), { 10, 10 }, 100) == V({ 27, 63 }));
    CHECK(Arrange(Arrangement::SpaceBetween(), { 10, 10 }, 100) == V({ 0, 90 }));
    CHECK(Arrange(Arrangement::SpaceAround(), { 10, 10 }, 100) == V({ 20, 70 }));
    CHECK(Arrange(Arrangement::Center(), { 10, 10 }, 100) == V({ 40, 50 }));
    CHECK(Arrange(Arrangement::End(), { 10, 10 }, 100) == V({ 80, 90 }));
    // RTL: Start packs at the right edge, first child rightmost.
    CHECK(Arrange(Arrangement::Start(), { 10, 10 }, 100, LayoutDirection::Rtl) == V({ 90, 80 }));
    CHECK(Arrange(Arrangement::End(), { 10, 10 }, 100, LayoutDirection::Rtl) == V({ 10, 0 }));
    CHECK(Arrange(Arrangement::AbsoluteLeft(), { 10, 10 }, 100, LayoutDirection::Rtl) == V({ 0, 10 }));
    // SpaceBetween with one child in RTL is right-aligned.
    CHECK(Arrange(Arrangement::SpaceBetween(), { 10 }, 100, LayoutDirection::Rtl) == V({ 90 }));
    CHECK(Arrange(Arrangement::SpacedBy(Dp(5)), { 10, 10 }, 100) == V({ 0, 15 }));
    CHECK(Arrange(Arrangement::SpacedBy(Dp(5)), { 10, 10 }, 100, LayoutDirection::Rtl) == V({ 90, 75 }));
    CHECK(Arrange(Arrangement::SpacedBy(Dp(5), AlignmentH::CenterHorizontally()), { 10, 10 }, 100) == V({ 38, 53 }));
    // spacedBy never overflows: the gap collapses at the end.
    CHECK(Arrange(Arrangement::SpacedBy(Dp(50)), { 40, 40 }, 100) == V({ 0, 60 }));
}

TEST(RowWeightsDistributeRemainderToFirstChildren) {
    Harness h;
    LayoutNode *a = nullptr, *b = nullptr, *c = nullptr;
    h.Run([&] {
        Row(FillMax(Modifier{}, Axis::Both), [&] {
            Box(Capture(Weight(Modifier{}, 1), a));
            Box(Capture(Weight(Modifier{}, 1), b));
            Box(Capture(Weight(Modifier{}, 1), c));
        });
    });
    CHECK_EQ(a->Size().width, 34);
    CHECK_EQ(b->Size().width, 33);
    CHECK_EQ(c->Size().width, 33);
    CHECK_EQ(b->Position().x, 34);
    CHECK_EQ(c->Position().x, 67);
    CHECK_EQ(a->Size().height, 0);   // weighted children get cross min 0; Box with no content
}

TEST(RowFixedThenWeighted) {
    Harness h;
    LayoutNode *fixed = nullptr, *weighted = nullptr;
    h.Run([&] {
        Row(FillMax(Modifier{}, Axis::Horizontal), Arrangement::SpacedBy(Dp(10)), AlignmentV::Top(), [&] {
            Box(Capture(SizeMod(Modifier{}, Dp(30), Dp(20)), fixed));
            Box(Capture(Height(Weight(Modifier{}, 1), Dp(5)), weighted));
        });
    });
    CHECK_EQ(weighted->Size().width, 60);   // 100 - 30 - 10 spacing
    CHECK_EQ(weighted->Position().x, 40);
}

TEST(PaddingIsRtlAware) {
    Harness h(LayoutDirection::Rtl);
    LayoutNode* child = nullptr;
    h.Run([&] {
        Box(SizeMod(Modifier{}, Dp(100), Dp(100)), Alignment::TopStart(), false, [&] {
            // A node's size includes its padding; the content inside is what moves.
            Box(FillMax(Padding(Modifier{}, PaddingValues{ Dp(10), Dp(0), Dp(20), Dp(0) }), Axis::Both), Alignment::TopStart(), false, [&] {
                Box(Capture(FillMax(Modifier{}, Axis::Both), child));
            });
        });
    });
    CHECK_EQ(child->Size().width, 70);
    CHECK_EQ(child->PositionInRoot().x, 20.0f);   // 100 - 70 - start(10)
}

TEST(BoxAlignmentAndRequiredOverflow) {
    Harness h;
    LayoutNode *centered = nullptr, *big = nullptr;
    h.Run([&] {
        Box(SizeMod(Modifier{}, Dp(100), Dp(100)), Alignment::Center(), false, [&] {
            Box(Capture(SizeMod(Modifier{}, Dp(20), Dp(20)), centered));
        });
    });
    CHECK_EQ(centered->Position().x, 40);
    CHECK_EQ(centered->Position().y, 40);
    Harness h2;
    h2.Run([&] {
        Box(SizeMod(Modifier{}, Dp(100), Dp(100)), Alignment::TopStart(), false, [&] {
            Box(Capture(RequiredSize(Modifier{}, Dp(150), Dp(150)), big));
        });
    });
    // Measured 150 but seen as 100 by the parent; the overflow is centred: -25.
    CHECK_EQ(big->Position().x, -25);
}

TEST(IntrinsicsThroughPaddingAndColumn) {
    Harness h;
    LayoutNode* col = nullptr;
    h.Run([&] {
        Column(Capture(Modifier{}, col), [&] {
            Box(SizeMod(Modifier{}, Dp(30), Dp(10)));
            Box(Padding(SizeMod(Modifier{}, Dp(50), Dp(10)), Dp(5)));
        });
    });
    // Second child is size(50x10) then padding inside it, so the size wins: 50 wide, 10 tall.
    CHECK_EQ(col->AsMeasurable().MaxIntrinsicWidth(Constraints::Infinity), 50);
    CHECK_EQ(col->AsMeasurable().MaxIntrinsicHeight(Constraints::Infinity), 20);
    // Padding outside the size adds to both axes.
    Harness h2;
    LayoutNode* col2 = nullptr;
    h2.Run([&] {
        Column(Capture(Modifier{}, col2), [&] {
            Box(SizeMod(Padding(Modifier{}, Dp(5)), Dp(50), Dp(10)));
        });
    });
    CHECK_EQ(col2->AsMeasurable().MaxIntrinsicWidth(Constraints::Infinity), 60);
    CHECK_EQ(col2->AsMeasurable().MinIntrinsicHeight(Constraints::Infinity), 20);
}

TEST(StateChangeRemeasuresOnlyWhatChanged) {
    Harness h;
    rt::State<float> width(10);
    LayoutNode *box = nullptr, *after = nullptr;
    h.Run([&] {
        Row(Modifier{}, [&] {
            Box(Capture(SizeMod(Modifier{}, Dp(width.Get()), Dp(10)), box));
            Box(Capture(SizeMod(Modifier{}, Dp(10), Dp(10)), after));
        });
    });
    CHECK_EQ(after->Position().x, 10);
    width.Set(25);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 100, 100 });
    CHECK_EQ(box->Size().width, 25);
    CHECK_EQ(after->Position().x, 25);
}
