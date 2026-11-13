#include "kiln/lifecycle/ViewModel.h"
#include <kiln/Function.h>

namespace kiln::lifecycle {

    namespace {
        const char* const kScopeKey = "androidx.lifecycle.viewmodel.internal.ViewModelCoroutineScope.JOB_KEY";
        const char* const kDefaultKey = "androidx.lifecycle.ViewModelProvider.DefaultKey";

        class FnCloseable final : public AutoCloseable {
        public:
            explicit FnCloseable(kiln::Function<void()> f) : m_F(std::move(f)) {}
            void Close() override { if (auto f = std::exchange(m_F, nullptr)) f(); }
        private:
            kiln::Function<void()> m_F;
        };

        class InitializerFactory final : public ViewModelProvider::Factory {
        public:
            explicit InitializerFactory(std::vector<ViewModelInitializer> i) : m_Initializers(std::move(i)) {}
            std::shared_ptr<ViewModel> Create(const ViewModelClass& modelClass, const CreationExtras& extras) override {
                for (const auto& i : m_Initializers)
                    if (i.clazz == modelClass)
                        if (auto vm = i.initializer(extras)) return vm;
                throw std::invalid_argument("No initializer set for given class " + modelClass.name);
            }
        private:
            std::vector<ViewModelInitializer> m_Initializers;
        };
    }

    std::shared_ptr<AutoCloseable> MakeCloseable(kiln::Function<void()> close) { return std::make_shared<FnCloseable>(std::move(close)); }

    // ---- ViewModel -------------------------------------------------------------------------

    ViewModel::ViewModel(std::vector<std::shared_ptr<AutoCloseable>> closeables) : m_Closeables(std::move(closeables)) {}

    ViewModel::ViewModel(std::shared_ptr<CloseableCoroutineScope> viewModelScope, std::vector<std::shared_ptr<AutoCloseable>> closeables)
        : m_Closeables(std::move(closeables)) {
        if (viewModelScope) m_Keyed[kScopeKey] = std::move(viewModelScope);
    }

    void ViewModel::AddCloseable(const std::string& key, std::shared_ptr<AutoCloseable> closeable) {
        if (m_Cleared) {
            if (closeable) closeable->Close();
            return;
        }
        auto old = std::exchange(m_Keyed[key], std::move(closeable));
        if (old) old->Close();
    }

    void ViewModel::AddCloseable(std::shared_ptr<AutoCloseable> closeable) {
        if (m_Cleared) {
            if (closeable) closeable->Close();
            return;
        }
        m_Closeables.push_back(std::move(closeable));
    }

    void ViewModel::Clear() {
        if (!m_Cleared) {
            m_Cleared = true;
            for (auto& [k, c] : m_Keyed) if (c) c->Close();
            for (auto& c : m_Closeables) if (c) c->Close();
            m_Closeables.clear();
        }
        OnCleared();
    }

    std::shared_ptr<CloseableCoroutineScope> ViewModelScope(ViewModel& viewModel) {
        if (auto existing = viewModel.GetCloseable<CloseableCoroutineScope>(kScopeKey)) return existing;
        auto scope = std::make_shared<CloseableCoroutineScope>();
        viewModel.AddCloseable(kScopeKey, scope);   // closed at once on a cleared ViewModel
        return scope;
    }

    // ---- ViewModelStore --------------------------------------------------------------------

    ViewModelStore::~ViewModelStore() = default;

    void ViewModelStore::Put(const std::string& key, std::shared_ptr<ViewModel> viewModel) {
        auto old = std::exchange(m_Map[key], std::move(viewModel));
        if (old && old != m_Map[key]) old->Clear();
    }
    std::shared_ptr<ViewModel> ViewModelStore::Get(const std::string& key) const {
        auto it = m_Map.find(key);
        return it == m_Map.end() ? nullptr : it->second;
    }
    std::set<std::string> ViewModelStore::Keys() const {
        std::set<std::string> keys;
        for (const auto& [k, v] : m_Map) keys.insert(k);
        return keys;
    }
    void ViewModelStore::Clear() {
        auto snapshot = std::move(m_Map);
        m_Map.clear();
        for (auto& [k, vm] : snapshot) vm->Clear();
    }

    // ---- CreationExtras --------------------------------------------------------------------

    bool CreationExtras::operator==(const CreationExtras& o) const {
        if (m_Extras.size() != o.m_Extras.size()) return false;
        for (const auto& [k, v] : m_Extras)
            if (!o.m_Extras.contains(k)) return false;
        return true;   // values are compared by key presence (std::any has no equality)
    }
    const CreationExtras& CreationExtras::Empty() {
        static const MutableCreationExtras empty;
        return empty;
    }
    MutableCreationExtras& MutableCreationExtras::operator+=(const CreationExtras& other) {
        for (const auto& [k, v] : other.m_Extras) m_Extras[k] = v;
        return *this;
    }
    MutableCreationExtras operator+(const CreationExtras& a, const CreationExtras& b) {
        MutableCreationExtras r(a);
        r += b;
        return r;
    }
    MutableCreationExtras MakeCreationExtras(const CreationExtras& initial, const kiln::Function<void(MutableCreationExtras&)>& builder) {
        MutableCreationExtras r(initial);
        if (builder) builder(r);
        return r;
    }

    // ---- ViewModelProvider -----------------------------------------------------------------

    std::shared_ptr<ViewModel> ViewModelProvider::Factory::Create(const ViewModelClass&, const CreationExtras&) {
        throw std::logic_error("`Factory.create(String, CreationExtras)` is not implemented. You may need to override the method and provide a "
                               "custom implementation.");
    }

    std::shared_ptr<ViewModel> ViewModelProvider::NewInstanceFactory::Create(const ViewModelClass& modelClass, const CreationExtras&) {
        if (!modelClass.newInstance) throw std::logic_error("Cannot create an instance of " + modelClass.name + " (no default constructor)");
        return modelClass.newInstance();
    }
    const ViewModelProvider::FactoryPtr& ViewModelProvider::NewInstanceFactory::Instance() {
        static const FactoryPtr instance = std::make_shared<NewInstanceFactory>();
        return instance;
    }

    ViewModelProvider ViewModelProvider::Create(ViewModelStoreOwner& owner) {
        return Create(owner, DefaultViewModelProviderFactoryOf(&owner), DefaultViewModelCreationExtrasOf(&owner));
    }
    ViewModelProvider ViewModelProvider::Create(ViewModelStoreOwner& owner, FactoryPtr factory) {
        return Create(owner, std::move(factory), DefaultViewModelCreationExtrasOf(&owner));
    }
    ViewModelProvider ViewModelProvider::Create(ViewModelStoreOwner& owner, FactoryPtr factory, const CreationExtras& extras) {
        return ViewModelProvider(owner.GetViewModelStore(), std::move(factory), MutableCreationExtras(extras));
    }
    ViewModelProvider ViewModelProvider::Create(ViewModelStore& store, FactoryPtr factory, const CreationExtras& extras) {
        return ViewModelProvider(store, std::move(factory), MutableCreationExtras(extras));
    }

    std::string ViewModelProvider::DefaultKey(const ViewModelClass& modelClass) { return std::string(kDefaultKey) + ":" + modelClass.name; }

    std::shared_ptr<ViewModel> ViewModelProvider::GetViewModel(const ViewModelClass& modelClass, const std::string& key) {
        if (auto existing = m_Store->Get(key); existing && modelClass.isInstance && modelClass.isInstance(existing.get())) return existing;
        MutableCreationExtras extras(m_Extras);
        extras.Set(ViewModelKey, key);
        auto vm = m_Factory->Create(modelClass, extras);
        if (!vm) throw std::logic_error("the factory made no " + modelClass.name);
        m_Store->Put(key, vm);
        return vm;
    }

    ViewModelProvider::FactoryPtr DefaultViewModelProviderFactoryOf(ViewModelStoreOwner* owner) {
        if (auto* d = dynamic_cast<HasDefaultViewModelProviderFactory*>(owner)) return d->DefaultViewModelProviderFactory();
        return ViewModelProvider::NewInstanceFactory::Instance();
    }
    MutableCreationExtras DefaultViewModelCreationExtrasOf(ViewModelStoreOwner* owner) {
        if (auto* d = dynamic_cast<HasDefaultViewModelProviderFactory*>(owner)) return d->DefaultViewModelCreationExtras();
        return MutableCreationExtras();
    }

    // ---- initializers ----------------------------------------------------------------------

    void InitializerViewModelFactoryBuilder::AddInitializer(const ViewModelClass& clazz,
                                                            kiln::Function<std::shared_ptr<ViewModel>(const CreationExtras&)> initializer) {
        for (const auto& i : m_Initializers)
            if (i.clazz == clazz) throw std::invalid_argument("A `initializer` with the same `clazz` has already been added: " + clazz.name + ".");
        m_Initializers.emplace_back(clazz, std::move(initializer));
    }
    ViewModelProvider::FactoryPtr InitializerViewModelFactoryBuilder::Build() const { return std::make_shared<InitializerFactory>(m_Initializers); }

    ViewModelProvider::FactoryPtr ViewModelFactory(const kiln::Function<void(InitializerViewModelFactoryBuilder&)>& builder) {
        InitializerViewModelFactoryBuilder b;
        builder(b);
        return b.Build();
    }

}
