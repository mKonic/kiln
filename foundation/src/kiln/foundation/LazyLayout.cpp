#include "kiln/foundation/LazyLayout.h"
#include <kiln/Function.h>

#include "kiln/runtime/Saveable.h"

#include <unordered_map>

namespace kiln::foundation {

    using namespace ui;

    rt::Key DefaultLazyLayoutKey(int index) {
        // DefaultLazyKey(index): a value of its own type in Compose; here a hash no user key of
        // an int or string gives.
        rt::Key k = rt::Key::Positional();
        k.value = (std::uint64_t(std::uint32_t(index)) * 0xC2B2AE3D27D4EB4Full) ^ 0x6C617A796B6579ull;
        k.value |= 1;
        k.explicitKey = true;
        return k;
    }

    namespace {
        // LazyLayoutItemContentFactory: one content object per key, so a slot recomposes only
        // when its item changes; the item's index is state its content reads.
        struct ItemContentFactory {
            kiln::Function<LazyLayoutItemProviderPtr()> provider;
            rt::SaveableStateHolder* holder = nullptr;
            struct Cached {
                std::shared_ptr<rt::State<int>> index;
                std::shared_ptr<const kiln::Function<void()>> content;
            };
            std::unordered_map<std::uint64_t, Cached> cache;

            std::shared_ptr<const kiln::Function<void()>> GetContent(int index, const rt::Key& key) {
                auto it = cache.find(key.value);
                if (it != cache.end()) {
                    if (it->second.index->Peek() != index) it->second.index->Set(index);
                    return it->second.content;
                }
                auto indexState = std::make_shared<rt::State<int>>(index);
                auto content = std::make_shared<const kiln::Function<void()>>([this, indexState, key] {
                    const LazyLayoutItemProviderPtr p = provider();
                    const int i = indexState->Get();
                    if (!p || i < 0 || i >= p->ItemCount()) return;
                    // The item may have moved: find it by key.
                    int current = i;
                    if (!(p->GetKey(i) == key)) {
                        const int found = p->GetIndex(key);
                        if (found < 0) return;
                        current = found;
                    }
                    auto body = [p, current, key] { p->Item(current, key); };
                    if (holder) rt::SaveableStateProvider(*holder, rt::detail::SaveableKey(key.value), body);
                    else body();
                });
                cache[key.value] = { indexState, content };
                return content;
            }
        };

        class MeasureScopeImpl final : public LazyLayoutMeasureScope {
        public:
            MeasureScopeImpl(SubcomposeMeasureScope& s, ItemContentFactory& f) : LazyLayoutMeasureScope(s), m_Scope(s), m_Factory(f), m_Provider(f.provider()) {}
            const std::vector<Measurable*>& Compose(int index) override {
                auto it = m_Measurables.find(index);
                if (it != m_Measurables.end()) return it->second;
                const rt::Key key = m_Provider->GetKey(index);
                return m_Measurables[index] = m_Scope.Subcompose(key, m_Factory.GetContent(index, key));
            }
            const std::vector<Placeable*>& Measure(int index, const Constraints& c) override {
                auto it = m_Placeables.find(index);
                if (it != m_Placeables.end()) return it->second;
                std::vector<Placeable*> out;
                for (Measurable* m : Compose(index)) out.push_back(&m->Measure(c));
                return m_Placeables[index] = std::move(out);
            }

        private:
            SubcomposeMeasureScope& m_Scope;
            ItemContentFactory& m_Factory;
            LazyLayoutItemProviderPtr m_Provider;
            std::unordered_map<int, std::vector<Measurable*>> m_Measurables;
            std::unordered_map<int, std::vector<Placeable*>> m_Placeables;
        };
    }

    void LazyLayout(kiln::Function<LazyLayoutItemProviderPtr()> itemProvider, const Modifier& modifier, LazyLayoutPrefetchState* prefetchState,
                    LazyLayoutMeasurePolicy measurePolicy, rt::Key key) {
        auto& c = *rt::Composition::Current();
        c.Section([&] {
            auto& holder = rt::RememberSaveableStateHolder();
            auto& factory = c.Remember<std::shared_ptr<ItemContentFactory>>([] { return std::make_shared<ItemContentFactory>(); });
            factory->provider = std::move(itemProvider);   // rememberUpdatedState
            factory->holder = &holder;
            auto& subcompose = c.Remember<std::shared_ptr<SubcomposeLayoutState>>([] { return std::make_shared<SubcomposeLayoutState>(); });
            auto& policy = c.Remember<std::shared_ptr<LazyLayoutMeasurePolicy>>([] { return std::make_shared<LazyLayoutMeasurePolicy>(); });
            *policy = std::move(measurePolicy);
            if (prefetchState) {
                std::weak_ptr<ItemContentFactory> wf = factory;
                SubcomposeLayoutState* state = subcompose.get();
                c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(prefetchState), reinterpret_cast<std::uintptr_t>(factory.get())),
                                   [prefetchState, wf, state] {
                                       LazyLayoutPrefetchState::Target t;
                                       t.subcompose = state;
                                       t.provider = [wf]() -> LazyLayoutItemProviderPtr { auto f = wf.lock(); return f ? f->provider() : nullptr; };
                                       t.content = [wf](int index, const rt::Key& k) -> std::shared_ptr<const kiln::Function<void()>> {
                                           auto f = wf.lock();
                                           return f ? f->GetContent(index, k) : nullptr;
                                       };
                                       prefetchState->SetTarget(std::move(t));
                                       return kiln::Function<void()>([prefetchState] { prefetchState->SetTarget(std::nullopt); });
                                   });
            }
            ItemContentFactory* f = factory.get();
            auto p = policy;
            SubcomposeLayout(*subcompose, modifier, [f, p](SubcomposeMeasureScope& s, const Constraints& constraints) {
                MeasureScopeImpl scope(s, *f);
                return (*p)(scope, constraints);
            });
        }, key);
    }

    // --- prefetch --------------------------------------------------------------------------------

    LazyLayoutPrefetchState::PrefetchHandle LazyLayoutPrefetchState::SchedulePrecomposition(int index) {
        PrefetchHandle h;
        if (!m_Target || !m_Target->subcompose) return h;
        auto provider = m_Target->provider();
        if (!provider || index < 0 || index >= provider->ItemCount()) return h;
        const rt::Key key = provider->GetKey(index);
        auto content = m_Target->content(index, key);
        if (!content) return h;
        auto c = content;
        h.m_Handle = std::make_shared<SubcomposeLayoutState::PrecomposedSlotHandle>(m_Target->subcompose->Precompose(key, [c] { (*c)(); }));
        return h;
    }

    LazyLayoutPrefetchState::PrefetchHandle LazyLayoutPrefetchState::SchedulePrecompositionAndPremeasure(int index, Constraints constraints,
                                                                                                        kiln::Function<void(const PrefetchResultScope&)> onItemPremeasured) {
        PrefetchHandle h = SchedulePrecomposition(index);
        if (!h.m_Handle) return h;
        PrefetchResultScope result;
        result.index = index;
        for (int i = 0; i < h.m_Handle->PlaceablesCount(); ++i) {
            h.m_Handle->Premeasure(i, constraints);
            result.sizes.push_back({});
        }
        if (onItemPremeasured) onItemPremeasured(result);
        return h;
    }

    std::vector<std::pair<int, std::optional<Constraints>>> LazyLayoutPrefetchState::CollectNestedPrefetchRequests() const {
        if (!m_OnNestedPrefetch) return {};
        NestedPrefetchScope scope;
        m_OnNestedPrefetch(scope);
        return scope.requests;
    }

    // --- pinning ---------------------------------------------------------------------------------

    void LazyLayoutPinnedItemList::Pin(std::shared_ptr<PinnedItem> item) {
        auto items = m_Items.Peek();
        items.push_back(std::move(item));
        m_Items.Set(std::move(items));
    }
    void LazyLayoutPinnedItemList::Release(const std::shared_ptr<PinnedItem>& item) {
        auto items = m_Items.Peek();
        auto it = std::find(items.begin(), items.end(), item);
        if (it == items.end()) return;
        items.erase(it);
        m_Items.Set(std::move(items));
    }

    namespace {
        // LazyLayoutPinnableItem's container: the first pin adds the item to the list and pins
        // the container around the layout; the last release undoes both.
        struct PinnableItem final : PinnableContainer {
            std::shared_ptr<LazyLayoutPinnedItemList::PinnedItem> item = std::make_shared<LazyLayoutPinnedItemList::PinnedItem>();
            LazyLayoutPinnedItemList* list = nullptr;
            std::shared_ptr<PinnableContainer> parent;
            int pins = 0;
            bool disposed = false;
            void Pin() override {
                if (disposed) throw std::logic_error("Pin should not be called on an already disposed item");
                if (pins++ == 0) {
                    list->Pin(item);
                    if (parent) parent->Pin();
                }
            }
            void Release() override {
                if (disposed || pins == 0) return;
                if (--pins == 0) Unpin();
            }
            void Unpin() {
                list->Release(item);
                if (parent) parent->Release();
            }
            void OnDisposed() {
                if (pins > 0) Unpin();
                pins = 0;
                disposed = true;
            }
        };
    }

    void LazyLayoutPinnableItem(const rt::Key& key, int index, LazyLayoutPinnedItemList& pinnedItemList, kiln::Function<void()> content) {
        auto& c = *rt::Composition::Current();
        auto& item = c.Remember<std::shared_ptr<PinnableItem>>([&] {
            auto p = std::make_shared<PinnableItem>();
            p->list = &pinnedItemList;
            p->item->key = key;
            return p;
        }, rt::Keys(key.value, reinterpret_cast<std::uintptr_t>(&pinnedItemList)));
        item->item->index = index;
        const auto& parent = c.Read(LocalPinnableContainer);
        if (parent != item->parent) {
            if (item->pins > 0) {
                if (item->parent) item->parent->Release();
                if (parent) parent->Pin();
            }
            item->parent = parent;
        }
        auto keep = item;
        c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(keep.get())), [keep] { return kiln::Function<void()>([keep] { keep->OnDisposed(); }); });
        c.Provide(LocalPinnableContainer, std::static_pointer_cast<PinnableContainer>(keep), std::move(content));
    }

    // --- cache windows -----------------------------------------------------------------------------

    LazyLayoutCacheWindowPtr MakeLazyLayoutCacheWindow(Dp ahead, Dp behind, bool nonScroll) {
        struct DpWindow final : LazyLayoutCacheWindow {
            Dp ahead, behind;
            bool nonScroll;
            DpWindow(Dp a, Dp b, bool n) : ahead(a), behind(b), nonScroll(n) {}
            bool IsNonScrollCachingEnabled() const override { return nonScroll; }
            int CalculateAheadWindow(const Density& d, int) const override { return d.RoundToPx(ahead); }
            int CalculateBehindWindow(const Density& d, int) const override { return d.RoundToPx(behind); }
        };
        return std::make_shared<DpWindow>(ahead, behind, nonScroll);
    }

    LazyLayoutCacheWindowPtr MakeLazyLayoutCacheWindowFraction(float aheadFraction, float behindFraction, bool nonScroll) {
        struct FractionWindow final : LazyLayoutCacheWindow {
            float ahead, behind;
            bool nonScroll;
            FractionWindow(float a, float b, bool n) : ahead(a), behind(b), nonScroll(n) {}
            bool IsNonScrollCachingEnabled() const override { return nonScroll; }
            int CalculateAheadWindow(const Density&, int viewport) const override { return RoundJava(float(viewport) * ahead); }
            int CalculateBehindWindow(const Density&, int viewport) const override { return RoundJava(float(viewport) * behind); }
        };
        return std::make_shared<FractionWindow>(aheadFraction, behindFraction, nonScroll);
    }

    LazyLayoutCacheWindowPtr LazyListDefaults::CacheWindow() {
        // DEFAULT_LAZY_LAYOUT_CACHE_WINDOW_AHEAD_FRACTION, non-scroll caching off.
        static const LazyLayoutCacheWindowPtr window = MakeLazyLayoutCacheWindowFraction(0.1f, 0, false);
        return window;
    }

}
