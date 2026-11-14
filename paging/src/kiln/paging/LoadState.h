#pragma once

// Paging's load states and configuration (paging-common: LoadType, LoadState, LoadStates,
// CombinedLoadStates, PagingConfig).

#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace kiln::paging {

    // Page.COUNT_UNDEFINED and PagingConfig.MAX_SIZE_UNBOUNDED
    inline constexpr int kCountUndefined = std::numeric_limits<int>::min();
    inline constexpr int kMaxSizeUnbounded = std::numeric_limits<int>::max();

    enum class LoadType : std::uint8_t { Refresh, Prepend, Append };

    // NotLoading(endOfPaginationReached), Loading or Error(error).
    struct LoadState {
        enum class Kind : std::uint8_t { NotLoading, Loading, Error };
        Kind kind = Kind::NotLoading;
        bool endOfPaginationReached = false;
        std::exception_ptr error;

        static LoadState NotLoading(bool endOfPaginationReached) { return { Kind::NotLoading, endOfPaginationReached, nullptr }; }
        static LoadState Loading() { return { Kind::Loading, false, nullptr }; }
        static LoadState Error(std::exception_ptr e) { return { Kind::Error, false, std::move(e) }; }
        bool IsNotLoading() const { return kind == Kind::NotLoading; }
        bool IsLoading() const { return kind == Kind::Loading; }
        bool IsError() const { return kind == Kind::Error; }
        // The error's what(), for showing it.
        std::string ErrorMessage() const {
            if (!error) return {};
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& e) {
                return e.what();
            } catch (...) {
                return "unknown error";
            }
        }
        bool operator==(const LoadState& o) const { return kind == o.kind && endOfPaginationReached == o.endOfPaginationReached && error == o.error; }
    };

    struct LoadStates {
        LoadState refresh, prepend, append;

        const LoadState& Get(LoadType type) const { return type == LoadType::Refresh ? refresh : type == LoadType::Prepend ? prepend : append; }
        LoadStates Modify(LoadType type, LoadState state) const {
            LoadStates out = *this;
            (type == LoadType::Refresh ? out.refresh : type == LoadType::Prepend ? out.prepend : out.append) = std::move(state);
            return out;
        }
        bool HasError() const { return refresh.IsError() || append.IsError() || prepend.IsError(); }
        bool IsIdle() const { return refresh.IsNotLoading() && append.IsNotLoading() && prepend.IsNotLoading(); }
        bool operator==(const LoadStates&) const = default;
        static LoadStates Idle() { return { LoadState::NotLoading(false), LoadState::NotLoading(false), LoadState::NotLoading(false) }; }
    };

    // What the UI shows: the source's states (and a mediator's, which kiln has none of yet, so
    // refresh / prepend / append are the source's).
    struct CombinedLoadStates {
        LoadState refresh, prepend, append;
        LoadStates source;
        std::optional<LoadStates> mediator;

        bool IsIdle() const { return source.IsIdle() && (!mediator || mediator->IsIdle()); }
        bool HasError() const { return source.HasError() || (mediator && mediator->HasError()); }
        bool operator==(const CombinedLoadStates&) const = default;
    };

    namespace detail {
        // computeHelperState
        inline LoadState HelperState(const LoadState& previous, const LoadState& sourceRefresh, const LoadState& source, const std::optional<LoadState>& remote) {
            if (!remote) return source;
            if (previous.IsLoading()) {
                if (sourceRefresh.IsNotLoading() && remote->IsNotLoading()) return *remote;
                if (remote->IsError()) return *remote;
                return previous;
            }
            return *remote;
        }
        // CombinedLoadStates?.computeNewState
        inline CombinedLoadStates ComputeNewState(const std::optional<CombinedLoadStates>& current, const LoadStates& source, const std::optional<LoadStates>& remote) {
            const LoadState incomplete = LoadState::NotLoading(false);
            auto of = [&](const LoadState CombinedLoadStates::* m, const LoadState LoadStates::* s) {
                return HelperState(current ? (*current).*m : incomplete, source.refresh, source.*s,
                                   remote ? std::optional<LoadState>((*remote).*s) : std::nullopt);
            };
            return { of(&CombinedLoadStates::refresh, &LoadStates::refresh), of(&CombinedLoadStates::prepend, &LoadStates::prepend),
                     of(&CombinedLoadStates::append, &LoadStates::append), source, remote };
        }
    }

    struct PagingConfig {
        // Items per page asked of the source.
        int pageSize = 20;
        // How near the edge of the loaded items an access asks for more; -1 is pageSize.
        int prefetchDistance = -1;
        // Unloaded items count as null placeholders when the source reports how many there are.
        bool enablePlaceholders = true;
        // The first load's size; -1 is pageSize * 3.
        int initialLoadSize = -1;
        // Above this many loaded items, pages far from the last access are dropped.
        int maxSize = kMaxSizeUnbounded;
        // With a source that supports jumps: an access this far past the loaded items reloads
        // around it instead of loading page by page.
        int jumpThreshold = kCountUndefined;

        // The defaults filled in and the combination checked, as PagingConfig's constructor does.
        PagingConfig Resolved() const {
            PagingConfig c = *this;
            if (c.pageSize < 1) throw std::invalid_argument("pageSize must be at least 1");
            if (c.prefetchDistance < 0) c.prefetchDistance = c.pageSize;
            if (c.initialLoadSize < 1) c.initialLoadSize = c.pageSize * 3;
            if (!c.enablePlaceholders && c.prefetchDistance == 0) {
                throw std::invalid_argument("Placeholders and prefetch are the only ways to trigger loading of more data in PagingData, "
                                            "so either placeholders must be enabled, or prefetch distance must be > 0.");
            }
            if (c.maxSize != kMaxSizeUnbounded && c.maxSize < c.pageSize + c.prefetchDistance * 2) {
                throw std::invalid_argument("Maximum size must be at least pageSize + 2*prefetchDist, pageSize=" + std::to_string(c.pageSize) +
                                            ", prefetchDist=" + std::to_string(c.prefetchDistance) + ", maxSize=" + std::to_string(c.maxSize));
            }
            if (c.jumpThreshold != kCountUndefined && c.jumpThreshold <= 0) {
                throw std::invalid_argument("jumpThreshold must be positive to enable jumps or COUNT_UNDEFINED to disable jumping.");
            }
            return c;
        }
        bool operator==(const PagingConfig&) const = default;
    };

}
