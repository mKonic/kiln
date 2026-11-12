#pragma once

// lifecycle-viewmodel: state that outlives the content showing it, kept in a ViewModelStore
// (the window's, a navigation entry's) and cleared when that owner goes for good.
//   - A ViewModel is created by a ViewModelProvider through a Factory, once per key in a store
//     (the default key names the class). Factories get CreationExtras (the key, saved-state
//     owners, default arguments).
//   - Kotlin's KClass is a ViewModelClass here: the type, its name and, for a type with a
//     default constructor, a way to make one (what NewInstanceFactory reflects on upstream).
//   - ViewModelScope: tasks cancelled when the ViewModel is cleared (viewModelScope).
// AndroidViewModel / AndroidViewModelFactory (an Application context) and the View-tree
// lookups have no kiln counterpart: kiln apps have no Application object or View tree.

#include "kiln/runtime/CoroutineScope.h"

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace kiln::lifecycle {

    class SavedStateHandle;

    // AutoCloseable: what a ViewModel closes when cleared.
    class AutoCloseable {
    public:
        virtual ~AutoCloseable() = default;
        virtual void Close() = 0;
    };
    std::shared_ptr<AutoCloseable> MakeCloseable(std::function<void()> close);

    class ViewModel {
    public:
        ViewModel() = default;
        explicit ViewModel(std::vector<std::shared_ptr<AutoCloseable>> closeables);
        // ViewModel(viewModelScope, closeables): `viewModelScope` becomes ViewModelScope (tests pass their own).
        ViewModel(std::shared_ptr<class CloseableCoroutineScope> viewModelScope, std::vector<std::shared_ptr<AutoCloseable>> closeables = {});
        virtual ~ViewModel() = default;
        ViewModel(const ViewModel&) = delete;
        ViewModel& operator=(const ViewModel&) = delete;

        // Closed with the ViewModel; replacing a keyed one closes the old. On a cleared
        // ViewModel it is closed at once.
        void AddCloseable(const std::string& key, std::shared_ptr<AutoCloseable> closeable);
        virtual void AddCloseable(std::shared_ptr<AutoCloseable> closeable);
        template<typename T = AutoCloseable>
        std::shared_ptr<T> GetCloseable(const std::string& key) const {
            auto it = m_Keyed.find(key);
            return it == m_Keyed.end() ? nullptr : std::dynamic_pointer_cast<T>(it->second);
        }
        // The store calls this when the ViewModel leaves it: closeables, then OnCleared.
        void Clear();
        bool IsCleared() const { return m_Cleared; }

    protected:
        virtual void OnCleared() {}

    private:
        std::map<std::string, std::shared_ptr<AutoCloseable>> m_Keyed;
        std::vector<std::shared_ptr<AutoCloseable>> m_Closeables;
        bool m_Cleared = false;
    };

    // viewModelScope's scope: a closeable coroutine scope (launches after closing do nothing).
    class CloseableCoroutineScope : public rt::CoroutineScope, public AutoCloseable {
    public:
        void Launch(rt::Task<> task) {
            if (m_Closed) return;
            rt::CoroutineScope::Launch(std::move(task));
        }
        void Close() override {
            m_Closed = true;
            auto cancelled = std::move(jobs);
            jobs.clear();
        }
        bool IsActive() const { return !m_Closed; }
    private:
        bool m_Closed = false;
    };
    // ViewModel.viewModelScope: made on first use, closed when the ViewModel is cleared.
    std::shared_ptr<CloseableCoroutineScope> ViewModelScope(ViewModel& viewModel);

    // ViewModelStore: the ViewModels of one owner by key.
    class ViewModelStore {
    public:
        virtual ~ViewModelStore();
        // Replaces (and clears) a ViewModel already under key.
        void Put(const std::string& key, std::shared_ptr<ViewModel> viewModel);
        std::shared_ptr<ViewModel> Get(const std::string& key) const;
        template<typename T>
        std::shared_ptr<T> GetOrPut(const std::string& key, const std::function<std::shared_ptr<T>()>& make) {
            auto it = m_Map.find(key);
            if (it == m_Map.end()) it = m_Map.emplace(key, make()).first;
            return std::static_pointer_cast<T>(it->second);
        }
        std::set<std::string> Keys() const;
        // Clears every ViewModel and empties the store.
        void Clear();
    private:
        std::map<std::string, std::shared_ptr<ViewModel>> m_Map;
    };

    class ViewModelStoreOwner {
    public:
        virtual ~ViewModelStoreOwner() = default;
        virtual ViewModelStore& GetViewModelStore() = 0;
    };

    // CreationExtras: what a factory gets besides the class, by typed key.
    class CreationExtras {
    public:
        // A key is its own identity (CreationExtras.Key<T>()); declare keys as globals.
        template<typename T>
        class Key {
        public:
            Key() = default;
            Key(const Key&) = delete;
            Key& operator=(const Key&) = delete;
        };
        virtual ~CreationExtras() = default;
        template<typename T>
        const T* Get(const Key<T>& key) const {
            auto it = m_Extras.find(&key);
            return it == m_Extras.end() ? nullptr : std::any_cast<T>(&it->second);
        }
        template<typename T>
        bool Contains(const Key<T>& key) const { return m_Extras.contains(&key); }
        bool operator==(const CreationExtras& o) const;
        // CreationExtras.Empty
        static const CreationExtras& Empty();
    protected:
        friend class MutableCreationExtras;
        std::map<const void*, std::any> m_Extras;
    };
    class MutableCreationExtras : public CreationExtras {
    public:
        MutableCreationExtras() = default;
        explicit MutableCreationExtras(const CreationExtras& initial) { m_Extras = initial.m_Extras; }
        template<typename T>
        void Set(const Key<T>& key, T value) { m_Extras[&key] = std::move(value); }
        // plusAssign
        MutableCreationExtras& operator+=(const CreationExtras& other);
    };
    MutableCreationExtras operator+(const CreationExtras& a, const CreationExtras& b);
    // CreationExtras(initialExtras, builderAction)
    MutableCreationExtras MakeCreationExtras(const CreationExtras& initial = CreationExtras::Empty(),
                                             const std::function<void(MutableCreationExtras&)>& builder = {});

    // KClass for ViewModels.
    struct ViewModelClass {
        std::type_index type;
        std::string name;
        std::function<std::shared_ptr<ViewModel>()> newInstance;   // empty without a default constructor
        bool (*isInstance)(const ViewModel*) = nullptr;            // KClass.isInstance
        // A constructor taking the ViewModel's SavedStateHandle (SavedStateViewModelFactory).
        std::function<std::shared_ptr<ViewModel>(std::shared_ptr<SavedStateHandle>)> newWithSavedStateHandle;
        bool operator==(const ViewModelClass& o) const { return type == o.type; }

        template<typename VM>
        static ViewModelClass Of() {
            static_assert(std::is_base_of_v<ViewModel, VM>, "a ViewModel class");
            ViewModelClass c{ std::type_index(typeid(VM)), typeid(VM).name(), {}, [](const ViewModel* p) { return dynamic_cast<const VM*>(p) != nullptr; } };
            if constexpr (std::is_default_constructible_v<VM>) c.newInstance = [] { return std::make_shared<VM>(); };
            if constexpr (std::is_constructible_v<VM, std::shared_ptr<SavedStateHandle>>)
                c.newWithSavedStateHandle = [](std::shared_ptr<SavedStateHandle> h) { return std::make_shared<VM>(std::move(h)); };
            return c;
        }
    };

    class ViewModelProvider {
    public:
        // Factory.create: the default throws (no way to make the class).
        class Factory {
        public:
            virtual ~Factory() = default;
            virtual std::shared_ptr<ViewModel> Create(const ViewModelClass& modelClass, const CreationExtras& extras);
        };
        using FactoryPtr = std::shared_ptr<Factory>;
        // NewInstanceFactory: the class's default constructor.
        class NewInstanceFactory : public Factory {
        public:
            std::shared_ptr<ViewModel> Create(const ViewModelClass& modelClass, const CreationExtras& extras) override;
            static const FactoryPtr& Instance();
        };

        // The key a ViewModel was created under, set in the extras its factory gets.
        static inline const CreationExtras::Key<std::string> ViewModelKey{};

        static ViewModelProvider Create(ViewModelStoreOwner& owner);
        static ViewModelProvider Create(ViewModelStoreOwner& owner, FactoryPtr factory);
        static ViewModelProvider Create(ViewModelStoreOwner& owner, FactoryPtr factory, const CreationExtras& extras);
        static ViewModelProvider Create(ViewModelStore& store, FactoryPtr factory = NewInstanceFactory::Instance(),
                                        const CreationExtras& extras = CreationExtras::Empty());

        // The ViewModel of VM's class under its default key (or `key`), made if the store has
        // none of that class there.
        template<typename VM>
        std::shared_ptr<VM> Get() { return std::static_pointer_cast<VM>(GetViewModel(ViewModelClass::Of<VM>(), DefaultKey(ViewModelClass::Of<VM>()))); }
        template<typename VM>
        std::shared_ptr<VM> Get(const std::string& key) { return std::static_pointer_cast<VM>(GetViewModel(ViewModelClass::Of<VM>(), key)); }

        std::shared_ptr<ViewModel> GetViewModel(const ViewModelClass& modelClass, const std::string& key);
        static std::string DefaultKey(const ViewModelClass& modelClass);

    private:
        ViewModelProvider(ViewModelStore& store, FactoryPtr factory, MutableCreationExtras extras)
            : m_Store(&store), m_Factory(std::move(factory)), m_Extras(std::move(extras)) {}
        ViewModelStore* m_Store;
        FactoryPtr m_Factory;
        MutableCreationExtras m_Extras;
    };

    // HasDefaultViewModelProviderFactory: an owner's factory and extras for ViewModels made
    // without one.
    class HasDefaultViewModelProviderFactory {
    public:
        virtual ~HasDefaultViewModelProviderFactory() = default;
        virtual ViewModelProvider::FactoryPtr DefaultViewModelProviderFactory() { return ViewModelProvider::NewInstanceFactory::Instance(); }
        virtual MutableCreationExtras DefaultViewModelCreationExtras() { return MutableCreationExtras(); }
    };
    // ViewModelStoreOwnerDefaults: the owner's (or the default) factory and extras.
    ViewModelProvider::FactoryPtr DefaultViewModelProviderFactoryOf(ViewModelStoreOwner* owner);
    MutableCreationExtras DefaultViewModelCreationExtrasOf(ViewModelStoreOwner* owner);

    // viewModelFactory { initializer { ... } }
    class ViewModelInitializer {
    public:
        ViewModelInitializer(ViewModelClass clazz, std::function<std::shared_ptr<ViewModel>(const CreationExtras&)> initializer)
            : clazz(std::move(clazz)), initializer(std::move(initializer)) {}
        ViewModelClass clazz;
        std::function<std::shared_ptr<ViewModel>(const CreationExtras&)> initializer;
    };
    class InitializerViewModelFactoryBuilder {
    public:
        // One initializer per class (std::invalid_argument for a second).
        void AddInitializer(const ViewModelClass& clazz, std::function<std::shared_ptr<ViewModel>(const CreationExtras&)> initializer);
        template<typename VM>
        void AddInitializer(std::function<std::shared_ptr<VM>(const CreationExtras&)> initializer) {
            AddInitializer(ViewModelClass::Of<VM>(), [f = std::move(initializer)](const CreationExtras& e) -> std::shared_ptr<ViewModel> { return f(e); });
        }
        ViewModelProvider::FactoryPtr Build() const;
    private:
        std::vector<ViewModelInitializer> m_Initializers;
    };
    // initializer<VM> { ... }
    template<typename VM, typename F>
    void Initializer(InitializerViewModelFactoryBuilder& builder, F initializer) {
        builder.AddInitializer<VM>(std::function<std::shared_ptr<VM>(const CreationExtras&)>(std::move(initializer)));
    }
    ViewModelProvider::FactoryPtr ViewModelFactory(const std::function<void(InitializerViewModelFactoryBuilder&)>& builder);

    // ViewModelLazy: the ViewModel fetched on first Value().
    template<typename VM>
    class ViewModelLazy {
    public:
        ViewModelLazy(std::function<ViewModelStore&()> storeProducer, std::function<ViewModelProvider::FactoryPtr()> factoryProducer,
                      std::function<MutableCreationExtras()> extrasProducer = [] { return MutableCreationExtras(); })
            : m_Store(std::move(storeProducer)), m_Factory(std::move(factoryProducer)), m_Extras(std::move(extrasProducer)) {}
        const std::shared_ptr<VM>& Value() {
            if (!m_Cached) m_Cached = ViewModelProvider::Create(m_Store(), m_Factory(), m_Extras()).template Get<VM>();
            return m_Cached;
        }
        bool IsInitialized() const { return m_Cached != nullptr; }
    private:
        std::function<ViewModelStore&()> m_Store;
        std::function<ViewModelProvider::FactoryPtr()> m_Factory;
        std::function<MutableCreationExtras()> m_Extras;
        std::shared_ptr<VM> m_Cached;
    };

}
