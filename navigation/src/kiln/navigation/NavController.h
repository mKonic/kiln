#pragma once

// navigation-runtime / navigation-common: back stack entries, NavOptions, navigators and their
// state, and NavController (NavController.kt, NavControllerImpl.kt). An entry's lifecycle is
// kept as a state only (kiln has no lifecycle owners): the host counts as RESUMED once a NavHost
// sets it, and visible entries are those at least STARTED.

#include "kiln/lifecycle/SavedStateHandle.h"
#include "kiln/navigation/NavDestination.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/runtime/State.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace kiln::navigation {

    using LifecycleState = lifecycle::State;

    // NavControllerViewModel: the entries' ViewModelStores by entry id, kept in the host's store.
    class NavControllerViewModel final : public lifecycle::ViewModel {
    public:
        lifecycle::ViewModelStore& Get(const std::string& id) {
            auto& s = m_Stores[id];
            if (!s) s = std::make_unique<lifecycle::ViewModelStore>();
            return *s;
        }
        void Clear(const std::string& id) {
            auto it = m_Stores.find(id);
            if (it == m_Stores.end()) return;
            auto store = std::move(it->second);
            m_Stores.erase(it);
            store->Clear();
        }
    protected:
        void OnCleared() override {
            for (auto& [id, s] : m_Stores) s->Clear();
            m_Stores.clear();
        }
    private:
        std::map<std::string, std::unique_ptr<lifecycle::ViewModelStore>> m_Stores;
    };

    // NavBackStackEntry: a destination on the back stack with its arguments. It is the owner of
    // what its content keeps:
    //   - a lifecycle (LocalLifecycleOwner) at the lower of the host's state and the entry's max;
    //   - a ViewModelStore (LocalViewModelStoreOwner) that lives until the entry is popped for
    //     good, its ViewModels' SavedStateHandles starting from the entry's arguments;
    //   - a saved-state registry (LocalSavedStateRegistryOwner) saved with the back stack.
    class NavBackStackEntry : public lifecycle::SavedStateRegistryOwner, public lifecycle::ViewModelStoreOwner,
                              public lifecycle::HasDefaultViewModelProviderFactory {
    public:
        NavBackStackEntry(NavDestination* destination, std::optional<NavArgs> arguments, LifecycleState host, std::string id,
                          std::shared_ptr<NavControllerViewModel> viewModels = nullptr, rt::SaveableStateRegistry::Values savedState = {});
        NavDestination* Destination() const { return m_Destination; }
        void SetDestination(NavDestination* d) { m_Destination = d; }
        const std::optional<NavArgs>& Arguments() const { return m_Args; }
        const std::string& Id() const { return m_Id; }
        LifecycleState MaxLifecycle() const { return m_Max; }
        void SetMaxLifecycle(LifecycleState s) { m_Max = s; UpdateState(); }
        void SetHostLifecycleState(LifecycleState s) { m_Host = s; UpdateState(); }
        // The current state: the lower of the host's and the max (observable).
        LifecycleState CurrentState() const { return m_Lifecycle.CurrentStateFlow().Get(); }
        lifecycle::Lifecycle& GetLifecycle() override { return m_Lifecycle; }
        void UpdateState();
        // Arguments as strings and numbers (convenience for content).
        std::optional<std::string> GetString(const std::string& key) const;
        std::optional<std::int32_t> GetInt(const std::string& key) const;
        // The arguments as a SavedState map (DEFAULT_ARGS for the entry's SavedStateHandles).
        rt::SavedValue ArgumentsAsSavedState() const;

        // Throws std::logic_error before the entry is on the back stack, or when the
        // controller has no ViewModelStore (NavHost gives it LocalViewModelStoreOwner's).
        lifecycle::ViewModelStore& GetViewModelStore() override;
        rt::SaveableStateRegistry& SavedStateRegistry() override { return m_Saved; }
        lifecycle::ViewModelProvider::FactoryPtr DefaultViewModelProviderFactory() override { return lifecycle::SavedStateViewModelFactory::Instance(); }
        lifecycle::MutableCreationExtras DefaultViewModelCreationExtras() override;
        // NavBackStackEntry.savedStateHandle: the entry's own handle (results passed back go here).
        std::shared_ptr<lifecycle::SavedStateHandle> SavedStateHandle();
        // What the entry saves (its registry's values).
        rt::SaveableStateRegistry::Values SaveState() const { return m_Saved.PerformSave(); }

    private:
        NavDestination* m_Destination;
        std::optional<NavArgs> m_Args;
        LifecycleState m_Host;
        LifecycleState m_Max = LifecycleState::Initialized;
        std::string m_Id;
        std::shared_ptr<NavControllerViewModel> m_ViewModels;
        rt::SaveableStateRegistry m_Saved;
        bool m_Attached = false;
        mutable lifecycle::LifecycleRegistry m_Lifecycle{ *this };
    };
    using EntryPtr = std::shared_ptr<NavBackStackEntry>;

    // NavBackStackEntryState: what an entry saves (id, destination id, arguments, its saved state).
    struct NavBackStackEntryState {
        std::string id;
        int destinationId = 0;
        std::optional<NavArgs> args;
        rt::SaveableStateRegistry::Values savedState;
    };

    // NavOptions: launchSingleTop, restoreState and popUpTo (route or id, inclusive, saveState).
    struct NavOptions {
        bool launchSingleTop = false;
        bool restoreState = false;
        int popUpToId = -1;
        std::optional<std::string> popUpToRoute;
        bool popUpToInclusive = false;
        bool popUpToSaveState = false;
    };
    // navOptions { ... } as a builder.
    struct NavOptionsBuilder {
        bool launchSingleTop = false;
        bool restoreState = false;
        void PopUpTo(std::string route, bool inclusive = false, bool saveState = false) {
            m_Options.popUpToRoute = std::move(route); m_Options.popUpToId = -1;
            m_Options.popUpToInclusive = inclusive; m_Options.popUpToSaveState = saveState;
        }
        void PopUpTo(int id, bool inclusive = false, bool saveState = false) {
            m_Options.popUpToRoute.reset(); m_Options.popUpToId = id;
            m_Options.popUpToInclusive = inclusive; m_Options.popUpToSaveState = saveState;
        }
        NavOptions Build() const { NavOptions o = m_Options; o.launchSingleTop = launchSingleTop; o.restoreState = restoreState; return o; }
    private:
        NavOptions m_Options;
    };
    inline NavOptions MakeNavOptions(const std::function<void(NavOptionsBuilder&)>& build) { NavOptionsBuilder b; build(b); return b.Build(); }

    class Navigator;

    // NavigatorState: a navigator's back stack and the entries in transition (observable).
    class NavigatorState {
    public:
        virtual ~NavigatorState() = default;
        const std::vector<EntryPtr>& BackStack() const { return m_BackStack.Get(); }
        const std::vector<EntryPtr>& TransitionsInProgress() const { return m_Transitions.Get(); }
        const std::vector<EntryPtr>& PeekBackStack() const { return m_BackStack.Peek(); }
        const std::vector<EntryPtr>& PeekTransitions() const { return m_Transitions.Peek(); }
        bool isNavigating = false;
        virtual void Push(const EntryPtr& entry);
        virtual void PushWithTransition(const EntryPtr& entry);
        virtual EntryPtr CreateBackStackEntry(NavDestination* destination, std::optional<NavArgs> arguments) = 0;
        virtual void Pop(const EntryPtr& popUpTo, bool saveState);
        virtual void PopWithTransition(const EntryPtr& popUpTo, bool saveState);
        virtual void OnLaunchSingleTop(const EntryPtr& entry);
        virtual void OnLaunchSingleTopWithTransition(const EntryPtr& entry);
        virtual void MarkTransitionComplete(const EntryPtr& entry);
        virtual void PrepareForTransition(const EntryPtr& entry);
    protected:
        void SetBackStack(std::vector<EntryPtr> v) { m_BackStack.Set(std::move(v)); }
        void AddTransition(const EntryPtr& e);
        void RemoveTransition(const EntryPtr& e);
    private:
        rt::State<std::vector<EntryPtr>, rt::NeverEqual> m_BackStack{ std::vector<EntryPtr>{} };
        rt::State<std::vector<EntryPtr>, rt::NeverEqual> m_Transitions{ std::vector<EntryPtr>{} };
    };

    // Navigator: navigates to the destinations it creates.
    class Navigator {
    public:
        explicit Navigator(std::string name) : m_Name(std::move(name)) {}
        virtual ~Navigator() = default;
        const std::string& Name() const { return m_Name; }
        bool IsAttached() const { return m_State != nullptr; }
        virtual void OnAttach(NavigatorState& state) { m_State = &state; }
        virtual NavDestinationPtr CreateDestination() = 0;
        virtual void Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions);
        virtual void OnLaunchSingleTop(const EntryPtr& entry);
        // navigate(destination, args, navOptions): the destination navigated to (null: none).
        virtual NavDestination* NavigateTo(NavDestination* destination, const std::optional<NavArgs>&, const std::optional<NavOptions>&) { return destination; }
        virtual void PopBackStack(const EntryPtr& popUpTo, bool savedState);
        virtual bool PopBackStack() { return true; }
    protected:
        NavigatorState& State() const { return *m_State; }
    private:
        std::string m_Name;
        NavigatorState* m_State = nullptr;
    };

    // NavigatorProvider: navigators by name.
    class NavigatorProvider {
    public:
        Navigator* AddNavigator(std::shared_ptr<Navigator> navigator);
        Navigator* Get(const std::string& name) const;
        template<typename N> N* Get() const {
            for (const auto& [n, nav] : m_Navigators) if (auto* p = dynamic_cast<N*>(nav.get())) return p;
            return nullptr;
        }
        const std::map<std::string, std::shared_ptr<Navigator>>& Navigators() const { return m_Navigators; }
    private:
        std::map<std::string, std::shared_ptr<Navigator>> m_Navigators;
    };

    // NavGraphNavigator: navigating to a graph navigates to its start destination.
    class NavGraphNavigator : public Navigator {
    public:
        explicit NavGraphNavigator(NavigatorProvider& provider) : Navigator("navigation"), m_Provider(provider) {}
        NavDestinationPtr CreateDestination() override { return std::make_shared<NavGraph>(); }
        void Navigate(const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions) override;
        const std::vector<EntryPtr>& BackStack() const { return State().BackStack(); }
    private:
        NavigatorProvider& m_Provider;
    };

    // NavController: the back stack of entries across navigators, navigated by route.
    class NavController {
    public:
        NavController();
        virtual ~NavController();
        NavController(const NavController&) = delete;
        NavController& operator=(const NavController&) = delete;

        NavigatorProvider& Provider() { return m_Provider; }
        NavGraph& Graph() const;
        bool HasGraph() const { return m_Graph != nullptr; }
        // setGraph: a new graph clears the back stack and navigates to its start destination.
        void SetGraph(std::shared_ptr<NavGraph> graph, std::optional<NavArgs> startDestinationArgs = std::nullopt);

        void Navigate(const std::string& route, const std::optional<NavOptions>& navOptions = std::nullopt);
        // navigate(request / deepLink): the destination whose deep link best matches (in the whole
        // graph, from the current one out); none throws std::invalid_argument.
        void Navigate(const NavDeepLinkRequest& request, const std::optional<NavOptions>& navOptions = std::nullopt);
        void Navigate(const NavUri& deepLink, const std::optional<NavOptions>& navOptions = std::nullopt) { Navigate(NavDeepLinkRequest::FromUri(deepLink), navOptions); }
        void Navigate(const std::string& route, const std::function<void(NavOptionsBuilder&)>& builder) { Navigate(route, MakeNavOptions(builder)); }
        bool PopBackStack();
        bool PopBackStack(const std::string& route, bool inclusive, bool saveState = false);
        bool PopBackStack(int destinationId, bool inclusive, bool saveState = false);
        bool NavigateUp();
        bool ClearBackStack(const std::string& route);

        // The current back stack (observable), and the entries a host shows (observable).
        const std::vector<EntryPtr>& CurrentBackStack() const { return m_CurrentBackStack.Get(); }
        const std::vector<EntryPtr>& VisibleEntries() const { return m_VisibleEntries.Get(); }
        EntryPtr CurrentBackStackEntry() const { return m_BackQueue.empty() ? nullptr : m_BackQueue.back(); }
        // The latest dispatched entry (currentBackStackEntryFlow; observable).
        EntryPtr CurrentBackStackEntryObserved() const { return m_CurrentEntry.Get(); }
        EntryPtr PreviousBackStackEntry() const;
        NavDestination* CurrentDestination() const { auto e = CurrentBackStackEntry(); return e ? e->Destination() : nullptr; }
        EntryPtr GetBackStackEntry(const std::string& route) const;
        EntryPtr GetBackStackEntry(int destinationId) const;

        using OnDestinationChanged = std::function<void(NavController&, NavDestination&, const std::optional<NavArgs>&)>;
        void AddOnDestinationChangedListener(OnDestinationChanged listener);

        // The host's lifecycle state.
        void SetHostLifecycleState(LifecycleState s);
        // NavHostController.setLifecycleOwner: the host state follows `owner` (NavHost passes
        // LocalLifecycleOwner); each event moves the entries with it.
        void SetLifecycleOwner(std::shared_ptr<lifecycle::LifecycleOwner> owner);
        // NavHostController.setViewModelStore: where the entries' ViewModelStores live (NavHost
        // passes LocalViewModelStoreOwner's). Changing it with entries on the back stack throws.
        void SetViewModelStore(lifecycle::ViewModelStore& store);
        // Called with an entry's id when its state is cleared for good (the host frees what it
        // kept for it: NavViewModelStoreProvider.clear).
        std::function<void(const std::string& id)> onEntryCleared;
        // Called when whether back can pop changes (OnBackPressedCallback enabled).
        std::function<void()> onBackEnabledChanged;

        // saveState / restoreState (before setGraph).
        rt::SavedValue SaveState() const;
        void RestoreState(const rt::SavedValue& state);

        // internal (navigator states)
        class NavigatorBackStack;
        NavigatorBackStack* StateFor(Navigator* navigator);

    private:
        friend class NavigatorBackStack;
        int DestinationCountOnBackStack() const;
        EntryPtr CreateEntry(NavDestination* destination, std::optional<NavArgs> arguments);
        EntryPtr Instantiate(const NavBackStackEntryState& s, NavDestination* destination);
        void NavigateInternal(Navigator* navigator, const std::vector<EntryPtr>& entries, const std::optional<NavOptions>& navOptions,
                              std::function<void(const EntryPtr&)> handler);
        void PopBackStackInternal(Navigator* navigator, const EntryPtr& popUpTo, bool saveState, std::function<void(const EntryPtr&)> handler);
        void Push(NavigatorBackStack& state, const EntryPtr& entry);
        void Pop(NavigatorBackStack& state, const EntryPtr& popUpTo, bool saveState, const std::function<void()>& super);
        void MarkTransitionComplete(NavigatorBackStack& state, const EntryPtr& entry, const std::function<void()>& super);
        void PrepareForTransition(const EntryPtr& entry);
        bool PopBackStackInternal(int destinationId, bool inclusive, bool saveState = false);
        bool PopBackStackInternal(const std::string& route, bool inclusive, bool saveState);
        bool ExecutePopOperations(const std::vector<Navigator*>& popOperations, NavDestination* foundDestination, bool inclusive, bool saveState);
        void PopBackStackFromNavigator(const EntryPtr& popUpTo, const std::function<void()>& onComplete);
        void PopEntryFromBackStack(const EntryPtr& popUpTo, bool saveState = false, std::deque<NavBackStackEntryState>* savedState = nullptr);
        bool ClearBackStackInternal(int destinationId);
        bool ClearBackStackInternal(const std::string& route);
        bool DispatchOnDestinationChanged();
        void UpdateBackStackLifecycle();
        std::vector<EntryPtr> PopulateVisibleEntries() const;
        void OnGraphCreated(const std::optional<NavArgs>& startDestinationArgs);
        NavDestination* FindDestination(int destinationId, NavDestination* matchingDest = nullptr) const;
        NavDestination* FindDestinationComprehensive(NavDestination* destination, int destinationId, bool searchChildren,
                                                     NavDestination* matchingDest = nullptr) const;
        NavDestination* FindDestination(const std::string& route) const;
        NavGraph& TopGraph() const;
        void NavigateTo(NavDestination* node, const std::optional<NavArgs>& args, const std::optional<NavOptions>& navOptions);
        bool LaunchSingleTopInternal(NavDestination* node, const std::optional<NavArgs>& args);
        bool RestoreStateInternal(int id, const std::optional<NavArgs>& args, const std::optional<NavOptions>& navOptions);
        bool RestoreStateInternal(const std::string& route);
        bool ExecuteRestoreState(const std::vector<EntryPtr>& entries, const std::optional<NavArgs>& args, const std::optional<NavOptions>& navOptions);
        std::vector<EntryPtr> InstantiateBackStack(const std::deque<NavBackStackEntryState>* state);
        void AddEntryToBackStack(NavDestination* node, const std::optional<NavArgs>& finalArgs, const EntryPtr& entry,
                                 const std::vector<EntryPtr>& restoredEntries = {});
        void LinkChildToParent(const EntryPtr& child, const EntryPtr& parent);
        EntryPtr UnlinkChildFromParent(const EntryPtr& child);
        void EmitBackStacks(bool currentToo);

        NavigatorProvider m_Provider;
        std::shared_ptr<NavGraph> m_Graph;
        std::deque<EntryPtr> m_BackQueue;
        rt::State<std::vector<EntryPtr>, rt::NeverEqual> m_CurrentBackStack{ std::vector<EntryPtr>{} };
        rt::State<std::vector<EntryPtr>, rt::NeverEqual> m_VisibleEntries{ std::vector<EntryPtr>{} };
        rt::State<EntryPtr> m_CurrentEntry{ nullptr };
        std::map<EntryPtr, EntryPtr> m_ChildToParent;
        std::map<EntryPtr, int> m_ParentToChildCount;
        std::map<int, std::optional<std::string>> m_BackStackMap;
        std::map<std::string, std::deque<NavBackStackEntryState>> m_BackStackStates;
        std::optional<std::vector<NavBackStackEntryState>> m_BackStackToRestore;
        std::map<Navigator*, std::unique_ptr<NavigatorBackStack>> m_NavigatorState;
        std::map<EntryPtr, bool> m_EntrySavedState;
        std::function<void(const EntryPtr&)> m_AddHandler, m_PopHandler;
        std::vector<OnDestinationChanged> m_Listeners;
        LifecycleState m_HostLifecycle = LifecycleState::Created;
        std::shared_ptr<lifecycle::LifecycleOwner> m_LifecycleOwner;
        std::shared_ptr<lifecycle::LifecycleEventObserver> m_LifecycleObserver;
        std::shared_ptr<NavControllerViewModel> m_ViewModel;
        // An entry's state is gone for good: its ViewModels and the host's saved content state.
        void ClearEntry(const std::string& id);
        int m_DispatchReentrant = 0;
        std::vector<EntryPtr> m_ToDispatch;
    };

    // NavControllerNavigatorState: a navigator's state forwarding to the controller.
    class NavController::NavigatorBackStack final : public NavigatorState {
    public:
        NavigatorBackStack(NavController& c, Navigator* n) : m_Controller(c), m_Navigator(n) {}
        Navigator* GetNavigator() const { return m_Navigator; }
        void Push(const EntryPtr& entry) override { m_Controller.Push(*this, entry); }
        void AddInternal(const EntryPtr& entry) { NavigatorState::Push(entry); }
        EntryPtr CreateBackStackEntry(NavDestination* d, std::optional<NavArgs> a) override { return m_Controller.CreateEntry(d, std::move(a)); }
        void Pop(const EntryPtr& popUpTo, bool saveState) override {
            m_Controller.Pop(*this, popUpTo, saveState, [this, popUpTo, saveState] { NavigatorState::Pop(popUpTo, saveState); });
        }
        void MarkTransitionComplete(const EntryPtr& entry) override {
            m_Controller.MarkTransitionComplete(*this, entry, [this, entry] { NavigatorState::MarkTransitionComplete(entry); });
        }
        void PrepareForTransition(const EntryPtr& entry) override { NavigatorState::PrepareForTransition(entry); m_Controller.PrepareForTransition(entry); }
    private:
        NavController& m_Controller;
        Navigator* m_Navigator;
    };

}
