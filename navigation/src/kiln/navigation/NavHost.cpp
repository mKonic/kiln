#include "kiln/navigation/NavHost.h"

#include "kiln/foundation/Back.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>

namespace kiln::navigation {

    // --- Navigators -----------------------------------------------------------------------------

    void ComposeNavigator::Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>&) {
        for (const auto& e : entries) State().PushWithTransition(e);
        if (m_IsPop.Peek()) m_IsPop.Set(false);
    }

    void ComposeNavigator::PopBackStack(const EntryPtr& popUpTo, bool savedState) {
        State().PopWithTransition(popUpTo, savedState);
        if (!m_IsPop.Peek()) m_IsPop.Set(true);
    }

    void DialogNavigator::PopBackStack(const EntryPtr& popUpTo, bool savedState) {
        State().PopWithTransition(popUpTo, savedState);
        // Dialogs above the popped one finish at once.
        const auto transitions = State().PeekTransitions();
        auto it = std::find(transitions.begin(), transitions.end(), popUpTo);
        const long popIndex = it == transitions.end() ? -1 : long(it - transitions.begin());
        for (long i = 0; i < long(transitions.size()); ++i)
            if (i > popIndex) OnTransitionComplete(transitions[std::size_t(i)]);
    }

    // --- NavGraphBuilder ------------------------------------------------------------------------

    namespace {
        void ApplyArguments(NavDestination& d, const std::vector<NamedNavArgument>& args, const std::vector<std::string>& deepLinks) {
            for (const auto& a : args) d.AddArgument(a.name, a.argument);
            for (const auto& l : deepLinks) d.AddDeepLink(l);
        }
        NavDestinationTransitions WithPopDefaults(NavDestinationTransitions t) {
            if (!t.popEnterTransition) t.popEnterTransition = t.enterTransition;
            if (!t.popExitTransition) t.popExitTransition = t.exitTransition;
            return t;
        }
    }

    void NavGraphBuilder::Composable(const std::string& route, std::function<void(const NavBackStackEntry&)> content, ComposableOptions options) {
        auto d = std::make_shared<ComposeNavigator::Destination>(std::move(content));
        // Arguments first: the route checks them.
        ApplyArguments(*d, options.arguments, options.deepLinks);
        d->SetRoute(route);
        d->transitions = WithPopDefaults(std::move(options.transitions));
        AddDestination(d);
    }

    void NavGraphBuilder::Navigation(const std::string& startDestination, const std::string& route, const std::function<void(NavGraphBuilder&)>& builder,
                                     ComposableOptions options) {
        NavGraphBuilder nested(m_Provider, startDestination, route);
        builder(nested);
        auto graph = nested.Build();
        ApplyArguments(*graph, options.arguments, options.deepLinks);
        if (auto* g = dynamic_cast<ComposeNavGraphNavigator::ComposeNavGraph*>(graph.get())) g->transitions = WithPopDefaults(std::move(options.transitions));
        AddDestination(graph);
    }

    void NavGraphBuilder::Dialog(const std::string& route, std::function<void(const NavBackStackEntry&)> content, DialogOptions options) {
        auto d = std::make_shared<DialogNavigator::Destination>(options.dialogProperties, std::move(content));
        ApplyArguments(*d, options.arguments, options.deepLinks);
        d->SetRoute(route);
        AddDestination(d);
    }

    std::shared_ptr<NavGraph> NavGraphBuilder::Build() {
        auto graph = std::static_pointer_cast<NavGraph>(m_Provider.Get("navigation")->CreateDestination());
        if (m_Route) graph->SetRoute(*m_Route);
        for (auto& d : m_Destinations) graph->AddDestination(d);
        graph->SetStartDestination(m_Start);
        return graph;
    }

    // --- NavHostController ----------------------------------------------------------------------

    NavHostController::NavHostController() {
        Provider().AddNavigator(std::make_shared<ComposeNavGraphNavigator>(Provider()));
        Provider().AddNavigator(std::make_shared<ComposeNavigator>());
        Provider().AddNavigator(std::make_shared<DialogNavigator>());
    }

    std::shared_ptr<NavGraph> NavHostController::CreateGraph(const std::string& startDestination, std::optional<std::string> route,
                                                             const std::function<void(NavGraphBuilder&)>& builder) {
        NavGraphBuilder b(Provider(), startDestination, std::move(route));
        builder(b);
        return b.Build();
    }

    NavHostController& RememberNavController(rt::Key key) {
        using Ptr = std::shared_ptr<NavHostController>;
        rt::Saver<Ptr> saver{
            [](const Ptr& c) { return c->SaveState(); },
            [](const rt::SavedValue& s) -> std::optional<Ptr> {
                auto c = std::make_shared<NavHostController>();
                c->RestoreState(s);
                return c;
            },
        };
        return *rt::RememberSaveable<Ptr>([] { return std::make_shared<NavHostController>(); }, saver, {}, key);
    }

    EntryPtr CurrentBackStackEntryAsState(const NavController& controller) { return controller.CurrentBackStackEntryObserved(); }

    // --- DefaultNavTransitions ------------------------------------------------------------------

    namespace DefaultNavTransitions {
        NavEnter EnterTransition() { return [](NavTransitionScope&) -> std::optional<foundation::EnterTransition> { return foundation::FadeIn(anim::Tween(700)); }; }
        NavExit ExitTransition() { return [](NavTransitionScope&) -> std::optional<foundation::ExitTransition> { return foundation::FadeOut(anim::Tween(700)); }; }
        NavPredictiveEnter PredictivePopEnterTransition() {
            return [](NavTransitionScope&, ui::SwipeEdge) -> std::optional<foundation::EnterTransition> { return foundation::FadeIn(anim::Spring(1.0f, 1600.0f)); };
        }
        NavPredictiveExit PredictivePopExitTransition() {
            return [](NavTransitionScope&, ui::SwipeEdge) -> std::optional<foundation::ExitTransition> {
                return foundation::ScaleOut(foundation::DefaultEnterExitSpring(), 0.7f);
            };
        }
    }

    // --- NavHost --------------------------------------------------------------------------------

    namespace {
        std::uint64_t EntryKey(const EntryPtr& e) { return std::uint64_t(std::hash<std::string>{}(e->Id())) | 1u; }

        const NavDestinationTransitions* TransitionsOf(NavDestination* d) {
            if (auto* c = dynamic_cast<ComposeNavigator::Destination*>(d)) return &c->transitions;
            if (auto* g = dynamic_cast<ComposeNavGraphNavigator::ComposeNavGraph*>(d)) return &g->transitions;
            return nullptr;
        }

        // The first transition the destination's hierarchy gives (hierarchy.firstNotNullOfOrNull).
        template<typename R, typename Get>
        std::optional<R> FromHierarchy(NavDestination* d, Get get) {
            for (NavDestination* h : d->Hierarchy())
                if (const auto* t = TransitionsOf(h))
                    if (auto r = get(*t)) return r;
            return std::nullopt;
        }

        // NavHostEventHandler: the back gesture scrubs the transition to the previous entry; its
        // completion pops the composable back stack.
        struct NavHostBackHandler final : ui::BackCallback {
            ComposeNavigator* navigator = nullptr;
            EntryPtr currentEntry;
            rt::State<bool> inPredictiveBack{ false };
            rt::State<float> progress{ 0 };
            rt::State<ui::SwipeEdge> swipeEdge{ ui::SwipeEdge::None };
            void OnBackStarted(const ui::BackEvent& e) override {
                PrepareTransition();
                inPredictiveBack.Set(true);
                progress.Set(e.progress);
                swipeEdge.Set(e.swipeEdge);
            }
            void OnBackProgressed(const ui::BackEvent& e) override {
                inPredictiveBack.Set(true);
                progress.Set(e.progress);
                swipeEdge.Set(e.swipeEdge);
            }
            void OnBack() override {
                if (!inPredictiveBack.Peek()) PrepareTransition();
                if (currentEntry) navigator->PopBackStack(currentEntry, false);
                Reset();
            }
            void OnBackCancelled() override { Reset(); }
            void PrepareTransition() {
                const auto backStack = navigator->BackStack();
                if (backStack.size() < 2) return;
                navigator->PrepareForTransition(backStack.back());
                navigator->PrepareForTransition(backStack[backStack.size() - 2]);
                currentEntry = backStack.back();
            }
            void Reset() {
                inPredictiveBack.Set(false);
                progress.Set(0);
                swipeEdge.Set(ui::SwipeEdge::None);
                currentEntry = nullptr;
            }
        };

        // LocalOwnersProvider: the entry's saveable state, kept under its id.
        void LocalOwnersProvider(const EntryPtr& entry, rt::SaveableStateHolder& holder, std::function<void()> content) {
            rt::SaveableStateProvider(holder, entry->Id(), std::move(content));
        }

        struct HostBook {
            std::map<std::uint64_t, EntryPtr> entries;   // content key -> entry
            std::map<std::string, float> zIndices;        // entry id -> z index
            std::vector<EntryPtr> visible;
        };
    }

    void NavHost(NavHostController& navController, std::shared_ptr<NavGraph> graph, const Modifier& modifier, const NavHostOptions& options, rt::Key key) {
        auto& c = *rt::Composition::Current();
        NavHostController* nc = &navController;
        c.Child([&c, nc, graph, modifier, options] {
            // The host is resumed (the activity's lifecycle owner).
            nc->SetHostLifecycleState(LifecycleState::Resumed);
            nc->SetGraph(graph);
            auto* composeNavigator = nc->Provider().Get<ComposeNavigator>();
            if (!composeNavigator) return;
            const std::vector<EntryPtr> currentBackStack = composeNavigator->BackStack();

            // Back handling.
            ui::BackDispatcher* dispatcher = c.Read(foundation::LocalBackDispatcher);
            auto& handler = c.Remember<std::shared_ptr<NavHostBackHandler>>([composeNavigator] {
                auto h = std::make_shared<NavHostBackHandler>();
                h->navigator = composeNavigator;
                h->enabled = false;
                return h;
            });
            {
                auto keep = handler;
                c.DisposableEffect(rt::Keys(dispatcher), [dispatcher, keep] {
                    dispatcher->Add(keep.get());
                    return std::function<void()>([dispatcher, keep] { dispatcher->Remove(keep.get()); });
                });
                const bool enabled = currentBackStack.size() > 1;
                c.SideEffect([dispatcher, keep, enabled] { keep->enabled = enabled; dispatcher->EnabledChanged(); });
            }
            const bool inPredictiveBack = handler->inPredictiveBack.Get();
            const float progress = handler->progress.Get();
            const ui::SwipeEdge swipeEdge = handler->swipeEdge.Get();

            rt::SaveableStateHolder& holder = rt::RememberSaveableStateHolder();
            rt::SaveableStateHolder* holderPtr = &holder;
            nc->onEntryCleared = [holderPtr](const std::string& id) { holderPtr->RemoveState(id); };

            std::vector<EntryPtr> visibleEntries;
            for (const auto& e : nc->VisibleEntries())
                if (e->Destination()->NavigatorName() == ComposeNavigator::kName) visibleEntries.push_back(e);
            const EntryPtr backStackEntry = visibleEntries.empty() ? nullptr : visibleEntries.back();
            auto& book = c.Remember<std::shared_ptr<HostBook>>([] { return std::make_shared<HostBook>(); });
            book->visible = visibleEntries;
            for (const auto& e : visibleEntries) book->entries[EntryKey(e)] = e;
            for (const auto& e : currentBackStack) book->entries[EntryKey(e)] = e;

            if (backStackEntry) {
                const bool isPop = composeNavigator->IsPop();
                auto bookRef = book;
                auto scopeFor = [bookRef](foundation::AnimatedContentTransitionScope& s) {
                    auto find = [&](std::uint64_t k) -> EntryPtr { auto it = bookRef->entries.find(k); return it == bookRef->entries.end() ? nullptr : it->second; };
                    return NavTransitionScope{ s, find(s.initialState.value), find(s.targetState.value) };
                };
                auto finalEnter = [options, inPredictiveBack, isPop, swipeEdge](NavTransitionScope& s) -> EnterTransition {
                    NavDestination* target = s.targetState->Destination();
                    if (inPredictiveBack) {
                        if (auto r = FromHierarchy<EnterTransition>(target, [&](const NavDestinationTransitions& t) {
                                return t.predictivePopEnterTransition ? t.predictivePopEnterTransition(s, swipeEdge) : std::nullopt; })) return *r;
                        return options.predictivePopEnterTransition ? options.predictivePopEnterTransition(s, swipeEdge).value_or(EnterTransition{}) : EnterTransition{};
                    }
                    if (isPop) {
                        if (auto r = FromHierarchy<EnterTransition>(target, [&](const NavDestinationTransitions& t) {
                                return t.popEnterTransition ? t.popEnterTransition(s) : std::nullopt; })) return *r;
                        const NavEnter& pop = options.popEnterTransition ? options.popEnterTransition : options.enterTransition;
                        return pop ? pop(s).value_or(EnterTransition{}) : EnterTransition{};
                    }
                    if (auto r = FromHierarchy<EnterTransition>(target, [&](const NavDestinationTransitions& t) {
                            return t.enterTransition ? t.enterTransition(s) : std::nullopt; })) return *r;
                    return options.enterTransition ? options.enterTransition(s).value_or(EnterTransition{}) : EnterTransition{};
                };
                auto finalExit = [options, inPredictiveBack, isPop, swipeEdge](NavTransitionScope& s) -> ExitTransition {
                    NavDestination* initial = s.initialState->Destination();
                    if (inPredictiveBack) {
                        if (auto r = FromHierarchy<ExitTransition>(initial, [&](const NavDestinationTransitions& t) {
                                return t.predictivePopExitTransition ? t.predictivePopExitTransition(s, swipeEdge) : std::nullopt; })) return *r;
                        return options.predictivePopExitTransition ? options.predictivePopExitTransition(s, swipeEdge).value_or(ExitTransition{}) : ExitTransition{};
                    }
                    if (isPop) {
                        if (auto r = FromHierarchy<ExitTransition>(initial, [&](const NavDestinationTransitions& t) {
                                return t.popExitTransition ? t.popExitTransition(s) : std::nullopt; })) return *r;
                        const NavExit& pop = options.popExitTransition ? options.popExitTransition : options.exitTransition;
                        return pop ? pop(s).value_or(ExitTransition{}) : ExitTransition{};
                    }
                    if (auto r = FromHierarchy<ExitTransition>(initial, [&](const NavDestinationTransitions& t) {
                            return t.exitTransition ? t.exitTransition(s) : std::nullopt; })) return *r;
                    return options.exitTransition ? options.exitTransition(s).value_or(ExitTransition{}) : ExitTransition{};
                };
                auto finalSize = [options](NavTransitionScope& s) -> std::optional<SizeTransform> {
                    if (auto r = FromHierarchy<SizeTransform>(s.targetState->Destination(), [&](const NavDestinationTransitions& t) {
                            return t.sizeTransform ? t.sizeTransform(s) : std::nullopt; })) return r;
                    return options.sizeTransform ? options.sizeTransform(s) : std::nullopt;
                };

                // Leaving the composition finishes the transitions in progress.
                c.DisposableEffect(rt::Keys(true), [composeNavigator, bookRef] {
                    return std::function<void()>([composeNavigator, bookRef] {
                        for (const auto& e : bookRef->visible) composeNavigator->OnTransitionComplete(e);
                    });
                });

                auto& transitionState = c.RememberNew<foundation::SeekableTransitionState<std::uint64_t>>({}, {}, EntryKey(backStackEntry));
                auto& transition = foundation::UpdateTransition(transitionState);
                auto* ts = &transitionState;
                auto* tr = &transition;
                if (inPredictiveBack) {
                    std::optional<std::uint64_t> previous;
                    if (currentBackStack.size() > 1) previous = EntryKey(currentBackStack[currentBackStack.size() - 2]);
                    c.LaunchedEffect(rt::Keys(progress), [ts, progress, previous]() -> rt::Task<> {
                        return [](foundation::SeekableTransitionState<std::uint64_t>* s, float p, std::optional<std::uint64_t> prev) -> rt::Task<> {
                            if (prev) co_await s->SeekTo(p, *prev);
                        }(ts, progress, previous);
                    });
                } else {
                    const std::uint64_t target = EntryKey(backStackEntry);
                    c.LaunchedEffect(rt::Keys(target), [ts, tr, target]() -> rt::Task<> {
                        return [](foundation::SeekableTransitionState<std::uint64_t>* s, foundation::Transition<std::uint64_t>* t, std::uint64_t to) -> rt::Task<> {
                            if (s->CurrentState() != to) {
                                co_await s->AnimateTo(to);
                            } else {
                                // A cancelled gesture: back to the current entry over what is left.
                                const float fraction = s->Fraction();
                                const std::int64_t totalMs = t->Core().TotalDurationNanos() / 1000000;
                                co_await anim::Animate(fraction, 0.0f, anim::Tween(int(fraction * float(totalMs))), [s, to](float value, float) {
                                    if (value > 0) rt::Launch(rt::Dispatcher::Main(), s->SeekTo(value));
                                    if (value == 0) rt::Launch(rt::Dispatcher::Main(), s->SnapTo(to));
                                });
                            }
                        }(ts, tr, target);
                    });
                }

                foundation::AnimatedContent(transition, modifier,
                    [bookRef, ts, inPredictiveBack, holderPtr, backStackEntry](rt::Key k) {
                        auto it = bookRef->entries.find(k.value);
                        if (it == bookRef->entries.end()) return;
                        const bool cancelAnimation = ts->CurrentState() == EntryKey(backStackEntry);
                        EntryPtr current;
                        if (inPredictiveBack || cancelAnimation) current = it->second;
                        else for (auto v = bookRef->visible.rbegin(); v != bookRef->visible.rend(); ++v) if (*v == it->second) { current = *v; break; }
                        if (!current) return;
                        LocalOwnersProvider(current, *holderPtr, [current] {
                            static_cast<ComposeNavigator::Destination*>(current->Destination())->content(*current);
                        });
                    },
                    [bookRef, scopeFor, finalEnter, finalExit, finalSize, isPop, inPredictiveBack](foundation::AnimatedContentTransitionScope& scope) {
                        NavTransitionScope s = scopeFor(scope);
                        ContentTransform t;
                        const bool initialVisible = s.initialState && std::find(bookRef->visible.begin(), bookRef->visible.end(), s.initialState) != bookRef->visible.end();
                        if (initialVisible && s.targetState) {
                            float& initialZ = bookRef->zIndices[s.initialState->Id()];
                            const float targetZ = s.targetState->Id() == s.initialState->Id() ? initialZ : (isPop || inPredictiveBack) ? initialZ - 1 : initialZ + 1;
                            bookRef->zIndices[s.targetState->Id()] = targetZ;
                            t.enter = finalEnter(s);
                            t.exit = finalExit(s);
                            t.targetContentZIndex = targetZ;
                        }
                        t.sizeTransform = s.targetState ? finalSize(s) : std::nullopt;
                        return t;
                    },
                    options.contentAlignment);

                // Settled: the visible entries' transitions are done.
                const std::uint64_t current = transition.CurrentState(), targetState = transition.TargetState();
                if (current == targetState) {
                    const EntryPtr navCurrent = nc->CurrentBackStackEntry();
                    const bool done = !navCurrent || targetState == EntryKey(backStackEntry);
                    if (done) {
                        c.SideEffect([composeNavigator, bookRef, targetState] {
                            for (const auto& e : std::vector<EntryPtr>(bookRef->visible)) composeNavigator->OnTransitionComplete(e);
                            std::string keepId;
                            if (auto it = bookRef->entries.find(targetState); it != bookRef->entries.end()) keepId = it->second->Id();
                            std::erase_if(bookRef->zIndices, [&](const auto& kv) { return kv.first != keepId; });
                        });
                    }
                }
            }

            if (auto* dialogNavigator = nc->Provider().Get<DialogNavigator>()) DialogHost(*dialogNavigator);
        }, key);
    }

    void NavHost(NavHostController& navController, const std::string& startDestination, const std::function<void(NavGraphBuilder&)>& builder,
                 const Modifier& modifier, const NavHostOptions& options, std::optional<std::string> route, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto& graph = c.Remember<std::shared_ptr<NavGraph>>([&] { return navController.CreateGraph(startDestination, route, builder); },
                                                            rt::Keys(startDestination, route.value_or(std::string())), key);
        NavHost(navController, graph, modifier, options, key);
    }

    void DialogHost(DialogNavigator& dialogNavigator, rt::Key key) {
        auto& c = *rt::Composition::Current();
        DialogNavigator* dn = &dialogNavigator;
        c.Child([&c, dn] {
            rt::SaveableStateHolder& holder = rt::RememberSaveableStateHolder();
            rt::SaveableStateHolder* holderPtr = &holder;
            const std::vector<EntryPtr> backStack = dn->BackStack();
            const std::vector<EntryPtr> transitions = dn->TransitionsInProgress();
            auto& toDispose = c.Remember<std::shared_ptr<rt::StateList<EntryPtr>>>([] { return std::make_shared<rt::StateList<EntryPtr>>(); });
            // The dialogs at least STARTED are shown.
            for (const auto& entry : backStack) {
                if (int(entry->CurrentState()) < int(LifecycleState::Started)) continue;
                auto* destination = static_cast<DialogNavigator::Destination*>(entry->Destination());
                auto list = toDispose;
                c.Section([&c, dn, entry, destination, holderPtr, list] {
                    foundation::Dialog([dn, entry] { dn->Dismiss(entry); }, destination->dialogProperties, [&c, dn, entry, destination, holderPtr, list] {
                        auto& cc = *rt::Composition::Current();
                        (void)c;
                        cc.DisposableEffect(rt::Keys(entry.get()), [dn, entry, list] {
                            list->Add(entry);
                            return std::function<void()>([dn, entry, list] {
                                dn->OnTransitionComplete(entry);
                                list->Update([&](std::vector<EntryPtr>& v) { std::erase(v, entry); });
                            });
                        });
                        LocalOwnersProvider(entry, *holderPtr, [entry, destination] { destination->content(*entry); });
                    });
                }, rt::Key(entry->Id()));
            }
            // Popped dialogs that are not being disposed finish their transition.
            const std::vector<EntryPtr> disposing = toDispose->Get();
            c.SideEffect([dn, transitions, disposing] {
                for (const auto& e : transitions) {
                    const auto& stack = dn->BackStack();
                    if (std::find(stack.begin(), stack.end(), e) == stack.end() && std::find(disposing.begin(), disposing.end(), e) == disposing.end())
                        dn->OnTransitionComplete(e);
                }
            });
        }, key);
    }

}
