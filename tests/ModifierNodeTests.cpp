// Node features: modifier locals, observed reads, traversal by key, placement callbacks and
// composition locals read from a node.
#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/MediaQuery.h"
#include "kiln/foundation/FrameRate.h"
#include "kiln/ui/PlatformTextInput.h"
#include "kiln/foundation/Lighting.h"

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

TEST(VisibilityBoundsAndDebouncedRectCallbacks) {
    rt::Dispatcher::Main().SetVirtualTime(true);
    LayoutBoundsHolder holder;   // before the harness: its teardown clears it
    std::vector<bool> visible;
    std::vector<IntRect> rects;
    Harness h;
    rt::State<int> y(0);
    h.Run([&] {
        Box(OffsetMod(Modifier{}, Dp(0), Dp(float(y.Get()))), Alignment::TopStart(), false, [&] {
            Box(OnLayoutRectChanged(OnVisibilityChanged(LayoutBounds(SizeMod(Modifier{}, Dp(100), Dp(100)), holder),
                                                        [&](bool v) { visible.push_back(v); }, 0, 0.5f),
                                    [&](const RelativeLayoutBounds& b) { rects.push_back(b.BoundsInRoot()); }));
        });
    });
    CHECK((visible == std::vector<bool>{ true }));
    CHECK(holder.Bounds().has_value());
    CHECK_EQ(holder.Bounds()->Height(), 100);
    CHECK_EQ(int(rects.size()), 1);   // the first report is immediate
    y.Set(350);   // a quarter inside the 400 px window: not more than half
    h.Pump();
    CHECK((visible == std::vector<bool>{ true, false }));
    CHECK_EQ(holder.Bounds()->PositionInRoot().y, 350);
    CHECK_EQ(int(rects.size()), 1);   // debounced (64 ms)
    rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(70));
    h.Pump();
    CHECK_EQ(int(rects.size()), 2);
    CHECK_EQ(rects.back().top, 350);
    rt::Dispatcher::Main().SetVirtualTime(false);
}

#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Semantics.h"

namespace { inline ui::SemanticsPropertyKey<int> PriorityKey{ "Priority", [](const std::optional<int>& p, const int& c) { return std::optional<int>((p ? *p : 0) + c); } }; }

TEST(CustomSemanticsKeysMergeAndLazyIndexForKey) {
    Harness h;
    h.Run([&] {
        Box(Semantics(Modifier{}, [](SemanticsConfiguration& c) { c.Set(PriorityKey, 1); }, true), Alignment::TopStart(), false, [&] {
            Box(Semantics(SizeMod(Modifier{}, Dp(10), Dp(10)), [](SemanticsConfiguration& c) { c.Set(PriorityKey, 2); }));
        });
        LazyColumn(SizeMod(Modifier{}, Dp(100), Dp(100)), RememberLazyListState(), [](LazyListScope& s) {
            s.Items(10, [](int) { Box(SizeMod(Modifier{}, Dp(10), Dp(10))); }, [](int i) { return rt::Key(1000 + i); });
        });
    });
    int merged = -1, index = -1;
    for (const auto& n : GetAllSemanticsNodes(h.owner)) {
        if (const int* p = n.config.GetOrNull(PriorityKey)) merged = std::max(merged, *p);
        if (n.config.indexForKey) index = n.config.indexForKey(rt::Key(1007));
    }
    CHECK_EQ(merged, 3);   // the child's 2 merged into the parent's 1
    CHECK_EQ(index, 7);
}

#include "kiln/foundation/Scroll.h"

TEST(MotionFrameOfReferenceLeavesScrollingOut) {
    Harness h;
    ScrollState* scroll = nullptr;
    LayoutCoordinates root, item;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(OnGloballyPositioned(SizeMod(Modifier{}, Dp(100), Dp(100)), [&](const LayoutCoordinates& c) { root = c; }), Alignment::TopStart(), false, [&] {
                scroll = &RememberScrollState();
                Column(VerticalScroll(FillMax(Modifier{}, Axis::Both, 1), *scroll), [&] {
                    Box(SizeMod(Modifier{}, Dp(100), Dp(150)));
                    Box(OnGloballyPositioned(SizeMod(Modifier{}, Dp(100), Dp(50)), [&](const LayoutCoordinates& c) { item = c; }));
                });
            });
        });
    });
    scroll->ScrollTo(50);
    h.Pump();
    CHECK_EQ(root.LocalPositionOf(item, {}).y, 100.0f);         // 150 down, scrolled up 50
    CHECK_EQ(root.LocalPositionOf(item, {}, false).y, 150.0f);  // structural: without the scroll
}

TEST(MediaQueryAnswersFromTheWindowAndDevices) {
    Harness h;
    h.owner.SetDensity({ 2, 1 });
    OwnerMediaScope& media = MediaScopeOf(h.owner);
    float width = -1;
    UiMediaScope::PointerPrecision precision{};
    UiMediaScope::ViewingDistance distance{};
    int computedRuns = 0;
    bool wide = false;
    h.Run([&] {
        rt::Composition::Current()->Provide(LocalOwner, &h.owner, [&] {
            width = MediaQuery<float>([](const UiMediaScope& m) { return m.WindowWidth().value; });
            precision = MediaQuery<UiMediaScope::PointerPrecision>([](const UiMediaScope& m) { return m.GetPointerPrecision(); });
            distance = MediaQuery<UiMediaScope::ViewingDistance>([](const UiMediaScope& m) { return m.GetViewingDistance(); });
            auto& d = ComputedMediaQuery<bool>([&](const UiMediaScope& m) { ++computedRuns; return m.WindowWidth().value >= 100; });
            wide = d.Get();
        });
    });
    CHECK_EQ(width, 200.0f);   // 400 px at density 2
    CHECK(wide);
    CHECK(precision == UiMediaScope::PointerPrecision::None);
    // Reading the pointer precision listens to the devices: a touchscreen, then a mouse.
    CHECK(h.owner.Devices().ListenerCount() == 1);
    h.owner.Devices().Update({ 3, false, 0x1002, 0 });
    h.Pump();
    CHECK(precision == UiMediaScope::PointerPrecision::Coarse);
    h.owner.Devices().Update({ 4, false, 0x2002, 0 });
    h.Pump();
    CHECK(precision == UiMediaScope::PointerPrecision::Fine);
    h.owner.Devices().Remove(4);
    h.Pump();
    CHECK(precision == UiMediaScope::PointerPrecision::Coarse);
    media.SetFeatures({ .television = true });
    h.Pump();
    CHECK(distance == UiMediaScope::ViewingDistance::Far);
    UiMediaScope::WindowPosture posture;
    posture.folds.push_back({ UiMediaScope::FoldState::HalfOpened, UiMediaScope::FoldOrientation::Horizontal });
    CHECK(posture.IsTabletop() && !posture.IsFlat());
}

TEST(PreferredFrameRateVotesFromWhatDraws) {
    Harness h;
    rt::State<bool> fast(false);
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Box(PreferredFrameRate(SizeMod(Modifier{}, Dp(10), Dp(10)), FrameRateCategory::Normal()));
            Box(PreferredFrameRate(SizeMod(Modifier{}, Dp(10), Dp(10)), 30.0f));
            if (fast.Get()) Box(PreferredFrameRate(SizeMod(Modifier{}, Dp(10), Dp(10)), FrameRateCategory::High()));
        });
    });
    h.owner.Draw({});
    CHECK(h.owner.LastFrameRateVote().rate == 30.0f && h.owner.LastFrameRateVote().category == FrameRateCategory::Normal());
    fast.Set(true);
    h.Pump();
    h.owner.Draw({});
    CHECK(h.owner.LastFrameRateVote().category == FrameRateCategory::High());
    fast.Set(false);
    h.Pump();
    h.owner.Draw({});
    CHECK(h.owner.LastFrameRateVote().category == FrameRateCategory::Normal());
}

namespace {
    struct FakeRequest final : TextInputSession {
        ImeOptions options;
        ImeOptions Options() const override { return options; }
        TextInputSnapshot Snapshot() const override { return {}; }
        void CommitText(std::u16string_view, int, bool) override {}
        void SetComposingText(std::u16string_view, int, const std::vector<text::Range<text::SpanStyle>>*, bool) override {}
        void SetComposingRegion(int, int) override {}
        void FinishComposingText() override {}
        void DeleteSurroundingText(int, int) override {}
        void DeleteSurroundingTextInCodePoints(int, int) override {}
        void SetSelection(int, int) override {}
        void PerformImeAction(ImeAction) override {}
    };
    // Makes every request below it a number keyboard.
    struct NumberInterceptor final : PlatformTextInputInterceptor {
        int seen = 0;
        rt::Task<> InterceptStartInputMethod(PlatformTextInputMethodRequest& request, PlatformTextInputSession& next) override {
            ++seen;
            return Forward(&request, &next);
        }
        static rt::Task<> Forward(PlatformTextInputMethodRequest* request, PlatformTextInputSession* next) {
            FakeRequest numbers;
            numbers.options = request->Options();
            numbers.options.keyboardType = KeyboardType::Number;
            co_await next->StartInputMethod(numbers);
        }
    };
}

TEST(PlatformTextInputSessionsStartStopAndIntercept) {
    Harness h;
    std::vector<std::string> log;
    TextInputHost host;
    host.start = [&](const ImeOptions& o, const TextInputSnapshot&) { log.push_back(o.keyboardType == KeyboardType::Number ? "start:number" : "start"); };
    host.stop = [&] { log.push_back("stop"); };
    h.owner.SetTextInputHost(host);
    auto interceptor = std::make_shared<NumberInterceptor>();
    rt::State<bool> intercept(false);
    h.Run([&] {
        if (intercept.Get()) InterceptPlatformTextInput(interceptor, [&] { Box(ProbeMod(SizeMod(Modifier{}, Dp(10), Dp(10)), 1)); });
        else Box(ProbeMod(SizeMod(Modifier{}, Dp(10), Dp(10)), 1));
    });
    auto probe = [&]() -> ModifierNode& {
        for (LayoutNode* n = &h.owner.Root(); n;) {
            for (const ModifierNode* m : n->ModifierNodes()) if (dynamic_cast<const ProbeNode*>(m)) return const_cast<ModifierNode&>(*m);
            n = n->Children().empty() ? nullptr : n->Children()[0];
        }
        throw std::logic_error("no probe");
    };
    FakeRequest request;
    auto session = [](ModifierNode* node, FakeRequest* r) -> rt::Task<> {
        co_await EstablishTextInputSession(*node, [r](PlatformTextInputSessionScope& s) -> rt::Task<> { return s.StartInputMethod(*r); });
    };
    rt::Job first = rt::Launch(rt::Dispatcher::Main(), session(&probe(), &request));
    CHECK(log == std::vector<std::string>{ "start" });
    // A second session cancels the first (which stops its input method).
    rt::Job second = rt::Launch(rt::Dispatcher::Main(), session(&probe(), &request));
    CHECK(log == (std::vector<std::string>{ "start", "stop", "start" }));
    second.Cancel();
    CHECK(log.back() == "stop");
    // Under an interceptor the request it passes on reaches the host.
    intercept.Set(true);
    h.Pump();
    log.clear();
    rt::Job third = rt::Launch(rt::Dispatcher::Main(), session(&probe(), &request));
    CHECK(interceptor->seen == 1 && log == std::vector<std::string>{ "start:number" });
    third.Cancel();
    CHECK(log.back() == "stop");
}

TEST(LightAnchorsPublishWhereTheyAreAndLightsBuildFromAngles) {
    Harness h;
    LightAnchorState anchor;
    rt::State<bool> shown(true);
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            if (shown.Get()) Box(LightAnchor(OffsetMod(SizeMod(Modifier{}, Dp(10), Dp(10)), Dp(30), Dp(40)), anchor));
        });
    });
    CHECK(anchor.PeekCoordinates().has_value());
    const kiln::ui::Offset at = anchor.PeekCoordinates()->PositionInRoot();
    CHECK(at.x == 30 && at.y == 40);
    shown.Set(false);
    h.Pump();
    CHECK(!anchor.PeekCoordinates().has_value());   // released when the anchor leaves
    const Light d = DirectionalLightFromAngles(Color(0xFFFFFFFFu), 0, 90, 1);
    CHECK(std::abs(d.directionZ - 1) < 1e-6f && std::abs(d.directionX) < 1e-6f);
    const Light s = SpotLightFromAngles(Color(0xFFFFFFFFu), LightPosition::Local({ Dp(0), Dp(0) }), 0, 45, Dp(10), Dp(100), 30, 1);
    CHECK(std::abs(s.targetOffset.x.value + 10) < 1e-4f && std::abs(s.targetOffset.y.value) < 1e-4f);
}
