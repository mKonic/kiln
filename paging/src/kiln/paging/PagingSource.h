#pragma once

// PagingSource and PagingState (paging-common): where pages come from.
//
//   class NotesSource : public paging::PagingSource<int, Note> {
//       rt::Task<Result> Load(Params params) override {
//           const int page = params.key.value_or(0);
//           std::vector<Note> notes = co_await api.Notes(page, params.loadSize);
//           co_return Page{ .data = notes, .prevKey = page > 0 ? std::optional(page - 1) : std::nullopt,
//                           .nextKey = notes.empty() ? std::nullopt : std::optional(page + 1) };
//       }
//       std::optional<int> GetRefreshKey(const State& state) override {
//           if (!state.anchorPosition) return std::nullopt;
//           const auto* page = state.ClosestPageToPosition(*state.anchorPosition);
//           return page && page->prevKey ? std::optional(*page->prevKey + 1) : std::nullopt;
//       }
//   };

#include "kiln/paging/LoadState.h"

#include "kiln/runtime/Task.h"

#include <kiln/Function.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace kiln::paging {

    // LoadParams: Refresh (key may be absent), Append or Prepend (key present).
    template<typename Key>
    struct LoadParams {
        LoadType type = LoadType::Refresh;
        std::optional<Key> key;
        int loadSize = 0;
        bool placeholdersEnabled = false;
    };

    // LoadResult.Page
    template<typename Key, typename Value>
    struct Page {
        std::vector<Value> data;
        // The keys of the pages before and after this one; absent at that end of the data.
        std::optional<Key> prevKey, nextKey;
        // Items before and after this page that are not loaded, when known (placeholders).
        int itemsBefore = kCountUndefined, itemsAfter = kCountUndefined;
        bool operator==(const Page&) const = default;
        // iterator: over the items.
        auto begin() const { return data.begin(); }
        auto end() const { return data.end(); }
    };
    // LoadResult.Error
    struct LoadError {
        std::exception_ptr error;
        template<typename E> static LoadError Of(E e) { return { std::make_exception_ptr(std::move(e)) }; }
    };
    // LoadResult.Invalid: the source is no longer valid; a new one is made.
    struct LoadInvalid {};
    template<typename Key, typename Value>
    using LoadResult = std::variant<Page<Key, Value>, LoadError, LoadInvalid>;

    // A snapshot of what is loaded and the last accessed position, for GetRefreshKey.
    template<typename Key, typename Value>
    struct PagingState {
        std::vector<Page<Key, Value>> pages;
        // The last accessed index in the list, placeholders included.
        std::optional<int> anchorPosition;
        PagingConfig config;
        int leadingPlaceholderCount = 0;
        std::vector<std::optional<Key>> loadKeys;   // the key each page was loaded with

        bool IsEmpty() const {
            return std::all_of(pages.begin(), pages.end(), [](const auto& p) { return p.data.empty(); });
        }
        const Value* FirstItemOrNull() const {
            for (const auto& p : pages)
                if (!p.data.empty()) return &p.data.front();
            return nullptr;
        }
        const Value* LastItemOrNull() const {
            for (auto it = pages.rbegin(); it != pages.rend(); ++it)
                if (!it->data.empty()) return &it->data.back();
            return nullptr;
        }
        // The loaded item nearest the position.
        const Value* ClosestItemToPosition(int position) const {
            if (IsEmpty()) return nullptr;
            const auto [pageIndex, index] = PagedIndices(position);
            if (index < 0) return FirstItemOrNull();
            if (pageIndex == int(pages.size()) - 1 && index > int(pages.back().data.size()) - 1) return LastItemOrNull();
            return &pages[std::size_t(pageIndex)].data[std::size_t(index)];
        }
        // The loaded page nearest the position.
        const Page<Key, Value>* ClosestPageToPosition(int position) const {
            if (IsEmpty()) return nullptr;
            const auto [pageIndex, index] = PagedIndices(position);
            return index < 0 ? &pages.front() : &pages[std::size_t(pageIndex)];
        }
        // The nearest item, searching outward from the position, that the predicate accepts.
        template<typename P> const Value* ClosestItemAroundPosition(int position, P predicate) const {
            if (IsEmpty()) return nullptr;
            const auto [pageIndex, index] = PagedIndices(position);
            const int last = int(pages.size()) - 1;
            int firstNonEmpty = 0, lastNonEmpty = last;
            while (pages[std::size_t(firstNonEmpty)].data.empty()) ++firstNonEmpty;
            while (pages[std::size_t(lastNonEmpty)].data.empty()) --lastNonEmpty;
            bool prependComplete = false, appendComplete = false;
            int pPage = -1, pIndex = -1, aPage = -1, aIndex = -1;
            if (index < 0) {
                prependComplete = true;
                aPage = firstNonEmpty;
                aIndex = 0;
            } else if (pageIndex == last && index > int(pages.back().data.size()) - 1) {
                appendComplete = true;
                pPage = lastNonEmpty;
                pIndex = int(pages[std::size_t(lastNonEmpty)].data.size()) - 1;
            } else {
                pPage = aPage = pageIndex;
                pIndex = aIndex = index;
            }
            while (!prependComplete || !appendComplete) {
                if (!prependComplete) {
                    while (pIndex < 0 && --pPage >= 0) pIndex = int(pages[std::size_t(pPage)].data.size()) - 1;
                    if (pPage < 0) {
                        prependComplete = true;
                    } else {
                        const Value& item = pages[std::size_t(pPage)].data[std::size_t(pIndex)];
                        if (predicate(item)) return &item;
                        --pIndex;
                    }
                }
                if (!appendComplete) {
                    while (aIndex > int(pages[std::size_t(aPage)].data.size()) - 1 && ++aPage <= last)
                        aIndex = pages[std::size_t(aPage)].data.empty() ? std::numeric_limits<int>::max() : 0;
                    if (aPage > last) {
                        appendComplete = true;
                    } else {
                        const Value& item = pages[std::size_t(aPage)].data[std::size_t(aIndex)];
                        if (predicate(item)) return &item;
                        ++aIndex;
                    }
                }
            }
            return nullptr;
        }

    private:
        // anchorPositionToPagedIndices: the page and the index in it (which may be outside it).
        std::pair<int, int> PagedIndices(int position) const {
            int pageIndex = 0;
            int index = position - leadingPlaceholderCount;
            while (pageIndex < int(pages.size()) - 1 && index > int(pages[std::size_t(pageIndex)].data.size()) - 1) {
                index -= int(pages[std::size_t(pageIndex)].data.size());
                ++pageIndex;
            }
            return { pageIndex, index };
        }
    };

    template<typename Key, typename Value>
    class PagingSource {
    public:
        using Params = LoadParams<Key>;
        using Page = paging::Page<Key, Value>;
        using Result = LoadResult<Key, Value>;
        using State = PagingState<Key, Value>;

        virtual ~PagingSource() = default;
        // Loads the page for params.key (the first page when it is absent), params.loadSize items.
        virtual rt::Task<Result> Load(Params params) = 0;
        // The key to load a replacement source's first page with, so it shows the same place:
        // usually derived from state.anchorPosition. Absent starts over.
        virtual std::optional<Key> GetRefreshKey(const State& state) = 0;
        // Loads around any key work (positional data): jumps are possible.
        virtual bool JumpingSupported() const { return false; }
        // The same key may come back as nextKey / prevKey in a row.
        virtual bool KeyReuseSupported() const { return false; }

        bool Invalid() const { return m_Invalid; }
        // The data changed: the Pager makes a new source and loads again around the same place.
        void Invalidate() {
            if (m_Invalid) return;
            m_Invalid = true;
            auto callbacks = std::move(m_Callbacks);
            m_Callbacks.clear();
            for (auto& [id, fn] : callbacks) fn();
        }
        // Called once when the source is invalidated (at once when it already is). The id
        // unregisters it.
        int RegisterInvalidatedCallback(kiln::Function<void()> callback) {
            if (m_Invalid) {
                callback();
                return 0;
            }
            m_Callbacks[++m_NextId] = std::move(callback);
            return m_NextId;
        }
        void UnregisterInvalidatedCallback(int id) { m_Callbacks.erase(id); }

    private:
        bool m_Invalid = false;
        int m_NextId = 0;
        std::map<int, kiln::Function<void()>> m_Callbacks;
    };


    // InvalidatingPagingSourceFactory: a pagingSourceFactory whose Invalidate() invalidates every
    // source it made. Keep it in a shared_ptr and give the Pager [f] { return (*f)(); }.
    template<typename Key, typename Value>
    class InvalidatingPagingSourceFactory {
    public:
        using Source = PagingSource<Key, Value>;
        explicit InvalidatingPagingSourceFactory(kiln::Function<std::shared_ptr<Source>()> factory) : m_Factory(std::move(factory)) {}

        std::shared_ptr<Source> operator()() {
            std::shared_ptr<Source> source = m_Factory();
            std::lock_guard lock(m_Mutex);
            m_Sources.push_back(source);
            return source;
        }
        // Only the sources made so far: one made while invalidating (a new generation) stays.
        void Invalidate() {
            std::vector<std::shared_ptr<Source>> previous;
            {
                std::lock_guard lock(m_Mutex);
                previous.swap(m_Sources);
            }
            for (const auto& source : previous)
                if (!source->Invalid()) source->Invalidate();
        }
        std::vector<std::shared_ptr<Source>> PagingSources() const {
            std::lock_guard lock(m_Mutex);
            return m_Sources;
        }

    private:
        kiln::Function<std::shared_ptr<Source>()> m_Factory;
        mutable std::mutex m_Mutex;
        std::vector<std::shared_ptr<Source>> m_Sources;
    };

}
