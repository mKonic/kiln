#include "Check.h"
#include <cmath>

#include <cmath>
#include "kiln/foundation/Compose.h"
#include <cmath>

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

namespace {
    Offset MapH(const float h[9], Offset p) {
        const float w = h[6] * p.x + h[7] * p.y + h[8];
        return { (h[0] * p.x + h[1] * p.y + h[2]) / w, (h[3] * p.x + h[4] * p.y + h[5]) / w };
    }
}

TEST(GraphicsLayerMatrixRotatesInPerspective) {
    const Size size{ 200, 100 };   // pivot at the centre (100, 50)
    float h[9];
    GraphicsLayerParams p;
    p.rotationY = 90;              // edge-on: everything on the vertical line through the pivot
    GraphicsLayerMatrix(p, size, h);
    CHECK(std::abs(MapH(h, { 0, 0 }).x - 100) < 0.01f);
    CHECK(std::abs(MapH(h, { 200, 100 }).x - 100) < 0.01f);
    p.rotationY = 180;             // mirrored left to right, no perspective left
    GraphicsLayerMatrix(p, size, h);
    CHECK(std::abs(MapH(h, { 0, 0 }).x - 200) < 0.01f && std::abs(MapH(h, { 0, 0 }).y) < 0.01f);
    p.rotationY = 0;
    p.rotationX = 180;             // upside down
    GraphicsLayerMatrix(p, size, h);
    CHECK(std::abs(MapH(h, { 0, 0 }).y - 100) < 0.01f && std::abs(MapH(h, { 0, 0 }).x) < 0.01f);
    // A tilt: one vertical edge comes nearer (taller), the other recedes (shorter).
    p.rotationX = 0;
    p.rotationY = 60;
    GraphicsLayerMatrix(p, size, h);
    const float left = MapH(h, { 0, 100 }).y - MapH(h, { 0, 0 }).y, right = MapH(h, { 200, 100 }).y - MapH(h, { 200, 0 }).y;
    CHECK(std::abs(left - right) > 5);
    CHECK(std::abs((left + right) / 2 - 100) < 15);
    // No rotation: the 2D path (translation, rotation, scale about the pivot).
    GraphicsLayerParams q;
    q.translationX = 10;
    q.scaleX = 2;
    GraphicsLayerMatrix(q, size, h);
    CHECK(std::abs(MapH(h, { 0, 0 }).x - (-100 + 10)) < 0.01f);
}

TEST(GraphicsLayerBlockReadsStateWithoutRecomposing) {
    Harness h;
    rt::State<float> angle{ 0.0f };
    LayoutNode* inner = nullptr;
    int compositions = 0;
    h.Run([&] {
        ++compositions;
        Box(GraphicsLayer(SizeMod(Modifier{}, 40_dp, 20_dp), [&](GraphicsLayerParams& p) { p.rotationZ = angle.Get(); }),
            Alignment::TopStart(), false, [&] { Box(Capture(SizeMod(Modifier{}, 40_dp, 20_dp), inner)); });
    });
    CHECK(inner != nullptr);
    const Rect before = inner->BoundsInRoot();
    CHECK(std::abs(before.Width() - 40) < 0.5f);
    angle.Set(90);
    rt::Observation::ApplyChanges();
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 100, 100 });
    CHECK_EQ(compositions, 1);   // the block re-ran, the content did not recompose
    const Rect after = inner->BoundsInRoot();
    CHECK(std::abs(after.Width() - 20) < 0.5f && std::abs(after.Height() - 40) < 0.5f);
}

TEST(ProvidedLayoutDirectionAppliesToItsSubtree) {
    // An RTL window with an LTR subtree (and the reverse): rows place by the direction they
    // are composed under, not the window's.
    for (auto window : { LayoutDirection::Rtl, LayoutDirection::Ltr }) {
        const auto inner = window == LayoutDirection::Rtl ? LayoutDirection::Ltr : LayoutDirection::Rtl;
        Harness h(window);
        h.Run([&] {
            Column(Modifier{}, [&] {
                Row(Width(Modifier{}, Dp(100)), [&] { Box(SizeMod(Modifier{}, Dp(10), Dp(10))); });
                rt::Composition::Current()->Provide(ui::LocalLayoutDirection, inner, [&] {
                    Row(Width(Modifier{}, Dp(100)), [&] { Box(SizeMod(Modifier{}, Dp(10), Dp(10))); });
                });
            });
        });
        const LayoutNode* column = h.owner.Root().Children()[0];
        const LayoutNode* outerFirst = column->Children()[0]->Children()[0];
        const LayoutNode* innerFirst = column->Children()[1]->Children()[0];
        const float outerX = outerFirst->BoundsInRoot().left, innerX = innerFirst->BoundsInRoot().left;
        CHECK_EQ(outerX, window == LayoutDirection::Rtl ? 90.f : 0.f);
        CHECK_EQ(innerX, inner == LayoutDirection::Rtl ? 90.f : 0.f);
    }
}

TEST(MultiContentLayoutGroupsChildrenByContent) {
    // Layout(contents): the policy sees each content's children apart; the nodes are the
    // layout's children, and the empty contents stay as empty groups.
    struct Policy final : MultiContentMeasurePolicy {
        std::vector<std::size_t> sizes;
        MeasureResult Measure(MeasureScope& s, const std::vector<std::vector<Measurable*>>& contents, const Constraints& c) override {
            sizes.clear();
            std::vector<Placeable*> placed;
            int x = 0;
            for (const auto& group : contents) {
                sizes.push_back(group.size());
                for (Measurable* m : group) placed.push_back(&m->Measure(c.CopyMaxDimensions()));
            }
            return s.Layout(c.maxWidth, c.maxHeight, [placed, x](PlacementScope& p) mutable {
                for (Placeable* pl : placed) { p.Place(*pl, x, 0); x += pl->Width(); }
            });
        }
    };
    Harness h;
    auto policy = std::make_shared<Policy>();
    rt::State<bool> third{ false };
    LayoutNode *a = nullptr, *c = nullptr;
    h.Run([&] {
        Layout(Modifier{}, { [&] { Box(Capture(SizeMod(Modifier{}, Dp(10), Dp(10)), a)); Box(SizeMod(Modifier{}, Dp(20), Dp(10))); },
                             [] {},
                             [&] { if (third.Get()) Box(Capture(SizeMod(Modifier{}, Dp(5), Dp(5)), c)); } },
               policy);
    });
    CHECK((policy->sizes == std::vector<std::size_t>{ 2, 0, 0 }));
    third.Set(true);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 100, 100 });
    CHECK((policy->sizes == std::vector<std::size_t>{ 2, 0, 1 }));
    CHECK(a && c && a->Parent() == c->Parent());
    CHECK(c && int(c->PositionInRoot().x) == 30);   // after the first content's two
}

TEST(LayoutIdTagsChildrenForTheirParent) {
    Owner owner;
    rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
    owner.SetDensity({ 1, 1 });
    std::vector<std::string> ids;
    composition.SetContent([&] {
        auto policy = std::make_shared<LambdaMeasurePolicy>([&](MeasureScope& s, std::span<Measurable* const> children, const Constraints&) {
            ids.clear();
            for (Measurable* m : children) {
                const std::string* id = LayoutIdOf(*m);
                ids.push_back(id ? *id : "-");
            }
            return s.Layout(0, 0, [](PlacementScope&) {});
        });
        Layout(Modifier{}, policy, [] {
            Box(LayoutId(Modifier{}, "a"));
            Box(Modifier{});
            Box(Padding(LayoutId(Modifier{}, "c"), Dp(2)));
        });
    });
    composition.Recompose();
    owner.MeasureAndLayout({ 100, 100 });
    CHECK((ids == std::vector<std::string>{ "a", "-", "c" }));
}
