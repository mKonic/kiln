#pragma once

// Paged data in content (paging-compose: LazyPagingItems, collectAsLazyPagingItems, itemKey,
// itemContentType).
//
//   auto& notes = paging::CollectAsLazyPagingItems(model.notes);   // a CachedIn flow
//   LazyColumn({}, list, [&](LazyListScope& s) {
//       s.Items(notes.ItemCount(), [&](int i) {
//           if (const Note* note = notes.Get(i)) NoteRow(*note); else PlaceholderRow();
//       }, paging::ItemKey(notes, [](const Note& n) { return rt::Key(n.id); }));
//       if (notes.LoadState().append.IsLoading()) s.Item([] { CircularProgressIndicator(); });
//   });

#include "kiln/paging/PagingDataPresenter.h"

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"

namespace kiln::paging {

    template<typename T>
    class LazyPagingItems {
    public:
        explicit LazyPagingItems(PagingDataFlow<T> flow)
            : m_Flow(std::move(flow)),
              m_Presenter([this](const PagingDataEvent<T>&) { UpdateSnapshot(); }, m_Flow.Replay()),
              m_Snapshot(std::make_shared<const ItemSnapshotList<T>>(m_Presenter.Snapshot())),
              m_LoadState(m_Presenter.LoadStateFlow().Value().value_or(InitialLoadStates())) {}
        LazyPagingItems(const LazyPagingItems&) = delete;

        // Items and placeholders. Reading it in content recomposes when the list changes.
        int ItemCount() const { return m_Snapshot.Get()->Size(); }
        // The item at index, or null while it is a placeholder; tells the Pager it was read, so
        // call it for the items being shown.
        const T* Get(int index) {
            m_Presenter.Get(index);
            return m_Snapshot.Get()->Get(index);
        }
        // The item without telling the Pager (for keys and the like).
        const T* Peek(int index) const { return m_Snapshot.Get()->Get(index); }
        const ItemSnapshotList<T>& ItemSnapshot() const { return *m_Snapshot.Get(); }
        // Loads again what failed.
        void Retry() { m_Presenter.Retry(); }
        // Loads again around the place being shown, from a new source.
        void Refresh() { m_Presenter.Refresh(); }
        // refresh / prepend / append states, for progress and error rows.
        const CombinedLoadStates& LoadState() const { return m_LoadState.Get(); }

        // What CollectAsLazyPagingItems runs while composed.
        rt::Task<> CollectPagingData() {
            return rt::CollectLatest<PagingData<T>>(m_Flow.flow, [this](PagingData<T> data) { return m_Presenter.CollectFrom(std::move(data)); });
        }
        rt::Task<> CollectLoadState() {
            return m_Presenter.LoadStateFlow().Collect(kiln::Function<void(const std::optional<CombinedLoadStates>&)>(
                [this](const std::optional<CombinedLoadStates>& s) { if (s) m_LoadState.Set(*s); }));
        }

    private:
        static CombinedLoadStates InitialLoadStates() {
            const LoadStates initial{ LoadState::Loading(), LoadState::NotLoading(false), LoadState::NotLoading(false) };
            return { initial.refresh, initial.prepend, initial.append, initial, std::nullopt };
        }
        void UpdateSnapshot() { m_Snapshot.Set(std::make_shared<const ItemSnapshotList<T>>(m_Presenter.Snapshot())); }

        PagingDataFlow<T> m_Flow;
        PagingDataPresenter<T> m_Presenter;
        rt::State<std::shared_ptr<const ItemSnapshotList<T>>> m_Snapshot;
        rt::State<CombinedLoadStates> m_LoadState;
    };

    namespace detail {
        // Keeps the items alive for as long as the task runs.
        template<typename T>
        rt::Task<> HoldTask(std::shared_ptr<LazyPagingItems<T>> held, rt::Task<> task) {
            co_await std::move(task);
        }
    }

    // collectAsLazyPagingItems(): presents the flow's paged data while the caller is composed.
    template<typename T>
    LazyPagingItems<T>& CollectAsLazyPagingItems(const PagingDataFlow<T>& flow, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        const rt::Keys keys(reinterpret_cast<std::uintptr_t>(flow.Identity()));
        auto& items = c.Remember<std::shared_ptr<LazyPagingItems<T>>>([&] { return std::make_shared<LazyPagingItems<T>>(flow); }, keys, key);
        std::shared_ptr<LazyPagingItems<T>> held = items;
        const rt::Keys effectKeys(reinterpret_cast<std::uintptr_t>(held.get()));
        c.LaunchedEffect(effectKeys, [held] { return detail::HoldTask<T>(held, held->CollectPagingData()); }, key);
        c.LaunchedEffect(effectKeys, [held] { return detail::HoldTask<T>(held, held->CollectLoadState()); }, rt::Key(std::int64_t(key.value) ^ 0x5bd1e995));
        return *items;
    }
    template<typename T>
    LazyPagingItems<T>& CollectAsLazyPagingItems(const rt::Flow<PagingData<T>>& flow, rt::Key key = {}) {
        return CollectAsLazyPagingItems(PagingDataFlow<T>(flow), key);
    }

    // itemKey { }: keys for LazyListScope::Items; a placeholder's key is its index.
    template<typename T, typename F>
    kiln::Function<rt::Key(int)> ItemKey(LazyPagingItems<T>& items, F keyOf) {
        return [&items, keyOf = std::move(keyOf)](int index) -> rt::Key {
            const T* item = items.Peek(index);
            return item ? rt::Key(keyOf(*item)) : rt::Key(std::int64_t(index) ^ std::int64_t(0x7061676570680000));
        };
    }
    // itemContentType { }: content types for LazyListScope::Items; placeholders share one.
    template<typename T, typename F>
    kiln::Function<rt::Key(int)> ItemContentType(LazyPagingItems<T>& items, F typeOf) {
        return [&items, typeOf = std::move(typeOf)](int index) -> rt::Key {
            const T* item = items.Peek(index);
            return item ? rt::Key(typeOf(*item)) : rt::Key("kiln.paging.placeholder");
        };
    }


}
