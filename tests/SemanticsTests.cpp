#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(kiln::Function<void()> content) {
            composition.SetContent(std::move(content));
            composition.Recompose();
            owner.MeasureAndLayout({ 300, 300 });
        }
    };
    Modifier Label(Modifier m, std::string s) {
        return Semantics(std::move(m), [s](SemanticsConfiguration& c) { c.text.push_back(s); });
    }
    int Count(const SemanticsNode& n) {
        int k = 1;
        for (const auto& c : n.children) k += Count(c);
        return k;
    }
}

TEST(SemanticsTreeSkipsNodesWithoutContent) {
    Harness h;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Label(SizeMod({}, Dp(10), Dp(20)), "a"));
            Box(SizeMod({}, Dp(10), Dp(20)));
            Box(Modifier{}, Alignment::TopStart(), false, [&] { Box(Label(SizeMod({}, Dp(10), Dp(20)), "b")); });
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    CHECK_EQ(root.id, -1);
    CHECK_EQ(int(root.children.size()), 2);   // a, b (wrappers without content are transparent)
    CHECK_EQ(root.children[0].config.text[0], std::string("a"));
    CHECK_EQ(root.children[1].config.text[0], std::string("b"));
    CHECK_EQ(root.children[1].boundsInRoot.top, 40.f);
}

TEST(ClickableMergesDescendants) {
    Harness h;
    int clicks = 0;
    h.Run([&] {
        Row(Clickable({}, [&] { ++clicks; }), [&] {
            Box(Label(SizeMod({}, Dp(10), Dp(10)), "Save"));
            Box(Label(SizeMod({}, Dp(10), Dp(10)), "now"));
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    CHECK_EQ(int(root.children.size()), 1);
    const auto& button = root.children[0];
    CHECK_EQ(int(button.config.text.size()), 2);
    CHECK_EQ(button.config.text[1], std::string("now"));
    CHECK(button.children.empty());           // merged children are not exposed separately
    CHECK(bool(button.config.onClick));
    CHECK(button.config.onClick());
    CHECK_EQ(clicks, 1);
    // A clickable row holding a button: the button stays its own child node, below any
    // wrapper; the row merges the rest.
    int inner = 0;
    h.Run([&] {
        Row(Clickable({}, [&] { ++clicks; }), [&] {
            Box(Label(SizeMod({}, Dp(10), Dp(10)), "Row"));
            Box(Modifier{}, Alignment::TopStart(), false, [&] {
                Box(Label(Clickable(SizeMod({}, Dp(10), Dp(10)), [&] { ++inner; }), "Inner"));
            });
        });
    });
    const SemanticsNode nested = h.owner.BuildSemanticsTree();
    CHECK_EQ(int(nested.children.size()), 1);
    if (nested.children.size() == 1) {
        const auto& row = nested.children[0];
        CHECK(row.config.text == std::vector<std::string>{ "Row" });
        CHECK_EQ(int(row.children.size()), 1);
        if (row.children.size() == 1) {
            CHECK(row.children[0].config.text == std::vector<std::string>{ "Inner" });
            CHECK(row.children[0].config.onClick && row.children[0].config.onClick());
            CHECK_EQ(inner, 1);
        }
    }
}

TEST(ClearAndSetReplacesDescendants) {
    Harness h;
    h.Run([&] {
        Box(ClearAndSetSemantics({}, [](SemanticsConfiguration& c) { c.contentDescription.push_back("Avatar"); }), Alignment::TopStart(), false, [&] {
            Box(Label(SizeMod({}, Dp(10), Dp(10)), "hidden"));
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    CHECK_EQ(Count(root), 2);
    CHECK_EQ(root.children[0].config.contentDescription[0], std::string("Avatar"));
    CHECK(root.children[0].config.text.empty());
}

TEST(EmptyClearAndSetHidesDescendants) {
    // clearAndSetSemantics {} sets nothing: there is no node, and nothing below shows either
    // (a medium app bar hides its collapsed title row this way).
    Harness h;
    h.Run([&] {
        Box(ClearAndSetSemantics({}, [](SemanticsConfiguration&) {}), Alignment::TopStart(), false, [&] {
            Box(Label(SizeMod({}, Dp(10), Dp(10)), "hidden"));
        });
    });
    CHECK_EQ(Count(h.owner.BuildSemanticsTree()), 1);   // the root alone
    CHECK_EQ(Count(h.owner.BuildSemanticsTree(false)), 1);
}

TEST(SemanticsVersionBumpsOnChangeAndIdsStayStable) {
    Harness h;
    rt::State<std::string> label("one");
    rt::State<bool> show(true);
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Label(SizeMod({}, Dp(10), Dp(10)), label.Get()));
            if (show.Get()) Box(Label(SizeMod({}, Dp(10), Dp(10)), "extra"));
        });
    });
    const int id = h.owner.BuildSemanticsTree().children[0].id;
    auto v = h.owner.SemanticsVersion();
    label.Set("two");
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(h.owner.SemanticsVersion() != v);
    SemanticsNode root = h.owner.BuildSemanticsTree();
    CHECK_EQ(root.children[0].id, id);
    CHECK_EQ(root.children[0].config.text[0], std::string("two"));
    v = h.owner.SemanticsVersion();
    show.Set(false);
    h.composition.Recompose();
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK(h.owner.SemanticsVersion() != v);
    CHECK_EQ(int(h.owner.BuildSemanticsTree().children.size()), 1);
    CHECK(h.owner.FindBySemanticsId(id) != nullptr);
}

TEST(ZIndexDecidesWhichSiblingCoversTheOther) {
    // zSortedChildren: a sibling placed first but with a higher zIndex draws on top, so it is
    // the one left uncovered (a scaffold's FAB over its bottom bar).
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(Label(ZIndex(SizeMod({}, Dp(50), Dp(50)), 1), "top"));
            Box(Label(SizeMod({}, Dp(60), Dp(60)), "below"));
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    std::vector<std::string> kept;
    kiln::Function<void(const SemanticsNode&)> walk = [&](const SemanticsNode& n) {
        for (const auto& t : n.config.text) kept.push_back(t);
        for (const auto& c : n.children) walk(c);
    };
    walk(root);
    CHECK((kept == std::vector<std::string>{ "below", "top" }));   // drawing order
}

TEST(ClippedAndCoveredNodesAreDropped) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            // A 100x50 clipping viewport over a 100x200 column: rows below 50 px are clipped away.
            Box(ClipToBounds(SizeMod({}, Dp(100), Dp(50))), Alignment::TopStart(), false, [&] {
                Column(SizeMod(WrapContent({}, Axis::Vertical, Alignment::TopStart(), true), Dp(100), Dp(200)), [&] {
                    Box(Label(SizeMod({}, Dp(100), Dp(40)), "visible"));
                    Box(Label(SizeMod({}, Dp(100), Dp(40)), "partly"));
                    Box(Label(SizeMod({}, Dp(100), Dp(40)), "clipped"));
                });
            });
            // Drawn later at the same place: covers "under" completely.
            Box(Label(OffsetMod(SizeMod({}, Dp(50), Dp(50)), Dp(200), Dp(0)), "under"));
            Box(Label(OffsetMod(SizeMod({}, Dp(60), Dp(60)), Dp(195), Dp(0)), "over"));
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    std::vector<std::string> seen;
    for (const auto& c : root.children) seen.push_back(c.config.text[0]);
    CHECK_EQ(int(seen.size()), 3);
    CHECK_EQ(seen[0], std::string("visible"));
    CHECK_EQ(seen[1], std::string("partly"));
    CHECK_EQ(seen[2], std::string("over"));
    CHECK_EQ(root.children[1].boundsInRoot.bottom, 50.f);   // clipped to the viewport
}

#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Scroll.h"

namespace {
    const SemanticsNode* FindScrollable(const SemanticsNode& n) {
        if (n.config.scrollBy) return &n;
        for (const auto& c : n.children) if (const auto* f = FindScrollable(c)) return f;
        return nullptr;
    }
    void Frames(Harness& h, int n) {
        static std::int64_t t = 0;
        for (int i = 0; i < n; ++i) {
            t += 16'000'000;
            rt::Dispatcher::Main().RunFrame(t);
            rt::Composition::RecomposeAll();
            h.owner.MeasureAndLayout({ 300, 300 });
        }
    }
}

// Scrolling moves nodes without changing a configuration: accessibility hears of it, and what
// follows configurations alone (autofill's sync) has nothing to redo.
TEST(ScrollingInvalidatesSemanticsBoundsOnly) {
    Harness h;
    ScrollState state;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Column(VerticalScroll(Height(Modifier{}, Dp(100)), state), [] {
                for (int i = 0; i < 20; ++i) Box(Label(SizeMod({}, Dp(50), Dp(50)), "row"));
            });
        });
    });
    CHECK(state.MaxValue() > 120);
    const auto all = h.owner.SemanticsVersion(), config = h.owner.SemanticsConfigVersion();
    state.ScrollTo(120);
    Frames(h, 2);
    CHECK(h.owner.SemanticsVersion() != all);
    CHECK_EQ(h.owner.SemanticsConfigVersion(), config);
}

TEST(VerticalScrollExposesItsRangeAndScrollsByForAccessibility) {
    Harness h;
    ScrollState state;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Column(VerticalScroll(Height(Modifier{}, Dp(100)), state), [] {
                for (int i = 0; i < 10; ++i) Box(SizeMod(Modifier{}, Dp(50), Dp(50)));
            });
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    const SemanticsNode* n = FindScrollable(root);
    CHECK(n != nullptr);
    if (!n) return;
    CHECK(n->config.verticalScrollAxisRange.has_value());
    CHECK(!n->config.horizontalScrollAxisRange.has_value());
    CHECK_EQ(n->config.verticalScrollAxisRange->maxValue(), 400.0f);
    CHECK_EQ(n->config.verticalScrollAxisRange->value(), 0.0f);
    CHECK(n->config.scrollBy(0, 100));
    Frames(h, 60);
    CHECK_EQ(state.Value(), 100);
    CHECK_EQ(n->config.verticalScrollAxisRange->value(), 100.0f);
}

TEST(LazyColumnExposesCollectionInfoAndScrollToIndex) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            LazyColumn(Height(Modifier{}, Dp(100)), state, [](LazyListScope& s) {
                s.Items(40, [](int) { Box(SizeMod(Modifier{}, Dp(50), Dp(25))); });
            });
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    const SemanticsNode* n = FindScrollable(root);
    CHECK(n != nullptr);
    if (!n) return;
    CHECK(n->config.isTraversalGroup);
    CHECK(n->config.collectionInfo == (std::pair<int, int>{ 40, 1 }));
    CHECK_EQ(n->config.verticalScrollAxisRange->maxValue(), 100.0f);   // estimated: can go on
    CHECK_EQ(n->config.scrollViewportLength(), 100.0f);
    CHECK(n->config.scrollToIndex(20));
    h.owner.MeasureAndLayout({ 300, 300 });
    CHECK_EQ(state.FirstVisibleItemIndex(), 20);
    CHECK_EQ(n->config.verticalScrollAxisRange->value(), 10000.0f);
}

TEST(SemanticsBoundsFollowLayerTransforms) {
    Harness h;
    h.Run([&] {
        GraphicsLayerParams p;
        p.translationX = 40;
        p.scaleY = 2;   // about the centre: 20 tall becomes 40, from y = -10
        Box(FillMax(Modifier{}, Axis::Both), Alignment::TopStart(), false, [&] {
            Box(Label(SizeMod(GraphicsLayer(Modifier{}, p), Dp(10), Dp(20)), "moved"));
        });
    });
    const auto root = h.owner.BuildSemanticsTree();
    CHECK_EQ(root.children.size(), 1u);
    const Rect b = root.children[0].boundsInRoot;
    CHECK_EQ(b.left, 40.f);
    CHECK_EQ(b.right, 50.f);
    CHECK_EQ(b.top, 0.f);   // -10, clipped to the window
    CHECK_EQ(b.bottom, 30.f);
}

TEST(SemanticsBoundsComeFromTheSemanticsModifierSpace) {
    // size(100, 40).padding(10).clickable: the clickable's bounds are inside the padding;
    // size(100, 40).clickable.padding(10): they cover the whole box.
    Harness h;
    h.Run([&] {
        Column(Modifier{}, [&] {
            Box(Clickable(Padding(SizeMod(Modifier{}, Dp(100), Dp(40)), Dp(10)), [] {}));
            Box(Padding(Clickable(SizeMod(Modifier{}, Dp(100), Dp(40)), [] {}), Dp(10)));
        });
    });
    const auto root = h.owner.BuildSemanticsTree();
    CHECK_EQ(int(root.children.size()), 2);
    if (root.children.size() == 2) {
        const Rect a = root.children[0].boundsInRoot, b = root.children[1].boundsInRoot;
        CHECK(a.left == 10 && a.top == 10 && a.right == 90 && a.bottom == 30);
        CHECK(b.left == 0 && b.top == 40 && b.right == 100 && b.bottom == 80);
    }
}

TEST(AClipAfterAnOffsetClipsWhereItWasMoved) {
    // offset then clip (a sheet moved by its drag offset): the content is clipped at the moved
    // rect, not at the node's unmoved bounds.
    Harness h;
    h.Run([&] {
        Modifier moved = LayoutMod(SizeMod({}, Dp(100), Dp(50)), [](MeasureScope& s, Measurable& m, const Constraints& c) {
            Placeable& p = m.Measure(c);
            return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 200); });
        });
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(ClipToBounds(moved), Alignment::TopStart(), false, [&] {
                Box(Label(SizeMod({}, Dp(10), Dp(10)), "inside"));
            });
        });
    });
    const SemanticsNode root = h.owner.BuildSemanticsTree();
    CHECK_EQ(int(root.children.size()), 1);
    if (root.children.empty()) return;
    CHECK_EQ(root.children[0].config.text[0], std::string("inside"));
    CHECK_EQ(root.children[0].boundsInRoot.top, 200.f);
    CHECK_EQ(root.children[0].boundsInRoot.bottom, 210.f);
}
