#pragma once

// Pager (paging-common: Pager, PageFetcher, PageFetcherSnapshot, PageFetcherSnapshotState): loads
// pages from a PagingSource as the presented list is read, and starts over with a new source
// when the current one is invalidated.
//
//   paging::Pager<int, Note> pager({ .pageSize = 30 }, [repo] { return std::make_shared<NotesSource>(repo); });
//   auto notes = paging::CachedIn(pager.Flow(), scope);   // keep; collect with CollectAsLazyPagingItems
//
// Everything runs on the main dispatcher; a source does its blocking work through
// rt::WithContext, a Database's Read, or an HTTP call.

#include "kiln/paging/PagingData.h"
#include "kiln/paging/PagingSource.h"

#include <cstdlib>
#include <map>
#include <memory>

namespace kiln::paging {

    namespace detail {

        // The pages one snapshot holds and what is known around them.
        template<typename K, typename V>
        struct SnapshotState {
            struct Loaded {
                Page<K, V> page;
                std::optional<K> loadKey;
            };
            explicit SnapshotState(const PagingConfig& c) : config(c) {}

            PagingConfig config;
            std::vector<Loaded> pages;
            int initialPageIndex = 0;
            int rawPlaceholdersBefore = 0, rawPlaceholdersAfter = 0;
            // Bumped when pages are dropped from that end: loads of older generations are discarded.
            rt::MutableStateFlow<int> prependGeneration{ 0 }, appendGeneration{ 0 };
            std::map<LoadType, ViewportHint> failedHints;
            LoadStates sourceLoadStates{ LoadState::Loading(), LoadState::NotLoading(false), LoadState::NotLoading(false) };

            int PlaceholdersBefore() const { return config.enablePlaceholders ? rawPlaceholdersBefore : 0; }
            int PlaceholdersAfter() const { return config.enablePlaceholders ? rawPlaceholdersAfter : 0; }
            void SetPlaceholdersBefore(int v) { rawPlaceholdersBefore = v == kCountUndefined ? 0 : v; }
            void SetPlaceholdersAfter(int v) { rawPlaceholdersAfter = v == kCountUndefined ? 0 : v; }
            int StorageCount() const {
                int n = 0;
                for (const auto& p : pages) n += int(p.page.data.size());
                return n;
            }
            int GenerationId(LoadType type) const {
                if (type == LoadType::Refresh) throw std::invalid_argument("Cannot get loadId for loadType: REFRESH");
                return type == LoadType::Prepend ? prependGeneration.Value() : appendGeneration.Value();
            }

            PageEvent<V> ToPageEvent(const Page<K, V>& page, LoadType type) const {
                const int sourcePageIndex = type == LoadType::Refresh ? 0 : type == LoadType::Prepend ? -initialPageIndex : int(pages.size()) - initialPageIndex - 1;
                std::vector<TransformablePage<V>> out{ TransformablePage<V>::Of(sourcePageIndex, page.data) };
                switch (type) {
                    case LoadType::Refresh: return PageEvent<V>::Refresh(std::move(out), PlaceholdersBefore(), PlaceholdersAfter(), sourceLoadStates);
                    case LoadType::Prepend: return PageEvent<V>::Prepend(std::move(out), PlaceholdersBefore(), sourceLoadStates);
                    default: return PageEvent<V>::Append(std::move(out), PlaceholdersAfter(), sourceLoadStates);
                }
            }

            // False when the load belongs to a generation that was dropped meanwhile.
            [[nodiscard]] bool Insert(int loadId, LoadType type, const Page<K, V>& page, const std::optional<K>& loadKey) {
                switch (type) {
                    case LoadType::Refresh:
                        if (!pages.empty()) throw std::logic_error("cannot receive multiple init calls");
                        if (loadId != 0) throw std::logic_error("init loadId must be the initial value, 0");
                        pages.push_back({ page, loadKey });
                        initialPageIndex = 0;
                        SetPlaceholdersAfter(page.itemsAfter);
                        SetPlaceholdersBefore(page.itemsBefore);
                        break;
                    case LoadType::Prepend:
                        if (pages.empty()) throw std::logic_error("should've received an init before prepend");
                        if (loadId != prependGeneration.Value()) return false;
                        pages.insert(pages.begin(), { page, loadKey });
                        ++initialPageIndex;
                        SetPlaceholdersBefore(page.itemsBefore == kCountUndefined ? std::max(0, PlaceholdersBefore() - int(page.data.size())) : page.itemsBefore);
                        failedHints.erase(LoadType::Prepend);
                        break;
                    case LoadType::Append:
                        if (pages.empty()) throw std::logic_error("should've received an init before append");
                        if (loadId != appendGeneration.Value()) return false;
                        pages.push_back({ page, loadKey });
                        SetPlaceholdersAfter(page.itemsAfter == kCountUndefined ? std::max(0, PlaceholdersAfter() - int(page.data.size())) : page.itemsAfter);
                        failedHints.erase(LoadType::Append);
                        break;
                }
                return true;
            }

            void Drop(const PageEvent<V>& event) {
                if (event.PageCount() > int(pages.size())) {
                    throw std::logic_error("invalid drop count. have " + std::to_string(pages.size()) + " but wanted to drop " + std::to_string(event.PageCount()));
                }
                failedHints.erase(event.loadType);
                sourceLoadStates = sourceLoadStates.Modify(event.loadType, LoadState::NotLoading(false));
                if (event.loadType == LoadType::Prepend) {
                    pages.erase(pages.begin(), pages.begin() + event.PageCount());
                    initialPageIndex -= event.PageCount();
                    SetPlaceholdersBefore(event.placeholdersRemaining);
                    prependGeneration.SetValue(prependGeneration.Value() + 1);
                } else {
                    pages.erase(pages.end() - event.PageCount(), pages.end());
                    SetPlaceholdersAfter(event.placeholdersRemaining);
                    appendGeneration.SetValue(appendGeneration.Value() + 1);
                }
            }

            // The pages to drop from `type`'s end to get back under maxSize, keeping what the
            // hint's position needs.
            std::optional<PageEvent<V>> DropEventOrNull(LoadType type, const ViewportHint& hint) const {
                if (config.maxSize == kMaxSizeUnbounded) return std::nullopt;
                if (pages.size() <= 2) return std::nullopt;
                const int storage = StorageCount();
                if (storage <= config.maxSize) return std::nullopt;
                if (type == LoadType::Refresh) throw std::invalid_argument("Drop LoadType must be PREPEND or APPEND");
                const int lastIndex = int(pages.size()) - 1;
                int pagesToDrop = 0, itemsToDrop = 0;
                while (int(pages.size()) - pagesToDrop > 2 && storage - itemsToDrop > config.maxSize) {
                    const int pageSize = int(pages[std::size_t(type == LoadType::Prepend ? pagesToDrop : lastIndex - pagesToDrop)].page.data.size());
                    const int itemsAfterDrop = (type == LoadType::Prepend ? hint.presentedItemsBefore : hint.presentedItemsAfter) - itemsToDrop - pageSize;
                    if (itemsAfterDrop < config.prefetchDistance) break;
                    itemsToDrop += pageSize;
                    ++pagesToDrop;
                }
                if (pagesToDrop == 0) return std::nullopt;
                return PageEvent<V>::Drop(type,
                    type == LoadType::Prepend ? -initialPageIndex : lastIndex - initialPageIndex - (pagesToDrop - 1),
                    type == LoadType::Prepend ? (pagesToDrop - 1) - initialPageIndex : lastIndex - initialPageIndex,
                    !config.enablePlaceholders ? 0 : type == LoadType::Prepend ? PlaceholdersBefore() + itemsToDrop : PlaceholdersAfter() + itemsToDrop);
            }

            PagingState<K, V> CurrentPagingState(const std::optional<ViewportHint>& hint) const {
                PagingState<K, V> state;
                for (const auto& p : pages) {
                    state.pages.push_back(p.page);
                    state.loadKeys.push_back(p.loadKey);
                }
                state.config = config;
                state.leadingPlaceholderCount = PlaceholdersBefore();
                if (hint) {
                    int anchor = PlaceholdersBefore();
                    const int first = -initialPageIndex;
                    const int last = int(pages.size()) - 1 - initialPageIndex;
                    for (int offset = first; offset < hint->pageOffset; ++offset)
                        anchor += offset > last ? config.pageSize : int(pages[std::size_t(offset + initialPageIndex)].page.data.size());
                    if (hint->presentedItemsAfter < 0) {
                        anchor += int(pages[std::size_t(hint->pageOffset + initialPageIndex)].page.data.size()) - 1 + std::abs(hint->presentedItemsAfter);
                    } else if (hint->presentedItemsBefore < 0) {
                        anchor += hint->presentedItemsBefore;
                    } else {
                        anchor += hint->indexInPage;
                    }
                    if (hint->pageOffset < first) anchor -= config.pageSize;
                    state.anchorPosition = anchor;
                }
                return state;
            }
        };

        // One generation: a PagingSource and the pages loaded from it.
        template<typename K, typename V>
        class PageFetcherSnapshot : public std::enable_shared_from_this<PageFetcherSnapshot<K, V>> {
        public:
            using Self = std::shared_ptr<PageFetcherSnapshot>;

            PageFetcherSnapshot(std::optional<K> initialKey, std::shared_ptr<PagingSource<K, V>> source, const PagingConfig& config,
                                rt::MutableStateFlow<int> retry, int initialLoadSize, kiln::Function<void()> jumpCallback)
                : m_InitialKey(std::move(initialKey)), m_Source(std::move(source)), m_Config(config), m_Retry(std::move(retry)),
                  m_InitialLoadSize(initialLoadSize), m_JumpCallback(std::move(jumpCallback)), m_State(config) {
                if (config.jumpThreshold != kCountUndefined && !m_Source->JumpingSupported()) {
                    throw std::invalid_argument("PagingConfig.jumpThreshold was set, but the associated PagingSource has not marked "
                                                "support for jumps by overriding PagingSource.jumpingSupported to true.");
                }
            }

            const std::shared_ptr<PagingSource<K, V>>& Source() const { return m_Source; }

            // The events of this generation; collected once (cachedIn shares it).
            rt::Flow<PageEvent<V>> PageEventFlow() {
                return rt::Flow<PageEvent<V>>::Create([self = this->shared_from_this()](rt::FlowCollector<PageEvent<V>> emit) -> rt::Task<> {
                    co_await emit(PageEvent<V>::LoadStateUpdate(self->m_State.sourceLoadStates));
                    if (self->m_Collected) {
                        throw std::logic_error("Attempt to collect twice from pageEventFlow, which is an illegal operation. "
                                               "Did you forget to call CachedIn(flow, scope)?");
                    }
                    self->m_Collected = true;
                    struct Stop {
                        Self self;
                        ~Stop() { self->CancelJobs(); }
                    } stop{ self };
                    if (!self->m_Closed) {
                        self->AddJob(RetryLoop(self));
                        self->AddJob(InitialLoad(self));
                    }
                    while (true) {
                        auto event = co_await self->m_Events->Next();
                        if (!event) break;
                        co_await emit(std::move(*event));
                    }
                });
            }

            void ProcessHint(const ViewportHint& hint) { m_Hints.ProcessHint(hint); }

            // Ends the event flow and its loads.
            void Close() {
                if (m_Closed) return;
                m_Closed = true;
                CancelJobs();
                m_Events->Close();
            }

            PagingState<K, V> CurrentPagingState() const { return m_State.CurrentPagingState(m_Hints.LastAccessHint()); }

            // Pager.append() / prepend(): a hint at that end of what is loaded.
            void ForceSetHint(LoadType type) {
                if (type == LoadType::Refresh) throw std::invalid_argument("Called for REFRESH but this should only be called for either APPEND or PREPEND loads.");
                const auto& st = m_State;
                if (st.pages.empty()) return;
                ViewportHint hint;
                hint.access = true;
                hint.originalPageOffsetFirst = -st.initialPageIndex;
                hint.originalPageOffsetLast = int(st.pages.size()) - st.initialPageIndex - 1;
                if (type == LoadType::Append) {
                    hint.pageOffset = hint.originalPageOffsetLast;
                    hint.indexInPage = int(st.pages.back().page.data.size()) - 1;
                    hint.presentedItemsBefore = st.StorageCount() - 1;
                    hint.presentedItemsAfter = 0;
                } else {
                    hint.pageOffset = hint.originalPageOffsetFirst;
                    hint.indexInPage = 0;
                    hint.presentedItemsBefore = 0;
                    hint.presentedItemsAfter = st.StorageCount() - 1;
                }
                m_Hints.ForceSetHint(type, hint);
            }

        private:
            void AddJob(rt::Task<> task) {
                // The job may add more jobs as it starts, so it gets its slot afterwards.
                rt::Job job = rt::Launch(rt::Dispatcher::Main(), std::move(task));
                std::erase_if(m_Jobs, [](const rt::Job& j) { return j.Done(); });
                if (!m_Closed && !job.Done()) m_Jobs.push_back(std::move(job));
            }
            void CancelJobs() {
                auto jobs = std::move(m_Jobs);
                m_Jobs.clear();
            }
            void Send(PageEvent<V> event) { m_Events->TrySend(std::move(event)); }

            static rt::Task<> InitialLoad(Self self) {
                co_await self->DoInitialLoad();
                if (!self->m_State.sourceLoadStates.refresh.IsError()) self->StartConsumingHints();
            }

            // retry(): loads again whatever is in the error state.
            static rt::Task<> RetryLoop(Self self) {
                auto first = std::make_shared<bool>(true);
                co_await self->m_Retry.Collect(rt::FlowCollector<int>([self, first](int) -> rt::Task<> {
                    if (std::exchange(*first, false)) co_return;   // the value from before this generation
                    const LoadStates states = self->m_State.sourceLoadStates;
                    for (const LoadType type : { LoadType::Refresh, LoadType::Prepend, LoadType::Append }) {
                        if (!states.Get(type).IsError()) continue;
                        if (type == LoadType::Refresh) {
                            co_await self->DoInitialLoad();
                            if (!self->m_State.sourceLoadStates.refresh.IsError()) self->StartConsumingHints();
                        } else {
                            self->SetLoading(type);
                            auto failed = self->m_State.failedHints.find(type);
                            if (failed == self->m_State.failedHints.end()) throw std::logic_error("Cannot retry APPEND / PREPEND load on PagingSource without ViewportHint");
                            const ViewportHint hint = failed->second;
                            self->m_Hints.ForceSetHint(type, hint);
                        }
                    }
                }));
            }

            void StartConsumingHints() {
                Self self = this->shared_from_this();
                if (m_Config.jumpThreshold != kCountUndefined) AddJob(WatchForJump(self));
                AddJob(ConsumeHints(self, LoadType::Prepend));
                AddJob(ConsumeHints(self, LoadType::Append));
            }

            // An access further past the loaded items than the jump threshold refreshes around it.
            static rt::Task<> WatchForJump(Self self) {
                const int threshold = self->m_Config.jumpThreshold;
                auto hints = rt::Merge<ViewportHint>({ self->m_Hints.HintFor(LoadType::Append), self->m_Hints.HintFor(LoadType::Prepend) });
                const auto jump = co_await rt::FirstOrNull(hints, [threshold](const ViewportHint& h) {
                    return h.presentedItemsBefore * -1 > threshold || h.presentedItemsAfter * -1 > threshold;
                });
                if (jump && self->m_JumpCallback) self->m_JumpCallback();
            }

            struct GenerationalHint {
                int generationId = 0;
                ViewportHint hint;
            };

            // collectAsGenerationalViewportHints: loads for the most pressing hint of the current
            // generation, one load at a time.
            static rt::Task<> ConsumeHints(Self self, LoadType type) {
                const auto& generations = type == LoadType::Prepend ? self->m_State.prependGeneration : self->m_State.appendGeneration;
                rt::Flow<GenerationalHint> hints = rt::FlatMapLatest(generations.AsFlow(), [self, type](const int& generationId) -> rt::Flow<GenerationalHint> {
                    auto& st = self->m_State;
                    const LoadState& state = st.sourceLoadStates.Get(type);
                    if (state == LoadState::NotLoading(true)) return rt::EmptyFlow<GenerationalHint>();
                    if (!state.IsError()) st.sourceLoadStates = st.sourceLoadStates.Modify(type, LoadState::NotLoading(false));
                    // After a drop the hint that caused it is stale: wait for the next one.
                    return rt::Map(rt::Drop(self->m_Hints.HintFor(type), generationId == 0 ? 0 : 1),
                                   [generationId](const ViewportHint& h) { return GenerationalHint{ generationId, h }; });
                });
                auto previous = std::make_shared<std::optional<GenerationalHint>>();
                rt::Flow<GenerationalHint> reduced = rt::Map(hints, [previous, type](const GenerationalHint& next) {
                    const bool take = !*previous || next.generationId > (*previous)->generationId ||
                        (next.generationId == (*previous)->generationId && ShouldPrioritizeOver(next.hint, (*previous)->hint, type));
                    if (take) *previous = next;
                    return **previous;
                });
                co_await rt::Conflate(reduced).Collect(rt::FlowCollector<GenerationalHint>([self, type](GenerationalHint g) -> rt::Task<> {
                    co_await self->DoLoad(type, g);
                }));
            }

            LoadParams<K> ParamsFor(LoadType type, const std::optional<K>& key) const {
                return { type, key, type == LoadType::Refresh ? m_InitialLoadSize : m_Config.pageSize, m_Config.enablePlaceholders };
            }

            rt::Task<> DoInitialLoad() {
                Self self = this->shared_from_this();
                SetLoading(LoadType::Refresh);
                LoadResult<K, V> result = co_await m_Source->Load(ParamsFor(LoadType::Refresh, m_InitialKey));
                if (auto* page = std::get_if<Page<K, V>>(&result)) {
                    auto& st = m_State;
                    const bool applied = st.Insert(0, LoadType::Refresh, *page, m_InitialKey);
                    st.sourceLoadStates.refresh = LoadState::NotLoading(false);
                    if (!page->prevKey) st.sourceLoadStates.prepend = LoadState::NotLoading(true);
                    if (!page->nextKey) st.sourceLoadStates.append = LoadState::NotLoading(true);
                    if (applied) Send(st.ToPageEvent(*page, LoadType::Refresh));
                } else if (auto* error = std::get_if<LoadError>(&result)) {
                    SetError(LoadType::Refresh, LoadState::Error(error->error));
                } else {
                    OnInvalidLoad();
                }
            }

            rt::Task<> DoLoad(LoadType type, GenerationalHint generational) {
                if (type == LoadType::Refresh) throw std::invalid_argument("Use doInitialLoad for LoadType == REFRESH");
                Self self = this->shared_from_this();
                auto& st = m_State;
                const ViewportHint& hint = generational.hint;
                // Items already loaded beyond the hint's page, in the direction of the load.
                int itemsLoaded = 0;
                const int lastIndex = int(st.pages.size()) - 1;
                if (type == LoadType::Prepend) {
                    int firstPageIndex = st.initialPageIndex + hint.originalPageOffsetFirst - 1;
                    if (firstPageIndex > lastIndex) {
                        itemsLoaded += m_Config.pageSize * (firstPageIndex - lastIndex);
                        firstPageIndex = lastIndex;
                    }
                    for (int i = 0; i <= firstPageIndex; ++i) itemsLoaded += int(st.pages[std::size_t(i)].page.data.size());
                } else {
                    int lastPageIndex = st.initialPageIndex + hint.originalPageOffsetLast + 1;
                    if (lastPageIndex < 0) {
                        itemsLoaded += m_Config.pageSize * -lastPageIndex;
                        lastPageIndex = 0;
                    }
                    for (int i = lastPageIndex; i <= lastIndex; ++i) itemsLoaded += int(st.pages[std::size_t(i)].page.data.size());
                }

                std::optional<K> loadKey = NextLoadKeyOrNull(type, generational.generationId, hint.PresentedItemsBeyondAnchor(type) + itemsLoaded);
                if (loadKey) SetLoading(type);
                bool endOfPaginationReached = false;
                while (loadKey) {
                    const LoadParams<K> params = ParamsFor(type, loadKey);
                    LoadResult<K, V> result = co_await m_Source->Load(params);
                    if (auto* error = std::get_if<LoadError>(&result)) {
                        SetError(type, LoadState::Error(error->error));
                        st.failedHints[type] = hint;
                        co_return;
                    }
                    if (std::holds_alternative<LoadInvalid>(result)) {
                        OnInvalidLoad();
                        co_return;
                    }
                    const Page<K, V>& page = std::get<Page<K, V>>(result);
                    const std::optional<K>& nextKey = type == LoadType::Prepend ? page.prevKey : page.nextKey;
                    if (!m_Source->KeyReuseSupported() && nextKey == loadKey) {
                        throw std::logic_error(std::string("The same value was passed as the ") + (type == LoadType::Prepend ? "prevKey" : "nextKey") +
                            " in two sequential Pages loaded from a PagingSource. Re-using load keys in PagingSource is often an error, "
                            "and must be explicitly enabled by overriding PagingSource.keyReuseSupported.");
                    }
                    if (!st.Insert(generational.generationId, type, page, loadKey)) break;   // dropped meanwhile
                    itemsLoaded += int(page.data.size());
                    if (!nextKey) endOfPaginationReached = true;

                    // Over maxSize: drop from the other end.
                    const LoadType dropType = type == LoadType::Prepend ? LoadType::Append : LoadType::Prepend;
                    if (auto drop = st.DropEventOrNull(dropType, hint)) {
                        st.Drop(*drop);
                        Send(*drop);
                    }
                    loadKey = NextLoadKeyOrNull(type, generational.generationId, hint.PresentedItemsBeyondAnchor(type) + itemsLoaded);
                    if (!loadKey && !st.sourceLoadStates.Get(type).IsError()) {
                        st.sourceLoadStates = st.sourceLoadStates.Modify(type, LoadState::NotLoading(endOfPaginationReached));
                    }
                    Send(st.ToPageEvent(page, type));
                }
            }

            void SetLoading(LoadType type) {
                if (m_State.sourceLoadStates.Get(type) == LoadState::Loading()) return;
                m_State.sourceLoadStates = m_State.sourceLoadStates.Modify(type, LoadState::Loading());
                Send(PageEvent<V>::LoadStateUpdate(m_State.sourceLoadStates));
            }
            void SetError(LoadType type, const LoadState& error) {
                if (m_State.sourceLoadStates.Get(type) == error) return;
                m_State.sourceLoadStates = m_State.sourceLoadStates.Modify(type, error);
                Send(PageEvent<V>::LoadStateUpdate(m_State.sourceLoadStates));
            }

            // The key of the next page in that direction, while the hint's position still has
            // fewer than prefetchDistance loaded items beyond it.
            std::optional<K> NextLoadKeyOrNull(LoadType type, int generationId, int presentedItemsBeyondAnchor) const {
                const auto& st = m_State;
                if (generationId != st.GenerationId(type)) return std::nullopt;
                if (st.sourceLoadStates.Get(type).IsError()) return std::nullopt;
                if (presentedItemsBeyondAnchor >= m_Config.prefetchDistance) return std::nullopt;
                return type == LoadType::Prepend ? st.pages.front().page.prevKey : st.pages.back().page.nextKey;
            }

            void OnInvalidLoad() {
                Self self = this->shared_from_this();
                Close();
                m_Source->Invalidate();
            }

            std::optional<K> m_InitialKey;
            std::shared_ptr<PagingSource<K, V>> m_Source;
            PagingConfig m_Config;
            rt::MutableStateFlow<int> m_Retry;
            int m_InitialLoadSize;
            kiln::Function<void()> m_JumpCallback;
            HintHandler m_Hints;
            SnapshotState<K, V> m_State;
            std::shared_ptr<rt::Channel<PageEvent<V>>> m_Events = std::make_shared<rt::Channel<PageEvent<V>>>(std::size_t(1) << 20);
            std::vector<rt::Job> m_Jobs;
            bool m_Collected = false, m_Closed = false;
        };

        // The generations of a Pager: a new snapshot per refresh or invalidation.
        template<typename K, typename V>
        class PageFetcher : public std::enable_shared_from_this<PageFetcher<K, V>> {
        public:
            using Source = PagingSource<K, V>;
            using Snapshot = PageFetcherSnapshot<K, V>;

            PageFetcher(kiln::Function<std::shared_ptr<Source>()> factory, std::optional<K> initialKey, const PagingConfig& config)
                : m_Factory(std::move(factory)), m_InitialKey(std::move(initialKey)), m_Config(config) {}

            rt::Flow<PagingData<V>> Flow() {
                return rt::Flow<PagingData<V>>::Create([self = this->shared_from_this()](rt::FlowCollector<PagingData<V>> emit) -> rt::Task<> {
                    auto generation = std::make_shared<std::optional<Generation>>();
                    struct CloseLast {
                        std::shared_ptr<std::optional<Generation>> g;
                        ~CloseLast() { if (*g) (*g)->snapshot->Close(); }
                    } closeLast{ generation };
                    co_await self->Handle(Request{ LoadType::Refresh, RefreshType::Initial, {} }, generation, emit);
                    co_await self->m_Requests.Collect(rt::FlowCollector<Sent>([self, generation, emit](Sent sent) -> rt::Task<> {
                        if (sent.request) co_await self->Handle(*sent.request, generation, emit);
                    }));
                });
            }

            void Load(LoadType type) {
                if (type == LoadType::Refresh) throw std::invalid_argument("Load only applies to APPEND or PREPEND");
                SendRequest({ type, RefreshType::Initial, {} });
            }
            void RefreshAll() { SendRequest({ LoadType::Refresh, RefreshType::All, {} }); }
            void Refresh() { SendRequest({ LoadType::Refresh, RefreshType::Anchor, {} }); }
            void RefreshItem(kiln::Function<bool(const V&)> isItem) { SendRequest({ LoadType::Refresh, RefreshType::Item, std::move(isItem) }); }
            void Retry() { m_Retry.SetValue(m_Retry.Value() + 1); }

        private:
            enum class RefreshType : std::uint8_t { Initial, All, Anchor, Item };
            struct Request {
                LoadType type = LoadType::Refresh;
                RefreshType refresh = RefreshType::Initial;
                kiln::Function<bool(const V&)> isItem;
            };
            // ConflatedEventBus: the latest request, told apart by its number.
            struct Sent {
                int number = 0;
                std::optional<Request> request;
                bool operator==(const Sent& o) const { return number == o.number; }
            };
            struct Generation {
                std::shared_ptr<Snapshot> snapshot;
                std::optional<PagingState<K, V>> cachedInitialState;
            };

            void SendRequest(Request request) { m_Requests.SetValue(Sent{ m_Requests.Value().number + 1, std::move(request) }); }
            void Invalidate() { SendRequest({ LoadType::Refresh, RefreshType::Anchor, {} }); }

            rt::Task<> Handle(Request request, std::shared_ptr<std::optional<Generation>> generation, rt::FlowCollector<PagingData<V>> emit) {
                auto self = this->shared_from_this();
                if (request.type != LoadType::Refresh) {
                    if (!*generation) throw std::logic_error("Append or Prepend request should be sent after a Refresh.");
                    (*generation)->snapshot->ForceSetHint(request.type);
                    co_return;   // the same generation: nothing new to emit
                }
                std::weak_ptr<PageFetcher> weak = self;
                kiln::Function<void()> jump = [weak] { if (auto f = weak.lock()) f->Refresh(); };
                std::shared_ptr<Snapshot> snapshot;
                if (!*generation) {
                    snapshot = std::make_shared<Snapshot>(m_InitialKey, NewPagingSource(nullptr), m_Config, m_Retry, m_Config.initialLoadSize, std::move(jump));
                    *generation = Generation{ snapshot, std::nullopt };
                } else {
                    const Generation current = **generation;
                    auto source = NewPagingSource(current.snapshot->Source());
                    PagingState<K, V> state = current.snapshot->CurrentPagingState();
                    // An invalidation before the new generation loaded anything keeps the state
                    // the previous one started from.
                    if (state.pages.empty() && current.cachedInitialState && !current.cachedInitialState->pages.empty()) state = *current.cachedInitialState;
                    if (!state.anchorPosition && current.cachedInitialState && current.cachedInitialState->anchorPosition) state = *current.cachedInitialState;

                    std::optional<K> refreshKey;
                    int refreshSize = m_Config.initialLoadSize;
                    if (request.refresh == RefreshType::Item && !state.pages.empty()) {
                        // Reload from the page that holds the item.
                        std::size_t found = state.pages.size();
                        for (std::size_t i = 0; i < state.pages.size() && found == state.pages.size(); ++i)
                            if (std::any_of(state.pages[i].data.begin(), state.pages[i].data.end(), [&](const V& v) { return request.isItem(v); })) found = i;
                        if (found == state.pages.size()) throw std::invalid_argument("Invalid Refresh item. Item not found in the loaded items.");
                        refreshKey = state.loadKeys[found];
                    } else if (request.refresh == RefreshType::All && !state.pages.empty()) {
                        // Reload everything that is loaded, from the first page's key.
                        refreshKey = state.loadKeys.front();
                        refreshSize = 0;
                        for (const auto& p : state.pages) refreshSize += int(p.data.size());
                        refreshSize = std::max(refreshSize, 1);
                    } else {
                        refreshKey = source->GetRefreshKey(state);
                    }
                    current.snapshot->Close();
                    snapshot = std::make_shared<Snapshot>(std::move(refreshKey), std::move(source), m_Config, m_Retry, refreshSize, std::move(jump));
                    *generation = Generation{ snapshot, std::move(state) };
                }
                auto ui = std::make_shared<UiReceiver>();
                ui->retry = [weak] { if (auto f = weak.lock()) f->Retry(); };
                ui->refresh = [weak] { if (auto f = weak.lock()) f->Refresh(); };
                auto hints = std::make_shared<HintReceiver>();
                hints->processHint = [snapshot](const ViewportHint& hint) { snapshot->ProcessHint(hint); };
                co_await emit(PagingData<V>(snapshot->PageEventFlow(), std::move(ui), std::move(hints)));
            }

            std::shared_ptr<Source> NewPagingSource(const std::shared_ptr<Source>& current) {
                std::shared_ptr<Source> source = m_Factory();
                if (source == current) {
                    throw std::logic_error("An instance of PagingSource was re-used when Pager expected to create a new instance. "
                                           "Ensure that the pagingSourceFactory passed to Pager always returns a new instance of PagingSource.");
                }
                std::weak_ptr<PageFetcher> weak = this->weak_from_this();
                if (current) current->UnregisterInvalidatedCallback(m_InvalidatedCallback);
                m_InvalidatedCallback = source->RegisterInvalidatedCallback([weak] { if (auto f = weak.lock()) f->Invalidate(); });
                if (current) current->Invalidate();
                return source;
            }

            kiln::Function<std::shared_ptr<Source>()> m_Factory;
            std::optional<K> m_InitialKey;
            PagingConfig m_Config;
            rt::MutableStateFlow<Sent> m_Requests{ Sent{} };
            rt::MutableStateFlow<int> m_Retry{ 0 };
            int m_InvalidatedCallback = 0;
        };
    }

    // Pager(config, initialKey) { pagingSourceFactory }: the factory returns a new source each
    // time (after an invalidation the old one is not used again).
    template<typename Key, typename Value>
    class Pager {
    public:
        Pager(PagingConfig config, kiln::Function<std::shared_ptr<PagingSource<Key, Value>>()> pagingSourceFactory, std::optional<Key> initialKey = {})
            : m_Fetcher(std::make_shared<detail::PageFetcher<Key, Value>>(std::move(pagingSourceFactory), std::move(initialKey), config.Resolved())) {}

        // A PagingData per generation. Collecting starts the first load; share it with CachedIn.
        rt::Flow<PagingData<Value>> Flow() const { return m_Fetcher->Flow(); }
        // Loads the next page after / before what is loaded.
        void Append() { m_Fetcher->Load(LoadType::Append); }
        void Prepend() { m_Fetcher->Load(LoadType::Prepend); }
        // Loads everything that is loaded again, from a new source.
        void Refresh() { m_Fetcher->RefreshAll(); }
        // Loads again from the page that holds the item.
        void Refresh(const Value& item) requires std::equality_comparable<Value> {
            m_Fetcher->RefreshItem([item](const Value& v) { return v == item; });
        }
        // Loads again what failed.
        void Retry() { m_Fetcher->Retry(); }

    private:
        std::shared_ptr<detail::PageFetcher<Key, Value>> m_Fetcher;
    };

}
