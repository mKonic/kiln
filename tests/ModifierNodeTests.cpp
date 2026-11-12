// Node features: modifier locals, observed reads, traversal by key, placement callbacks and
// composition locals read from a node.
#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Observation::ApplyChanges();
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 400 });
            }
        }
    };

    inline ModifierLocal<int> LocalDepth = ModifierLocalOf<int>([] { return -1; });
    inline rt::Local<int> LocalAnswer{ 0 };
    const char kTreeKey = 0;

    // A node that provides `provide` (when >= 0), and records what it sees and is told.
    struct Probe {
        int provide = -1;
        int id = 0;
        bool operator==(const Probe&) const = default;
    };
    struct ProbeNode final : ModifierNode, TraversableNode, LayoutAwareModifier {
        Probe params;
        int seenLocal = -2, seenAnswer = -2, placed = 0, observed = 0;
        IntSize remeasured{ -1, -1 };
        rt::State<int>* watched = nullptr;
        explicit ProbeNode(Probe p) : params(p) {}
        void OnParamsChanged() {}
        void OnAttach() override {
            if (params.provide >= 0) Provide(LocalDepth, params.provide);
        }
        TraversableNode* AsTraversable() override { return this; }
        LayoutAwareModifier* AsLayoutAware() override { return this; }
        const void* TraverseKey() const override { return &kTreeKey; }
        void OnPlaced(const LayoutCoordinates&) override {
            ++placed;
            seenLocal = Current(LocalDepth);
            seenAnswer = CurrentValueOf(*this, LocalAnswer);
        }
        void OnRemeasured(IntSize s) override { remeasured = s; }
        void Watch(rt::State<int>& s) { watched = &s; ObserveReads([this] { (void)watched->Get(); }); }
        void OnObservedReadsChanged() override { ++observed; Watch(*watched); }
    };
    Modifier ProbeMod(Modifier m, int id, int provide = -1) { return m.With<ProbeNode>(Probe{ provide, id }); }

    std::vector<ProbeNode*> Probes(LayoutNode& root) {
        std::vector<ProbeNode*> out;
        std::function<void(LayoutNode&)> walk = [&](LayoutNode& n) {
            for (const ModifierNode* m : n.ModifierNodes())
                if (auto* p = dynamic_cast<const ProbeNode*>(m)) out.push_back(const_cast<ProbeNode*>(p));
            for (LayoutNode* c : n.Children()) walk(*c);
        };
        walk(root);
        return out;
    }
    ProbeNode* ById(LayoutNode& root, int id) {
        for (ProbeNode* p : Probes(root)) if (p->params.id == id) return p;
        return nullptr;
    }
}

TEST(ModifierLocalsReachLaterNodesAndDescendantsButNotTheProvider) {
    Harness h;
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalAnswer, 42, [&] {
            // 1 provides 10 and sees nothing above; 2 (after it) sees 10; 3 below provides 20
            // for 4, its child; 5 is a sibling of 3 and sees 10.
            Box(ProbeMod(ProbeMod(Modifier{}, 1, 10), 2), Alignment::TopStart(), false, [&] {
                Box(ProbeMod(SizeMod(Modifier{}, Dp(30), Dp(20)), 3, 20), Alignment::TopStart(), false, [&] {
                    Box(ProbeMod(Modifier{}, 4));
                });
                Box(ProbeMod(Modifier{}, 5));
            });
        });
    });
    auto& root = h.owner.Root();
    CHECK_EQ(ById(root, 1)->seenLocal, -1);
    CHECK_EQ(ById(root, 2)->seenLocal, 10);
    CHECK_EQ(ById(root, 3)->seenLocal, 10);
    CHECK_EQ(ById(root, 4)->seenLocal, 20);
    CHECK_EQ(ById(root, 5)->seenLocal, 10);
    CHECK_EQ(ById(root, 4)->seenAnswer, 42);   // the CompositionLocal where it was composed
    CHECK(ById(root, 3)->placed > 0);
    CHECK((ById(root, 3)->remeasured == IntSize{ 30, 20 }));
}

TEST(TraversalFindsNodesByKeyAndType) {
    Harness h;
    h.Run([&] {
        Box(ProbeMod(Modifier{}, 1), Alignment::TopStart(), false, [&] {
            Box(Modifier{}, Alignment::TopStart(), false, [&] {   // no probe here
                Box(ProbeMod(Modifier{}, 2), Alignment::TopStart(), false, [&] { Box(ProbeMod(Modifier{}, 3)); });
            });
            Box(ProbeMod(Modifier{}, 4));
        });
    });
    auto& root = h.owner.Root();
    ProbeNode* three = ById(root, 3);
    std::vector<int> up;
    TraverseAncestors<ProbeNode>(*three, [&](ProbeNode& p) { up.push_back(p.params.id); return true; });
    CHECK((up == std::vector<int>{ 2, 1 }));
    CHECK_EQ(FindNearestAncestor(*three)->params.id, 2);
    std::vector<int> children;
    TraverseChildren<ProbeNode>(*ById(root, 1), [&](ProbeNode& p) { children.push_back(p.params.id); return true; });
    CHECK((children == std::vector<int>{ 2, 4 }));   // not 3: below a match
    std::vector<int> all;
    TraverseDescendants<ProbeNode>(*ById(root, 1), [&](ProbeNode& p) { all.push_back(p.params.id); return TraverseDescendantsAction::ContinueTraversal; });
    CHECK((all == std::vector<int>{ 2, 3, 4 }));
    all.clear();
    TraverseDescendants<ProbeNode>(*ById(root, 1), [&](ProbeNode& p) {
        all.push_back(p.params.id);
        return p.params.id == 2 ? TraverseDescendantsAction::SkipSubtreeAndContinueTraversal : TraverseDescendantsAction::ContinueTraversal;
    });
    CHECK((all == std::vector<int>{ 2, 4 }));
}

TEST(ObserveReadsCallsBackOncePerChange) {
    Harness h;
    rt::State<int> s(0);
    h.Run([&] { Box(ProbeMod(Modifier{}, 1)); });
    ProbeNode* p = ById(h.owner.Root(), 1);
    p->Watch(s);
    s.Set(1);
    h.Pump();
    CHECK_EQ(p->observed, 1);
    s.Set(2);
    h.Pump();
    CHECK_EQ(p->observed, 2);   // observed again from the callback
}
