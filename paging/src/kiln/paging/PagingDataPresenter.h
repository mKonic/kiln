#pragma once

// Presenting PagingData (paging-common: PagingDataPresenter, PageStore, ItemSnapshotList,
// PagingDataEvent, CachedPagingData's cachedIn).

#include "kiln/paging/PagingData.h"

#include "kiln/runtime/CoroutineScope.h"

#include <deque>
#include <map>

namespace kiln::paging {

    // The presented list at one moment: loaded items with null placeholders around them.
    template<typename T>
    struct ItemSnapshotList {
        int placeholdersBefore = 0, placeholdersAfter = 0;
        std::vector<T> items;

        int Size() const { return placeholdersBefore + int(items.size()) + placeholdersAfter; }
        // The item, or null for a placeholder.
        const T* Get(int index) const {
            if (index < 0 || index >= Size()) throw std::out_of_range("Illegal attempt to access index " + std::to_string(index) + " in ItemSnapshotList of size " + std::to_string(Size()));
            const int local = index - placeholdersBefore;
            return local >= 0 && local < int(items.size()) ? &items[std::size_t(local)] : nullptr;
        }
    };

    // What changed in the presented list.
    template<typename T>
    struct PagingDataEvent {
        enum class Kind : std::uint8_t { Refresh, Prepend, Append, DropPrepend, DropAppend } kind = Kind::Refresh;
        std::vector<T> inserted;         // Prepend, Append
        int startIndex = 0;              // Append, DropAppend
        int dropCount = 0;               // DropPrepend, DropAppend
        int newPlaceholdersBefore = 0, oldPlaceholdersBefore = 0;   // Prepend, DropPrepend; Refresh
        int newPlaceholdersAfter = 0, oldPlaceholdersAfter = 0;     // Append, DropAppend; Refresh
        int newDataCount = 0, oldDataCount = 0;                     // Refresh
    };

    namespace detail {

        // The presented pages.
        template<typename T>
        class PageStore {
        public:
            PageStore() : PageStore({ TransformablePage<T>::Of(0, {}) }, 0, 0) {}
            PageStore(std::vector<TransformablePage<T>> pages, int placeholdersBefore, int placeholdersAfter)
                : m_Pages(std::move(pages)), m_PlaceholdersBefore(placeholdersBefore), m_PlaceholdersAfter(placeholdersAfter) {
                for (const auto& p : m_Pages) m_DataCount += int(p.data.size());
            }

            int DataCount() const { return m_DataCount; }
            int PlaceholdersBefore() const { return m_PlaceholdersBefore; }
            int PlaceholdersAfter() const { return m_PlaceholdersAfter; }
            int Size() const { return m_PlaceholdersBefore + m_DataCount + m_PlaceholdersAfter; }

            const T* Get(int index) const {
                if (index < 0 || index >= Size()) throw std::out_of_range("Index: " + std::to_string(index) + ", Size: " + std::to_string(Size()));
                const int local = index - m_PlaceholdersBefore;
                if (local < 0 || local >= m_DataCount) return nullptr;
                int at = local;
                for (const auto& p : m_Pages) {
                    if (int(p.data.size()) > at) return &p.data[std::size_t(at)];
                    at -= int(p.data.size());
                }
                return nullptr;
            }
            ItemSnapshotList<T> Snapshot() const {
                ItemSnapshotList<T> out{ m_PlaceholdersBefore, m_PlaceholdersAfter, {} };
                out.items.reserve(std::size_t(m_DataCount));
                for (const auto& p : m_Pages) out.items.insert(out.items.end(), p.data.begin(), p.data.end());
                return out;
            }

            ViewportHint InitializeHint() const {
                ViewportHint h;
                h.presentedItemsBefore = h.presentedItemsAfter = m_DataCount / 2;
                h.originalPageOffsetFirst = OffsetFirst();
                h.originalPageOffsetLast = OffsetLast();
                return h;
            }
            ViewportHint AccessHintForIndex(int index) const {
                std::size_t pageIndex = 0;
                int indexInPage = index - m_PlaceholdersBefore;
                while (indexInPage >= int(m_Pages[pageIndex].data.size()) && pageIndex < m_Pages.size() - 1) {
                    indexInPage -= int(m_Pages[pageIndex].data.size());
                    ++pageIndex;
                }
                return m_Pages[pageIndex].ViewportHintFor(indexInPage, index - m_PlaceholdersBefore, Size() - index - m_PlaceholdersAfter - 1, OffsetFirst(), OffsetLast());
            }

            // Applies a prepend, append or drop and says what changed.
            PagingDataEvent<T> ProcessEvent(const PageEvent<T>& event) {
                using Kind = typename PageEvent<T>::Kind;
                PagingDataEvent<T> out;
                if (event.kind == Kind::Insert && event.loadType != LoadType::Refresh) {
                    int insertSize = 0;
                    for (const auto& p : event.pages) {
                        insertSize += int(p.data.size());
                        out.inserted.insert(out.inserted.end(), p.data.begin(), p.data.end());
                    }
                    if (event.loadType == LoadType::Prepend) {
                        out.kind = PagingDataEvent<T>::Kind::Prepend;
                        out.oldPlaceholdersBefore = m_PlaceholdersBefore;
                        m_Pages.insert(m_Pages.begin(), event.pages.begin(), event.pages.end());
                        m_DataCount += insertSize;
                        m_PlaceholdersBefore = event.placeholdersBefore;
                        out.newPlaceholdersBefore = m_PlaceholdersBefore;
                    } else {
                        out.kind = PagingDataEvent<T>::Kind::Append;
                        out.oldPlaceholdersAfter = m_PlaceholdersAfter;
                        out.startIndex = m_PlaceholdersBefore + m_DataCount;
                        m_Pages.insert(m_Pages.end(), event.pages.begin(), event.pages.end());
                        m_DataCount += insertSize;
                        m_PlaceholdersAfter = event.placeholdersAfter;
                        out.newPlaceholdersAfter = m_PlaceholdersAfter;
                    }
                    return out;
                }
                if (event.kind == Kind::Drop) {
                    int dropped = 0;
                    std::erase_if(m_Pages, [&](const TransformablePage<T>& p) {
                        const bool drop = std::any_of(p.originalPageOffsets.begin(), p.originalPageOffsets.end(),
                                                      [&](int o) { return o >= event.minPageOffset && o <= event.maxPageOffset; });
                        if (drop) dropped += int(p.data.size());
                        return drop;
                    });
                    m_DataCount -= dropped;
                    out.dropCount = dropped;
                    if (event.loadType == LoadType::Prepend) {
                        out.kind = PagingDataEvent<T>::Kind::DropPrepend;
                        out.oldPlaceholdersBefore = m_PlaceholdersBefore;
                        m_PlaceholdersBefore = event.placeholdersRemaining;
                        out.newPlaceholdersBefore = m_PlaceholdersBefore;
                    } else {
                        out.kind = PagingDataEvent<T>::Kind::DropAppend;
                        out.oldPlaceholdersAfter = m_PlaceholdersAfter;
                        m_PlaceholdersAfter = event.placeholdersRemaining;
                        out.newPlaceholdersAfter = m_PlaceholdersAfter;
                        out.startIndex = m_PlaceholdersBefore + m_DataCount;
                    }
                    return out;
                }
                throw std::logic_error("Paging received an event to process StaticList or LoadStateUpdate while processing Inserts and Drops.");
            }

        private:
            int OffsetFirst() const { return *std::min_element(m_Pages.front().originalPageOffsets.begin(), m_Pages.front().originalPageOffsets.end()); }
            int OffsetLast() const { return *std::max_element(m_Pages.back().originalPageOffsets.begin(), m_Pages.back().originalPageOffsets.end()); }
            std::vector<TransformablePage<T>> m_Pages;
            int m_DataCount = 0;
            int m_PlaceholdersBefore = 0, m_PlaceholdersAfter = 0;
        };
    }

    // Holds the presented list of a PagingData and tells the Pager which items are read, so it
    // loads more. LazyPagingItems wraps one for content; use it directly to present paged data
    // elsewhere. Keep it at one address while it collects.
    template<typename T>
    class PagingDataPresenter {
    public:
        // `present` hears each change after it was applied; `cached` shows data at once (the
        // latest PagingData of a CachedIn flow).
        explicit PagingDataPresenter(kiln::Function<void(const PagingDataEvent<T>&)> present = {}, const std::optional<PagingData<T>>& cached = {})
            : m_Present(std::move(present)) {
            if (auto event = cached ? cached->CachedEvent() : std::nullopt) {
                m_Store = detail::PageStore<T>(event->pages, event->placeholdersBefore, event->placeholdersAfter);
                if (event->sourceLoadStates) SetLoadStates(*event->sourceLoadStates, event->mediatorLoadStates);
            }
        }
        PagingDataPresenter(const PagingDataPresenter&) = delete;
        PagingDataPresenter& operator=(const PagingDataPresenter&) = delete;

        // Presents the data until it ends or the task is cancelled; one PagingData at a time
        // (rt::CollectLatest over the Pager's flow).
        rt::Task<> CollectFrom(PagingData<T> data) {
            using Event = detail::PageEvent<T>;
            using Kind = typename Event::Kind;
            SetUiReceiver(data.Ui());
            co_await data.Events().Collect(rt::FlowCollector<Event>([this, data](Event event) -> rt::Task<> {
                // An event that arrives while an item is being read waits for the read to finish.
                if (m_InGetItem) co_await rt::Yield();
                if (event.kind == Kind::StaticList) {
                    PresentNewList({ detail::TransformablePage<T>::Of(0, std::move(event.data)) }, event.placeholdersBefore, event.placeholdersAfter,
                                   event.sourceLoadStates, event.mediatorLoadStates, data.Hints());
                } else if (event.IsRefresh()) {
                    PresentNewList(std::move(event.pages), event.placeholdersBefore, event.placeholdersAfter, event.sourceLoadStates, event.mediatorLoadStates, data.Hints());
                } else if (event.kind == Kind::Insert) {
                    const PagingDataEvent<T> change = m_Store.ProcessEvent(event);
                    if (m_Present) m_Present(change);
                    SetLoadStates(*event.sourceLoadStates, event.mediatorLoadStates);
                    const LoadStates& source = m_LoadStates.Value()->source;
                    const bool canContinueLoading = !(event.loadType == LoadType::Prepend && source.prepend.endOfPaginationReached) &&
                                                    !(event.loadType == LoadType::Append && source.append.endOfPaginationReached);
                    const bool emptyInsert = std::all_of(event.pages.begin(), event.pages.end(), [](const auto& p) { return p.data.empty(); });
                    if (!canContinueLoading) {
                        m_LastAccessedIndexUnfulfilled = false;
                    } else if (m_LastAccessedIndexUnfulfilled || emptyInsert) {
                        // The read item is still a placeholder (or nothing arrived): ask again.
                        const bool resend = emptyInsert || m_LastAccessedIndex < m_Store.PlaceholdersBefore() ||
                                            m_LastAccessedIndex > m_Store.PlaceholdersBefore() + m_Store.DataCount();
                        if (resend) {
                            if (m_Hints && m_Hints->processHint) m_Hints->processHint(m_Store.AccessHintForIndex(m_LastAccessedIndex));
                        } else {
                            m_LastAccessedIndexUnfulfilled = false;
                        }
                    }
                } else if (event.kind == Kind::Drop) {
                    const PagingDataEvent<T> change = m_Store.ProcessEvent(event);
                    if (m_Present) m_Present(change);
                    const LoadStates source = (m_LoadStates.Value() ? m_LoadStates.Value()->source : LoadStates::Idle()).Modify(event.loadType, LoadState::NotLoading(false));
                    SetLoadStates(source, m_LoadStates.Value() ? m_LoadStates.Value()->mediator : std::nullopt);
                    m_LastAccessedIndexUnfulfilled = false;
                } else {
                    SetLoadStates(*event.sourceLoadStates, event.mediatorLoadStates);
                }
                if (event.kind != Kind::LoadStateUpdate) {
                    const auto listeners = m_PagesUpdated;
                    for (const auto& [id, fn] : listeners) fn();
                }
            }));
        }

        // The item (null for a placeholder), noting the read so the Pager loads around it.
        const T* Get(int index) {
            m_InGetItem = true;
            m_LastAccessedIndexUnfulfilled = true;
            m_LastAccessedIndex = index;
            if (m_Hints && m_Hints->processHint) m_Hints->processHint(m_Store.AccessHintForIndex(index));
            const T* item = m_Store.Get(index);
            m_InGetItem = false;
            return item;
        }
        // The item without noting a read.
        const T* Peek(int index) const { return m_Store.Get(index); }
        ItemSnapshotList<T> Snapshot() const { return m_Store.Snapshot(); }
        int Size() const { return m_Store.Size(); }

        // Loads again what failed.
        void Retry() {
            if (m_Ui && m_Ui->retry) m_Ui->retry();
            else m_ShouldRetry = true;
        }
        // Loads again around the last read position, from a new source.
        void Refresh() {
            if (m_Ui && m_Ui->refresh) m_Ui->refresh();
            else m_ShouldRefresh = true;
        }

        // The load states; empty until the first arrive.
        const rt::MutableStateFlow<std::optional<CombinedLoadStates>>& LoadStateFlow() const { return m_LoadStates; }
        // After each change to the presented items. The id removes the listener.
        int AddOnPagesUpdatedListener(kiln::Function<void()> listener) {
            m_PagesUpdated[++m_NextListener] = std::move(listener);
            return m_NextListener;
        }
        void RemoveOnPagesUpdatedListener(int id) { m_PagesUpdated.erase(id); }

    private:
        void SetLoadStates(const LoadStates& source, const std::optional<LoadStates>& mediator) {
            m_LoadStates.SetValue(detail::ComputeNewState(m_LoadStates.Value(), source, mediator));
        }
        void SetUiReceiver(std::shared_ptr<const detail::UiReceiver> ui) {
            m_Ui = std::move(ui);
            // retry() / refresh() asked before any data was there.
            if (std::exchange(m_ShouldRetry, false) && m_Ui && m_Ui->retry) m_Ui->retry();
            if (std::exchange(m_ShouldRefresh, false) && m_Ui && m_Ui->refresh) m_Ui->refresh();
        }
        void PresentNewList(std::vector<detail::TransformablePage<T>> pages, int placeholdersBefore, int placeholdersAfter,
                            const std::optional<LoadStates>& source, const std::optional<LoadStates>& mediator, std::shared_ptr<const detail::HintReceiver> hints) {
            m_LastAccessedIndexUnfulfilled = false;
            PagingDataEvent<T> change;
            change.kind = PagingDataEvent<T>::Kind::Refresh;
            change.oldPlaceholdersBefore = m_Store.PlaceholdersBefore();
            change.oldPlaceholdersAfter = m_Store.PlaceholdersAfter();
            change.oldDataCount = m_Store.DataCount();
            m_Store = detail::PageStore<T>(std::move(pages), placeholdersBefore, placeholdersAfter);
            m_Hints = std::move(hints);
            change.newPlaceholdersBefore = placeholdersBefore;
            change.newPlaceholdersAfter = placeholdersAfter;
            change.newDataCount = m_Store.DataCount();
            if (m_Present) m_Present(change);
            if (source) SetLoadStates(*source, mediator);
            // Nothing to read in an empty list: this hint lets the Pager load on.
            if (m_Store.Size() == 0 && m_Hints && m_Hints->processHint) m_Hints->processHint(m_Store.InitializeHint());
        }

        kiln::Function<void(const PagingDataEvent<T>&)> m_Present;
        detail::PageStore<T> m_Store;
        std::shared_ptr<const detail::HintReceiver> m_Hints;
        std::shared_ptr<const detail::UiReceiver> m_Ui;
        bool m_ShouldRetry = false, m_ShouldRefresh = false;
        rt::MutableStateFlow<std::optional<CombinedLoadStates>> m_LoadStates{ std::optional<CombinedLoadStates>{} };
        std::map<int, kiln::Function<void()>> m_PagesUpdated;
        int m_NextListener = 0;
        bool m_InGetItem = false;
        bool m_LastAccessedIndexUnfulfilled = false;
        int m_LastAccessedIndex = 0;
    };

    // A flow of PagingData with what CachedIn remembers of it.
    template<typename T>
    struct PagingDataFlow {
        rt::Flow<PagingData<T>> flow;
        // The latest PagingData of a cached flow, to show its items before collecting.
        kiln::Function<std::optional<PagingData<T>>()> replay;

        PagingDataFlow() = default;
        PagingDataFlow(rt::Flow<PagingData<T>> f) : flow(std::move(f)) {}
        PagingDataFlow(rt::Flow<PagingData<T>> f, kiln::Function<std::optional<PagingData<T>>()> r) : flow(std::move(f)), replay(std::move(r)) {}
        const void* Identity() const { return flow.Identity(); }
        std::optional<PagingData<T>> Replay() const { return replay ? replay() : std::nullopt; }
    };

    namespace detail {

        // FlattenedPageEventStorage: everything received so far, as one refresh.
        template<typename T>
        class FlattenedPageEventStorage {
        public:
            void Add(const PageEvent<T>& event) {
                using Kind = typename PageEvent<T>::Kind;
                m_ReceivedFirstEvent = true;
                switch (event.kind) {
                    case Kind::Insert:
                        m_Source = *event.sourceLoadStates;
                        m_Mediator = event.mediatorLoadStates;
                        if (event.loadType == LoadType::Refresh) {
                            m_Pages.assign(event.pages.begin(), event.pages.end());
                            m_PlaceholdersAfter = event.placeholdersAfter;
                            m_PlaceholdersBefore = event.placeholdersBefore;
                        } else if (event.loadType == LoadType::Prepend) {
                            m_PlaceholdersBefore = event.placeholdersBefore;
                            m_Pages.insert(m_Pages.begin(), event.pages.begin(), event.pages.end());
                        } else {
                            m_PlaceholdersAfter = event.placeholdersAfter;
                            m_Pages.insert(m_Pages.end(), event.pages.begin(), event.pages.end());
                        }
                        break;
                    case Kind::Drop:
                        m_Source = m_Source.Modify(event.loadType, LoadState::NotLoading(false));
                        if (event.loadType == LoadType::Prepend) {
                            m_PlaceholdersBefore = event.placeholdersRemaining;
                            m_Pages.erase(m_Pages.begin(), m_Pages.begin() + event.PageCount());
                        } else {
                            m_PlaceholdersAfter = event.placeholdersRemaining;
                            m_Pages.erase(m_Pages.end() - event.PageCount(), m_Pages.end());
                        }
                        break;
                    case Kind::LoadStateUpdate:
                        m_Source = *event.sourceLoadStates;
                        m_Mediator = event.mediatorLoadStates;
                        break;
                    case Kind::StaticList:
                        if (event.sourceLoadStates) m_Source = *event.sourceLoadStates;
                        if (event.mediatorLoadStates) m_Mediator = event.mediatorLoadStates;
                        m_Pages.clear();
                        m_PlaceholdersAfter = m_PlaceholdersBefore = 0;
                        m_Pages.push_back(TransformablePage<T>::Of(0, event.data));
                        break;
                }
            }
            std::vector<PageEvent<T>> AsEvents() const {
                if (!m_ReceivedFirstEvent) return {};
                if (!m_Pages.empty()) {
                    return { PageEvent<T>::Refresh(std::vector<TransformablePage<T>>(m_Pages.begin(), m_Pages.end()), m_PlaceholdersBefore, m_PlaceholdersAfter, m_Source, m_Mediator) };
                }
                return { PageEvent<T>::LoadStateUpdate(m_Source, m_Mediator) };
            }

        private:
            int m_PlaceholdersBefore = 0, m_PlaceholdersAfter = 0;
            std::deque<TransformablePage<T>> m_Pages;
            LoadStates m_Source = LoadStates::Idle();
            std::optional<LoadStates> m_Mediator;
            bool m_ReceivedFirstEvent = false;
        };

        // CachedPageEventFlow: one collection of the source's events, shared by every collector;
        // a new collector first gets what was loaded so far as one refresh.
        template<typename T>
        class CachedPageEvents : public std::enable_shared_from_this<CachedPageEvents<T>> {
        public:
            using Indexed = std::pair<int, PageEvent<T>>;
            CachedPageEvents(rt::Flow<PageEvent<T>> source, std::shared_ptr<const rt::FrameGate> gate) : m_Source(std::move(source)), m_Gate(std::move(gate)) {}

            rt::Flow<PageEvent<T>> Downstream() {
                return rt::Flow<PageEvent<T>>::Create([self = this->shared_from_this()](rt::FlowCollector<PageEvent<T>> emit) -> rt::Task<> {
                    auto inbox = std::make_shared<rt::Channel<std::optional<Indexed>>>(std::size_t(1) << 20);
                    // History first, then live events numbered after it.
                    std::vector<Indexed> history;
                    {
                        const std::vector<PageEvent<T>> events = self->m_Storage.AsEvents();
                        const int start = self->m_MaxEventIndex - int(events.size()) + 1;
                        for (std::size_t i = 0; i < events.size(); ++i) history.emplace_back(start + int(i), events[i]);
                    }
                    self->m_Subscribers.push_back(inbox);
                    self->Start();
                    if (self->m_Finished) inbox->TrySend(std::nullopt);
                    int maxEventIndex = std::numeric_limits<int>::min();
                    for (auto& h : history) {
                        maxEventIndex = h.first;
                        co_await emit(std::move(h.second));
                    }
                    while (true) {
                        auto next = co_await inbox->Next();
                        if (!next || !*next) break;   // the source ended or was closed
                        if ((*next)->first > maxEventIndex) {
                            maxEventIndex = (*next)->first;
                            co_await emit(std::move((*next)->second));
                        }
                    }
                });
            }
            void Close() {
                m_Closed = true;
                m_Job.Cancel();
                Finish();
            }
            std::optional<PageEvent<T>> CachedEvent() const {
                const auto events = m_Storage.AsEvents();
                return !events.empty() && events.front().IsRefresh() ? std::optional(events.front()) : std::nullopt;
            }

        private:
            void Start() {
                if (m_Started || m_Closed) return;
                m_Started = true;
                m_Job = rt::Launch(rt::Dispatcher::Main(), Run(this->shared_from_this()), m_Gate);
            }
            static rt::Task<> Run(std::shared_ptr<CachedPageEvents> self) {
                auto index = std::make_shared<int>(0);
                try {
                    co_await self->m_Source.Collect(kiln::Function<void(const PageEvent<T>&)>([self, index](const PageEvent<T>& event) {
                        const int i = (*index)++;
                        self->m_MaxEventIndex = i;
                        self->m_Storage.Add(event);
                        self->Broadcast(Indexed{ i, event });
                    }));
                } catch (...) {
                    self->Finish();
                    throw;
                }
                self->Finish();
            }
            void Broadcast(std::optional<Indexed> value) {
                std::erase_if(m_Subscribers, [](const auto& w) { return w.expired(); });
                const auto subscribers = m_Subscribers;
                for (const auto& w : subscribers)
                    if (auto inbox = w.lock()) inbox->TrySend(value);
            }
            void Finish() {
                if (std::exchange(m_Finished, true)) return;
                Broadcast(std::nullopt);
            }

            rt::Flow<PageEvent<T>> m_Source;
            std::shared_ptr<const rt::FrameGate> m_Gate;
            FlattenedPageEventStorage<T> m_Storage;
            int m_MaxEventIndex = -1;
            std::vector<std::weak_ptr<rt::Channel<std::optional<Indexed>>>> m_Subscribers;
            rt::Job m_Job;
            bool m_Started = false, m_Closed = false, m_Finished = false;
        };
    }

    // cachedIn(scope): shares the paged data so that every collector (a list recreated after
    // navigation, a second list) sees what is already loaded instead of loading again. The
    // loading lives as long as the scope. Without it a Pager's PagingData can be collected once.
    template<typename T>
    PagingDataFlow<T> CachedIn(rt::Flow<PagingData<T>> flow, std::shared_ptr<rt::CoroutineScope> scope) {
        std::weak_ptr<rt::CoroutineScope> weakScope = scope;
        auto upstream = rt::Flow<PagingData<T>>::Create([flow = std::move(flow), weakScope](rt::FlowCollector<PagingData<T>> emit) -> rt::Task<> {
            struct Holder {
                std::shared_ptr<detail::CachedPageEvents<T>> current;
                ~Holder() { if (current) current->Close(); }
            };
            auto holder = std::make_shared<Holder>();
            co_await flow.Collect(rt::FlowCollector<PagingData<T>>([holder, emit, weakScope](PagingData<T> data) -> rt::Task<> {
                auto scope = weakScope.lock();
                auto cached = std::make_shared<detail::CachedPageEvents<T>>(data.Events(), scope ? scope->gate : nullptr);
                if (holder->current) holder->current->Close();
                holder->current = cached;
                co_await emit(PagingData<T>(cached->Downstream(), data.Ui(), data.Hints(), [cached] { return cached->CachedEvent(); }));
            }));
        });
        auto shared = rt::ShareIn(std::move(upstream), std::move(scope), rt::SharingStarted::Lazily(), 1);
        return PagingDataFlow<T>(shared.AsFlow(), [shared]() -> std::optional<PagingData<T>> {
            const auto& cache = shared.Hot_().ReplayCache();
            return cache.empty() ? std::nullopt : std::optional<PagingData<T>>(cache.front());
        });
    }
    template<typename T>
    PagingDataFlow<T> CachedIn(const PagingDataFlow<T>& flow, std::shared_ptr<rt::CoroutineScope> scope) { return CachedIn(flow.flow, std::move(scope)); }

}
