#pragma once

// PagingData and what flows inside it (paging-common: PagingData, PageEvent, TransformablePage,
// ViewportHint, HintHandler, PagingDataTransforms' map, flatMap and filter; separators are in
// Separators.h).

#include "kiln/paging/LoadState.h"

#include "kiln/runtime/Flow.h"

#include <kiln/Function.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::paging {

    namespace detail {

        // ViewportHint: Initial (no access yet) or Access (the item at pageOffset / indexInPage
        // was read), with how many presented items lie before and after it.
        struct ViewportHint {
            bool access = false;
            int pageOffset = 0, indexInPage = 0;
            int presentedItemsBefore = 0, presentedItemsAfter = 0;
            int originalPageOffsetFirst = 0, originalPageOffsetLast = 0;

            int PresentedItemsBeyondAnchor(LoadType type) const {
                if (type == LoadType::Refresh) throw std::invalid_argument("Cannot get presentedItems for loadType: REFRESH");
                return type == LoadType::Prepend ? presentedItemsBefore : presentedItemsAfter;
            }
            bool operator==(const ViewportHint&) const = default;
        };

        inline bool ShouldPrioritizeOver(const ViewportHint& hint, const std::optional<ViewportHint>& previous, LoadType type) {
            if (!previous) return true;
            if (!previous->access && hint.access) return true;
            if (!hint.access && previous->access) return false;
            if (hint.originalPageOffsetFirst != previous->originalPageOffsetFirst) return true;
            if (hint.originalPageOffsetLast != previous->originalPageOffsetLast) return true;
            if (previous->PresentedItemsBeyondAnchor(type) <= hint.PresentedItemsBeyondAnchor(type)) return false;
            return true;
        }

        // The latest hint in each direction, as flows that replay it.
        class HintHandler {
        public:
            const std::optional<ViewportHint>& LastAccessHint() const { return m_LastAccess; }
            rt::Flow<ViewportHint> HintFor(LoadType type) const {
                if (type == LoadType::Refresh) throw std::invalid_argument("invalid load type for hints");
                return (type == LoadType::Prepend ? m_PrependFlow : m_AppendFlow).AsFlow();
            }
            void ForceSetHint(LoadType type, const ViewportHint& hint) {
                if (type == LoadType::Refresh) throw std::invalid_argument("invalid load type for reset");
                Set(type, hint);
            }
            void ProcessHint(const ViewportHint& hint) {
                if (hint.access) m_LastAccess = hint;
                if (ShouldPrioritizeOver(hint, m_Prepend, LoadType::Prepend)) Set(LoadType::Prepend, hint);
                if (ShouldPrioritizeOver(hint, m_Append, LoadType::Append)) Set(LoadType::Append, hint);
            }

        private:
            void Set(LoadType type, const ViewportHint& hint) {
                (type == LoadType::Prepend ? m_Prepend : m_Append) = hint;
                (type == LoadType::Prepend ? m_PrependFlow : m_AppendFlow).TryEmit(hint);
            }
            std::optional<ViewportHint> m_Prepend, m_Append, m_LastAccess;
            rt::MutableSharedFlow<ViewportHint> m_PrependFlow{ 1 }, m_AppendFlow{ 1 };
        };

        // A page as presented: its items after transformations, and where they came from.
        template<typename T>
        struct TransformablePage {
            std::vector<int> originalPageOffsets;
            std::vector<T> data;
            int hintOriginalPageOffset = 0;
            std::optional<std::vector<int>> hintOriginalIndices;

            static TransformablePage Of(int originalPageOffset, std::vector<T> data) {
                return { { originalPageOffset }, std::move(data), originalPageOffset, std::nullopt };
            }
            ViewportHint ViewportHintFor(int index, int presentedBefore, int presentedAfter, int offsetFirst, int offsetLast) const {
                ViewportHint h;
                h.access = true;
                h.pageOffset = hintOriginalPageOffset;
                h.indexInPage = hintOriginalIndices && index >= 0 && index < int(hintOriginalIndices->size()) ? (*hintOriginalIndices)[std::size_t(index)] : index;
                h.presentedItemsBefore = presentedBefore;
                h.presentedItemsAfter = presentedAfter;
                h.originalPageOffsetFirst = offsetFirst;
                h.originalPageOffsetLast = offsetLast;
                return h;
            }
        };

        // PageEvent: Insert (a refresh, prepend or append of pages), Drop (pages removed from an
        // end), LoadStateUpdate, or StaticList (PagingData::From).
        template<typename T>
        struct PageEvent {
            enum class Kind : std::uint8_t { Insert, Drop, LoadStateUpdate, StaticList } kind = Kind::LoadStateUpdate;
            LoadType loadType = LoadType::Refresh;                 // Insert, Drop
            std::vector<TransformablePage<T>> pages;               // Insert
            int placeholdersBefore = -1, placeholdersAfter = -1;   // Insert, StaticList
            std::optional<LoadStates> sourceLoadStates;            // Insert, LoadStateUpdate; StaticList may have none
            std::optional<LoadStates> mediatorLoadStates;
            int minPageOffset = 0, maxPageOffset = 0, placeholdersRemaining = 0;   // Drop
            std::vector<T> data;                                   // StaticList

            int PageCount() const { return maxPageOffset - minPageOffset + 1; }
            bool IsRefresh() const { return kind == Kind::Insert && loadType == LoadType::Refresh; }

            static PageEvent Refresh(std::vector<TransformablePage<T>> pages, int before, int after, LoadStates source, std::optional<LoadStates> mediator = {}) {
                if (pages.empty()) throw std::invalid_argument("Cannot create a REFRESH Insert event with no TransformablePages as this could permanently stall pagination.");
                return Insert(LoadType::Refresh, std::move(pages), before, after, std::move(source), std::move(mediator));
            }
            static PageEvent Prepend(std::vector<TransformablePage<T>> pages, int before, LoadStates source) {
                return Insert(LoadType::Prepend, std::move(pages), before, -1, std::move(source), {});
            }
            static PageEvent Append(std::vector<TransformablePage<T>> pages, int after, LoadStates source) {
                return Insert(LoadType::Append, std::move(pages), -1, after, std::move(source), {});
            }
            static PageEvent Drop(LoadType type, int minPageOffset, int maxPageOffset, int placeholdersRemaining) {
                if (type == LoadType::Refresh) throw std::invalid_argument("Drop load type must be PREPEND or APPEND");
                PageEvent e;
                e.kind = Kind::Drop;
                e.loadType = type;
                e.minPageOffset = minPageOffset;
                e.maxPageOffset = maxPageOffset;
                e.placeholdersRemaining = placeholdersRemaining;
                if (e.PageCount() <= 0) throw std::invalid_argument("Drop count must be > 0, but was " + std::to_string(e.PageCount()));
                if (placeholdersRemaining < 0) throw std::invalid_argument("Invalid placeholdersRemaining " + std::to_string(placeholdersRemaining));
                return e;
            }
            static PageEvent LoadStateUpdate(LoadStates source, std::optional<LoadStates> mediator = {}) {
                PageEvent e;
                e.kind = Kind::LoadStateUpdate;
                e.sourceLoadStates = std::move(source);
                e.mediatorLoadStates = std::move(mediator);
                return e;
            }
            static PageEvent StaticList(std::vector<T> data, std::optional<LoadStates> source = {}, std::optional<LoadStates> mediator = {}, int before = 0, int after = 0) {
                PageEvent e;
                e.kind = Kind::StaticList;
                e.data = std::move(data);
                e.sourceLoadStates = std::move(source);
                e.mediatorLoadStates = std::move(mediator);
                e.placeholdersBefore = before;
                e.placeholdersAfter = after;
                return e;
            }

            // map: every item through fn.
            template<typename F> auto Map(const F& fn) const -> PageEvent<std::invoke_result_t<F, const T&>> {
                using R = std::invoke_result_t<F, const T&>;
                PageEvent<R> out = Shell<R>();
                for (const T& v : data) out.data.push_back(fn(v));
                for (const auto& p : pages) {
                    TransformablePage<R> q{ p.originalPageOffsets, {}, p.hintOriginalPageOffset, p.hintOriginalIndices };
                    for (const T& v : p.data) q.data.push_back(fn(v));
                    out.pages.push_back(std::move(q));
                }
                return out;
            }
            // flatMap: each item to any number of items, each remembering its source item for hints.
            template<typename F> auto FlatMap(const F& fn) const {
                using R = typename std::decay_t<std::invoke_result_t<F, const T&>>::value_type;
                PageEvent<R> out = Shell<R>();
                for (const T& v : data)
                    for (auto& r : fn(v)) out.data.push_back(std::move(r));
                for (const auto& p : pages) {
                    TransformablePage<R> q{ p.originalPageOffsets, {}, p.hintOriginalPageOffset, std::vector<int>{} };
                    for (std::size_t i = 0; i < p.data.size(); ++i) {
                        for (auto& r : fn(p.data[i])) q.data.push_back(std::move(r));
                        const int index = p.hintOriginalIndices ? (*p.hintOriginalIndices)[i] : int(i);
                        while (q.hintOriginalIndices->size() < q.data.size()) q.hintOriginalIndices->push_back(index);
                    }
                    out.pages.push_back(std::move(q));
                }
                return out;
            }
            // filter: the items the predicate accepts, remembering where each sat for hints.
            template<typename F> PageEvent Filter(const F& predicate) const {
                PageEvent out = Shell<T>();
                for (const T& v : data)
                    if (predicate(v)) out.data.push_back(v);
                for (const auto& p : pages) {
                    TransformablePage<T> q{ p.originalPageOffsets, {}, p.hintOriginalPageOffset, std::vector<int>{} };
                    for (std::size_t i = 0; i < p.data.size(); ++i) {
                        if (!predicate(p.data[i])) continue;
                        q.data.push_back(p.data[i]);
                        q.hintOriginalIndices->push_back(p.hintOriginalIndices ? (*p.hintOriginalIndices)[i] : int(i));
                    }
                    out.pages.push_back(std::move(q));
                }
                return out;
            }

        private:
            template<typename> friend struct PageEvent;
            static PageEvent Insert(LoadType type, std::vector<TransformablePage<T>> pages, int before, int after, LoadStates source, std::optional<LoadStates> mediator) {
                if (type != LoadType::Append && before < 0) throw std::invalid_argument("Prepend insert defining placeholdersBefore must be > 0, but was " + std::to_string(before));
                if (type != LoadType::Prepend && after < 0) throw std::invalid_argument("Append insert defining placeholdersAfter must be > 0, but was " + std::to_string(after));
                PageEvent e;
                e.kind = Kind::Insert;
                e.loadType = type;
                e.pages = std::move(pages);
                e.placeholdersBefore = before;
                e.placeholdersAfter = after;
                e.sourceLoadStates = std::move(source);
                e.mediatorLoadStates = std::move(mediator);
                return e;
            }
            // This event without its items.
            template<typename R> PageEvent<R> Shell() const {
                PageEvent<R> out;
                out.kind = static_cast<typename PageEvent<R>::Kind>(kind);
                out.loadType = loadType;
                out.placeholdersBefore = placeholdersBefore;
                out.placeholdersAfter = placeholdersAfter;
                out.sourceLoadStates = sourceLoadStates;
                out.mediatorLoadStates = mediatorLoadStates;
                out.minPageOffset = minPageOffset;
                out.maxPageOffset = maxPageOffset;
                out.placeholdersRemaining = placeholdersRemaining;
                return out;
            }
        };

        // What the presenting side calls back into the Pager with.
        struct UiReceiver {
            kiln::Function<void()> retry, refresh;
        };
        struct HintReceiver {
            kiln::Function<void(const ViewportHint&)> processHint;
        };
    }

    // One generation of paged data: the items loaded from one PagingSource, as they arrive. A
    // Pager's flow gives a new PagingData each time the source is invalidated or refreshed.
    // Present it with LazyPagingItems (or a PagingDataPresenter).
    template<typename T>
    class PagingData {
    public:
        PagingData(rt::Flow<detail::PageEvent<T>> flow, std::shared_ptr<const detail::UiReceiver> ui, std::shared_ptr<const detail::HintReceiver> hints,
                   kiln::Function<std::optional<detail::PageEvent<T>>()> cachedEvent = {})
            : m_Flow(std::move(flow)), m_Ui(std::move(ui)), m_Hints(std::move(hints)), m_CachedEvent(std::move(cachedEvent)) {}

        // PagingData.empty(): no items; with states, those load states.
        static PagingData Empty(std::optional<LoadStates> source = {}) { return From({}, std::move(source)); }
        // PagingData.from(data): a fixed list (previews, tests, data that is not paged).
        static PagingData From(std::vector<T> data, std::optional<LoadStates> source = {}, int placeholdersBefore = 0, int placeholdersAfter = 0) {
            using Event = detail::PageEvent<T>;
            auto flow = rt::FlowOf<Event>(Event::StaticList(data, source, {}, placeholdersBefore, placeholdersAfter));
            return PagingData(std::move(flow), nullptr, nullptr, [data, source, placeholdersBefore, placeholdersAfter]() -> std::optional<Event> {
                return Event::Refresh({ detail::TransformablePage<T>::Of(0, data) }, placeholdersBefore, placeholdersAfter, source.value_or(LoadStates::Idle()));
            });
        }

        // map { }: each item through fn.
        template<typename F> auto Map(F fn) const -> PagingData<std::invoke_result_t<F, const T&>> {
            using R = std::invoke_result_t<F, const T&>;
            auto f = std::make_shared<F>(std::move(fn));
            auto cached = m_CachedEvent;
            return PagingData<R>(rt::Map(m_Flow, [f](const detail::PageEvent<T>& e) { return e.Map(*f); }), m_Ui, m_Hints,
                                 [cached, f]() -> std::optional<detail::PageEvent<R>> {
                                     auto e = cached ? cached() : std::nullopt;
                                     return e ? std::optional(e->Map(*f)) : std::nullopt;
                                 });
        }
        // flatMap { }: each item to a list of items (none, one or more).
        template<typename F> auto FlatMap(F fn) const {
            using R = typename std::decay_t<std::invoke_result_t<F, const T&>>::value_type;
            auto f = std::make_shared<F>(std::move(fn));
            auto cached = m_CachedEvent;
            return PagingData<R>(rt::Map(m_Flow, [f](const detail::PageEvent<T>& e) { return e.FlatMap(*f); }), m_Ui, m_Hints,
                                 [cached, f]() -> std::optional<detail::PageEvent<R>> {
                                     auto e = cached ? cached() : std::nullopt;
                                     return e ? std::optional(e->FlatMap(*f)) : std::nullopt;
                                 });
        }
        // filter { }: only the items the predicate accepts.
        template<typename F> PagingData Filter(F predicate) const {
            auto f = std::make_shared<F>(std::move(predicate));
            auto cached = m_CachedEvent;
            return PagingData(rt::Map(m_Flow, [f](const detail::PageEvent<T>& e) { return e.Filter(*f); }), m_Ui, m_Hints,
                              [cached, f]() -> std::optional<detail::PageEvent<T>> {
                                  auto e = cached ? cached() : std::nullopt;
                                  return e ? std::optional(e->Filter(*f)) : std::nullopt;
                              });
        }

        const rt::Flow<detail::PageEvent<T>>& Events() const { return m_Flow; }
        const std::shared_ptr<const detail::UiReceiver>& Ui() const { return m_Ui; }
        const std::shared_ptr<const detail::HintReceiver>& Hints() const { return m_Hints; }
        // The data as one refresh, when it is already known (cachedIn, From).
        std::optional<detail::PageEvent<T>> CachedEvent() const { return m_CachedEvent ? m_CachedEvent() : std::nullopt; }

    private:
        rt::Flow<detail::PageEvent<T>> m_Flow;
        std::shared_ptr<const detail::UiReceiver> m_Ui;
        std::shared_ptr<const detail::HintReceiver> m_Hints;
        kiln::Function<std::optional<detail::PageEvent<T>>()> m_CachedEvent;
    };

}
