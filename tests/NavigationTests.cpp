#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/navigation/NavHost.h"
#include "kiln/runtime/Saveable.h"

#include <chrono>

using namespace kiln;
using namespace kiln::navigation;

namespace {
    std::vector<std::string> Routes(const NavController& c) {
        std::vector<std::string> out;
        for (const auto& e : c.CurrentBackStack())
            if (!e->Destination()->IsGraph()) out.push_back(e->Destination()->Route().value_or("?"));
        return out;
    }

    // A controller whose navigators are attached without a NavHost.
    std::unique_ptr<NavHostController> Controller(const std::function<void(NavGraphBuilder&)>& builder, const std::string& start) {
        auto c = std::make_unique<NavHostController>();
        c->SetHostLifecycleState(LifecycleState::Resumed);
        c->SetGraph(c->CreateGraph(start, std::nullopt, builder));
        return c;
    }
    void Screens(NavGraphBuilder& b) {
        b.Composable("home", [](const NavBackStackEntry&) {});
        b.Composable("detail/{id}", [](const NavBackStackEntry&) {}, { { NavArgumentOf("id", NavType::Int) } });
        b.Composable("profile?name={name}", [](const NavBackStackEntry&) {}, { { NavArgumentOf("name", NavType::String, false, NavValue(std::string("me"))) } });
    }
}

TEST(RoutesMatchWithTypedArguments) {
    ComposeNavigator::Destination detail([](const NavBackStackEntry&) {});
    detail.AddArgument("id", NavArgument{ NavType::Int });
    detail.SetRoute("detail/{id}");
    const auto m = detail.MatchRoute("detail/42");
    CHECK(m.has_value());
    CHECK(m && m->matchingArgs && std::get<std::int32_t>(m->matchingArgs->at("id")) == 42);
    CHECK(!detail.MatchRoute("detail/abc"));   // not an integer
    CHECK(!detail.MatchRoute("other/42"));
    ComposeNavigator::Destination profile([](const NavBackStackEntry&) {});
    profile.AddArgument("name", NavArgument{ NavType::String, false, NavValue(std::string("me")) });
    profile.SetRoute("profile?name={name}");
    const auto withName = profile.MatchRoute("profile?name=ann%20b");
    CHECK(withName && std::get<std::string>(withName->matchingArgs->at("name")) == "ann b");
    CHECK(profile.MatchRoute("profile").has_value());   // the default fills in
}

TEST(NavControllerNavigatesPopsAndLaunchesSingleTop) {
    auto c = Controller(Screens, "home");
    CHECK(Routes(*c) == std::vector<std::string>{ "home" });
    c->Navigate("detail/7");
    CHECK(Routes(*c) == (std::vector<std::string>{ "home", "detail/{id}" }));
    CHECK(c->CurrentBackStackEntry()->GetInt("id") == std::optional<std::int32_t>(7));
    c->Navigate("profile");
    CHECK(c->CurrentBackStackEntry()->GetString("name") == std::optional<std::string>("me"));
    // launchSingleTop replaces the top entry instead of stacking another.
    c->Navigate("profile?name=zoe", [](NavOptionsBuilder& o) { o.launchSingleTop = true; });
    CHECK_EQ(Routes(*c).size(), std::size_t(3));
    CHECK(c->CurrentBackStackEntry()->GetString("name") == std::optional<std::string>("zoe"));
    // popUpTo(home) then navigate: the back stack is home, detail.
    c->Navigate("detail/9", [](NavOptionsBuilder& o) { o.PopUpTo("home"); });
    CHECK(Routes(*c) == (std::vector<std::string>{ "home", "detail/{id}" }));
    CHECK(c->PopBackStack());
    CHECK(Routes(*c) == std::vector<std::string>{ "home" });
    CHECK(!c->NavigateUp());   // the start destination stays
}

TEST(NavControllerSavesAndRestoresBackStacks) {
    // Bottom navigation: popUpTo(start, saveState) + restoreState keeps each tab's stack.
    auto c = Controller([](NavGraphBuilder& b) {
        b.Composable("a", [](const NavBackStackEntry&) {});
        b.Composable("a2", [](const NavBackStackEntry&) {});
        b.Composable("b", [](const NavBackStackEntry&) {});
    }, "a");
    c->Navigate("a2");
    const std::string a2Id = c->CurrentBackStackEntry()->Id();
    auto tab = [&](const std::string& route) {
        c->Navigate(route, [](NavOptionsBuilder& o) { o.PopUpTo("a", false, true); o.launchSingleTop = true; o.restoreState = true; });
    };
    tab("b");
    CHECK(Routes(*c) == (std::vector<std::string>{ "a", "b" }));
    tab("a");
    CHECK(Routes(*c) == (std::vector<std::string>{ "a", "a2" }));
    CHECK_EQ(c->CurrentBackStackEntry()->Id(), a2Id);   // the same entry comes back
    // The saved state survives a save / restore of the controller.
    auto d = std::make_unique<NavHostController>();
    d->RestoreState(c->SaveState());
    d->SetHostLifecycleState(LifecycleState::Resumed);
    d->SetGraph(d->CreateGraph("a", std::nullopt, [](NavGraphBuilder& b) {
        b.Composable("a", [](const NavBackStackEntry&) {});
        b.Composable("a2", [](const NavBackStackEntry&) {});
        b.Composable("b", [](const NavBackStackEntry&) {});
    }));
    CHECK(Routes(*d) == (std::vector<std::string>{ "a", "a2" }));
    CHECK_EQ(d->CurrentBackStackEntry()->Id(), a2Id);
}

TEST(NestedGraphsEnterAtTheirStartDestination) {
    auto c = Controller([](NavGraphBuilder& b) {
        b.Composable("welcome", [](const NavBackStackEntry&) {});
        b.Navigation("list", "flow", [](NavGraphBuilder& n) {
            n.Composable("list", [](const NavBackStackEntry&) {});
            n.Composable("item", [](const NavBackStackEntry&) {});
        });
    }, "welcome");
    c->Navigate("flow");
    CHECK(Routes(*c) == (std::vector<std::string>{ "welcome", "list" }));
    c->Navigate("item");
    // Popping the whole flow graph leaves welcome.
    CHECK(c->PopBackStack("flow", true));
    CHECK(Routes(*c) == std::vector<std::string>{ "welcome" });
}

namespace {
    struct Harness {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
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
}

TEST(NavHostShowsTheCurrentEntryAndBackRestoresTheLastOne) {
    Harness h;
    NavHostController* nav = nullptr;
    std::vector<std::string> shown;
    int* homeCounter = nullptr;
    h.Run([&] {
        auto& c = RememberNavController();
        nav = &c;
        NavHost(c, "home", [&](NavGraphBuilder& b) {
            b.Composable("home", [&](const NavBackStackEntry&) {
                shown.push_back("home");
                homeCounter = &rt::RememberSaveable<int>([] { return 0; });
                foundation::Box(ui::Modifier{});
            });
            b.Composable("detail/{id}", [&](const NavBackStackEntry& e) {
                shown.push_back("detail" + std::to_string(e.GetInt("id").value_or(-1)));
                foundation::Box(ui::Modifier{});
            }, { { NavArgumentOf("id", NavType::Int) } });
        });
    });
    CHECK(!shown.empty() && shown.back() == "home");
    *homeCounter = 3;
    nav->Navigate("detail/5");
    shown.clear();
    h.Frames(60);
    CHECK(std::find(shown.begin(), shown.end(), "detail5") != shown.end());
    shown.clear();
    h.Frames(2);
    CHECK(std::find(shown.begin(), shown.end(), "home") == shown.end());
    CHECK(ui::BackDispatcher::Main().HasEnabledCallbacks());
    ui::BackDispatcher::Main().Completed();
    h.Frames(60);
    CHECK(Routes(*nav) == std::vector<std::string>{ "home" });
    CHECK(homeCounter && *homeCounter == 3);
    CHECK(!ui::BackDispatcher::Main().HasEnabledCallbacks());
}
