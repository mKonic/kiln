#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/navigation3/NavDisplay.h"
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
        void Run(std::function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
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
        s.Entry<Detail>([&](const Detail& d) { shown = d.id; }, Metadata([](MetadataScope& m) { m.Put(TitleMetadataKey, std::function<std::string()>([] { return std::string("detail"); })); }));
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
