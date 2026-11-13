#pragma once

// lifecycle-viewmodel-compose: ViewModels in composition.
//   - LocalViewModelStoreOwner: the store content's ViewModels live in (a navigation entry's,
//     else the window's: the host default upstream reads from the View tree).
//   - ViewModelOf<VM>(): viewModel<VM>() (a C++ function cannot share the class's name).
//   - RememberViewModelStoreProvider / RememberViewModelStoreOwner: child stores scoped to a
//     place in composition, kept while it is composed and cleared when it leaves for good.
//   - LocalSavedStateRegistryOwner (savedstate-compose): where SavedStateHandles save.

#include "kiln/lifecycle/LifecycleCompose.h"
#include <kiln/Function.h>
#include "kiln/lifecycle/SavedStateHandle.h"

namespace kiln::lifecycle {

    using ViewModelStoreOwnerPtr = std::shared_ptr<ViewModelStoreOwner>;
    using SavedStateRegistryOwnerPtr = std::shared_ptr<SavedStateRegistryOwner>;

    inline rt::Local<ViewModelStoreOwnerPtr> LocalViewModelStoreOwner{ nullptr };
    inline rt::Local<SavedStateRegistryOwnerPtr> LocalSavedStateRegistryOwner{ nullptr };

    // LocalViewModelStoreOwner.current: provided, else the window's (null outside a window).
    ViewModelStoreOwnerPtr CurrentViewModelStoreOwner();
    SavedStateRegistryOwnerPtr CurrentSavedStateRegistryOwner();
    // The window's owners (ViewModelStoreOwnerHostDefaultKey): the host sets them.
    void SetHostViewModelStoreOwner(ui::Owner& owner, ViewModelStoreOwnerPtr viewModelStoreOwner);
    void SetHostSavedStateRegistryOwner(ui::Owner& owner, SavedStateRegistryOwnerPtr savedStateRegistryOwner);

    namespace detail {
        ViewModelStoreOwner& RequireViewModelStoreOwner(const ViewModelStoreOwnerPtr& owner);
    }

    // viewModel<VM>(viewModelStoreOwner, key, factory, extras): the owner's ViewModel of VM's
    // class (made by `factory`, else the owner's default factory). Throws std::logic_error
    // without an owner.
    template<typename VM>
    std::shared_ptr<VM> ViewModelOf(ViewModelStoreOwnerPtr viewModelStoreOwner = nullptr, std::optional<std::string> key = std::nullopt,
                                    ViewModelProvider::FactoryPtr factory = nullptr, std::optional<MutableCreationExtras> extras = std::nullopt) {
        ViewModelStoreOwnerPtr resolved = viewModelStoreOwner ? viewModelStoreOwner : CurrentViewModelStoreOwner();
        ViewModelStoreOwner& owner = detail::RequireViewModelStoreOwner(resolved);
        auto provider = ViewModelProvider::Create(owner, factory ? factory : DefaultViewModelProviderFactoryOf(&owner),
                                                  extras ? *extras : DefaultViewModelCreationExtrasOf(&owner));
        return key ? provider.template Get<VM>(*key) : provider.template Get<VM>();
    }
    // viewModel<VM>(viewModelStoreOwner, key) { initializer }
    template<typename VM, typename F>
        requires std::is_invocable_v<F&, const CreationExtras&>
    std::shared_ptr<VM> ViewModelOf(F initializer, ViewModelStoreOwnerPtr viewModelStoreOwner = nullptr, std::optional<std::string> key = std::nullopt) {
        auto factory = ViewModelFactory([&](InitializerViewModelFactoryBuilder& b) { Initializer<VM>(b, std::move(initializer)); });
        return ViewModelOf<VM>(std::move(viewModelStoreOwner), std::move(key), factory);
    }

    // SavedStateHandle.saveable(key, saver, init) (@SavedStateHandleSaveableApi): a value
    // restored from the handle, else init(); the value as it is now is saved (a saver turns it
    // into a SavedValue). The property-delegate forms have no C++ counterpart.
    template<typename T>
    T Saveable(SavedStateHandle& handle, const std::string& key, rt::Saver<T> saver, const kiln::Function<T()>& init) {
        std::optional<T> value;
        if (auto stored = handle.Get(key)) {
            const auto m = SavedStateMap(*stored);
            if (auto it = m.find("value"); it != m.end()) value = saver.restore(it->second);
        }
        if (!value) value = init();
        handle.SetSavedStateProvider(key, [saver, v = *value] { return SavedStateOf({ { "value", saver.save(v) } }); });
        return *value;
    }
    template<typename T>
    T Saveable(SavedStateHandle& handle, const std::string& key, const kiln::Function<T()>& init) { return Saveable<T>(handle, key, rt::AutoSaver<T>(), init); }
    // saveable(key, stateSaver, init) for state: the state's latest value is what is saved.
    template<typename T>
    std::shared_ptr<rt::State<T>> SaveableState(SavedStateHandle& handle, const std::string& key, rt::Saver<T> stateSaver,
                                                const kiln::Function<std::shared_ptr<rt::State<T>>()>& init) {
        std::shared_ptr<rt::State<T>> state;
        if (auto stored = handle.Get(key)) {
            const auto m = SavedStateMap(*stored);
            if (auto it = m.find("value"); it != m.end())
                if (auto v = stateSaver.restore(it->second)) state = std::make_shared<rt::State<T>>(std::move(*v));
        }
        if (!state) state = init();
        handle.SetSavedStateProvider(key, [stateSaver, state] { return SavedStateOf({ { "value", stateSaver.save(state->Peek()) } }); });
        return state;
    }

    // rememberViewModelStoreProvider(key, parent, defaultArgs): a provider kept in the parent's
    // store under this call's key; its stores are cleared when the call leaves while the
    // lifecycle is at least CREATED (not when the whole window goes, which clears the parent).
    std::shared_ptr<ViewModelStoreProvider> RememberViewModelStoreProvider(ViewModelStoreOwnerPtr parent = nullptr, SavedState defaultArgs = {},
                                                                           rt::Key key = {});
    std::shared_ptr<ViewModelStoreProvider> RememberViewModelStoreProvider(const std::string& providerKey, ViewModelStoreOwnerPtr parent,
                                                                           SavedState defaultArgs = {}, rt::Key key = {});
    // rememberViewModelStoreOwner(provider, key, savedStateRegistryOwner): the provider's owner
    // for `ownerKey`, held while composed.
    ViewModelStoreOwnerPtr RememberViewModelStoreOwner(const std::shared_ptr<ViewModelStoreProvider>& provider, const std::string& ownerKey,
                                                       SavedStateRegistryOwnerPtr savedStateRegistryOwner = nullptr, rt::Key key = {});
    // rememberViewModelStoreOwner(parent, ...): a store scoped to this call.
    ViewModelStoreOwnerPtr RememberViewModelStoreOwner(ViewModelStoreOwnerPtr parent = nullptr, SavedState defaultArgs = {}, rt::Key key = {});

}
