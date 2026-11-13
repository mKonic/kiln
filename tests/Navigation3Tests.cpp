#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/SharedTransition.h"
#include "kiln/lifecycle/LifecycleCompose.h"
#include "kiln/navigation3/NavDisplay.h"
#include "kiln/navigation3/ResultEventBus.h"
#include "kiln/runtime/Saveable.h"

#include <chrono>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::navigation3;

namespace {
    struct Home { bool operator==(const Home&) const = default; };
    struct Detail { int id; bool operator==(const Detail&) const = default; };
    using Route = std::variant<Home, Detail>;
    std::string NavKeyToString(const Home&) { return "Home"; }
    std::string NavKeyToString(const Detail& d) { return "Detail(" + std::to_string(d.id) + ")"; }

    struct Harness {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        ui::BackDispatcher& back = ui::BackDispatcher::Main();
        std::int64_t frameNanos = 0;
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 600 });
            }
        }
        void Frames(int n) {
            auto& d = rt::Dispatcher::Main();
            for (int i = 0; i < n; ++i) {
                d.AdvanceTimeBy(std::chrono::milliseconds(16));
                frameNanos += 16'000'000;
                d.RunPending();
                d.RunFrame(frameNanos);
                Pump();
            }
        }
    };

    // A scene showing the last two entries side by side (a minimal list-detail).
    struct TwoPaneScene final : Scene<Route> {
        NavEntries<Route> entries, previous;
        std::string Key() const override { return "two-pane"; }
        const NavEntries<Route>& Entries() const override { return entries; }
        const NavEntries<Route>& PreviousEntries() const override { return previous; }
        void Content() const override {
            const auto es = entries;
            Row(Modifier{}, [es] { for (const auto& e : es) Box(Modifier{}, ui::Alignment::TopStart(), false, [e] { e.Content(); }); });
        }
        bool Equals(const Scene<Route>& other) const override {
            const auto& o = static_cast<const TwoPaneScene&>(other);
            return entries == o.entries && previous == o.previous;
        }
    };
}

TEST(EntryProviderMatchesKeysAndClasses) {
    int shown = -1;
    auto provider = EntryProvider<Route>([&](EntryProviderScope<Route>& s) {
        s.Entry<Home>(Home{}, [&](const Home&) { shown = 0; });
        s.Entry<Detail>([&](const Detail& d) { shown = d.id; }, Metadata([](MetadataScope& m) { m.Put(TitleMetadataKey, kiln::Function<std::string()>([] { return std::string("detail"); })); }));
    });
    const NavEntry<Route> home = provider(Home{});
    const NavEntry<Route> d7 = provider(Detail{ 7 });
    home.Content();
    CHECK_EQ(shown, 0);
    d7.Content();
    CHECK_EQ(shown, 7);
    CHECK(home.ContentKey() != d7.ContentKey());
    CHECK(d7.ContentKey() != provider(Detail{ 8 }).ContentKey());
    CHECK(Get(d7.Metadata(), TitleMetadataKey) != nullptr);
    CHECK(Get(home.Metadata(), TitleMetadataKey) == nullptr);
}

TEST(NavDisplayShowsTheLastEntryAndBackPopsIt) {
    Harness h;
    NavBackStack<Route>* stack = nullptr;
    std::vector<std::string> composed;
    int* homeCounter = nullptr;
    std::vector<std::string> popped;
    h.Run([&] {
        auto& s = RememberNavBackStack<Route>(rt::Saver<Route>{ [](const Route&) { return rt::SavedValue(); }, [](const rt::SavedValue&) { return std::optional<Route>(); } },
                                            { Home{} });
        stack = &s;
        NavDisplayOptions<Route> options;
        auto& holder = rt::RememberSaveableStateHolder();
        auto saveable = SaveableStateHolderNavEntryDecorator<Route>(holder);
        auto& tracker = rt::Composition::Current()->Remember<std::shared_ptr<NavEntryDecorator<Route>>>([&] {
            return std::make_shared<NavEntryDecorator<Route>>([&](const std::string& k) { popped.push_back(k); }, [](const NavEntry<Route>& e) { e.Content(); });
        });
        options.entryDecorators = std::vector<NavEntryDecorator<Route>>{ saveable, *tracker };
        NavDisplay(s, EntryProvider<Route>([&](EntryProviderScope<Route>& p) {
            p.Entry<Home>([&](const Home&) {
                composed.push_back("home");
                homeCounter = &rt::RememberSaveable<int>([] { return 0; });
                Box(Modifier{});
            });
            p.Entry<Detail>([&](const Detail& d) { composed.push_back("detail" + std::to_string(d.id)); Box(Modifier{}); });
        }), Modifier{}, options);
    });
    CHECK(!composed.empty() && composed.back() == "home");
    *homeCounter = 5;
    stack->Add(Detail{ 1 });
    composed.clear();
    h.Frames(60);
    CHECK(std::find(composed.begin(), composed.end(), "detail1") != composed.end());
    composed.clear();
    h.Frames(2);
    CHECK(std::find(composed.begin(), composed.end(), "home") == composed.end());   // settled: home left the composition
    // Back pops the detail; home comes back with its saved state.
    CHECK(h.back.HasEnabledCallbacks());
    h.back.Completed();
    h.Frames(60);
    CHECK_EQ(stack->Size(), std::size_t(1));
    CHECK(homeCounter && *homeCounter == 5);
    CHECK_EQ(popped.size(), std::size_t(1));
    CHECK(!popped.empty() && popped[0] == DefaultContentKey(Route(Detail{ 1 })));
    CHECK(!h.back.HasEnabledCallbacks());
}

TEST(EntryKeepsItsStateWhenItMovesIntoAnotherScene) {
    Harness h;
    NavBackStack<Route>* stack = nullptr;
    int* homeState = nullptr;
    int homeCreated = 0;
    h.Run([&] {
        auto& s = RememberNavBackStack<Route>(rt::Saver<Route>{ [](const Route&) { return rt::SavedValue(); }, [](const rt::SavedValue&) { return std::optional<Route>(); } },
                                            { Home{} });
        stack = &s;
        NavDisplayOptions<Route> options;
        auto& strategy = rt::Composition::Current()->Remember<SceneStrategyPtr<Route>>([] {
            return MakeSceneStrategy<Route>([](const SceneStrategyScope<Route>&, const NavEntries<Route>& entries) -> ScenePtr<Route> {
                if (entries.size() < 2 || !std::holds_alternative<Detail>(entries.back().Key())) return nullptr;
                auto scene = std::make_shared<TwoPaneScene>();
                scene->entries = { entries[entries.size() - 2], entries.back() };
                scene->previous = NavEntries<Route>(entries.begin(), entries.end() - 2);
                return scene;
            });
        });
        options.sceneStrategies = { strategy };
        NavDisplay(s, EntryProvider<Route>([&](EntryProviderScope<Route>& p) {
            p.Entry<Home>([&](const Home&) {
                homeState = &rt::Composition::Current()->Remember<int>([&] { ++homeCreated; return 1; });
                Box(Modifier{});
            });
            p.Entry<Detail>([](const Detail&) { Box(Modifier{}); });
        }), Modifier{}, options);
    });
    CHECK_EQ(homeCreated, 1);
    *homeState = 42;
    // The two-pane scene takes over: home moves into it with its remembered state.
    stack->Add(Detail{ 3 });
    h.Frames(60);
    CHECK_EQ(homeCreated, 1);
    CHECK(homeState && *homeState == 42);
}

TEST(EntriesMoveBetweenScenesAsSharedElements) {
    Harness h;
    NavBackStack<Route>* stack = nullptr;
    SharedTransitionScope* shared = nullptr;
    bool homeSawScope = false;
    int activeFrames = 0;
    h.Run([&] {
        auto& s = RememberNavBackStack<Route>(rt::Saver<Route>{ [](const Route&) { return rt::SavedValue(); }, [](const rt::SavedValue&) { return std::optional<Route>(); } },
                                            { Home{} });
        stack = &s;
        SharedTransitionLayout(Modifier{}, [&](SharedTransitionScope& scope) {
            shared = &scope;
            NavDisplayOptions<Route> options;
            auto& strategy = rt::Composition::Current()->Remember<SceneStrategyPtr<Route>>([] {
                return MakeSceneStrategy<Route>([](const SceneStrategyScope<Route>&, const NavEntries<Route>& entries) -> ScenePtr<Route> {
                    if (entries.size() < 2 || !std::holds_alternative<Detail>(entries.back().Key())) return nullptr;
                    auto scene = std::make_shared<TwoPaneScene>();
                    scene->entries = { entries[entries.size() - 2], entries.back() };
                    scene->previous = NavEntries<Route>(entries.begin(), entries.end() - 2);
                    return scene;
                });
            });
            options.sceneStrategies = { strategy };
            options.sharedTransitionScope = &scope;
            NavDisplay(s, EntryProvider<Route>([&](EntryProviderScope<Route>& p) {
                p.Entry<Home>([&](const Home&) {
                    homeSawScope = NavAnimatedContentScope().transition != nullptr;
                    Box(FillMax(Modifier{}, Axis::Both));
                });
                p.Entry<Detail>([](const Detail&) { Box(FillMax(Modifier{}, Axis::Both)); });
            }), Modifier{}, options);
        });
    });
    CHECK(homeSawScope && shared && !shared->IsTransitionActive());
    // Home goes from the whole display into the two-pane scene's first pane: a shared element
    // animating between the two scenes' bounds.
    stack->Add(Detail{ 2 });
    for (int i = 0; i < 60; ++i) {
        h.Frames(1);
        if (shared->IsTransitionActive()) ++activeFrames;
    }
    CHECK(activeFrames > 2);
    CHECK(!shared->IsTransitionActive());
}

TEST(ResultEventBusDeliversResultsToTheWaitingEntry) {
    Harness h;
    std::vector<int> received;
    std::string latest;
    std::shared_ptr<ResultEventBus> bus;
    rt::State<bool> waiting(true);
    h.Run([&] {
        bus = RememberResultEventBus();
        rt::Composition::Current()->Provide(LocalResultEventBus, bus, [&] {
            if (waiting.Get()) ResultEffect<int>([&](const int& v) { received.push_back(v); });
            latest = ConflateAsState<std::string>(bus, "name", std::string("none"));
        });
    });
    CHECK_EQ(latest, std::string("none"));
    bus->SendResult(1);
    bus->SendResult(2);
    bus->SendResult("name", std::any(std::string("ada")));
    h.Pump();
    CHECK_EQ(received.size(), std::size_t(2));
    CHECK_EQ(received.back(), 2);
    CHECK_EQ(latest, std::string("ada"));
    // Sent while nobody waits: kept until an effect collects it.
    waiting.Set(false);
    h.Pump();
    bus->SendResult(3);
    h.Pump();
    CHECK_EQ(received.size(), std::size_t(2));
    waiting.Set(true);
    h.Pump();
    CHECK_EQ(received.size(), std::size_t(3));
    CHECK_EQ(received.back(), 3);
    // Removed results are dropped; the effect keeps waiting on the new channel.
    waiting.Set(false);
    h.Pump();
    bus->SendResult(4);
    bus->RemoveResult<int>();
    waiting.Set(true);
    h.Pump();
    bus->SendResult(5);
    h.Pump();
    CHECK_EQ(received.size(), std::size_t(4));
    CHECK_EQ(received.back(), 5);
}

TEST(NavDisplayEntriesResumeOnceSettledAndPoppedOnesStopAtCreated) {
    Harness h;
    NavBackStack<Route>* stack = nullptr;
    lifecycle::LifecycleOwnerPtr home, detail;
    h.Run([&] {
        auto& s = RememberNavBackStack<Route>(rt::Saver<Route>{ [](const Route&) { return rt::SavedValue(); }, [](const rt::SavedValue&) { return std::optional<Route>(); } },
                                            { Home{} });
        stack = &s;
        NavDisplay(s, EntryProvider<Route>([&](EntryProviderScope<Route>& p) {
            p.Entry<Home>([&](const Home&) { home = lifecycle::CurrentLifecycleOwner(); Box(Modifier{}); });
            p.Entry<Detail>([&](const Detail&) { detail = lifecycle::CurrentLifecycleOwner(); Box(Modifier{}); });
        }));
    });
    h.Frames(2);
    CHECK(home && home->GetLifecycle().CurrentState() == lifecycle::State::Resumed);
    // Pushing: the new entry is STARTED while the transition runs, RESUMED once settled.
    stack->Add(Detail{ 1 });
    h.Frames(3);
    CHECK(detail && detail->GetLifecycle().CurrentState() == lifecycle::State::Started);
    CHECK(home->GetLifecycle().CurrentState() == lifecycle::State::Started);
    h.Frames(60);
    CHECK(detail->GetLifecycle().CurrentState() == lifecycle::State::Resumed);
    // Popping: the popped entry is capped at CREATED while it animates out, then destroyed.
    auto popped = detail;
    stack->RemoveLast();
    h.Frames(3);
    CHECK(popped->GetLifecycle().CurrentState() == lifecycle::State::Created);
    h.Frames(60);
    CHECK(popped->GetLifecycle().CurrentState() == lifecycle::State::Destroyed);
    CHECK(home->GetLifecycle().CurrentState() == lifecycle::State::Resumed);
}
