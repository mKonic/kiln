#include "kiln/lifecycle/SavedStateHandle.h"
#include <kiln/Function.h>

#include <unordered_map>

namespace kiln::lifecycle {

    // ---- SavedState as a map ---------------------------------------------------------------

    SavedState SavedStateOf(const std::map<std::string, SavedState>& values) {
        SavedState::List pairs;
        for (const auto& [k, v] : values) pairs.emplace_back(SavedState::List{ SavedState(k), v });
        return SavedState(std::move(pairs));
    }
    std::map<std::string, SavedState> SavedStateMap(const SavedState& state) {
        std::map<std::string, SavedState> m;
        const auto* l = std::get_if<SavedState::List>(&state.v);
        if (!l) return m;
        for (const auto& item : *l) {
            const auto* pair = std::get_if<SavedState::List>(&item.v);
            if (!pair || pair->size() != 2) continue;
            if (const auto* k = std::get_if<std::string>(&(*pair)[0].v)) m[*k] = (*pair)[1];
        }
        return m;
    }

    // ---- SavedStateHandle ------------------------------------------------------------------

    SavedState SavedStateHandle::Value::Current() const {
        if (provider) return provider();
        if (child) return child->SaveState();
        if (flow) return flow->Peek();
        return value;
    }

    SavedStateHandle::SavedStateHandle(std::map<std::string, SavedState> initialState) {
        for (auto& [k, v] : initialState) m_Values[k].value = std::move(v);
    }

    bool SavedStateHandle::Contains(const std::string& key) const {
        auto it = m_Values.find(key);
        if (it == m_Values.end()) return false;
        return !it->second.provider || !it->second.value.IsNull();
    }
    std::set<std::string> SavedStateHandle::Keys() const {
        std::set<std::string> keys;
        for (const auto& [k, v] : m_Values) keys.insert(k);
        return keys;
    }
    std::optional<SavedState> SavedStateHandle::Get(const std::string& key) const {
        auto it = m_Values.find(key);
        if (it == m_Values.end()) return std::nullopt;
        const Value& v = it->second;
        if (v.flow) return v.flow->Peek();
        if (v.child) return v.child->SaveState();
        return v.value;   // a provider's slot keeps the last value set
    }
    void SavedStateHandle::Set(const std::string& key, SavedState value) {
        Value& v = m_Values[key];
        if (v.flow) v.flow->Set(std::move(value));
        else {
            v.child.reset();
            v.value = std::move(value);
        }
    }
    std::optional<SavedState> SavedStateHandle::Remove(const std::string& key) {
        auto latest = Get(key);
        m_Values.erase(key);
        return latest;
    }
    rt::State<SavedState>& SavedStateHandle::GetMutableStateFlow(const std::string& key, SavedState initialValue) {
        auto it = m_Values.find(key);
        if (it != m_Values.end() && it->second.flow) return *it->second.flow;
        SavedState start = std::move(initialValue);
        if (it != m_Values.end()) {
            if (auto current = Get(key)) start = *current;
        }
        Value& v = m_Values[key];
        v.provider = nullptr;
        v.child.reset();
        v.flow = std::make_unique<rt::State<SavedState>>(std::move(start));
        return *v.flow;
    }
    void SavedStateHandle::SetSavedStateProvider(const std::string& key, kiln::Function<SavedState()> provider) {
        auto current = Get(key);
        Value& v = m_Values[key];
        v.flow.reset();
        v.child.reset();
        v.value = current.value_or(SavedState());
        v.provider = std::move(provider);
    }
    void SavedStateHandle::ClearSavedStateProvider(const std::string& key) {
        auto it = m_Values.find(key);
        if (it == m_Values.end() || !it->second.provider) return;
        SavedState last = it->second.value;
        m_Values.erase(it);
        if (!last.IsNull()) Set(key, std::move(last));
    }
    SavedStateHandle& SavedStateHandle::GetOrCreateSavedStateHandle(const std::string& key) {
        Value& v = m_Values[key];
        if (!v.child) {
            // A stored map becomes the child's values.
            v.child = std::make_shared<SavedStateHandle>(SavedStateMap(v.flow ? v.flow->Peek() : v.value));
            v.flow.reset();
            v.provider = nullptr;
            v.value = {};
        }
        return *v.child;
    }
    SavedState SavedStateHandle::SaveState() const {
        std::map<std::string, SavedState> values;
        for (const auto& [k, v] : m_Values) values[k] = v.Current();
        return SavedStateOf(values);
    }
    std::shared_ptr<SavedStateHandle> SavedStateHandle::CreateHandle(const std::optional<SavedState>& restoredState,
                                                                     const std::optional<SavedState>& defaultState) {
        std::map<std::string, SavedState> state;
        if (defaultState) state = SavedStateMap(*defaultState);
        if (restoredState)
            for (auto& [k, v] : SavedStateMap(*restoredState)) state[k] = v;
        return std::make_shared<SavedStateHandle>(std::move(state));
    }

    // ---- SavedStateHandleController --------------------------------------------------------

    namespace {
        const char* const kSavedStateKey = "androidx.lifecycle.internal.SavedStateHandlesProvider";

        // The handles, kept as a ViewModel so they go with the owner's store.
        class HandlesHolder final : public ViewModel {
        public:
            std::map<std::string, std::shared_ptr<SavedStateHandle>> handles;
        };

        class Controller final : public std::enable_shared_from_this<Controller> {
        public:
            Controller(SavedStateRegistryOwner& owner, ViewModelStoreOwner& viewModelStoreOwner) : m_Owner(&owner), m_ViewModelOwner(&viewModelStoreOwner) {}

            SavedState SaveState() {
                std::map<std::string, SavedState> out = m_Restored;
                for (const auto& [key, handle] : Holder().handles) {
                    SavedState saved = handle->SaveState();
                    if (!SavedStateMap(saved).empty()) out[key] = std::move(saved);
                }
                m_IsRestored = false;
                return SavedStateOf(out);
            }
            void PerformRestore() {
                if (m_IsRestored) return;
                if (auto restored = m_Owner->SavedStateRegistry().ConsumeRestored(kSavedStateKey))
                    for (auto& [k, v] : SavedStateMap(*restored)) m_Restored[k] = v;
                m_IsRestored = true;
                Holder();
            }
            std::optional<SavedState> ConsumeRestoredStateForKey(const std::string& key) {
                PerformRestore();
                auto it = m_Restored.find(key);
                if (it == m_Restored.end()) return std::nullopt;
                SavedState result = std::move(it->second);
                m_Restored.erase(it);
                return result;
            }
            std::shared_ptr<SavedStateHandle> GetOrCreateHandle(const std::string& key, const std::optional<SavedState>& defaultArgs) {
                auto& handles = Holder().handles;
                auto it = handles.find(key);
                if (it == handles.end()) it = handles.emplace(key, SavedStateHandle::CreateHandle(ConsumeRestoredStateForKey(key), defaultArgs)).first;
                return it->second;
            }

            // Registry -> controller (upstream: the provider registered under the key).
            static std::shared_ptr<Controller> GetOrNull(SavedStateRegistryOwner& owner) {
                auto& table = Table();
                auto it = table.find(&owner.SavedStateRegistry());
                if (it == table.end()) return nullptr;
                if (it->second.alive.expired()) { table.erase(it); return nullptr; }
                return it->second.controller.lock();
            }
            static std::shared_ptr<Controller> GetOrCreate(SavedStateRegistryOwner& owner, ViewModelStoreOwner& viewModelStoreOwner) {
                if (auto c = GetOrNull(owner)) return c;
                auto controller = std::make_shared<Controller>(owner, viewModelStoreOwner);
                rt::SaveableStateRegistry& registry = owner.SavedStateRegistry();
                // The registry keeps the controller, as it keeps a SavedStateProvider.
                registry.RegisterProvider(kSavedStateKey, [controller] { return controller->SaveState(); });
                Table()[&registry] = Registration{ controller, registry.AliveToken() };
                if (IsAtLeast(owner.GetLifecycle().CurrentState(), State::Created)) {
                    controller->PerformRestore();
                } else {
                    std::weak_ptr<Controller> weak = controller;
                    // The lifecycle owns the observer; the observer reaches itself weakly (a
                    // strong capture of its own pointer would keep it alive forever).
                    auto self = std::make_shared<std::weak_ptr<LifecycleEventObserver>>();
                    *self = AddObserver(owner.GetLifecycle(), [weak, self](LifecycleOwner& source, Event event) {
                        if (event != Event::OnCreate) throw std::logic_error(std::string("Next event must be ON_CREATE, it was ") + ToString(event));
                        if (auto o = self->lock()) source.GetLifecycle().RemoveObserver(o);
                        if (auto c = weak.lock()) c->PerformRestore();
                    });
                }
                return controller;
            }

        private:
            HandlesHolder& Holder() {
                if (!m_Holder || m_Holder->IsCleared()) {
                    auto factory = ViewModelFactory([](InitializerViewModelFactoryBuilder& b) {
                        Initializer<HandlesHolder>(b, [](const CreationExtras&) { return std::make_shared<HandlesHolder>(); });
                    });
                    m_Holder = ViewModelProvider::Create(*m_ViewModelOwner, factory).Get<HandlesHolder>();
                }
                return *m_Holder;
            }
            struct Registration {
                std::weak_ptr<Controller> controller;
                std::weak_ptr<const char> alive;
            };
            static std::unordered_map<const rt::SaveableStateRegistry*, Registration>& Table() {
                static std::unordered_map<const rt::SaveableStateRegistry*, Registration> table;
                return table;
            }
            SavedStateRegistryOwner* m_Owner;
            ViewModelStoreOwner* m_ViewModelOwner;
            std::map<std::string, SavedState> m_Restored;
            bool m_IsRestored = false;
            std::shared_ptr<HandlesHolder> m_Holder;
        };
    }

    void EnableSavedStateHandles(SavedStateRegistryOwner& savedStateOwner, ViewModelStoreOwner& viewModelStoreOwner) {
        const State s = savedStateOwner.GetLifecycle().CurrentState();
        if (s != State::Initialized && s != State::Created)
            throw std::invalid_argument(std::string("Failed to enable `SavedStateHandle`. The `Lifecycle.State` must be `INITIALIZED` or `CREATED`, but was `") +
                                        ToString(s) + "`.");
        Controller::GetOrCreate(savedStateOwner, viewModelStoreOwner);
    }

    std::shared_ptr<SavedStateHandle> CreateSavedStateHandle(const CreationExtras& extras) {
        auto* const* owner = extras.Get(SavedStateRegistryOwnerKey);
        if (!owner || !*owner) throw std::invalid_argument("CreationExtras must have a value by `SAVED_STATE_REGISTRY_OWNER_KEY`");
        const std::string* key = extras.Get(ViewModelProvider::ViewModelKey);
        if (!key) throw std::invalid_argument("CreationExtras must have a value by `VIEW_MODEL_KEY`");
        auto* const* vmOwner = extras.Get(ViewModelStoreOwnerKey);
        if (!vmOwner || !*vmOwner) throw std::invalid_argument("CreationExtras must have a value by `VIEW_MODEL_STORE_OWNER_KEY`");
        auto controller = Controller::GetOrNull(**owner);
        if (!controller) throw std::logic_error("enableSavedStateHandles() wasn't called prior to createSavedStateHandle() call");
        const SavedState* args = extras.Get(DefaultArgsKey);
        return controller->GetOrCreateHandle(*key, args ? std::optional<SavedState>(*args) : std::nullopt);
    }

    // ---- SavedStateViewModelFactory --------------------------------------------------------

    std::shared_ptr<ViewModel> SavedStateViewModelFactory::Create(const ViewModelClass& modelClass, const CreationExtras& extras) {
        if (modelClass.newWithSavedStateHandle && extras.Contains(SavedStateRegistryOwnerKey) && extras.Contains(ViewModelStoreOwnerKey))
            return modelClass.newWithSavedStateHandle(CreateSavedStateHandle(extras));
        if (modelClass.newWithSavedStateHandle && !modelClass.newInstance)
            throw std::invalid_argument("SAVED_STATE_REGISTRY_OWNER_KEY and VIEW_MODEL_STORE_OWNER_KEY must be provided in the creation extras to "
                                        "successfully create a ViewModel.");
        return ViewModelProvider::NewInstanceFactory::Instance()->Create(modelClass, extras);
    }
    const ViewModelProvider::FactoryPtr& SavedStateViewModelFactory::Instance() {
        static const ViewModelProvider::FactoryPtr instance = std::make_shared<SavedStateViewModelFactory>();
        return instance;
    }

    // ---- ViewModelStoreOwner factory -------------------------------------------------------

    namespace {
        SavedState MergeArgs(const CreationExtras& extras, const SavedState& defaultArgs) {
            std::map<std::string, SavedState> merged;
            if (const SavedState* existing = extras.Get(DefaultArgsKey)) merged = SavedStateMap(*existing);
            for (auto& [k, v] : SavedStateMap(defaultArgs)) merged[k] = v;
            return SavedStateOf(merged);
        }

        class SimpleStoreOwner : public ViewModelStoreOwner, public HasDefaultViewModelProviderFactory {
        public:
            SimpleStoreOwner(ViewModelStore& store, SavedState args, const CreationExtras& extras, ViewModelProvider::FactoryPtr factory)
                : m_Store(&store), m_Args(std::move(args)), m_Extras(extras), m_Factory(std::move(factory)) {}
            ViewModelStore& GetViewModelStore() override { return *m_Store; }
            ViewModelProvider::FactoryPtr DefaultViewModelProviderFactory() override { return m_Factory; }
            MutableCreationExtras DefaultViewModelCreationExtras() override {
                MutableCreationExtras extras(m_Extras);
                extras.Set(DefaultArgsKey, MergeArgs(extras, m_Args));
                extras.Set(ViewModelStoreOwnerKey, static_cast<ViewModelStoreOwner*>(this));
                return extras;
            }
        protected:
            ViewModelStore* m_Store;
            SavedState m_Args;
            MutableCreationExtras m_Extras;
            ViewModelProvider::FactoryPtr m_Factory;
        };

        class SavedStoreOwner final : public SimpleStoreOwner, public SavedStateRegistryOwner {
        public:
            SavedStoreOwner(ViewModelStore& store, SavedStateRegistryOwner& saved, SavedState args, const CreationExtras& extras,
                            ViewModelProvider::FactoryPtr factory)
                : SimpleStoreOwner(store, std::move(args), extras, std::move(factory)), m_Saved(&saved) {}
            rt::SaveableStateRegistry& SavedStateRegistry() override { return m_Saved->SavedStateRegistry(); }
            Lifecycle& GetLifecycle() override { return m_Saved->GetLifecycle(); }
            MutableCreationExtras DefaultViewModelCreationExtras() override {
                MutableCreationExtras extras = SimpleStoreOwner::DefaultViewModelCreationExtras();
                extras.Set(SavedStateRegistryOwnerKey, static_cast<SavedStateRegistryOwner*>(this));
                return extras;
            }
        private:
            SavedStateRegistryOwner* m_Saved;
        };
    }

    std::shared_ptr<ViewModelStoreOwner> MakeViewModelStoreOwner(ViewModelStore& viewModelStore, SavedState defaultArgs,
                                                                 const CreationExtras& defaultCreationExtras, ViewModelProvider::FactoryPtr defaultFactory) {
        return std::make_shared<SimpleStoreOwner>(viewModelStore, std::move(defaultArgs), defaultCreationExtras, std::move(defaultFactory));
    }
    std::shared_ptr<ViewModelStoreOwner> MakeViewModelStoreOwner(ViewModelStore& viewModelStore, SavedStateRegistryOwner& savedStateRegistryOwner,
                                                                 SavedState defaultArgs, const CreationExtras& defaultCreationExtras,
                                                                 ViewModelProvider::FactoryPtr defaultFactory) {
        auto owner = std::make_shared<SavedStoreOwner>(viewModelStore, savedStateRegistryOwner, std::move(defaultArgs), defaultCreationExtras,
                                                       std::move(defaultFactory));
        if (!Controller::GetOrNull(*owner)) EnableSavedStateHandles(*owner, *owner);
        return owner;
    }

    // ---- ViewModelStoreProvider ------------------------------------------------------------

    class ViewModelStoreProvider::StateHolder final : public ViewModel {
    public:
        std::map<std::string, std::shared_ptr<Entry>> entries;
        Entry& GetOrCreate(const std::string& key) {
            auto& e = entries[key];
            if (!e) e = std::make_shared<Entry>();
            return *e;
        }
        void Remove(const std::string& key) {
            auto it = entries.find(key);
            if (it == entries.end()) return;
            auto entry = it->second;
            entries.erase(it);
            entry->store.Clear();
        }
        void ClearKey(const std::string& key) {
            auto it = entries.find(key);
            if (it == entries.end()) return;
            it->second->disposable = true;
            if (it->second->refCount <= 0) Remove(key);
        }
    protected:
        void OnCleared() override {
            auto snapshot = entries;
            for (auto& [key, entry] : snapshot) {
                entry->disposable = true;
                if (entry->refCount <= 0) Remove(key);
            }
        }
    };

    ViewModelStoreProvider::ViewModelStoreProvider(ViewModelStore* parentStore, std::string parentKey, SavedState defaultArgs,
                                                   const CreationExtras& defaultCreationExtras, ViewModelProvider::FactoryPtr defaultFactory)
        : m_ParentStore(parentStore), m_ParentKey(std::move(parentKey)), m_DefaultArgs(std::move(defaultArgs)),
          m_DefaultExtras(defaultCreationExtras), m_DefaultFactory(std::move(defaultFactory)) {}

    std::shared_ptr<ViewModelStoreProvider> ViewModelStoreProvider::FromOwner(ViewModelStoreOwner* parentOwner, std::string parentKey, SavedState defaultArgs) {
        return std::make_shared<ViewModelStoreProvider>(parentOwner ? &parentOwner->GetViewModelStore() : nullptr, std::move(parentKey),
                                                        std::move(defaultArgs), DefaultViewModelCreationExtrasOf(parentOwner),
                                                        DefaultViewModelProviderFactoryOf(parentOwner));
    }

    ViewModelStoreProvider::StateHolder& ViewModelStoreProvider::Holder() {
        if (!m_Holder) {
            if (m_ParentStore) m_Holder = m_ParentStore->GetOrPut<StateHolder>(m_ParentKey, [] { return std::make_shared<StateHolder>(); });
            else m_Holder = std::make_shared<StateHolder>();
        }
        return *m_Holder;
    }

    ViewModelStoreProvider::ReferenceToken ViewModelStoreProvider::AcquireToken(const std::string& key) {
        Holder().GetOrCreate(key).refCount++;
        std::shared_ptr<Entry> entry = m_Holder->entries[key];
        return MakeCloseable([this, key, weak = std::weak_ptr<Entry>(entry)] {
            auto e = weak.lock();
            if (!e) return;
            e->refCount--;
            if (e->disposable && e->refCount <= 0) {
                m_Owners.erase(key);
                if (m_Holder) m_Holder->Remove(key);
            }
        });
    }
    ViewModelStore& ViewModelStoreProvider::GetOrCreate(const std::string& key) { return Holder().GetOrCreate(key).store; }

    std::shared_ptr<ViewModelStoreOwner> ViewModelStoreProvider::GetOrCreateOwner(const std::string& key, SavedStateRegistryOwner* savedStateRegistryOwner) {
        auto& owner = m_Owners[key];
        if (!owner) {
            ViewModelStore& store = GetOrCreate(key);
            owner = savedStateRegistryOwner ? MakeViewModelStoreOwner(store, *savedStateRegistryOwner, m_DefaultArgs, m_DefaultExtras, m_DefaultFactory)
                                            : MakeViewModelStoreOwner(store, m_DefaultArgs, m_DefaultExtras, m_DefaultFactory);
        }
        return owner;
    }
    void ViewModelStoreProvider::ClearKey(const std::string& key) {
        m_Owners.erase(key);
        Holder().ClearKey(key);
    }
    void ViewModelStoreProvider::ClearAllKeys() {
        auto& entries = Holder().entries;
        if (auto it = entries.find(ProviderMarkerKey); it != entries.end() && it->second->refCount > 0) return;
        m_Owners.clear();
        // stateHolder.onCleared(): every key without a token goes.
        auto snapshot = entries;
        for (auto& [key, entry] : snapshot) {
            entry->disposable = true;
            if (entry->refCount <= 0) m_Holder->Remove(key);
        }
    }

}
