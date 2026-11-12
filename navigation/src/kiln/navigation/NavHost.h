#pragma once

// navigation-compose: the composable and dialog navigators, NavGraphBuilder's composable /
// navigation / dialog, NavHost (an AnimatedContent over the back stack's top entry that a
// predictive back gesture scrubs), DialogHost and rememberNavController. Not here (no kiln
// counterpart): the AnimatedContentScope given to content, ViewModel and lifecycle owners.

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Dialog.h"
#include "kiln/navigation/NavController.h"

namespace kiln::navigation {

    using ui::Alignment;
    using ui::Modifier;
    using foundation::ContentTransform;
    using foundation::EnterTransition;
    using foundation::ExitTransition;
    using foundation::SizeTransform;

    // AnimatedContentTransitionScope<NavBackStackEntry>: the scope with the entries it goes between.
    struct NavTransitionScope {
        foundation::AnimatedContentTransitionScope& scope;
        EntryPtr initialState, targetState;
    };
    using NavEnter = std::function<std::optional<EnterTransition>(NavTransitionScope&)>;
    using NavExit = std::function<std::optional<ExitTransition>(NavTransitionScope&)>;
    using NavPredictiveEnter = std::function<std::optional<EnterTransition>(NavTransitionScope&, ui::SwipeEdge)>;
    using NavPredictiveExit = std::function<std::optional<ExitTransition>(NavTransitionScope&, ui::SwipeEdge)>;
    using NavSizeTransform = std::function<std::optional<SizeTransform>(NavTransitionScope&)>;

    // A destination's own transitions (ComposeNavigator.Destination, ComposeNavGraph).
    struct NavDestinationTransitions {
        NavEnter enterTransition;
        NavExit exitTransition;
        NavEnter popEnterTransition;
        NavExit popExitTransition;
        NavPredictiveEnter predictivePopEnterTransition;
        NavPredictiveExit predictivePopExitTransition;
        NavSizeTransform sizeTransform;
    };

    // ComposeNavigator: destinations whose content NavHost shows.
    class ComposeNavigator final : public Navigator {
    public:
        static constexpr const char* kName = "composable";
        struct Destination final : NavDestination {
            explicit Destination(std::function<void(const NavBackStackEntry&)> c) : NavDestination(kName), content(std::move(c)) {}
            std::function<void(const NavBackStackEntry&)> content;
            NavDestinationTransitions transitions;
        };
        ComposeNavigator() : Navigator(kName) {}
        NavDestinationPtr CreateDestination() override { return std::make_shared<Destination>([](const NavBackStackEntry&) {}); }
        void Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions) override;
        void PopBackStack(const EntryPtr& popUpTo, bool savedState) override;
        void PrepareForTransition(const EntryPtr& entry) { State().PrepareForTransition(entry); }
        void OnTransitionComplete(const EntryPtr& entry) { State().MarkTransitionComplete(entry); }
        const std::vector<EntryPtr>& BackStack() const { return State().BackStack(); }
        const std::vector<EntryPtr>& TransitionsInProgress() const { return State().TransitionsInProgress(); }
        bool IsPop() const { return m_IsPop.Get(); }
    private:
        rt::State<bool> m_IsPop{ false };
    };

    // DialogNavigator: destinations shown in dialogs (floating windows).
    class DialogNavigator final : public Navigator {
    public:
        static constexpr const char* kName = "dialog";
        struct Destination final : NavDestination {
            Destination(foundation::DialogProperties p, std::function<void(const NavBackStackEntry&)> c)
                : NavDestination(kName), dialogProperties(p), content(std::move(c)) {}
            bool IsFloatingWindow() const override { return true; }
            foundation::DialogProperties dialogProperties;
            std::function<void(const NavBackStackEntry&)> content;
        };
        DialogNavigator() : Navigator(kName) {}
        NavDestinationPtr CreateDestination() override { return std::make_shared<Destination>(foundation::DialogProperties{}, [](const NavBackStackEntry&) {}); }
        void Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>&) override { for (const auto& e : entries) State().Push(e); }
        void PopBackStack(const EntryPtr& popUpTo, bool savedState) override;
        void Dismiss(const EntryPtr& entry) { PopBackStack(entry, false); }
        void OnTransitionComplete(const EntryPtr& entry) { State().MarkTransitionComplete(entry); }
        const std::vector<EntryPtr>& BackStack() const { return State().BackStack(); }
        const std::vector<EntryPtr>& TransitionsInProgress() const { return State().TransitionsInProgress(); }
    };

    // ComposeNavGraphNavigator: graphs that carry transitions for their destinations.
    class ComposeNavGraphNavigator final : public NavGraphNavigator {
    public:
        struct ComposeNavGraph final : NavGraph {
            NavDestinationTransitions transitions;
        };
        explicit ComposeNavGraphNavigator(NavigatorProvider& p) : NavGraphNavigator(p) {}
        NavDestinationPtr CreateDestination() override { return std::make_shared<ComposeNavGraph>(); }
    };

    struct ComposableOptions {
        std::vector<NamedNavArgument> arguments;
        std::vector<std::string> deepLinks;
        NavDestinationTransitions transitions;   // pop transitions default to the enter / exit ones
    };
    struct DialogOptions {
        std::vector<NamedNavArgument> arguments;
        std::vector<std::string> deepLinks;
        foundation::DialogProperties dialogProperties;
    };

    // NavGraphBuilder: a graph's destinations.
    class NavGraphBuilder {
    public:
        NavGraphBuilder(NavigatorProvider& provider, std::string startDestination, std::optional<std::string> route)
            : m_Provider(provider), m_Start(std::move(startDestination)), m_Route(std::move(route)) {}
        NavigatorProvider& Provider() { return m_Provider; }
        void AddDestination(NavDestinationPtr d) { m_Destinations.push_back(std::move(d)); }
        // composable(route, arguments, deepLinks, transitions) { entry -> }
        void Composable(const std::string& route, std::function<void(const NavBackStackEntry&)> content, ComposableOptions options = {});
        // navigation(startDestination, route, ...) { nested graph }
        void Navigation(const std::string& startDestination, const std::string& route, const std::function<void(NavGraphBuilder&)>& builder,
                        ComposableOptions options = {});
        // dialog(route, arguments, deepLinks, dialogProperties) { entry -> }
        void Dialog(const std::string& route, std::function<void(const NavBackStackEntry&)> content, DialogOptions options = {});
        std::shared_ptr<NavGraph> Build();
    private:
        NavigatorProvider& m_Provider;
        std::string m_Start;
        std::optional<std::string> m_Route;
        std::vector<NavDestinationPtr> m_Destinations;
    };

    // NavHostController: a NavController with the compose navigators.
    class NavHostController : public NavController {
    public:
        NavHostController();
        // createGraph(startDestination, route) { ... }
        std::shared_ptr<NavGraph> CreateGraph(const std::string& startDestination, std::optional<std::string> route,
                                              const std::function<void(NavGraphBuilder&)>& builder);
    };

    // rememberNavController(): saved and restored with the back stack.
    NavHostController& RememberNavController(rt::Key key = {});
    // currentBackStackEntryAsState()
    EntryPtr CurrentBackStackEntryAsState(const NavController& controller);

    // DefaultNavTransitions (Android): fade in / out over 700 ms; predictive back: fadeIn(spring)
    // and scaleOut(0.7).
    namespace DefaultNavTransitions {
        NavEnter EnterTransition();
        NavExit ExitTransition();
        NavPredictiveEnter PredictivePopEnterTransition();
        NavPredictiveExit PredictivePopExitTransition();
    }

    struct NavHostOptions {
        Alignment contentAlignment = Alignment::TopStart();
        NavEnter enterTransition = DefaultNavTransitions::EnterTransition();
        NavExit exitTransition = DefaultNavTransitions::ExitTransition();
        NavEnter popEnterTransition;   // unset: enterTransition
        NavExit popExitTransition;     // unset: exitTransition
        NavPredictiveEnter predictivePopEnterTransition = DefaultNavTransitions::PredictivePopEnterTransition();
        NavPredictiveExit predictivePopExitTransition = DefaultNavTransitions::PredictivePopExitTransition();
        NavSizeTransform sizeTransform;
    };

    // NavHost(navController, graph, ...)
    void NavHost(NavHostController& navController, std::shared_ptr<NavGraph> graph, const Modifier& modifier = {}, const NavHostOptions& options = {},
                 rt::Key key = {});
    // NavHost(navController, startDestination, route, ...) { builder }: the graph is built once per
    // start destination and route.
    void NavHost(NavHostController& navController, const std::string& startDestination, const std::function<void(NavGraphBuilder&)>& builder,
                 const Modifier& modifier = {}, const NavHostOptions& options = {}, std::optional<std::string> route = std::nullopt, rt::Key key = {});
    // DialogHost(dialogNavigator)
    void DialogHost(DialogNavigator& dialogNavigator, rt::Key key = {});

}
