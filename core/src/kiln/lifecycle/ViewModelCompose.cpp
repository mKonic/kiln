#include "kiln/lifecycle/ViewModelCompose.h"

#include "kiln/ui/LayoutNode.h"

namespace kiln::lifecycle {

    namespace {
        const char* const kStoreTag = "viewModelStoreOwner";
        const char* const kSavedTag = "savedStateRegistryOwner";

        template<typename T>
        std::shared_ptr<T> Host(const char* tag) {
            auto* c = rt::Composition::Current();
            if (!c) return nullptr;
            ui::Owner* o = c->Read(ui::LocalOwner);
            if (!o) return nullptr;
            return std::static_pointer_cast<T>(o->Service(tag));
        }
    }

    ViewModelStoreOwnerPtr CurrentViewModelStoreOwner() {
        auto* c = rt::Composition::Current();
        if (!c) throw std::logic_error("LocalViewModelStoreOwner is read in composition");
        if (auto p = c->Read(LocalViewModelStoreOwner)) return p;
        return Host<ViewModelStoreOwner>(kStoreTag);
    }
    SavedStateRegistryOwnerPtr CurrentSavedStateRegistryOwner() {
        auto* c = rt::Composition::Current();
        if (!c) throw std::logic_error("LocalSavedStateRegistryOwner is read in composition");
        if (auto p = c->Read(LocalSavedStateRegistryOwner)) return p;
        return Host<SavedStateRegistryOwner>(kSavedTag);
    }
    void SetHostViewModelStoreOwner(ui::Owner& owner, ViewModelStoreOwnerPtr p) { owner.Service(kStoreTag) = std::move(p); }
    void SetHostSavedStateRegistryOwner(ui::Owner& owner, SavedStateRegistryOwnerPtr p) { owner.Service(kSavedTag) = std::move(p); }

    ViewModelStoreOwner& detail::RequireViewModelStoreOwner(const ViewModelStoreOwnerPtr& owner) {
        if (!owner) throw std::logic_error("No ViewModelStoreOwner was provided via LocalViewModelStoreOwner");
        return *owner;
    }

    std::shared_ptr<ViewModelStoreProvider> RememberViewModelStoreProvider(ViewModelStoreOwnerPtr parent, SavedState defaultArgs, rt::Key key) {
        auto* c = rt::Composition::Current();
        const std::string providerKey = "ViewModelStoreProvider:" + std::to_string(c->CompositeKey(key));
        return RememberViewModelStoreProvider(providerKey, std::move(parent), std::move(defaultArgs), key);
    }

    std::shared_ptr<ViewModelStoreProvider> RememberViewModelStoreProvider(const std::string& providerKey, ViewModelStoreOwnerPtr parent,
                                                                           SavedState defaultArgs, rt::Key key) {
        auto* c = rt::Composition::Current();
        if (!parent) parent = CurrentViewModelStoreOwner();
        if (!parent) throw std::logic_error("CompositionLocal LocalViewModelStoreOwner not present");
        auto& provider = c->Remember<std::shared_ptr<ViewModelStoreProvider>>(
            [&] { return ViewModelStoreProvider::FromOwner(parent.get(), providerKey, defaultArgs); },
            rt::Keys(static_cast<const void*>(parent.get()), providerKey), key);
        const LifecycleOwnerPtr lifecycleOwner = CurrentLifecycleOwner();
        c->DisposableEffect(rt::Keys(static_cast<const void*>(provider.get()), static_cast<const void*>(lifecycleOwner.get())),
                            [p = provider, lifecycleOwner] {
            auto token = p->AcquireToken(ViewModelStoreProvider::ProviderMarkerKey);
            return std::function<void()>([p, token, lifecycleOwner] {
                token->Close();
                // Leaving while the window lives: this place is gone for good.
                if (IsAtLeast(lifecycleOwner->GetLifecycle().CurrentState(), State::Created)) p->ClearAllKeys();
            });
        }, key);
        return provider;
    }

    ViewModelStoreOwnerPtr RememberViewModelStoreOwner(const std::shared_ptr<ViewModelStoreProvider>& provider, const std::string& ownerKey,
                                                       SavedStateRegistryOwnerPtr savedStateRegistryOwner, rt::Key key) {
        auto* c = rt::Composition::Current();
        auto& owner = c->Remember<ViewModelStoreOwnerPtr>(
            [&] { return provider->GetOrCreateOwner(ownerKey, savedStateRegistryOwner.get()); },
            rt::Keys(static_cast<const void*>(provider.get()), ownerKey, static_cast<const void*>(savedStateRegistryOwner.get())), key);
        c->DisposableEffect(rt::Keys(static_cast<const void*>(owner.get())), [provider, ownerKey] {
            auto token = provider->AcquireToken(ownerKey);
            return std::function<void()>([token] { token->Close(); });
        }, key);
        return owner;
    }

    ViewModelStoreOwnerPtr RememberViewModelStoreOwner(ViewModelStoreOwnerPtr parent, SavedState defaultArgs, rt::Key key) {
        auto* c = rt::Composition::Current();
        const std::string k = std::to_string(c->CompositeKey(key));
        auto provider = RememberViewModelStoreProvider("ViewModelStoreProvider:" + k, std::move(parent), std::move(defaultArgs), key);
        return RememberViewModelStoreOwner(provider, k, CurrentSavedStateRegistryOwner(), key);
    }

}
