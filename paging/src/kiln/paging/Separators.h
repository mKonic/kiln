#pragma once

// Separators between paged items, and headers and footers (paging-common Separators.kt:
// TerminalSeparatorType, insertInternalSeparators, separatorPage, SeparatorState,
// insertEventSeparators).

#include "kiln/paging/PagingData.h"

#include <kiln/Function.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace kiln::paging {

    // When terminal separators (header and footer) show: once both the PagingSource and a
    // RemoteMediator reached the end in that direction (FullyComplete; the source alone without a
    // mediator), or as soon as the source has (SourceComplete).
    enum class TerminalSeparatorType : std::uint8_t { FullyComplete, SourceComplete };

    namespace detail {

        // The generator sees the items on each side (null at the ends) and returns a separator, or
        // nothing. Items are T; the output list holds R (T converts to R: a variant, a base type).
        template<typename T, typename R>
        using SeparatorGenerator = kiln::Function<std::optional<R>(const T* before, const T* after)>;

        template<typename T, typename R>
        TransformablePage<R> ConvertPage(const TransformablePage<T>& p) {
            if constexpr (std::is_same_v<T, R>) {
                return p;
            } else {
                TransformablePage<R> out{ p.originalPageOffsets, {}, p.hintOriginalPageOffset, p.hintOriginalIndices };
                out.data.reserve(p.data.size());
                for (const T& v : p.data) out.data.push_back(R(v));
                return out;
            }
        }

        template<typename T, typename R>
        PageEvent<R> ConvertEvent(const PageEvent<T>& e, std::vector<TransformablePage<R>> pages) {
            PageEvent<R> out;
            out.kind = static_cast<typename PageEvent<R>::Kind>(e.kind);
            out.loadType = e.loadType;
            out.pages = std::move(pages);
            out.placeholdersBefore = e.placeholdersBefore;
            out.placeholdersAfter = e.placeholdersAfter;
            out.sourceLoadStates = e.sourceLoadStates;
            out.mediatorLoadStates = e.mediatorLoadStates;
            out.minPageOffset = e.minPageOffset;
            out.maxPageOffset = e.maxPageOffset;
            out.placeholdersRemaining = e.placeholdersRemaining;
            return out;
        }
        template<typename T, typename R>
        PageEvent<R> ConvertEvent(const PageEvent<T>& e) {
            std::vector<TransformablePage<R>> pages;
            for (const auto& p : e.pages) pages.push_back(ConvertPage<T, R>(p));
            PageEvent<R> out = ConvertEvent<T, R>(e, std::move(pages));
            for (const T& v : e.data) out.data.push_back(R(v));
            return out;
        }

        // insertInternalSeparators: separators inside a page (between pages is SeparatorState's).
        template<typename T, typename R>
        TransformablePage<R> InsertInternalSeparators(const TransformablePage<T>& page, const SeparatorGenerator<T, R>& generator) {
            if (page.data.empty()) return ConvertPage<T, R>(page);
            std::vector<R> outputList;
            std::vector<int> outputIndices;
            outputList.reserve(page.data.size() + 4);
            outputList.push_back(R(page.data.front()));
            outputIndices.push_back(page.hintOriginalIndices ? page.hintOriginalIndices->front() : 0);
            for (std::size_t i = 1; i < page.data.size(); ++i) {
                const T& item = page.data[i];
                if (std::optional<R> separator = generator(&page.data[i - 1], &item)) {
                    outputList.push_back(std::move(*separator));
                    outputIndices.push_back(int(i));
                }
                outputList.push_back(R(item));
                outputIndices.push_back(int(i));
            }
            // No separators: the page as it came.
            if (outputList.size() == page.data.size()) return ConvertPage<T, R>(page);
            return { page.originalPageOffsets, std::move(outputList), page.hintOriginalPageOffset, std::move(outputIndices) };
        }

        template<typename R>
        TransformablePage<R> SeparatorPage(R separator, std::vector<int> originalPageOffsets, int hintOriginalPageOffset, int hintOriginalIndex) {
            return { std::move(originalPageOffsets), { std::move(separator) }, hintOriginalPageOffset, std::vector<int>{ hintOriginalIndex } };
        }

        // addSeparatorPage with the adjacent pages: their offsets together, sorted.
        template<typename T, typename R>
        void AddSeparatorPage(std::vector<TransformablePage<R>>& out, std::optional<R> separator, const TransformablePage<T>* before,
                              const TransformablePage<T>* after, int hintOriginalPageOffset, int hintOriginalIndex) {
            if (!separator) return;
            std::vector<int> offsets;
            if (before && after) {
                offsets = before->originalPageOffsets;
                offsets.insert(offsets.end(), after->originalPageOffsets.begin(), after->originalPageOffsets.end());
                std::sort(offsets.begin(), offsets.end());
                offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
            } else if (after) {
                offsets = after->originalPageOffsets;
            } else if (before) {
                offsets = before->originalPageOffsets;
            } else {
                throw std::invalid_argument("Separator page expected adjacentPageBefore or adjacentPageAfter, but both were null.");
            }
            out.push_back(SeparatorPage<R>(std::move(*separator), std::move(offsets), hintOriginalPageOffset, hintOriginalIndex));
        }

        template<typename T, typename R>
        class SeparatorState {
        public:
            SeparatorState(TerminalSeparatorType type, SeparatorGenerator<T, R> generator) : m_Type(type), m_Generator(std::move(generator)) {}

            PageEvent<R> OnEvent(const PageEvent<T>& event) {
                PageEvent<R> out;
                switch (event.kind) {
                    case PageEvent<T>::Kind::Insert: out = OnInsert(event); break;
                    case PageEvent<T>::Kind::Drop: out = OnDrop(event); break;
                    case PageEvent<T>::Kind::LoadStateUpdate: out = OnLoadStateUpdate(event); break;
                    case PageEvent<T>::Kind::StaticList: out = OnStaticList(event); break;
                }
                if ((m_EndTerminalSeparatorDeferred || m_StartTerminalSeparatorDeferred) && !m_PageStash.empty())
                    throw std::logic_error("deferred terminal separator, page stash should be empty");
                return out;
            }

        private:
            static bool MediatorEndNotFalse(const std::optional<LoadStates>& mediator, LoadType type) {
                return !mediator || mediator->Get(type).endOfPaginationReached;
            }
            bool TerminatesStart(const PageEvent<T>& e) const {
                if (e.loadType == LoadType::Append) return m_StartTerminalSeparatorDeferred;
                const bool source = e.sourceLoadStates && e.sourceLoadStates->prepend.endOfPaginationReached;
                return m_Type == TerminalSeparatorType::FullyComplete ? source && MediatorEndNotFalse(e.mediatorLoadStates, LoadType::Prepend) : source;
            }
            bool TerminatesEnd(const PageEvent<T>& e) const {
                if (e.loadType == LoadType::Prepend) return m_EndTerminalSeparatorDeferred;
                const bool source = e.sourceLoadStates && e.sourceLoadStates->append.endOfPaginationReached;
                return m_Type == TerminalSeparatorType::FullyComplete ? source && MediatorEndNotFalse(e.mediatorLoadStates, LoadType::Append) : source;
            }
            static int FirstHintIndex(const TransformablePage<T>& p) { return p.hintOriginalIndices ? p.hintOriginalIndices->front() : 0; }
            static int LastHintIndex(const TransformablePage<T>& p) { return p.hintOriginalIndices ? p.hintOriginalIndices->back() : int(p.data.size()) - 1; }

            PageEvent<R> OnInsert(const PageEvent<T>& event) {
                const bool terminatesStart = TerminatesStart(event);
                const bool terminatesEnd = TerminatesEnd(event);
                const bool eventEmpty = std::all_of(event.pages.begin(), event.pages.end(), [](const auto& p) { return p.data.empty(); });
                if (m_HeaderAdded && event.loadType == LoadType::Prepend && !eventEmpty) throw std::invalid_argument("Additional prepend event after prepend state is done");
                if (m_FooterAdded && event.loadType == LoadType::Append && !eventEmpty) throw std::invalid_argument("Additional append event after append state is done");

                if (event.sourceLoadStates) m_SourceStates = *event.sourceLoadStates;
                m_MediatorStates = event.mediatorLoadStates;
                if (event.loadType != LoadType::Append) m_PlaceholdersBefore = event.placeholdersBefore;
                if (event.loadType != LoadType::Prepend) m_PlaceholdersAfter = event.placeholdersAfter;

                if (eventEmpty) {
                    if (!terminatesStart && !terminatesEnd) return ConvertEvent<T, R>(event);
                    if (m_HeaderAdded && m_FooterAdded) return ConvertEvent<T, R>(event);
                    if (m_PageStash.empty()) {
                        if (terminatesStart && terminatesEnd && !m_HeaderAdded && !m_FooterAdded) {
                            // Empty and terminal at both ends: one separator.
                            std::optional<R> separator = m_Generator(nullptr, nullptr);
                            m_EndTerminalSeparatorDeferred = m_StartTerminalSeparatorDeferred = false;
                            m_HeaderAdded = m_FooterAdded = true;
                            if (!separator) return ConvertEvent<T, R>(event);
                            std::vector<TransformablePage<R>> pages;
                            pages.push_back(SeparatorPage<R>(std::move(*separator), { 0 }, 0, 0));
                            return ConvertEvent<T, R>(event, std::move(pages));
                        }
                        // Not yet possible: deferred.
                        if (terminatesEnd && !m_FooterAdded) m_EndTerminalSeparatorDeferred = true;
                        if (terminatesStart && !m_HeaderAdded) m_StartTerminalSeparatorDeferred = true;
                        return ConvertEvent<T, R>(event);
                    }
                }

                std::vector<TransformablePage<R>> outList;
                std::vector<TransformablePage<T>> stashOutList;
                const TransformablePage<T>* firstNonEmpty = nullptr;
                const TransformablePage<T>* lastNonEmpty = nullptr;
                std::size_t firstIndex = 0, lastIndex = 0;
                const std::size_t pageLast = event.pages.empty() ? 0 : event.pages.size() - 1;
                if (!eventEmpty) {
                    std::size_t i = 0;
                    while (i < pageLast && event.pages[i].data.empty()) ++i;
                    firstIndex = i;
                    firstNonEmpty = &event.pages[i];
                    i = pageLast;
                    while (i > 0 && event.pages[i].data.empty()) --i;
                    lastIndex = i;
                    lastNonEmpty = &event.pages[i];
                }

                // Header.
                if (terminatesStart && !m_HeaderAdded) {
                    m_HeaderAdded = true;
                    const TransformablePage<T>& after = eventEmpty ? m_PageStash.front() : *firstNonEmpty;
                    AddSeparatorPage<T, R>(outList, m_Generator(nullptr, &after.data.front()), nullptr, &after, after.hintOriginalPageOffset, FirstHintIndex(after));
                }

                if (!eventEmpty) {
                    for (std::size_t i = 0; i < firstIndex; ++i) outList.push_back(InsertInternalSeparators(event.pages[i], m_Generator));
                    // Between the last stashed page and the first new one (append).
                    if (event.loadType == LoadType::Append && !m_PageStash.empty()) {
                        const TransformablePage<T>& lastStash = m_PageStash.back();
                        AddSeparatorPage<T, R>(outList, m_Generator(&lastStash.data.back(), &firstNonEmpty->data.front()), &lastStash, firstNonEmpty,
                                               firstNonEmpty->hintOriginalPageOffset, FirstHintIndex(*firstNonEmpty));
                    }
                    stashOutList.push_back(ToStash(*firstNonEmpty));
                    outList.push_back(InsertInternalSeparators(*firstNonEmpty, m_Generator));
                    // The pages between, some maybe empty.
                    const TransformablePage<T>* before = firstNonEmpty;
                    for (std::size_t i = firstIndex + 1; i <= lastIndex; ++i) {
                        const TransformablePage<T>& page = event.pages[i];
                        if (!page.data.empty()) {
                            const bool prepend = event.loadType == LoadType::Prepend;
                            AddSeparatorPage<T, R>(outList, m_Generator(&before->data.back(), &page.data.front()), before, &page,
                                                   prepend ? before->hintOriginalPageOffset : page.hintOriginalPageOffset,
                                                   prepend ? LastHintIndex(*before) : FirstHintIndex(page));
                            stashOutList.push_back(ToStash(page));
                        }
                        outList.push_back(InsertInternalSeparators(page, m_Generator));
                        if (!page.data.empty()) before = &page;
                    }
                    // Between the last new page and the first stashed one (prepend).
                    if (event.loadType == LoadType::Prepend && !m_PageStash.empty()) {
                        const TransformablePage<T>& after = m_PageStash.front();
                        AddSeparatorPage<T, R>(outList, m_Generator(&lastNonEmpty->data.back(), &after.data.front()), lastNonEmpty, &after,
                                               lastNonEmpty->hintOriginalPageOffset, LastHintIndex(*lastNonEmpty));
                    }
                    for (std::size_t i = lastIndex + 1; i < event.pages.size(); ++i) outList.push_back(InsertInternalSeparators(event.pages[i], m_Generator));
                }

                // Footer.
                if (terminatesEnd && !m_FooterAdded) {
                    m_FooterAdded = true;
                    const TransformablePage<T>& before = eventEmpty ? m_PageStash.back() : *lastNonEmpty;
                    AddSeparatorPage<T, R>(outList, m_Generator(&before.data.back(), nullptr), &before, nullptr, before.hintOriginalPageOffset, LastHintIndex(before));
                }

                m_EndTerminalSeparatorDeferred = m_StartTerminalSeparatorDeferred = false;
                if (event.loadType == LoadType::Append) m_PageStash.insert(m_PageStash.end(), stashOutList.begin(), stashOutList.end());
                else m_PageStash.insert(m_PageStash.begin(), stashOutList.begin(), stashOutList.end());
                return ConvertEvent<T, R>(event, std::move(outList));
            }

            PageEvent<R> OnDrop(const PageEvent<T>& event) {
                m_SourceStates = m_SourceStates.Modify(event.loadType, LoadState::NotLoading(false));
                if (event.loadType == LoadType::Prepend) {
                    m_PlaceholdersBefore = event.placeholdersRemaining;
                    m_HeaderAdded = false;
                } else if (event.loadType == LoadType::Append) {
                    m_PlaceholdersAfter = event.placeholdersRemaining;
                    m_FooterAdded = false;
                }
                if (m_PageStash.empty()) {
                    if (event.loadType == LoadType::Prepend) m_StartTerminalSeparatorDeferred = false;
                    else m_EndTerminalSeparatorDeferred = false;
                }
                // The stashes that depend on a dropped page.
                std::erase_if(m_PageStash, [&](const TransformablePage<T>& stash) {
                    return std::any_of(stash.originalPageOffsets.begin(), stash.originalPageOffsets.end(),
                                       [&](int o) { return o >= event.minPageOffset && o <= event.maxPageOffset; });
                });
                return ConvertEvent<T, R>(event);
            }

            PageEvent<R> OnLoadStateUpdate(const PageEvent<T>& event) {
                const std::optional<LoadStates> prevMediator = m_MediatorStates;
                const LoadStates source = event.sourceLoadStates.value_or(LoadStates::Idle());
                // Redundant updates pass, so terminal separators are not added out of place.
                if (m_SourceStates == source && prevMediator == event.mediatorLoadStates) return ConvertEvent<T, R>(event);
                m_SourceStates = source;
                m_MediatorStates = event.mediatorLoadStates;
                // A mediator's end of pagination becomes an empty insert, which can carry the
                // header or footer.
                const auto& mediator = event.mediatorLoadStates;
                if (mediator && mediator->prepend.endOfPaginationReached && (!prevMediator || !(prevMediator->prepend == mediator->prepend))) {
                    PageEvent<T> insert = PageEvent<T>::Prepend({}, m_PlaceholdersBefore, source);
                    insert.mediatorLoadStates = mediator;
                    return OnInsert(insert);
                }
                if (mediator && mediator->append.endOfPaginationReached && (!prevMediator || !(prevMediator->append == mediator->append))) {
                    PageEvent<T> insert = PageEvent<T>::Append({}, m_PlaceholdersAfter, source);
                    insert.mediatorLoadStates = mediator;
                    return OnInsert(insert);
                }
                return ConvertEvent<T, R>(event);
            }

            PageEvent<R> OnStaticList(const PageEvent<T>& event) {
                std::vector<R> data;
                // Up to size, for the footer.
                for (std::size_t i = 0; i <= event.data.size(); ++i) {
                    const T* before = i > 0 ? &event.data[i - 1] : nullptr;
                    const T* item = i < event.data.size() ? &event.data[i] : nullptr;
                    if (std::optional<R> separator = m_Generator(before, item)) data.push_back(std::move(*separator));
                    if (item) data.push_back(R(*item));
                }
                return PageEvent<R>::StaticList(std::move(data), event.sourceLoadStates, event.mediatorLoadStates);
            }

            static TransformablePage<T> ToStash(const TransformablePage<T>& page) {
                return { page.originalPageOffsets, { page.data.front(), page.data.back() }, page.hintOriginalPageOffset,
                         std::vector<int>{ FirstHintIndex(page), LastHintIndex(page) } };
            }

            TerminalSeparatorType m_Type;
            SeparatorGenerator<T, R> m_Generator;
            // The pages seen, first and last items only, without empty ones.
            std::vector<TransformablePage<T>> m_PageStash;
            bool m_EndTerminalSeparatorDeferred = false, m_StartTerminalSeparatorDeferred = false;
            LoadStates m_SourceStates = LoadStates::Idle();
            std::optional<LoadStates> m_MediatorStates;
            int m_PlaceholdersBefore = 0, m_PlaceholdersAfter = 0;
            bool m_FooterAdded = false, m_HeaderAdded = false;
        };

    }

    // insertSeparators: a separator (or none) between each pair of items, and before the first and
    // after the last once that end is reached (see TerminalSeparatorType).
    template<typename R, typename T, typename G>
    PagingData<R> InsertSeparators(const PagingData<T>& data, G generator, TerminalSeparatorType type = TerminalSeparatorType::FullyComplete) {
        auto state = std::make_shared<detail::SeparatorState<T, R>>(type, detail::SeparatorGenerator<T, R>(std::move(generator)));
        // No cached event, as upstream's: the separators depend on the events seen before.
        return PagingData<R>(rt::Map(data.Events(), [state](const detail::PageEvent<T>& e) { return state->OnEvent(e); }), data.Ui(), data.Hints());
    }

    // insertHeaderItem / insertFooterItem: one item before the first / after the last.
    template<typename T>
    PagingData<T> InsertHeaderItem(const PagingData<T>& data, T item, TerminalSeparatorType type = TerminalSeparatorType::FullyComplete) {
        return InsertSeparators<T>(data, [item](const T* before, const T*) { return before ? std::nullopt : std::optional<T>(item); }, type);
    }
    template<typename T>
    PagingData<T> InsertFooterItem(const PagingData<T>& data, T item, TerminalSeparatorType type = TerminalSeparatorType::FullyComplete) {
        return InsertSeparators<T>(data, [item](const T*, const T* after) { return after ? std::nullopt : std::optional<T>(item); }, type);
    }

}
