#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/lifecycle/LifecycleCompose.h"
#include "kiln/navigation/NavHost.h"
#include "kiln/ui/Volition.h"
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
    std::unique_ptr<NavHostController> Controller(const kiln::Function<void(NavGraphBuilder&)>& builder, const std::string& start) {
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

TEST(DeepLinksMatchUrisActionsAndMimeTypes) {
    auto c = Controller([](NavGraphBuilder& b) {
        b.Composable("home", [](const NavBackStackEntry&) {});
        b.Composable("item/{id}", [](const NavBackStackEntry&) {},
                     { { NavArgumentOf("id", NavType::Int) }, { NavDeepLink("https://example.com/items/{id}") } });
        b.Composable("share", [](const NavBackStackEntry&) {}, { {}, { NavDeepLinkOf([](NavDeepLinkDslBuilder& d) {
            d.action = "android.intent.action.SEND";
            d.mimeType = "image/*";
        }) } });
        b.Composable("text", [](const NavBackStackEntry&) {}, { {}, { NavDeepLinkOf([](NavDeepLinkDslBuilder& d) { d.mimeType = "text/plain"; }),
                                                                      NavDeepLinkOf([](NavDeepLinkDslBuilder& d) { d.mimeType = "image/svg+xml"; }) } });
    }, "home");
    // A uri gives the arguments; the route pattern itself is not a deep link.
    c->Navigate(NavUri("https://example.com/items/42"));
    CHECK(Routes(*c).back() == "item/{id}" && c->CurrentBackStackEntry()->GetInt("id") == std::optional<std::int32_t>(42));
    bool threw = false;
    try { c->Navigate(NavUri("https://example.com/items/abc")); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // Action and mime type: every part the deep link has must match; a wildcard subtype matches
    // any, and an exact one ranks above it.
    c->Navigate(NavDeepLinkRequest{ std::nullopt, std::string("android.intent.action.SEND"), std::string("image/png") });
    CHECK(Routes(*c).back() == "share");
    threw = false;
    try { c->Navigate(NavDeepLinkRequest::FromAction("android.intent.action.SEND")); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);   // the share link also needs a mime type
    c->Navigate(NavDeepLinkRequest::FromMimeType("text/plain"));
    CHECK(Routes(*c).back() == "text");
    c->Navigate(NavDeepLinkRequest::FromMimeType("image/svg+xml"));
    CHECK(Routes(*c).back() == "text");   // "svg+xml" matches itself (escaped; upstream puts the "+" in its regex raw)
    const NavDeepLink wild = NavDeepLinkOf([](NavDeepLinkDslBuilder& d) { d.mimeType = "image/*"; });
    CHECK(wild.GetMimeTypeMatchRating("image/png") == 2 && wild.GetMimeTypeMatchRating("video/mp4") == -1);
    CHECK(NavDeepLinkOf([](NavDeepLinkDslBuilder& d) { d.mimeType = "*/*"; }).GetMimeTypeMatchRating("video/mp4") == 0);
    // A deep link missing a required argument, an empty action and a bad mime type are errors.
    threw = false;
    ComposeNavigator::Destination needsId([](const NavBackStackEntry&) {});
    needsId.AddArgument("id", NavArgument{ NavType::Int });
    try { needsId.AddDeepLink(NavDeepLink("https://example.com/other")); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { (void)NavDeepLinkRequest::FromMimeType("plain"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { (void)NavDeepLinkOf([](NavDeepLinkDslBuilder&) {}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
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
}

TEST(NavHostShowsTheCurrentEntryAndBackRestoresTheLastOne) {
    Harness h;
    NavHostController* nav = nullptr;
    std::vector<std::string> shown;
    int* homeCounter = nullptr;
    bool homeHadScope = false;
    h.Run([&] {
        auto& c = RememberNavController();
        nav = &c;
        NavHost(c, "home", [&](NavGraphBuilder& b) {
            b.Composable("home", [&](const NavBackStackEntry&) {
                shown.push_back("home");
                // The AnimatedContentScope upstream gives composable content (sharedElement,
                // animateEnterExit): LocalAnimatedVisibilityScope here.
                homeHadScope = rt::Composition::Current()->Read(foundation::LocalAnimatedVisibilityScope) != nullptr;
                homeCounter = &rt::RememberSaveable<int>([] { return 0; });
                foundation::Box(ui::Modifier{});
            });
            b.Composable("detail/{id}", [&](const NavBackStackEntry& e) {
                shown.push_back("detail" + std::to_string(e.GetInt("id").value_or(-1)));
                foundation::Box(ui::Modifier{});
            }, { { NavArgumentOf("id", NavType::Int) } });
        });
    });
    CHECK(!shown.empty() && shown.back() == "home" && homeHadScope);
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

TEST(NavHostEntriesAreLifecycleOwnersFollowingTheHost) {
    Harness h;
    struct HostOwner final : lifecycle::LifecycleOwner {
        lifecycle::LifecycleRegistry registry{ *this };
        lifecycle::Lifecycle& GetLifecycle() override { return registry; }
    };
    auto host = std::make_shared<HostOwner>();
    host->registry.SetCurrentState(LifecycleState::Resumed);
    NavHostController* nav = nullptr;
    lifecycle::LifecycleOwnerPtr homeOwner;
    std::vector<std::string> events;
    h.Run([&] {
        rt::Composition::Current()->Provide(lifecycle::LocalLifecycleOwner, lifecycle::LifecycleOwnerPtr(host), [&] {
            auto& c = RememberNavController();
            nav = &c;
            NavHost(c, "home", [&](NavGraphBuilder& b) {
                b.Composable("home", [&](const NavBackStackEntry&) {
                    homeOwner = lifecycle::CurrentLifecycleOwner();
                    lifecycle::LifecycleResumeEffect(rt::Keys(0), [&](lifecycle::LifecycleResumePauseEffectScope& s) {
                        events.push_back("resume");
                        return s.OnPauseOrDispose([&](lifecycle::LifecycleOwner&) { events.push_back("pause"); });
                    });
                });
                b.Composable("detail", [&](const NavBackStackEntry&) {});
            });
        });
    });
    h.Frames(2);
    auto entry = nav->CurrentBackStackEntry();
    CHECK(homeOwner == lifecycle::LifecycleOwnerPtr(entry) && entry->CurrentState() == LifecycleState::Resumed);
    CHECK(events == std::vector<std::string>{ "resume" });
    // The host pausing pauses the entry (and its effects).
    host->registry.HandleLifecycleEvent(lifecycle::Event::OnPause);
    CHECK(entry->CurrentState() == LifecycleState::Started && events.back() == "pause");
    host->registry.HandleLifecycleEvent(lifecycle::Event::OnResume);
    // Navigating away drops home below RESUMED, then to CREATED once the transition ends.
    nav->Navigate("detail");
    h.Frames(60);
    CHECK(entry->CurrentState() == LifecycleState::Created);
    CHECK(events == (std::vector<std::string>{ "resume", "pause", "resume", "pause" }));
}

TEST(NavHostAnswersVolitionWhereAndGo) {
    Harness h;
    h.Run([&] {
        auto& c = RememberNavController();
        NavHost(c, "home", [&](NavGraphBuilder& b) {
            b.Composable("home", [](const NavBackStackEntry&) { foundation::Box(ui::Modifier{}); });
            b.Composable("detail/{id}", [](const NavBackStackEntry&) { foundation::Box(ui::Modifier{}); },
                         { { NavArgumentOf("id", NavType::Int) } });
        });
    });
    namespace v = ui::volition;
    auto call = [](std::string_view m, std::string_view a = {}) { return v::Call({}, { "KilnActivity" }, m, a); };
    CHECK_EQ(call("where"), std::string("{\"activity\":\"KilnActivity\",\"ready\":true,\"route\":\"home\",\"stack\":[\"home\"]}"));
    CHECK_EQ(call("destinations"), std::string("back  up one destination\nhome  back to the start of the graph\nhome\ndetail/{id}"));
    CHECK_EQ(call("go", "detail:42"), std::string("ok: went to detail/42"));
    h.Frames(60);
    CHECK_EQ(call("check", "route=detail/{id}"), std::string("ok: route is detail/{id}"));
    CHECK_EQ(call("check", "from=home"), std::string("ok: from is home"));
    CHECK_EQ(call("check", "stack~home"), std::string("ok: stack is [\"home\",\"detail\\/{id}\"]"));
    CHECK_EQ(call("go", "nowhere"), std::string("error: no destination 'nowhere' - try destinations"));
    CHECK_EQ(call("go", "back"), std::string("ok: back to \"home\""));
    h.Frames(60);
    CHECK_EQ(call("go", "back"), std::string("error: nothing to go back to"));
    // Gone with its NavHost.
    h.Run([] {});
    CHECK_EQ(call("destinations"), std::string("error: this app has registered no destinations"));
}
