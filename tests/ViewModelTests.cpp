#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/lifecycle/ViewModelCompose.h"
#include "kiln/navigation/NavHost.h"
#include "kiln/navigation3/NavDisplay.h"
#include "kiln/navigation3/ViewModelStoreNavEntryDecorator.h"
#include "kiln/runtime/Channel.h"
#include "kiln/ui/LayoutNode.h"

#include <chrono>

using namespace kiln;
using namespace kiln::lifecycle;

namespace {
    struct Counter final : ViewModel {
        int value = 0;
        bool* cleared = nullptr;
    protected:
        void OnCleared() override { if (cleared) *cleared = true; }
    };
    struct Named final : ViewModel {
        explicit Named(std::string n) : name(std::move(n)) {}
        std::string name;
    };
    struct WithHandle final : ViewModel {
        explicit WithHandle(std::shared_ptr<SavedStateHandle> h) : handle(std::move(h)) {}
        std::shared_ptr<SavedStateHandle> handle;
    };

    // A window-like owner: lifecycle, store and a saved-state registry (what App's is).
    struct WindowOwner final : SavedStateRegistryOwner, ViewModelStoreOwner, HasDefaultViewModelProviderFactory {
        LifecycleRegistry registry{ *this };
        ViewModelStore store;
        rt::SaveableStateRegistry saved;
        explicit WindowOwner(rt::SaveableStateRegistry::Values restored = {}) : saved(std::move(restored)) {
            registry.SetCurrentState(State::Created);
            EnableSavedStateHandles(*this);
            registry.SetCurrentState(State::Resumed);
        }
        Lifecycle& GetLifecycle() override { return registry; }
        ViewModelStore& GetViewModelStore() override { return store; }
        rt::SaveableStateRegistry& SavedStateRegistry() override { return saved; }
        ViewModelProvider::FactoryPtr DefaultViewModelProviderFactory() override { return SavedStateViewModelFactory::Instance(); }
        MutableCreationExtras DefaultViewModelCreationExtras() override {
            MutableCreationExtras e;
            e.Set(SavedStateRegistryOwnerKey, static_cast<SavedStateRegistryOwner*>(this));
            e.Set(ViewModelStoreOwnerKey, static_cast<ViewModelStoreOwner*>(this));
            return e;
        }
    };

    struct Harness {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t frameNanos = 0;
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(kiln::Function<void()> content) {
            composition.SetContent([this, content = std::move(content)] { rt::Composition::Current()->Provide(ui::LocalOwner, &owner, content); });
            Pump();
        }
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
}

TEST(ViewModelProviderKeepsOnePerKeyAndClearsWithTheStore) {
    ViewModelStore store;
    auto provider = ViewModelProvider::Create(store);
    auto a = provider.Get<Counter>();
    a->value = 3;
    CHECK(provider.Get<Counter>() == a && provider.Get<Counter>("other") != a);
    CHECK(store.Keys().size() == 2);

    // Closeables and the scope close with the ViewModel; then OnCleared.
    bool cleared = false, closed = false, ended = false;
    a->cleared = &cleared;
    a->AddCloseable(MakeCloseable([&] { closed = true; }));
    rt::Channel<int> never;
    ViewModelScope(*a)->Launch([](rt::Channel<int>& ch, bool& ended) -> rt::Task<> {
        struct End { bool* e; ~End() { *e = true; } } end{ &ended };
        co_await ch.Next();
    }(never, ended));
    store.Clear();
    CHECK(cleared && closed && ended && store.Keys().empty());
    // A closeable added after clearing closes at once.
    bool late = false;
    a->AddCloseable("k", MakeCloseable([&] { late = true; }));
    CHECK(late);

    // Initializers; the factory sees the key; no default constructor and no initializer throws.
    std::string seenKey;
    auto factory = ViewModelFactory([&](InitializerViewModelFactoryBuilder& b) {
        Initializer<Named>(b, [&](const CreationExtras& e) {
            seenKey = *e.Get(ViewModelProvider::ViewModelKey);
            return std::make_shared<Named>("made");
        });
    });
    auto p2 = ViewModelProvider::Create(store, factory);
    CHECK(p2.Get<Named>("n")->name == "made" && seenKey == "n");
    bool threw = false;
    try { ViewModelProvider::Create(store).Get<Named>("x"); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    ViewModelLazy<Counter> lazy([&]() -> ViewModelStore& { return store; }, [] { return ViewModelProvider::NewInstanceFactory::Instance(); });
    CHECK(!lazy.IsInitialized() && lazy.Value() && lazy.IsInitialized() && lazy.Value() == provider.Get<Counter>());
}

TEST(SavedStateHandleSurvivesSaveAndRestore) {
    rt::SaveableStateRegistry::Values saved;
    {
        WindowOwner window;
        auto vm = ViewModelProvider::Create(window).Get<WithHandle>();
        auto& h = *vm->handle;
        h.Set("count", 4);
        auto& flow = h.GetMutableStateFlow("query", std::string("a"));
        flow.Set(std::string("kiln"));
        int asked = 0;
        h.SetSavedStateProvider("computed", [&] { ++asked; return rt::SavedValue(std::string("now")); });
        h.GetOrCreateSavedStateHandle("child").Set("x", true);
        CHECK(h.Contains("count") && h.Get("query") == rt::SavedValue(std::string("kiln")));
        CHECK(h.Keys().size() == 4);
        saved = window.saved.PerformSave();
        CHECK(asked == 1);
    }
    // A new process: the same ViewModel key gets its values back.
    WindowOwner window(saved);
    auto vm = ViewModelProvider::Create(window).Get<WithHandle>();
    auto& h = *vm->handle;
    CHECK(h.Get("count") == rt::SavedValue(4));
    CHECK(h.Get("query") == rt::SavedValue(std::string("kiln")));
    CHECK(h.Get("computed") == rt::SavedValue(std::string("now")));
    CHECK(h.GetOrCreateSavedStateHandle("child").Get("x") == rt::SavedValue(true));
    CHECK(h.Remove("count") == rt::SavedValue(4) && !h.Contains("count"));
}

TEST(ComposedStoresClearWhenTheirPlaceLeaves) {
    Harness h;
    auto window = std::make_shared<WindowOwner>();
    SetHostViewModelStoreOwner(h.owner, window);
    SetHostSavedStateRegistryOwner(h.owner, window);
    rt::State<bool> shown{ true };
    std::shared_ptr<Counter> windowVm, scopedVm;
    bool scopedCleared = false;
    h.Run([&] {
        windowVm = ViewModelOf<Counter>();
        if (shown.Get()) {
            auto scoped = RememberViewModelStoreOwner();
            scopedVm = ViewModelOf<Counter>(scoped);
            scopedVm->cleared = &scopedCleared;
        }
    });
    CHECK(windowVm && scopedVm && windowVm != scopedVm);
    CHECK(windowVm == ViewModelProvider::Create(*window).Get<Counter>());
    shown.Set(false);
    rt::Observation::ApplyChanges();
    h.Pump();
    CHECK(scopedCleared);
}

TEST(NavigationEntriesOwnViewModelsWithTheirArguments) {
    Harness h;
    auto window = std::make_shared<WindowOwner>();
    SetHostViewModelStoreOwner(h.owner, window);
    navigation::NavHostController* nav = nullptr;
    std::shared_ptr<WithHandle> detailVm;
    bool detailCleared = false;
    struct Tracker final : ViewModel {
        bool* cleared = nullptr;
    protected:
        void OnCleared() override { if (cleared) *cleared = true; }
    };
    h.Run([&] {
        auto& c = navigation::RememberNavController();
        nav = &c;
        navigation::NavHost(c, "home", [&](navigation::NavGraphBuilder& b) {
            b.Composable("home", [](const navigation::NavBackStackEntry&) {});
            b.Composable("detail/{id}", [&](const navigation::NavBackStackEntry&) {
                detailVm = ViewModelOf<WithHandle>();
                ViewModelOf<Tracker>()->cleared = &detailCleared;
            }, { { navigation::NavArgumentOf("id", navigation::NavType::Int) } });
        });
    });
    nav->Navigate("detail/7");
    h.Frames(60);
    CHECK(detailVm && detailVm->handle->Get("id") == rt::SavedValue(7));
    auto entry = nav->CurrentBackStackEntry();
    CHECK(detailVm == ViewModelProvider::Create(*entry).Get<WithHandle>());
    // Popping clears the entry's ViewModels.
    nav->PopBackStack();
    h.Frames(60);
    CHECK(detailCleared);
}

TEST(Navigation3EntriesGetStoresClearedOnPop) {
    struct Home { bool operator==(const Home&) const = default; };
    struct Detail { bool operator==(const Detail&) const = default; };
    using Route = std::variant<Home, Detail>;
    Harness h;
    auto window = std::make_shared<WindowOwner>();
    SetHostViewModelStoreOwner(h.owner, window);
    navigation3::NavBackStack<Route>* stack = nullptr;
    std::shared_ptr<Counter> homeVm, detailVm;
    bool detailCleared = false;
    h.Run([&] {
        auto& s = navigation3::RememberNavBackStack<Route>(
            rt::Saver<Route>{ [](const Route&) { return rt::SavedValue(); }, [](const rt::SavedValue&) { return std::optional<Route>(); } }, { Home{} });
        stack = &s;
        navigation3::NavDisplayOptions<Route> options;
        auto& holder = rt::RememberSaveableStateHolder();
        options.entryDecorators = std::vector<navigation3::NavEntryDecorator<Route>>{ navigation3::SaveableStateHolderNavEntryDecorator<Route>(holder),
                                                                                     navigation3::RememberViewModelStoreNavEntryDecorator<Route>() };
        navigation3::NavDisplay(s, navigation3::EntryProvider<Route>([&](navigation3::EntryProviderScope<Route>& p) {
            p.Entry<Home>([&](const Home&) { homeVm = ViewModelOf<Counter>(); });
            p.Entry<Detail>([&](const Detail&) {
                detailVm = ViewModelOf<Counter>();
                detailVm->cleared = &detailCleared;
            });
        }), ui::Modifier{}, options);
    });
    stack->Add(Detail{});
    h.Frames(60);
    CHECK(homeVm && detailVm && homeVm != detailVm);
    stack->RemoveLast();
    h.Frames(60);
    CHECK(detailCleared);
}

TEST(SavedStateHandleSaveableKeepsItsLatestValue) {
    SavedStateHandle h;
    int inits = 0;
    auto state = SaveableState<int>(h, "count", rt::AutoSaver<int>(), [&] { ++inits; return std::make_shared<rt::State<int>>(1); });
    state->Set(9);
    const std::string name = Saveable<std::string>(h, "name", [] { return std::string("first"); });
    // Restored into a new handle: the latest values, without running init.
    auto restored = SavedStateHandle::CreateHandle(h.SaveState(), std::nullopt);
    auto again = SaveableState<int>(*restored, "count", rt::AutoSaver<int>(), [&] { ++inits; return std::make_shared<rt::State<int>>(1); });
    CHECK(again->Peek() == 9 && inits == 1);
    CHECK(Saveable<std::string>(*restored, "name", [] { return std::string("other"); }) == name);
}
