#pragma once

// lifecycle-viewmodel-savedstate: a ViewModel's saved state (SavedStateHandle), kept through
// process death by the owner's saved-state registry, plus ViewModelStoreProvider, which hands
// out child stores by key (navigation entries, rememberViewModelStoreOwner).
//   - SavedState (a Bundle upstream) is rt::SavedValue; a SavedStateRegistry is
//     rt::SaveableStateRegistry, reached through a SavedStateRegistryOwner.
//   - A ViewModel whose class can be built from a std::shared_ptr<SavedStateHandle> gets one
//     from SavedStateViewModelFactory (the owners' default factory), with the owner's default
//     arguments (a navigation entry's arguments) as its initial values.

#include "kiln/lifecycle/Lifecycle.h"
#include "kiln/lifecycle/ViewModel.h"
#include "kiln/runtime/Saveable.h"

namespace kiln::lifecycle {

    // SavedStateRegistryOwner (androidx.savedstate): a lifecycle owner saving into a registry.
    class SavedStateRegistryOwner : public virtual LifecycleOwner {
    public:
        virtual rt::SaveableStateRegistry& SavedStateRegistry() = 0;
        // Where EnableSavedStateHandles keeps its controller (upstream: a provider registered in
        // the registry under a fixed key).
        std::shared_ptr<void> savedStateHandlesController;
    };

    using SavedState = rt::SavedValue;
    // SavedState as a string map ([key, value] pairs, as rt::MapSaver keeps it) and back.
    SavedState SavedStateOf(const std::map<std::string, SavedState>& values);
    std::map<std::string, SavedState> SavedStateMap(const SavedState& state);

    class SavedStateHandle {
    public:
        SavedStateHandle() = default;
        explicit SavedStateHandle(std::map<std::string, SavedState> initialState);

        bool Contains(const std::string& key) const;
        std::set<std::string> Keys() const;
        std::optional<SavedState> Get(const std::string& key) const;
        void Set(const std::string& key, SavedState value);
        std::optional<SavedState> Remove(const std::string& key);
        // getStateFlow / getMutableStateFlow: the value as observable state; writes to the
        // mutable one are what is saved.
        const rt::State<SavedState>& GetStateFlow(const std::string& key, SavedState initialValue) { return GetMutableStateFlow(key, std::move(initialValue)); }
        rt::State<SavedState>& GetMutableStateFlow(const std::string& key, SavedState initialValue);
        // A provider's value is saved under key in place of a stored one.
        void SetSavedStateProvider(const std::string& key, std::function<SavedState()> provider);
        void ClearSavedStateProvider(const std::string& key);
        // A child handle saved under key.
        SavedStateHandle& GetOrCreateSavedStateHandle(const std::string& key);

        // What is saved: every value (providers asked now) as one SavedState.
        SavedState SaveState() const;
        bool IsEmpty() const { return m_Values.empty(); }
        // createHandle(restored, defaults): restored values over the defaults.
        static std::shared_ptr<SavedStateHandle> CreateHandle(const std::optional<SavedState>& restoredState, const std::optional<SavedState>& defaultState);

    private:
        struct Value {
            SavedState value;
            std::unique_ptr<rt::State<SavedState>> flow;
            std::function<SavedState()> provider;
            std::shared_ptr<SavedStateHandle> child;
            SavedState Current() const;
        };
        std::map<std::string, Value> m_Values;
    };

    // CreationExtras keys a SavedStateHandle is made from.
    inline const CreationExtras::Key<SavedStateRegistryOwner*> SavedStateRegistryOwnerKey{};
    inline const CreationExtras::Key<ViewModelStoreOwner*> ViewModelStoreOwnerKey{};
    inline const CreationExtras::Key<SavedState> DefaultArgsKey{};

    // enableSavedStateHandles: before the owner is STARTED (std::invalid_argument after).
    void EnableSavedStateHandles(SavedStateRegistryOwner& savedStateOwner, ViewModelStoreOwner& viewModelStoreOwner);
    template<typename T>
    void EnableSavedStateHandles(T& owner) { EnableSavedStateHandles(static_cast<SavedStateRegistryOwner&>(owner), static_cast<ViewModelStoreOwner&>(owner)); }
    // CreationExtras.createSavedStateHandle: the handle for extras' ViewModel key.
    std::shared_ptr<SavedStateHandle> CreateSavedStateHandle(const CreationExtras& extras);

    // SavedStateViewModelFactory: a class taking a std::shared_ptr<SavedStateHandle> gets its
    // handle; otherwise its default constructor.
    class SavedStateViewModelFactory : public ViewModelProvider::Factory {
    public:
        std::shared_ptr<ViewModel> Create(const ViewModelClass& modelClass, const CreationExtras& extras) override;
        static const ViewModelProvider::FactoryPtr& Instance();
    };

    // ViewModelStoreOwner(viewModelStore, ...) (ViewModelStoreOwnerFactory): an owner over
    // `viewModelStore` (which must outlive it) with these defaults; with a saved-state owner,
    // its ViewModels can have SavedStateHandles.
    std::shared_ptr<ViewModelStoreOwner> MakeViewModelStoreOwner(ViewModelStore& viewModelStore, SavedState defaultArgs = {},
                                                                 const CreationExtras& defaultCreationExtras = CreationExtras::Empty(),
                                                                 ViewModelProvider::FactoryPtr defaultFactory = SavedStateViewModelFactory::Instance());
    std::shared_ptr<ViewModelStoreOwner> MakeViewModelStoreOwner(ViewModelStore& viewModelStore, SavedStateRegistryOwner& savedStateRegistryOwner,
                                                                 SavedState defaultArgs = {},
                                                                 const CreationExtras& defaultCreationExtras = CreationExtras::Empty(),
                                                                 ViewModelProvider::FactoryPtr defaultFactory = SavedStateViewModelFactory::Instance());

    // ViewModelStoreProvider: child stores by key under a parent store (kept there as a
    // ViewModel, so they live as long as the parent's ViewModels). A key's store goes when it
    // is cleared and no token for it is open.
    class ViewModelStoreProvider {
    public:
        // ProviderMarkerKey: the provider's own token (rememberViewModelStoreProvider).
        static inline const std::string ProviderMarkerKey = "androidx.lifecycle.viewmodel.ViewModelStoreProvider.ProviderMarkerKey";
        // ReferenceToken: closing it releases the key.
        using ReferenceToken = std::shared_ptr<AutoCloseable>;

        explicit ViewModelStoreProvider(ViewModelStore* parentStore, std::string parentKey = {}, SavedState defaultArgs = {},
                                        const CreationExtras& defaultCreationExtras = CreationExtras::Empty(),
                                        ViewModelProvider::FactoryPtr defaultFactory = SavedStateViewModelFactory::Instance());
        // From a parent owner: its store, default extras and factory.
        static std::shared_ptr<ViewModelStoreProvider> FromOwner(ViewModelStoreOwner* parentOwner, std::string parentKey = {}, SavedState defaultArgs = {});

        ReferenceToken AcquireToken(const std::string& key);
        ViewModelStore& GetOrCreate(const std::string& key);
        std::shared_ptr<ViewModelStoreOwner> GetOrCreateOwner(const std::string& key, SavedStateRegistryOwner* savedStateRegistryOwner = nullptr);
        void ClearKey(const std::string& key);
        // Clears every key without an open token (not while the provider's own token is open).
        void ClearAllKeys();

    private:
        struct Entry {
            ViewModelStore store;
            int refCount = 0;
            bool disposable = false;
        };
        class StateHolder;
        StateHolder& Holder();
        ViewModelStore* m_ParentStore;
        std::string m_ParentKey;
        SavedState m_DefaultArgs;
        MutableCreationExtras m_DefaultExtras;
        ViewModelProvider::FactoryPtr m_DefaultFactory;
        std::shared_ptr<StateHolder> m_Holder;
        std::map<std::string, std::shared_ptr<ViewModelStoreOwner>> m_Owners;
    };

}
