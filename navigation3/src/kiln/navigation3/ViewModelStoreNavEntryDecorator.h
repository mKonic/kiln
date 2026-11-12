#pragma once

// ViewModelStoreNavEntryDecorator (lifecycle-viewmodel-navigation3): each entry gets its own
// ViewModelStore (LocalViewModelStoreOwner within it), kept while the entry is on the back
// stack and cleared when it is popped.

#include "kiln/lifecycle/ViewModelCompose.h"
#include "kiln/navigation3/NavEntry.h"

namespace kiln::navigation3 {

    template<typename T>
    class ViewModelStoreNavEntryDecorator : public NavEntryDecorator<T> {
    public:
        explicit ViewModelStoreNavEntryDecorator(std::shared_ptr<lifecycle::ViewModelStoreProvider> viewModelStoreProvider)
            : NavEntryDecorator<T>(
                  [viewModelStoreProvider](const std::string& contentKey) { viewModelStoreProvider->ClearKey(contentKey); },
                  [viewModelStoreProvider](const NavEntry<T>& entry) {
                      const auto owner = lifecycle::RememberViewModelStoreOwner(viewModelStoreProvider, entry.ContentKey(),
                                                                                lifecycle::CurrentSavedStateRegistryOwner());
                      rt::Composition::Current()->Provide(lifecycle::LocalViewModelStoreOwner, owner, [entry] { entry.Content(); });
                  }) {}
    };

    // rememberViewModelStoreNavEntryDecorator(viewModelStoreProvider)
    template<typename T>
    ViewModelStoreNavEntryDecorator<T> RememberViewModelStoreNavEntryDecorator(std::shared_ptr<lifecycle::ViewModelStoreProvider> viewModelStoreProvider,
                                                                              rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& decorator = c.Remember<std::shared_ptr<ViewModelStoreNavEntryDecorator<T>>>(
            [&] { return std::make_shared<ViewModelStoreNavEntryDecorator<T>>(viewModelStoreProvider); },
            rt::Keys(static_cast<const void*>(viewModelStoreProvider.get())), key);
        return *decorator;
    }
    // rememberViewModelStoreNavEntryDecorator(viewModelStoreOwner = LocalViewModelStoreOwner)
    template<typename T>
    ViewModelStoreNavEntryDecorator<T> RememberViewModelStoreNavEntryDecorator(lifecycle::ViewModelStoreOwnerPtr viewModelStoreOwner = nullptr,
                                                                              rt::Key key = {}) {
        if (!viewModelStoreOwner) viewModelStoreOwner = lifecycle::CurrentViewModelStoreOwner();
        if (!viewModelStoreOwner) throw std::logic_error("No ViewModelStoreOwner was provided via LocalViewModelStoreOwner");
        auto provider = lifecycle::RememberViewModelStoreProvider(viewModelStoreOwner, {}, key);
        return RememberViewModelStoreNavEntryDecorator<T>(provider, key);
    }

}
