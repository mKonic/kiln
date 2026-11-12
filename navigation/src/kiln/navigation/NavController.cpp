#include "kiln/navigation/NavController.h"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace kiln::navigation {

    namespace {
        std::string RandomUuid() {
            static std::mt19937_64 rng{ std::random_device{}() };
            const std::uint64_t a = rng(), b = rng();
            char buf[40];
            std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%012llx", unsigned(a >> 32), unsigned((a >> 16) & 0xffff), unsigned(a & 0xfff),
                          unsigned(0x8000 | (b >> 50 & 0x3fff)), (unsigned long long)(b & 0xffffffffffffull));
            return buf;
        }
        bool AtLeast(LifecycleState a, LifecycleState b) { return int(a) >= int(b); }
        bool Contains(const std::vector<EntryPtr>& v, const EntryPtr& e) { return std::find(v.begin(), v.end(), e) != v.end(); }
    }

    // --- NavBackStackEntry ----------------------------------------------------------------------

    NavBackStackEntry::NavBackStackEntry(NavDestination* destination, std::optional<NavArgs> arguments, LifecycleState host, std::string id)
        : m_Destination(destination), m_Args(std::move(arguments)), m_Host(host), m_Id(id.empty() ? RandomUuid() : std::move(id)) {}

    void NavBackStackEntry::UpdateState() {
        const LifecycleState s = int(m_Host) < int(m_Max) ? m_Host : m_Max;
        // An entry that never got to CREATED has nothing to destroy.
        if (m_Lifecycle.CurrentState() == LifecycleState::Initialized && s == LifecycleState::Destroyed) return;
        m_Lifecycle.SetCurrentState(s);
    }

    std::optional<std::string> NavBackStackEntry::GetString(const std::string& key) const {
        if (!m_Args) return std::nullopt;
        auto it = m_Args->find(key);
        if (it == m_Args->end()) return std::nullopt;
        if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
        return std::nullopt;
    }

    std::optional<std::int32_t> NavBackStackEntry::GetInt(const std::string& key) const {
        if (!m_Args) return std::nullopt;
        auto it = m_Args->find(key);
        if (it == m_Args->end()) return std::nullopt;
        if (const auto* i = std::get_if<std::int32_t>(&it->second)) return *i;
        return std::nullopt;
    }

    // --- NavigatorState -------------------------------------------------------------------------

    void NavigatorState::Push(const EntryPtr& entry) {
        auto v = m_BackStack.Peek();
        v.push_back(entry);
        m_BackStack.Set(std::move(v));
    }

    void NavigatorState::AddTransition(const EntryPtr& e) {
        if (Contains(m_Transitions.Peek(), e)) return;
        auto v = m_Transitions.Peek();
        v.push_back(e);
        m_Transitions.Set(std::move(v));
    }

    void NavigatorState::RemoveTransition(const EntryPtr& e) {
        if (!Contains(m_Transitions.Peek(), e)) return;
        auto v = m_Transitions.Peek();
        std::erase(v, e);
        m_Transitions.Set(std::move(v));
    }

    void NavigatorState::PushWithTransition(const EntryPtr& entry) {
        if (Contains(m_Transitions.Peek(), entry) && Contains(m_BackStack.Peek(), entry)) return;
        if (!m_BackStack.Peek().empty()) AddTransition(m_BackStack.Peek().back());
        AddTransition(entry);
        Push(entry);
    }

    void NavigatorState::Pop(const EntryPtr& popUpTo, bool) {
        std::vector<EntryPtr> v;
        for (const auto& e : m_BackStack.Peek()) {
            if (e == popUpTo) break;
            v.push_back(e);
        }
        m_BackStack.Set(std::move(v));
    }

    void NavigatorState::PopWithTransition(const EntryPtr& popUpTo, bool saveState) {
        const auto& stack = m_BackStack.Peek();
        if (Contains(m_Transitions.Peek(), popUpTo) && !Contains(stack, popUpTo)) return;
        AddTransition(popUpTo);
        // The entry that comes back: the last one below popUpTo.
        auto popIt = std::find(stack.rbegin(), stack.rend(), popUpTo);
        if (popIt != stack.rend() && std::next(popIt) != stack.rend()) AddTransition(*std::next(popIt));
        Pop(popUpTo, saveState);
    }

    void NavigatorState::OnLaunchSingleTop(const EntryPtr& entry) {
        auto v = m_BackStack.Peek();
        for (auto it = v.rbegin(); it != v.rend(); ++it)
            if ((*it)->Id() == entry->Id()) { *it = entry; break; }
        m_BackStack.Set(std::move(v));
    }

    void NavigatorState::OnLaunchSingleTopWithTransition(const EntryPtr& entry) {
        for (auto it = m_BackStack.Peek().rbegin(); it != m_BackStack.Peek().rend(); ++it)
            if ((*it)->Id() == entry->Id()) { AddTransition(*it); break; }
        AddTransition(entry);
        OnLaunchSingleTop(entry);
    }

    void NavigatorState::MarkTransitionComplete(const EntryPtr& entry) { RemoveTransition(entry); }
    void NavigatorState::PrepareForTransition(const EntryPtr& entry) { AddTransition(entry); }

    // --- Navigator ------------------------------------------------------------------------------

    void Navigator::Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions) {
        for (const auto& entry : entries) {
            NavDestination* to = NavigateTo(entry->Destination(), entry->Arguments(), navOptions);
            if (!to) continue;
            if (to == entry->Destination()) State().Push(entry);
            else State().Push(State().CreateBackStackEntry(to, to->AddInDefaultArgs(entry->Arguments())));
        }
    }

    void Navigator::OnLaunchSingleTop(const EntryPtr& entry) {
        NavOptions o;
        o.launchSingleTop = true;
        NavigateTo(entry->Destination(), std::nullopt, o);
        State().OnLaunchSingleTop(entry);
    }

    void Navigator::PopBackStack(const EntryPtr& popUpTo, bool savedState) {
        const auto backStack = State().PeekBackStack();
        if (!Contains(backStack, popUpTo)) throw std::logic_error("popBackStack was called with an entry which does not exist in back stack");
        EntryPtr lastPopped;
        for (auto it = backStack.rbegin(); it != backStack.rend();) {
            if (!PopBackStack()) break;
            lastPopped = *it++;
            if (lastPopped == popUpTo) break;
        }
        if (lastPopped) State().Pop(lastPopped, savedState);
    }

    Navigator* NavigatorProvider::AddNavigator(std::shared_ptr<Navigator> navigator) {
        Navigator* raw = navigator.get();
        auto& slot = m_Navigators[navigator->Name()];
        if (slot && slot->IsAttached()) throw std::logic_error("Navigator is already attached");
        slot = std::move(navigator);
        return raw;
    }

    Navigator* NavigatorProvider::Get(const std::string& name) const {
        auto it = m_Navigators.find(name);
        if (it == m_Navigators.end()) throw std::logic_error("Could not find Navigator with name \"" + name + "\". You must call NavController.addNavigator() for each navigation type.");
        return it->second.get();
    }

    void NavGraphNavigator::Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions) {
        for (const auto& entry : entries) {
            auto* destination = static_cast<NavGraph*>(entry->Destination());
            std::optional<NavArgs> args = entry->Arguments();
            const int startId = destination->StartDestinationId();
            const auto& startRoute = destination->StartDestinationRoute();
            if (startId == 0 && !startRoute) throw std::logic_error("no start destination defined for " + destination->DisplayName());
            NavDestination* start = startRoute ? destination->FindNode(*startRoute, false) : destination->NodeById(startId);
            if (!start) throw std::invalid_argument("navigation destination " + destination->StartDestDisplayName() + " is not a direct child of this NavGraph");
            if (startRoute) {
                if (start->Route() != *startRoute) {
                    const auto m = start->MatchRoute(*startRoute);
                    if (m && m->matchingArgs && !m->matchingArgs->empty()) {
                        NavArgs merged = *m->matchingArgs;
                        if (args) for (const auto& [k, v] : *args) merged[k] = v;
                        args = merged;
                    }
                }
                for (const auto& [k, a] : start->Arguments())
                    if (!a.isNullable && !a.IsDefaultValuePresent() && (!args || !args->contains(k)))
                        throw std::invalid_argument("Cannot navigate to startDestination. Missing required arguments [" + k + "]");
            }
            Navigator* navigator = m_Provider.Get(start->NavigatorName());
            navigator->Navigate({ State().CreateBackStackEntry(start, start->AddInDefaultArgs(args)) }, navOptions);
        }
    }

    // --- NavController --------------------------------------------------------------------------

    NavController::NavController() { m_Provider.AddNavigator(std::make_shared<NavGraphNavigator>(m_Provider)); }

    NavController::~NavController() {
        if (m_LifecycleOwner) m_LifecycleOwner->GetLifecycle().RemoveObserver(m_LifecycleObserver);
    }

    void NavController::SetLifecycleOwner(std::shared_ptr<lifecycle::LifecycleOwner> owner) {
        if (owner == m_LifecycleOwner) return;
        if (m_LifecycleOwner) m_LifecycleOwner->GetLifecycle().RemoveObserver(m_LifecycleObserver);
        m_LifecycleOwner = std::move(owner);
        if (!m_LifecycleObserver)
            m_LifecycleObserver = lifecycle::MakeEventObserver([this](lifecycle::LifecycleOwner&, lifecycle::Event e) {
                SetHostLifecycleState(lifecycle::TargetState(e));
            });
        if (m_LifecycleOwner) m_LifecycleOwner->GetLifecycle().AddObserver(m_LifecycleObserver);
    }

    NavGraph& NavController::Graph() const {
        if (!m_Graph) throw std::logic_error("You must call setGraph() before calling getGraph()");
        return *m_Graph;
    }

    NavController::NavigatorBackStack* NavController::StateFor(Navigator* navigator) {
        auto& s = m_NavigatorState[navigator];
        if (!s) s = std::make_unique<NavigatorBackStack>(*this, navigator);
        return s.get();
    }

    void NavController::SetHostLifecycleState(LifecycleState s) {
        if (m_HostLifecycle == s) return;
        m_HostLifecycle = s;
        for (const auto& e : std::vector<EntryPtr>(m_BackQueue.begin(), m_BackQueue.end())) e->SetHostLifecycleState(s);
        if (m_Graph) EmitBackStacks(false);
    }

    EntryPtr NavController::CreateEntry(NavDestination* destination, std::optional<NavArgs> arguments) {
        return std::make_shared<NavBackStackEntry>(destination, std::move(arguments), m_HostLifecycle, std::string());
    }

    EntryPtr NavController::Instantiate(const NavBackStackEntryState& s, NavDestination* destination) {
        return std::make_shared<NavBackStackEntry>(destination, destination->AddInDefaultArgs(s.args), m_HostLifecycle, s.id);
    }

    void NavController::LinkChildToParent(const EntryPtr& child, const EntryPtr& parent) {
        m_ChildToParent[child] = parent;
        ++m_ParentToChildCount[parent];
    }

    EntryPtr NavController::UnlinkChildFromParent(const EntryPtr& child) {
        auto it = m_ChildToParent.find(child);
        if (it == m_ChildToParent.end()) return nullptr;
        EntryPtr parent = it->second;
        m_ChildToParent.erase(it);
        auto c = m_ParentToChildCount.find(parent);
        if (c != m_ParentToChildCount.end() && --c->second == 0) {
            if (auto* s = StateFor(m_Provider.Get(parent->Destination()->NavigatorName()))) s->MarkTransitionComplete(parent);
            m_ParentToChildCount.erase(parent);
        }
        return parent;
    }

    void NavController::NavigateInternal(Navigator* navigator, const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions,
                                         std::function<void(const EntryPtr&)> handler) {
        m_AddHandler = std::move(handler);
        navigator->Navigate(entries, navOptions);
        m_AddHandler = nullptr;
    }

    void NavController::PopBackStackInternal(Navigator* navigator, const EntryPtr& popUpTo, bool saveState, std::function<void(const EntryPtr&)> handler) {
        m_PopHandler = std::move(handler);
        navigator->PopBackStack(popUpTo, saveState);
        m_PopHandler = nullptr;
    }

    void NavController::Push(NavigatorBackStack& state, const EntryPtr& entry) {
        Navigator* destinationNavigator = m_Provider.Get(entry->Destination()->NavigatorName());
        if (destinationNavigator == state.GetNavigator()) {
            // An add outside of navigate() is ignored.
            if (m_AddHandler) {
                auto handler = m_AddHandler;
                handler(entry);
                state.AddInternal(entry);
            }
        } else {
            auto it = m_NavigatorState.find(destinationNavigator);
            if (it == m_NavigatorState.end()) throw std::logic_error("NavigatorBackStack for " + entry->Destination()->NavigatorName() + " should already be created");
            it->second->Push(entry);
        }
    }

    void NavController::Pop(NavigatorBackStack& state, const EntryPtr& popUpTo, bool saveState, const std::function<void()>& super) {
        Navigator* destinationNavigator = m_Provider.Get(popUpTo->Destination()->NavigatorName());
        m_EntrySavedState[popUpTo] = saveState;
        if (destinationNavigator == state.GetNavigator()) {
            if (m_PopHandler) {
                auto handler = m_PopHandler;
                handler(popUpTo);
                super();
            } else {
                PopBackStackFromNavigator(popUpTo, super);
            }
        } else {
            m_NavigatorState[destinationNavigator]->Pop(popUpTo, saveState);
        }
    }

    void NavController::MarkTransitionComplete(NavigatorBackStack& state, const EntryPtr& entry, const std::function<void()>& super) {
        const bool savedState = m_EntrySavedState.contains(entry) && m_EntrySavedState[entry];
        super();
        m_EntrySavedState.erase(entry);
        const bool inQueue = std::find(m_BackQueue.begin(), m_BackQueue.end(), entry) != m_BackQueue.end();
        if (!inQueue) {
            UnlinkChildFromParent(entry);
            if (AtLeast(entry->CurrentState(), LifecycleState::Created)) entry->SetMaxLifecycle(LifecycleState::Destroyed);
            const bool sameIdOnStack = std::any_of(m_BackQueue.begin(), m_BackQueue.end(), [&](const EntryPtr& e) { return e->Id() == entry->Id(); });
            if (!sameIdOnStack && !savedState && onEntryCleared) onEntryCleared(entry->Id());
            UpdateBackStackLifecycle();
            m_VisibleEntries.Set(PopulateVisibleEntries());
        } else if (!state.isNavigating) {
            UpdateBackStackLifecycle();
            EmitBackStacks(true);
        }
    }

    void NavController::PrepareForTransition(const EntryPtr& entry) {
        if (std::find(m_BackQueue.begin(), m_BackQueue.end(), entry) == m_BackQueue.end())
            throw std::logic_error("Cannot transition entry that is not in the back stack");
        entry->SetMaxLifecycle(LifecycleState::Started);
    }

    void NavController::AddOnDestinationChangedListener(OnDestinationChanged listener) {
        m_Listeners.push_back(listener);
        if (!m_BackQueue.empty()) listener(*this, *m_BackQueue.back()->Destination(), m_BackQueue.back()->Arguments());
    }

    bool NavController::PopBackStack() {
        if (m_BackQueue.empty()) return false;
        return PopBackStack(CurrentDestination()->Id(), true);
    }

    bool NavController::PopBackStack(int destinationId, bool inclusive, bool saveState) {
        const bool popped = PopBackStackInternal(destinationId, inclusive, saveState);
        return popped && DispatchOnDestinationChanged();
    }

    bool NavController::PopBackStack(const std::string& route, bool inclusive, bool saveState) {
        const bool popped = PopBackStackInternal(route, inclusive, saveState);
        return popped && DispatchOnDestinationChanged();
    }

    int NavController::DestinationCountOnBackStack() const {
        return int(std::count_if(m_BackQueue.begin(), m_BackQueue.end(), [](const EntryPtr& e) { return !e->Destination()->IsGraph(); }));
    }

    bool NavController::NavigateUp() {
        // No deep link stack to relaunch here: up is a pop.
        if (DestinationCountOnBackStack() == 1) return false;
        return PopBackStack();
    }

    bool NavController::PopBackStackInternal(int destinationId, bool inclusive, bool saveState) {
        if (m_BackQueue.empty()) return false;
        std::vector<Navigator*> popOperations;
        NavDestination* found = nullptr;
        for (auto it = m_BackQueue.rbegin(); it != m_BackQueue.rend(); ++it) {
            NavDestination* destination = (*it)->Destination();
            Navigator* navigator = m_Provider.Get(destination->NavigatorName());
            if (inclusive || destination->Id() != destinationId) popOperations.push_back(navigator);
            if (destination->Id() == destinationId) { found = destination; break; }
        }
        if (!found) return false;   // not on the back stack: ignored
        return ExecutePopOperations(popOperations, found, inclusive, saveState);
    }

    bool NavController::PopBackStackInternal(const std::string& route, bool inclusive, bool saveState) {
        if (m_BackQueue.empty()) return false;
        std::vector<Navigator*> popOperations;
        NavDestination* found = nullptr;
        for (auto it = m_BackQueue.rbegin(); it != m_BackQueue.rend(); ++it) {
            const bool hasRoute = (*it)->Destination()->HasRoute(route, (*it)->Arguments());
            if (inclusive || !hasRoute) popOperations.push_back(m_Provider.Get((*it)->Destination()->NavigatorName()));
            if (hasRoute) { found = (*it)->Destination(); break; }
        }
        if (!found) return false;
        return ExecutePopOperations(popOperations, found, inclusive, saveState);
    }

    bool NavController::ExecutePopOperations(const std::vector<Navigator*>& popOperations, NavDestination* foundDestination, bool inclusive, bool saveState) {
        bool popped = false;
        std::deque<NavBackStackEntryState> savedState;
        for (Navigator* navigator : popOperations) {
            bool receivedPop = false;
            PopBackStackInternal(navigator, m_BackQueue.back(), saveState, [&](const EntryPtr& entry) {
                receivedPop = true;
                popped = true;
                PopEntryFromBackStack(entry, saveState, &savedState);
            });
            if (!receivedPop) break;
        }
        if (saveState) {
            // The destinations of a start destination chain remember the saved stack.
            auto chain = [&](NavDestination* d, const std::optional<std::string>& id) {
                for (; d; d = (d->Parent() && d->Parent()->StartDestinationId() == d->Id()) ? static_cast<NavDestination*>(d->Parent()) : nullptr) {
                    if (m_BackStackMap.contains(d->Id())) break;
                    m_BackStackMap[d->Id()] = id;
                }
            };
            if (!inclusive) chain(foundDestination, savedState.empty() ? std::nullopt : std::optional<std::string>(savedState.front().id));
            if (!savedState.empty()) {
                const auto& first = savedState.front();
                chain(FindDestination(first.destinationId), first.id);
                for (const auto& [k, v] : m_BackStackMap)
                    if (v == first.id) { m_BackStackStates[first.id] = savedState; break; }
            }
        }
        if (onBackEnabledChanged) onBackEnabledChanged();
        return popped;
    }

    void NavController::PopBackStackFromNavigator(const EntryPtr& popUpTo, const std::function<void()>& onComplete) {
        auto it = std::find(m_BackQueue.begin(), m_BackQueue.end(), popUpTo);
        if (it == m_BackQueue.end()) return;   // not on the back stack: ignored
        const std::size_t popIndex = std::size_t(it - m_BackQueue.begin());
        if (popIndex + 1 != m_BackQueue.size()) PopBackStackInternal(m_BackQueue[popIndex + 1]->Destination()->Id(), true, false);
        PopEntryFromBackStack(popUpTo);
        onComplete();
        if (onBackEnabledChanged) onBackEnabledChanged();
        DispatchOnDestinationChanged();
    }

    void NavController::PopEntryFromBackStack(const EntryPtr& popUpTo, bool saveState, std::deque<NavBackStackEntryState>* savedState) {
        const EntryPtr entry = m_BackQueue.back();
        if (entry != popUpTo) throw std::logic_error("Attempted to pop a destination which is not the top of the back stack");
        m_BackQueue.pop_back();
        Navigator* navigator = m_Provider.Get(entry->Destination()->NavigatorName());
        auto st = m_NavigatorState.find(navigator);
        const bool transitioning = (st != m_NavigatorState.end() && Contains(st->second->PeekTransitions(), entry)) || m_ParentToChildCount.contains(entry);
        if (AtLeast(entry->CurrentState(), LifecycleState::Created)) {
            if (saveState) {
                entry->SetMaxLifecycle(LifecycleState::Created);
                if (savedState) savedState->push_front(NavBackStackEntryState{ entry->Id(), entry->Destination()->Id(), entry->Arguments() });
            }
            if (!transitioning) {
                entry->SetMaxLifecycle(LifecycleState::Destroyed);
                UnlinkChildFromParent(entry);
            } else {
                entry->SetMaxLifecycle(LifecycleState::Created);
            }
        }
        if (!saveState && !transitioning && onEntryCleared) onEntryCleared(entry->Id());
    }

    bool NavController::ClearBackStack(const std::string& route) {
        const bool cleared = ClearBackStackInternal(route);
        return cleared && DispatchOnDestinationChanged();
    }

    bool NavController::ClearBackStackInternal(int destinationId) {
        for (auto& [n, s] : m_NavigatorState) s->isNavigating = true;
        NavOptions o;
        o.restoreState = true;
        const bool restored = RestoreStateInternal(destinationId, std::nullopt, o);
        for (auto& [n, s] : m_NavigatorState) s->isNavigating = false;
        return restored && PopBackStackInternal(destinationId, true, false);
    }

    bool NavController::ClearBackStackInternal(const std::string& route) {
        for (auto& [n, s] : m_NavigatorState) s->isNavigating = true;
        const bool restored = RestoreStateInternal(route);
        for (auto& [n, s] : m_NavigatorState) s->isNavigating = false;
        return restored && PopBackStackInternal(route, true, false);
    }

    void NavController::EmitBackStacks(bool currentToo) {
        if (currentToo) m_CurrentBackStack.Set(std::vector<EntryPtr>(m_BackQueue.begin(), m_BackQueue.end()));
        m_VisibleEntries.Set(PopulateVisibleEntries());
    }

    bool NavController::DispatchOnDestinationChanged() {
        // A graph left on top (its children all popped) goes too.
        while (!m_BackQueue.empty() && m_BackQueue.back()->Destination()->IsGraph()) PopEntryFromBackStack(m_BackQueue.back());
        const EntryPtr last = CurrentBackStackEntry();
        if (last) m_ToDispatch.push_back(last);
        ++m_DispatchReentrant;
        UpdateBackStackLifecycle();
        --m_DispatchReentrant;
        if (m_DispatchReentrant == 0) {
            const auto list = std::move(m_ToDispatch);
            m_ToDispatch.clear();
            for (const auto& entry : list) {
                for (const auto& l : std::vector<OnDestinationChanged>(m_Listeners)) l(*this, *entry->Destination(), entry->Arguments());
                m_CurrentEntry.Set(entry);
            }
            EmitBackStacks(true);
        }
        return last != nullptr;
    }

    void NavController::UpdateBackStackLifecycle() {
        const std::vector<EntryPtr> backStack(m_BackQueue.begin(), m_BackQueue.end());
        if (backStack.empty()) return;
        NavDestination* topmost = backStack.back()->Destination();
        std::vector<NavDestination*> nextResumed{ topmost };
        if (topmost->IsSupportingPane()) {
            const bool onlyFloating = topmost->IsFloatingWindow();
            for (auto it = std::next(backStack.rbegin()); it != backStack.rend(); ++it) {
                NavDestination* d = (*it)->Destination();
                if (onlyFloating && !d->IsFloatingWindow() && !d->IsGraph()) break;
                nextResumed.push_back(d);
                if (!d->IsSupportingPane() && !d->IsGraph()) break;
            }
        }
        std::vector<NavDestination*> nextStarted;
        if (nextResumed.back()->IsFloatingWindow()) {
            for (auto it = backStack.rbegin(); it != backStack.rend(); ++it) {
                NavDestination* d = (*it)->Destination();
                nextStarted.push_back(d);
                if (!d->IsFloatingWindow() && !d->IsSupportingPane() && !d->IsGraph()) break;
            }
        }
        std::map<EntryPtr, LifecycleState> upward;
        for (auto it = backStack.rbegin(); it != backStack.rend(); ++it) {
            const EntryPtr& entry = *it;
            const LifecycleState currentMax = entry->MaxLifecycle();
            NavDestination* d = entry->Destination();
            if (!nextResumed.empty() && nextResumed.front()->Id() == d->Id()) {
                if (currentMax != LifecycleState::Resumed) {
                    auto st = m_NavigatorState.find(m_Provider.Get(d->NavigatorName()));
                    const bool transitioning = st != m_NavigatorState.end() && Contains(st->second->PeekTransitions(), entry);
                    auto childCount = m_ParentToChildCount.find(entry);
                    const bool childrenDone = childCount != m_ParentToChildCount.end() && childCount->second == 0;
                    upward[entry] = (!transitioning && !childrenDone) ? LifecycleState::Resumed : LifecycleState::Started;
                }
                if (!nextStarted.empty() && nextStarted.front()->Id() == d->Id()) nextStarted.erase(nextStarted.begin());
                nextResumed.erase(nextResumed.begin());
                if (d->Parent()) nextResumed.push_back(d->Parent());
            } else if (!nextStarted.empty() && d->Id() == nextStarted.front()->Id()) {
                NavDestination* started = nextStarted.front();
                nextStarted.erase(nextStarted.begin());
                if (currentMax == LifecycleState::Resumed) entry->SetMaxLifecycle(LifecycleState::Started);
                else if (currentMax != LifecycleState::Started) upward[entry] = LifecycleState::Started;
                if (started->Parent() && std::find(nextStarted.begin(), nextStarted.end(), started->Parent()) == nextStarted.end())
                    nextStarted.push_back(started->Parent());
            } else {
                entry->SetMaxLifecycle(LifecycleState::Created);
            }
        }
        for (const auto& entry : backStack) {
            auto u = upward.find(entry);
            if (u != upward.end()) entry->SetMaxLifecycle(u->second);
            else entry->UpdateState();
        }
    }

    std::vector<EntryPtr> NavController::PopulateVisibleEntries() const {
        std::vector<EntryPtr> entries;
        for (const auto& [n, state] : m_NavigatorState)
            for (const auto& e : state->PeekTransitions())
                if (!Contains(entries, e) && !AtLeast(e->MaxLifecycle(), LifecycleState::Started)) entries.push_back(e);
        for (const auto& e : m_BackQueue)
            if (!Contains(entries, e) && AtLeast(e->MaxLifecycle(), LifecycleState::Started)) entries.push_back(e);
        std::erase_if(entries, [](const EntryPtr& e) { return e->Destination()->IsGraph(); });
        return entries;
    }

    void NavController::SetGraph(std::shared_ptr<NavGraph> graph, std::optional<NavArgs> startDestinationArgs) {
        if (m_Graph == graph) return;
        if (m_Graph) {
            std::vector<int> savedIds;
            for (const auto& [id, v] : m_BackStackMap) savedIds.push_back(id);
            for (int id : savedIds) ClearBackStackInternal(id);
            PopBackStackInternal(m_Graph->Id(), true);
        }
        m_Graph = std::move(graph);
        OnGraphCreated(startDestinationArgs);
    }

    void NavController::OnGraphCreated(const std::optional<NavArgs>& startDestinationArgs) {
        if (m_BackStackToRestore) {
            for (const auto& state : *m_BackStackToRestore) {
                NavDestination* node = FindDestination(state.destinationId);
                if (!node) throw std::logic_error("Restoring the Navigation back stack failed: destination " + std::to_string(state.destinationId) + " cannot be found");
                EntryPtr entry = Instantiate(state, node);
                auto* backStack = StateFor(m_Provider.Get(node->NavigatorName()));
                m_BackQueue.push_back(entry);
                backStack->AddInternal(entry);
                if (NavGraph* parent = entry->Destination()->Parent()) LinkChildToParent(entry, GetBackStackEntry(parent->Id()));
            }
            if (onBackEnabledChanged) onBackEnabledChanged();
            m_BackStackToRestore.reset();
        }
        for (const auto& [name, navigator] : m_Provider.Navigators())
            if (!navigator->IsAttached()) navigator->OnAttach(*StateFor(navigator.get()));
        if (m_Graph && m_BackQueue.empty()) NavigateTo(m_Graph.get(), startDestinationArgs, std::nullopt);
        else DispatchOnDestinationChanged();
    }

    NavDestination* NavController::FindDestination(int destinationId, NavDestination* matchingDest) const {
        if (!m_Graph) return nullptr;
        if (m_Graph->Id() == destinationId) {
            if (!matchingDest) return m_Graph.get();
            if (m_Graph.get() == matchingDest && !matchingDest->Parent()) return m_Graph.get();
        }
        NavDestination* current = m_BackQueue.empty() ? m_Graph.get() : m_BackQueue.back()->Destination();
        return FindDestinationComprehensive(current, destinationId, false, matchingDest);
    }

    NavDestination* NavController::FindDestinationComprehensive(NavDestination* destination, int destinationId, bool searchChildren,
                                                                NavDestination* matchingDest) const {
        if (destination->Id() == destinationId) {
            if (!matchingDest) return destination;
            if (destination == matchingDest && destination->Parent() == matchingDest->Parent()) return destination;
        }
        NavGraph* graph = destination->IsGraph() ? static_cast<NavGraph*>(destination) : destination->Parent();
        return graph->FindNodeComprehensive(destinationId, graph, searchChildren, matchingDest);
    }

    NavDestination* NavController::FindDestination(const std::string& route) const {
        if (!m_Graph) return nullptr;
        if (m_Graph->Route() == route || m_Graph->MatchRoute(route)) return m_Graph.get();
        return TopGraph().FindNode(route);
    }

    NavGraph& NavController::TopGraph() const {
        NavDestination* current = m_BackQueue.empty() ? m_Graph.get() : m_BackQueue.back()->Destination();
        return current->IsGraph() ? static_cast<NavGraph&>(*current) : *current->Parent();
    }

    void NavController::Navigate(const std::string& route, const std::optional<NavOptions>& navOptions) {
        if (!m_Graph) throw std::logic_error("Cannot navigate to " + route + ". Navigation graph has not been set for NavController.");
        NavGraph& top = TopGraph();
        const auto match = top.MatchRouteComprehensive(route, true, true, &top);
        if (!match) throw std::invalid_argument("Navigation destination that matches route " + route + " cannot be found in the navigation graph");
        NavDestination* node = match->destination;
        std::optional<NavArgs> args = node->AddInDefaultArgs(match->matchingArgs);
        if (!args) args = NavArgs{};
        NavigateTo(node, args, navOptions);
    }

    void NavController::NavigateTo(NavDestination* node, const std::optional<NavArgs>& args, const std::optional<NavOptions>& navOptions) {
        for (auto& [n, s] : m_NavigatorState) s->isNavigating = true;
        bool popped = false, launchSingleTop = false, navigated = false;
        if (navOptions) {
            if (navOptions->popUpToRoute) popped = PopBackStackInternal(*navOptions->popUpToRoute, navOptions->popUpToInclusive, navOptions->popUpToSaveState);
            else if (navOptions->popUpToId != -1) popped = PopBackStackInternal(navOptions->popUpToId, navOptions->popUpToInclusive, navOptions->popUpToSaveState);
        }
        const std::optional<NavArgs> finalArgs = node->AddInDefaultArgs(args);
        if (navOptions && navOptions->restoreState && m_BackStackMap.contains(node->Id())) {
            navigated = RestoreStateInternal(node->Id(), finalArgs, navOptions);
        } else {
            launchSingleTop = navOptions && navOptions->launchSingleTop && LaunchSingleTopInternal(node, args);
            if (!launchSingleTop) {
                EntryPtr entry = CreateEntry(node, finalArgs);
                Navigator* navigator = m_Provider.Get(node->NavigatorName());
                NavigateInternal(navigator, { entry }, navOptions, [&](const EntryPtr& e) {
                    navigated = true;
                    AddEntryToBackStack(node, finalArgs, e);
                });
            }
        }
        if (onBackEnabledChanged) onBackEnabledChanged();
        for (auto& [n, s] : m_NavigatorState) s->isNavigating = false;
        if (popped || navigated || launchSingleTop) DispatchOnDestinationChanged();
        else UpdateBackStackLifecycle();
    }

    bool NavController::LaunchSingleTopInternal(NavDestination* node, const std::optional<NavArgs>& args) {
        const EntryPtr current = CurrentBackStackEntry();
        long nodeIndex = -1;
        for (long i = long(m_BackQueue.size()) - 1; i >= 0; --i)
            if (m_BackQueue[std::size_t(i)]->Destination() == node) { nodeIndex = i; break; }
        if (nodeIndex == -1) return false;
        if (node->IsGraph()) {
            const auto hierarchy = static_cast<NavGraph*>(node)->ChildHierarchy();
            if (long(m_BackQueue.size()) - nodeIndex != long(hierarchy.size())) return false;
            for (std::size_t i = 0; i < hierarchy.size(); ++i)
                if (m_BackQueue[std::size_t(nodeIndex) + i]->Destination()->Id() != hierarchy[i]->Id()) return false;
        } else if (!current || node->Id() != current->Destination()->Id()) {
            return false;
        }
        std::deque<EntryPtr> temp;
        while (long(m_BackQueue.size()) - 1 >= nodeIndex) {
            EntryPtr old = m_BackQueue.back();
            m_BackQueue.pop_back();
            UnlinkChildFromParent(old);
            temp.push_front(std::make_shared<NavBackStackEntry>(old->Destination(), old->Destination()->AddInDefaultArgs(args), m_HostLifecycle, old->Id()));
        }
        for (const auto& e : temp) {
            if (NavGraph* parent = e->Destination()->Parent()) LinkChildToParent(e, GetBackStackEntry(parent->Id()));
            m_BackQueue.push_back(e);
        }
        for (const auto& e : temp) m_Provider.Get(e->Destination()->NavigatorName())->OnLaunchSingleTop(e);
        return true;
    }

    bool NavController::RestoreStateInternal(int id, const std::optional<NavArgs>& args, const std::optional<NavOptions>& navOptions) {
        auto it = m_BackStackMap.find(id);
        if (it == m_BackStackMap.end()) return false;
        const std::optional<std::string> backStackId = it->second;
        std::erase_if(m_BackStackMap, [&](const auto& kv) { return kv.second == backStackId; });
        std::optional<std::deque<NavBackStackEntryState>> state;
        if (backStackId) {
            auto s = m_BackStackStates.find(*backStackId);
            if (s != m_BackStackStates.end()) { state = std::move(s->second); m_BackStackStates.erase(s); }
        }
        const auto entries = InstantiateBackStack(state ? &*state : nullptr);
        return ExecuteRestoreState(entries, args, navOptions);
    }

    bool NavController::RestoreStateInternal(const std::string& route) {
        int id = RouteId(route);
        if (m_BackStackMap.contains(id)) return RestoreStateInternal(id, std::nullopt, std::nullopt);
        NavDestination* matching = FindDestination(route);
        if (!matching) throw std::logic_error("Restore State failed: route " + route + " cannot be found from the current destination");
        id = matching->Id();
        const std::optional<std::string> backStackId = m_BackStackMap.contains(id) ? m_BackStackMap[id] : std::nullopt;
        std::erase_if(m_BackStackMap, [&](const auto& kv) { return kv.second == backStackId; });
        std::optional<std::deque<NavBackStackEntryState>> state;
        if (backStackId) {
            auto s = m_BackStackStates.find(*backStackId);
            if (s != m_BackStackStates.end()) { state = std::move(s->second); m_BackStackStates.erase(s); }
        }
        const auto match = matching->MatchRoute(route);
        const std::optional<NavArgs> firstArgs = state && !state->empty() ? state->front().args : std::nullopt;
        if (!match || !match->HasMatchingArgs(firstArgs)) return false;
        return ExecuteRestoreState(InstantiateBackStack(state ? &*state : nullptr), std::nullopt, std::nullopt);
    }

    bool NavController::ExecuteRestoreState(const std::vector<EntryPtr>& entries, const std::optional<NavArgs>& args, const std::optional<NavOptions>& navOptions) {
        // Entries grouped by consecutive navigator (graphs come back with their children).
        std::vector<std::vector<EntryPtr>> grouped;
        for (const auto& e : entries) {
            if (e->Destination()->IsGraph()) continue;
            if (!grouped.empty() && grouped.back().back()->Destination()->NavigatorName() == e->Destination()->NavigatorName()) grouped.back().push_back(e);
            else grouped.push_back({ e });
        }
        bool navigated = false;
        for (const auto& list : grouped) {
            Navigator* navigator = m_Provider.Get(list.front()->Destination()->NavigatorName());
            std::size_t lastNavigatedIndex = 0;
            NavigateInternal(navigator, list, navOptions, [&](const EntryPtr& entry) {
                navigated = true;
                auto it = std::find(entries.begin(), entries.end(), entry);
                std::vector<EntryPtr> restored;
                if (it != entries.end()) {
                    const std::size_t index = std::size_t(it - entries.begin());
                    restored.assign(entries.begin() + std::ptrdiff_t(lastNavigatedIndex), entries.begin() + std::ptrdiff_t(index) + 1);
                    lastNavigatedIndex = index + 1;
                }
                AddEntryToBackStack(entry->Destination(), args, entry, restored);
            });
        }
        return navigated;
    }

    std::vector<EntryPtr> NavController::InstantiateBackStack(const std::deque<NavBackStackEntryState>* state) {
        std::vector<EntryPtr> out;
        NavDestination* current = m_BackQueue.empty() ? m_Graph.get() : m_BackQueue.back()->Destination();
        if (!state) return out;
        for (const auto& s : *state) {
            NavDestination* node = FindDestinationComprehensive(current, s.destinationId, true);
            if (!node) throw std::logic_error("Restore State failed: destination " + std::to_string(s.destinationId) + " cannot be found from the current destination");
            out.push_back(Instantiate(s, node));
            current = node;
        }
        return out;
    }

    void NavController::AddEntryToBackStack(NavDestination* node, const std::optional<NavArgs>& finalArgs, const EntryPtr& entry,
                                            const std::vector<EntryPtr>& restoredEntries) {
        NavDestination* newDest = entry->Destination();
        // A non-floating destination dismisses the floating ones (dialogs) on top.
        if (!newDest->IsFloatingWindow())
            while (!m_BackQueue.empty() && m_BackQueue.back()->Destination()->IsFloatingWindow() &&
                   PopBackStackInternal(m_BackQueue.back()->Destination()->Id(), true)) {
            }
        auto restoredFor = [&](NavDestination* d) -> EntryPtr {
            for (auto it = restoredEntries.rbegin(); it != restoredEntries.rend(); ++it) if ((*it)->Destination() == d) return *it;
            return nullptr;
        };
        std::deque<EntryPtr> hierarchy;
        NavDestination* destination = newDest;
        if (node->IsGraph()) {
            do {
                NavGraph* parent = destination->Parent();
                if (parent) {
                    EntryPtr e = restoredFor(parent);
                    if (!e) e = CreateEntry(parent, finalArgs);
                    hierarchy.push_front(e);
                    if (!m_BackQueue.empty() && m_BackQueue.back()->Destination() == parent) PopEntryFromBackStack(m_BackQueue.back());
                }
                destination = parent;
            } while (destination && destination != node);
        }
        destination = hierarchy.empty() ? newDest : hierarchy.front()->Destination();
        while (destination && FindDestination(destination->Id(), destination) != destination) {
            NavGraph* parent = destination->Parent();
            if (parent) {
                const std::optional<NavArgs> args = finalArgs && finalArgs->empty() ? std::nullopt : finalArgs;
                EntryPtr e = restoredFor(parent);
                if (!e) e = CreateEntry(parent, parent->AddInDefaultArgs(args));
                hierarchy.push_front(e);
            }
            destination = parent;
        }
        NavDestination* overlapping = hierarchy.empty() ? newDest : hierarchy.front()->Destination();
        while (!m_BackQueue.empty() && m_BackQueue.back()->Destination()->IsGraph() &&
               !static_cast<NavGraph*>(m_BackQueue.back()->Destination())->NodeById(overlapping->Id()))
            PopEntryFromBackStack(m_BackQueue.back());
        const EntryPtr first = !m_BackQueue.empty() ? m_BackQueue.front() : (hierarchy.empty() ? nullptr : hierarchy.front());
        if (!first || first->Destination() != m_Graph.get()) {
            EntryPtr e = restoredFor(m_Graph.get());
            if (!e) e = CreateEntry(m_Graph.get(), m_Graph->AddInDefaultArgs(finalArgs));
            hierarchy.push_front(e);
        }
        for (const auto& e : hierarchy) {
            auto it = m_NavigatorState.find(m_Provider.Get(e->Destination()->NavigatorName()));
            if (it == m_NavigatorState.end()) throw std::logic_error("NavigatorBackStack for " + node->NavigatorName() + " should already be created");
            it->second->AddInternal(e);
        }
        for (const auto& e : hierarchy) m_BackQueue.push_back(e);
        m_BackQueue.push_back(entry);
        std::vector<EntryPtr> linked(hierarchy.begin(), hierarchy.end());
        linked.push_back(entry);
        for (const auto& e : linked)
            if (NavGraph* parent = e->Destination()->Parent()) LinkChildToParent(e, GetBackStackEntry(parent->Id()));
    }

    EntryPtr NavController::GetBackStackEntry(int destinationId) const {
        for (auto it = m_BackQueue.rbegin(); it != m_BackQueue.rend(); ++it)
            if ((*it)->Destination()->Id() == destinationId) return *it;
        throw std::invalid_argument("No destination with ID " + std::to_string(destinationId) + " is on the NavController's back stack.");
    }

    EntryPtr NavController::GetBackStackEntry(const std::string& route) const {
        for (auto it = m_BackQueue.rbegin(); it != m_BackQueue.rend(); ++it)
            if ((*it)->Destination()->HasRoute(route, (*it)->Arguments())) return *it;
        throw std::invalid_argument("No destination with route " + route + " is on the NavController's back stack.");
    }

    EntryPtr NavController::PreviousBackStackEntry() const {
        if (m_BackQueue.size() < 2) return nullptr;
        for (auto it = std::next(m_BackQueue.rbegin()); it != m_BackQueue.rend(); ++it)
            if (!(*it)->Destination()->IsGraph()) return *it;
        return nullptr;
    }

    // --- Saved state ----------------------------------------------------------------------------

    namespace {
        rt::SavedValue SaveValue(const NavValue& v) {
            using L = rt::SavedValue::List;
            return std::visit([](const auto& x) -> rt::SavedValue {
                using X = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<X, std::monostate>) return L{ 0 };
                else if constexpr (std::is_same_v<X, bool>) return L{ 1, x };
                else if constexpr (std::is_same_v<X, std::int32_t>) return L{ 2, x };
                else if constexpr (std::is_same_v<X, std::int64_t>) return L{ 3, x };
                else if constexpr (std::is_same_v<X, float>) return L{ 4, double(x) };
                else return L{ 5, x };
            }, v);
        }
        NavValue RestoreValue(const rt::SavedValue& s) {
            const auto* l = std::get_if<rt::SavedValue::List>(&s.v);
            if (!l || l->empty()) return {};
            const auto kind = std::get_if<std::int64_t>(&(*l)[0].v);
            if (!kind || *kind == 0 || l->size() < 2) return {};
            const rt::SavedValue& v = (*l)[1];
            switch (*kind) {
                case 1: if (auto* b = std::get_if<bool>(&v.v)) return *b; break;
                case 2: if (auto* i = std::get_if<std::int64_t>(&v.v)) return std::int32_t(*i); break;
                case 3: if (auto* i = std::get_if<std::int64_t>(&v.v)) return std::int64_t(*i); break;
                case 4: if (auto* d = std::get_if<double>(&v.v)) return float(*d); break;
                case 5: if (auto* str = std::get_if<std::string>(&v.v)) return *str; break;
            }
            return {};
        }
        rt::SavedValue SaveEntryState(const NavBackStackEntryState& s) {
            rt::SavedValue::List args;
            if (s.args) for (const auto& [k, v] : *s.args) { args.push_back(k); args.push_back(SaveValue(v)); }
            return rt::SavedValue::List{ s.id, s.destinationId, s.args.has_value(), args };
        }
        NavBackStackEntryState RestoreEntryState(const rt::SavedValue& s) {
            NavBackStackEntryState out;
            const auto* l = std::get_if<rt::SavedValue::List>(&s.v);
            if (!l || l->size() < 4) return out;
            if (auto* id = std::get_if<std::string>(&(*l)[0].v)) out.id = *id;
            if (auto* d = std::get_if<std::int64_t>(&(*l)[1].v)) out.destinationId = int(*d);
            const auto* has = std::get_if<bool>(&(*l)[2].v);
            const auto* args = std::get_if<rt::SavedValue::List>(&(*l)[3].v);
            if (has && *has && args) {
                NavArgs a;
                for (std::size_t i = 0; i + 1 < args->size(); i += 2)
                    if (auto* k = std::get_if<std::string>(&(*args)[i].v)) a[*k] = RestoreValue((*args)[i + 1]);
                out.args = std::move(a);
            }
            return out;
        }
    }

    rt::SavedValue NavController::SaveState() const {
        rt::SavedValue::List backStack, mapIds, mapValues, stateIds, states;
        if (!m_BackQueue.empty())
            for (const auto& e : m_BackQueue) backStack.push_back(SaveEntryState({ e->Id(), e->Destination()->Id(), e->Arguments() }));
        else if (m_BackStackToRestore)
            for (const auto& s : *m_BackStackToRestore) backStack.push_back(SaveEntryState(s));
        for (const auto& [destId, id] : m_BackStackMap) { mapIds.push_back(destId); mapValues.push_back(id.value_or("")); }
        for (const auto& [id, stack] : m_BackStackStates) {
            stateIds.push_back(id);
            rt::SavedValue::List list;
            for (const auto& s : stack) list.push_back(SaveEntryState(s));
            states.push_back(list);
        }
        return rt::SavedValue::List{ backStack, mapIds, mapValues, stateIds, states };
    }

    void NavController::RestoreState(const rt::SavedValue& state) {
        const auto* l = std::get_if<rt::SavedValue::List>(&state.v);
        if (!l || l->size() != 5) return;
        auto list = [&](std::size_t i) -> const rt::SavedValue::List& { static const rt::SavedValue::List empty; auto* p = std::get_if<rt::SavedValue::List>(&(*l)[i].v); return p ? *p : empty; };
        if (!list(0).empty()) {
            std::vector<NavBackStackEntryState> stack;
            for (const auto& s : list(0)) stack.push_back(RestoreEntryState(s));
            m_BackStackToRestore = std::move(stack);
        }
        m_BackStackStates.clear();
        const auto& ids = list(1);
        const auto& values = list(2);
        for (std::size_t i = 0; i < ids.size() && i < values.size(); ++i) {
            const auto* id = std::get_if<std::int64_t>(&ids[i].v);
            const auto* v = std::get_if<std::string>(&values[i].v);
            if (id && v) m_BackStackMap[int(*id)] = v->empty() ? std::nullopt : std::optional<std::string>(*v);
        }
        const auto& stateIds = list(3);
        const auto& states = list(4);
        for (std::size_t i = 0; i < stateIds.size() && i < states.size(); ++i) {
            const auto* id = std::get_if<std::string>(&stateIds[i].v);
            const auto* stack = std::get_if<rt::SavedValue::List>(&states[i].v);
            if (!id || !stack) continue;
            std::deque<NavBackStackEntryState> d;
            for (const auto& s : *stack) d.push_back(RestoreEntryState(s));
            m_BackStackStates[*id] = std::move(d);
        }
    }

}
