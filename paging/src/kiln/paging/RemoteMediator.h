#pragma once

// RemoteMediator (paging-common: RemoteMediator, RemoteMediatorAccessor's AccessorState and
// RemoteMediatorAccessImpl, SingleRunner, PageFetcher.injectRemoteEvents): a network layer
// that fills the local database the PagingSource reads, when the source reaches an end.

#include "kiln/paging/PagingData.h"
#include "kiln/paging/PagingSource.h"

#include "kiln/runtime/Flow.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <variant>

namespace kiln::paging {

    // RemoteMediator.MediatorResult: Success (with whether the remote has no more in that
    // direction) or Error.
    struct MediatorResult {
        struct Success {
            bool endOfPaginationReached = false;
        };
        struct Error {
            std::exception_ptr throwable;
        };
        std::variant<Success, Error> value;

        static MediatorResult Succeeded(bool endOfPaginationReached) { return { Success{ endOfPaginationReached } }; }
        template<typename E> static MediatorResult Failed(E e) { return { Error{ std::make_exception_ptr(std::move(e)) } }; }
        static MediatorResult Failed(std::exception_ptr e) { return { Error{ std::move(e) } }; }
    };

    // RemoteMediator.InitializeAction: refresh from the remote first (the local data may be stale),
    // or show what is stored and load from the remote only at its ends.
    enum class InitializeAction : std::uint8_t { LaunchInitialRefresh, SkipInitialRefresh };

    // Loads from the network into local storage when the PagingSource runs out (Append / Prepend,
    // with the state of what is loaded), or to start over (Refresh). Storing the results should
    // invalidate the PagingSource, so the new data shows.
    template<typename Key, typename Value>
    class RemoteMediator {
    public:
        using MediatorResult = paging::MediatorResult;
        using InitializeAction = paging::InitializeAction;
        virtual ~RemoteMediator() = default;
        virtual rt::Task<MediatorResult> Load(LoadType loadType, PagingState<Key, Value> state) = 0;
        // Before the first load: whether to refresh from the remote.
        virtual rt::Task<InitializeAction> Initialize() { co_return InitializeAction::LaunchInitialRefresh; }
    };

    namespace detail {

        // AccessorState: per load type whether it may run (blocked until a refresh, or done), its
        // error, and the queued requests (one a type, with the latest paging state).
        template<typename K, typename V>
        class AccessorState {
        public:
            enum class BlockState : std::uint8_t { Unblocked, Completed, RequiresRefresh };
            bool refreshAllowed = false;

            LoadStates ComputeLoadStates() const { return { Compute(LoadType::Refresh), Compute(LoadType::Prepend), Compute(LoadType::Append) }; }

            // A new request to launch; false when queued behind another, blocked or in error.
            bool Add(LoadType type, const PagingState<K, V>& state) {
                for (auto& p : m_Pending)
                    if (p.type == type) {
                        p.state = state;   // the same type: the newer state
                        return false;
                    }
                const BlockState block = m_Block[Index(type)];
                // Waiting for a refresh: queued, not launched (a refresh that reaches the end
                // drops it).
                if (block == BlockState::RequiresRefresh && type != LoadType::Refresh) {
                    m_Pending.push_back({ type, state });
                    return false;
                }
                if (block != BlockState::Unblocked && type != LoadType::Refresh) return false;
                if (type == LoadType::Refresh) SetError(LoadType::Refresh, std::nullopt);   // a refresh ignores its error
                if (m_Errors[Index(type)]) return false;
                m_Pending.push_back({ type, state });
                return true;
            }
            void SetBlockState(LoadType type, BlockState state) { m_Block[Index(type)] = state; }
            std::optional<PagingState<K, V>> PendingRefresh() const {
                for (const auto& p : m_Pending)
                    if (p.type == LoadType::Refresh) return p.state;
                return std::nullopt;
            }
            std::optional<std::pair<LoadType, PagingState<K, V>>> PendingBoundary() const {
                for (const auto& p : m_Pending)
                    if (p.type != LoadType::Refresh && m_Block[Index(p.type)] == BlockState::Unblocked) return std::pair{ p.type, p.state };
                return std::nullopt;
            }
            void ClearPendingRequests() { m_Pending.clear(); }
            void ClearPendingRequest(LoadType type) { std::erase_if(m_Pending, [type](const Pending& p) { return p.type == type; }); }
            void ClearErrors() { m_Errors = {}; }
            void SetError(LoadType type, std::optional<LoadState> error) { m_Errors[Index(type)] = std::move(error); }

        private:
            struct Pending {
                LoadType type;
                PagingState<K, V> state;
            };
            static std::size_t Index(LoadType t) { return std::size_t(t); }
            LoadState Compute(LoadType type) const {
                const BlockState block = m_Block[Index(type)];
                const bool pending = std::any_of(m_Pending.begin(), m_Pending.end(), [type](const Pending& p) { return p.type == type; });
                // Queued behind a refresh is not loading yet.
                if (pending && block != BlockState::RequiresRefresh) return LoadState::Loading();
                if (m_Errors[Index(type)]) return *m_Errors[Index(type)];
                if (block == BlockState::Completed) return LoadState::NotLoading(type != LoadType::Refresh);
                return LoadState::NotLoading(false);
            }
            std::array<BlockState, 3> m_Block{ BlockState::Unblocked, BlockState::Unblocked, BlockState::Unblocked };
            std::array<std::optional<LoadState>, 3> m_Errors;
            std::deque<Pending> m_Pending;
        };

        // SingleRunner(cancelPreviousInEqualPriority = false): one block at a time; a higher
        // priority one cancels the running one, an equal or lower one is skipped while it runs.
        class SingleRunner {
        public:
            // The token of the block now running, or 0 when it may not run.
            int TryEnqueue(int priority) {
                if (m_Running && !m_Job.Done() && m_Priority >= priority) return 0;
                if (m_Running) m_Job.Cancel();   // a lower priority block still running
                m_Running = true;
                m_Priority = priority;
                return ++m_Token;
            }
            void Started(int token, rt::Job job) {
                if (token == m_Token) m_Job = std::move(job);
            }
            // onFinish: the block ended (before the next one is launched from it).
            void Finished(int token) {
                if (token == m_Token) m_Running = false;
            }
            ~SingleRunner() { m_Job.Cancel(); }

        private:
            bool m_Running = false;
            int m_Priority = 0, m_Token = 0;
            rt::Job m_Job;
        };

        // RemoteMediatorAccessImpl: runs the mediator's loads in isolation (refresh before
        // boundaries) and keeps its load states.
        template<typename K, typename V>
        class RemoteMediatorAccessor : public std::enable_shared_from_this<RemoteMediatorAccessor<K, V>> {
        public:
            using State = AccessorState<K, V>;
            explicit RemoteMediatorAccessor(std::shared_ptr<RemoteMediator<K, V>> mediator) : m_Mediator(std::move(mediator)) {}

            const rt::MutableStateFlow<LoadStates>& StateFlow() const { return m_States; }

            void RequestRefreshIfAllowed(const PagingState<K, V>& state) {
                bool request = false;
                Use([&](State& s) { request = std::exchange(s.refreshAllowed, false); });
                if (request) RequestLoad(LoadType::Refresh, state);
            }
            void AllowRefresh() {
                Use([](State& s) { s.refreshAllowed = true; });
            }
            void RequestLoad(LoadType type, const PagingState<K, V>& state) {
                bool launch = false;
                Use([&](State& s) { launch = s.Add(type, state); });
                if (!launch) return;
                if (type == LoadType::Refresh) LaunchRefresh();
                else LaunchBoundary();
            }
            void RetryFailed(const PagingState<K, V>& state) {
                std::vector<LoadType> toStart;
                Use([&](State& s) {
                    const LoadStates states = s.ComputeLoadStates();
                    const bool refresh = states.refresh.IsError();
                    s.ClearErrors();
                    if (refresh) {
                        toStart.push_back(LoadType::Refresh);
                        s.SetBlockState(LoadType::Refresh, State::BlockState::Unblocked);
                    }
                    for (LoadType t : { LoadType::Append, LoadType::Prepend }) {
                        if (!states.Get(t).IsError()) continue;
                        if (!refresh) toStart.push_back(t);
                        s.ClearPendingRequest(t);
                    }
                });
                for (LoadType t : toStart) RequestLoad(t, state);
            }
            rt::Task<InitializeAction> Initialize() {
                const InitializeAction action = co_await m_Mediator->Initialize();
                if (action == InitializeAction::LaunchInitialRefresh) {
                    Use([](State& s) {
                        s.SetBlockState(LoadType::Append, State::BlockState::RequiresRefresh);
                        s.SetBlockState(LoadType::Prepend, State::BlockState::RequiresRefresh);
                    });
                }
                co_return action;
            }

        private:
            static constexpr int kPriorityRefresh = 2, kPriorityAppendPrepend = 1;

            template<typename F> void Use(F&& block) {
                block(m_State);
                m_States.SetValue(m_State.ComputeLoadStates());
            }
            void LaunchRefresh() {
                const int token = m_Runner.TryEnqueue(kPriorityRefresh);
                if (!token) return;
                m_Runner.Started(token, rt::Launch(rt::Dispatcher::Main(), RunRefresh(this->weak_from_this(), m_Mediator, token)));
            }
            void LaunchBoundary() {
                const int token = m_Runner.TryEnqueue(kPriorityAppendPrepend);
                if (!token) return;
                m_Runner.Started(token, rt::Launch(rt::Dispatcher::Main(), RunBoundary(this->weak_from_this(), m_Mediator, token)));
            }

            // The accessor is held only between the mediator's loads: when the pager's collection
            // ends it goes, and with it (its runner) this job.
            static rt::Task<> RunRefresh(std::weak_ptr<RemoteMediatorAccessor> weak, std::shared_ptr<RemoteMediator<K, V>> mediator, int token) {
                std::optional<PagingState<K, V>> pending;
                if (auto self = weak.lock()) pending = self->m_State.PendingRefresh();
                else co_return;
                bool launchBoundary = false;
                if (pending) {
                    const MediatorResult result = co_await mediator->Load(LoadType::Refresh, *pending);
                    auto self = weak.lock();
                    if (!self) co_return;
                    if (const auto* success = std::get_if<MediatorResult::Success>(&result.value)) {
                        self->Use([&](State& s) {
                            // Only the refresh: boundary requests stay, a refresh may not invalidate.
                            s.ClearPendingRequest(LoadType::Refresh);
                            if (success->endOfPaginationReached) {
                                s.SetBlockState(LoadType::Refresh, State::BlockState::Completed);
                                s.SetBlockState(LoadType::Prepend, State::BlockState::Completed);
                                s.SetBlockState(LoadType::Append, State::BlockState::Completed);
                                s.ClearPendingRequests();
                            } else {
                                s.SetBlockState(LoadType::Prepend, State::BlockState::Unblocked);
                                s.SetBlockState(LoadType::Append, State::BlockState::Unblocked);
                            }
                            s.SetError(LoadType::Prepend, std::nullopt);
                            s.SetError(LoadType::Append, std::nullopt);
                            launchBoundary = s.PendingBoundary().has_value();
                        });
                    } else {
                        const auto& error = std::get<MediatorResult::Error>(result.value);
                        self->Use([&](State& s) {
                            s.ClearPendingRequest(LoadType::Refresh);
                            s.SetError(LoadType::Refresh, LoadState::Error(error.throwable));
                            launchBoundary = s.PendingBoundary().has_value();
                        });
                    }
                }
                auto self = weak.lock();
                if (!self) co_return;
                self->m_Runner.Finished(token);
                // Outside the isolation, or the lower priority would be skipped.
                if (launchBoundary) self->LaunchBoundary();
            }

            static rt::Task<> RunBoundary(std::weak_ptr<RemoteMediatorAccessor> weak, std::shared_ptr<RemoteMediator<K, V>> mediator, int token) {
                while (true) {
                    std::optional<std::pair<LoadType, PagingState<K, V>>> pending;
                    if (auto self = weak.lock()) pending = self->m_State.PendingBoundary();
                    else co_return;
                    if (!pending) break;
                    const LoadType type = pending->first;
                    const MediatorResult result = co_await mediator->Load(type, std::move(pending->second));
                    auto self = weak.lock();
                    if (!self) co_return;
                    if (const auto* success = std::get_if<MediatorResult::Success>(&result.value)) {
                        self->Use([&](State& s) {
                            s.ClearPendingRequest(type);
                            if (success->endOfPaginationReached) s.SetBlockState(type, State::BlockState::Completed);
                        });
                    } else {
                        const auto& error = std::get<MediatorResult::Error>(result.value);
                        self->Use([&](State& s) {
                            s.ClearPendingRequest(type);
                            s.SetError(type, LoadState::Error(error.throwable));
                        });
                    }
                }
                if (auto self = weak.lock()) self->m_Runner.Finished(token);
            }

            std::shared_ptr<RemoteMediator<K, V>> m_Mediator;
            State m_State;
            rt::MutableStateFlow<LoadStates> m_States{ LoadStates::Idle() };
            SingleRunner m_Runner;
        };

        // injectRemoteEvents: the source's events with the mediator's states, and a load state
        // update each time the mediator's states change. Ends with the source's events.
        template<typename K, typename V>
        rt::Flow<PageEvent<V>> InjectRemoteEvents(rt::Flow<PageEvent<V>> source, std::shared_ptr<RemoteMediatorAccessor<K, V>> accessor) {
            if (!accessor) return source;
            return rt::Flow<PageEvent<V>>::Create([source = std::move(source), accessor](rt::FlowCollector<PageEvent<V>> emit) -> rt::Task<> {
                // Index 0: the mediator's states (the receiver); 1: the source's events.
                using Item = std::variant<LoadStates, PageEvent<V>>;
                auto ch = rt::detail::EventChannel<Item>();
                rt::Job remote = rt::Launch(rt::Dispatcher::Main(), rt::detail::ForwardChild<Item, LoadStates>(accessor->StateFlow().AsFlow(), 0, ch, [](LoadStates s) { return Item(std::move(s)); }));
                rt::Job events = rt::Launch(rt::Dispatcher::Main(), rt::detail::ForwardChild<Item, PageEvent<V>>(source, 1, ch, [](PageEvent<V> e) { return Item(std::move(e)); }));
                LoadStates sourceStates = LoadStates::Idle();
                std::optional<LoadStates> remoteState;
                std::optional<PageEvent<V>> sourceEvent;
                std::deque<rt::detail::ChildEvent<Item>> early;   // after a first value, until both have one
                auto transform = [&](bool fromReceiver) -> PageEvent<V> {
                    if (fromReceiver) return PageEvent<V>::LoadStateUpdate(sourceStates, remoteState);
                    PageEvent<V> e = *sourceEvent;
                    switch (e.kind) {
                        case PageEvent<V>::Kind::Insert:
                            if (e.sourceLoadStates) sourceStates = *e.sourceLoadStates;
                            e.mediatorLoadStates = remoteState;
                            return e;
                        case PageEvent<V>::Kind::Drop:
                            sourceStates = sourceStates.Modify(e.loadType, LoadState::NotLoading(false));
                            return e;
                        case PageEvent<V>::Kind::LoadStateUpdate:
                            sourceStates = e.sourceLoadStates.value_or(LoadStates::Idle());
                            return PageEvent<V>::LoadStateUpdate(sourceStates, remoteState);
                        default:
                            throw std::logic_error("Paging generated an event to display a static list that originated from a paginated source.");
                    }
                };
                bool initialDispatched = false;
                while (true) {
                    std::optional<rt::detail::ChildEvent<Item>> e;
                    if (initialDispatched && !early.empty()) {
                        e = std::move(early.front());
                        early.pop_front();
                    } else {
                        e = co_await ch->Next();
                    }
                    if (!e) break;
                    if (e->error) std::rethrow_exception(e->error);
                    if (!e->value) {
                        if (e->index == 1) break;   // the source's events ended
                        continue;
                    }
                    const bool fromReceiver = e->index == 0;
                    // UnbatchedFlowCombiner: a side's second value waits until both have one.
                    if (!initialDispatched && (fromReceiver ? remoteState.has_value() : sourceEvent.has_value())) {
                        early.push_back(std::move(*e));
                        continue;
                    }
                    if (fromReceiver) remoteState = std::get<LoadStates>(std::move(*e->value));
                    else sourceEvent = std::get<PageEvent<V>>(std::move(*e->value));
                    if (!remoteState || !sourceEvent) continue;
                    // The initial dispatch counts as the source's (its event is transformed).
                    const bool initial = !initialDispatched;
                    initialDispatched = true;
                    co_await emit(transform(initial ? false : fromReceiver));
                }
                remote.Cancel();
                events.Cancel();
            });
        }
    }

}
