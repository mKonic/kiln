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

TEST(ModifierLocalModifiersAndOnPlaced) {
    Harness h;
    rt::State<int> value(7);
    std::vector<int> consumed;
    int placed = 0;
    h.Run([&] {
        Box(ModifierLocalProvider<int>(Modifier{}, LocalDepth, [&] { return value.Get(); }), Alignment::TopStart(), false, [&] {
            Box(OnPlaced(ModifierLocalConsumer(Modifier{}, [&](const ModifierNode& n) { consumed.push_back(n.Current(LocalDepth)); }),
                         [&](const LayoutCoordinates& c) { ++placed; CHECK(c.IsAttached()); }));
        });
    });
    CHECK(!consumed.empty());
    CHECK_EQ(consumed.back(), 7);
    CHECK(placed > 0);
    value.Set(8);   // the provider provides anew, the consumer reads again
    h.Pump();
    CHECK_EQ(consumed.back(), 8);
}

#include "kiln/ui/PlatformServices.h"

TEST(PlatformServiceLocalsDefaultToTheOwner) {
    Harness h;
    std::shared_ptr<FocusManager> focus;
    std::shared_ptr<Clipboard> clipboard;
    std::shared_ptr<UriHandler> uris;
    std::shared_ptr<TextToolbar> toolbar;
    std::vector<std::string> locales;
    std::string opened;
    h.owner.SetUriHandler([&](const std::string& u) { opened = u; return true; });
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalOwner, &h.owner, [&] {
            rt::Composition::Current()->Provide(LocalLocaleList, std::optional<std::vector<std::string>>({ "de-DE", "en-US" }), [&] {
                focus = CurrentFocusManager();
                clipboard = CurrentClipboard();
                uris = CurrentUriHandler();
                toolbar = CurrentTextToolbar();
                locales = CurrentLocaleList();
            });
        });
    });
    CHECK(focus && clipboard && uris && toolbar);
    CHECK((locales == std::vector<std::string>{ "de-DE", "en-US" }));
    uris->OpenUri("https://example.com");
    CHECK_EQ(opened, std::string("https://example.com"));
    // A clip entry round-trips through the owner's clipboard (no host: kept locally as text).
    auto set = rt::Launch(rt::Dispatcher::Main(), clipboard->SetClipEntry(ClipEntry{ { ClipItem{ "hello", std::nullopt, std::nullopt } } }));
    std::optional<ClipEntry> got;
    auto get = rt::Launch(rt::Dispatcher::Main(), [&]() -> rt::Task<> { got = co_await clipboard->GetClipEntry(); }());
    h.Pump();
    CHECK(got && got->items.size() == 1 && got->items[0].text == std::optional<std::string>("hello"));
    // The legacy toolbar shows the asked items and runs the clicked one.
    bool copied = false;
    toolbar->ShowMenu({ 0, 0, 10, 10 }, [&] { copied = true; }, {}, {}, [] {});
    CHECK(toolbar->Status() == TextToolbarStatus::Shown);
    CHECK_EQ(int(h.owner.TextToolbarEntries().size()), 2);
    h.owner.OnTextToolbarItem(0);
    CHECK(copied);
    CHECK(toolbar->Status() == TextToolbarStatus::Hidden);
}

#include "kiln/foundation/DrawCache.h"

TEST(DrawWithCacheRebuildsOnlyWhenItsInputsChange) {
    Harness h;
    rt::State<float> width(40), tint(1);
    int builds = 0, draws = 0;
    float seenWidth = 0;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(DrawWithCache(SizeMod(Modifier{}, Dp(width.Get()), Dp(20)), [&](CacheDrawScope& c) {
                ++builds;
                seenWidth = c.GetSize().width;
                (void)tint.Get();   // read while building: a change rebuilds
                auto layer = c.ObtainGraphicsLayer();
                return c.OnDrawBehind([&, layer](ui::DrawScope&) { ++draws; });
            }));
        });
    });
    auto draw = [&] { h.owner.Draw(ui::DrawTarget{}); };
    draw();
    draw();
    CHECK_EQ(builds, 1);
    CHECK_EQ(draws, 2);
    CHECK_EQ(seenWidth, 40.0f);
    tint.Set(2);
    h.Pump();
    draw();
    CHECK(builds >= 2);
    const int before = builds;
    width.Set(60);
    h.Pump();
    draw();
    CHECK(builds > before);
    CHECK_EQ(seenWidth, 60.0f);
}
