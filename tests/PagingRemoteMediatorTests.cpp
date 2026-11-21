// RemoteMediator (paging-common RemoteMediatorAccessorTest; a Pager with a mediator as in
// PageFetcherTest's remote cases): refresh before boundaries, queued and conflated requests,
// end of pagination, errors and retries, isolation, and the mediator's states in the events.
#include "Check.h"

#include "kiln/paging/LazyPagingItems.h"
#include "kiln/paging/Pager.h"

using namespace kiln;
using namespace kiln::paging;

namespace {
    using State = PagingState<int, int>;

    // Virtual time: each round moves the clock and runs what is due.
    void Advance(int ms) {
        for (int i = 0; i < ms; ++i) {
            rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(1));
            rt::Dispatcher::Main().RunPending();
        }
    }
    void AdvanceUntilIdle() { Advance(2000); }

    // createMockState: distinct states, told apart by their anchor.
    State MockState(int id) {
        State s;
        s.anchorPosition = id;
        s.config = PagingConfig{ .pageSize = 1 }.Resolved();
        return s;
    }

    // RemoteMediatorMock: records loads (type and the state's anchor), each taking loadDelay ms.
    class Mock : public RemoteMediator<int, int> {
    public:
        int loadDelay = 100;
        // As upstream's mock: tests that want the initial refresh say so.
        InitializeAction initializeResult = InitializeAction::SkipInitialRefresh;
        kiln::Function<MediatorResult(LoadType, const State&)> loadCallback;
        std::vector<std::pair<LoadType, int>> loads;

        // newLoadEvents: the loads since the last call.
        std::vector<std::pair<LoadType, int>> NewLoads() { return std::exchange(loads, {}); }

        rt::Task<MediatorResult> Load(LoadType type, State state) override {
            loads.push_back({ type, state.anchorPosition.value_or(-1) });
            co_await rt::Delay(loadDelay);
            co_return loadCallback ? loadCallback(type, state) : MediatorResult::Succeeded(false);
        }
        rt::Task<InitializeAction> Initialize() override { co_return initializeResult; }
    };

    using Accessor = paging::detail::RemoteMediatorAccessor<int, int>;
    std::shared_ptr<Accessor> CreateAccessor(std::shared_ptr<Mock> mock) {
        auto accessor = std::make_shared<Accessor>(mock);
        rt::Job init = rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<Accessor> a) -> rt::Task<> { co_await a->Initialize(); }(accessor));
        rt::Dispatcher::Main().RunPending();
        return accessor;
    }
    using Loads = std::vector<std::pair<LoadType, int>>;
    const LoadState kIncomplete = LoadState::NotLoading(false), kComplete = LoadState::NotLoading(true);
    LoadStates Of(LoadState refresh, LoadState prepend, LoadState append) { return { refresh, prepend, append }; }
}

TEST(RemoteMediator_requestRefreshIfAllowed) {
    // noop: refresh not allowed.
    auto mock = std::make_shared<Mock>();
    auto accessor = CreateAccessor(mock);
    accessor->RequestRefreshIfAllowed(MockState(0));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads().empty());
    // simple: allowRefresh lets one through.
    mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::LaunchInitialRefresh;
    accessor = CreateAccessor(mock);
    accessor->AllowRefresh();
    accessor->RequestRefreshIfAllowed(MockState(0));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 0 } }));
    accessor->RequestRefreshIfAllowed(MockState(0));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads().empty());
    // retry: a failed refresh runs again on retryFailed.
    mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::LaunchInitialRefresh;
    mock->loadCallback = [](LoadType, const State&) { return MediatorResult::Failed(std::runtime_error("x")); };
    accessor = CreateAccessor(mock);
    accessor->AllowRefresh();
    accessor->RequestRefreshIfAllowed(MockState(0));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 0 } }));
    mock->loadCallback = [](LoadType, const State&) { return MediatorResult::Succeeded(false); };
    accessor->RetryFailed(MockState(0));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 0 } }));
}

TEST(RemoteMediator_queuesBoundaryBehindRefresh) {
    auto mock = std::make_shared<Mock>();
    auto accessor = CreateAccessor(mock);
    accessor->RequestLoad(LoadType::Refresh, MockState(1));
    Advance(50);
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 1 } }));
    CHECK(accessor->StateFlow().Value() == Of(LoadState::Loading(), kIncomplete, kIncomplete));
    accessor->RequestLoad(LoadType::Prepend, MockState(1));
    accessor->RequestLoad(LoadType::Append, MockState(1));
    CHECK(mock->NewLoads().empty());
    CHECK(accessor->StateFlow().Value() == Of(LoadState::Loading(), LoadState::Loading(), LoadState::Loading()));
    // The newer state replaces the queued one.
    accessor->RequestLoad(LoadType::Prepend, MockState(2));
    accessor->RequestLoad(LoadType::Append, MockState(2));
    CHECK(mock->NewLoads().empty());
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Prepend, 2 }, { LoadType::Append, 2 } }));
    CHECK(accessor->StateFlow().Value() == Of(kIncomplete, kIncomplete, kIncomplete));
}

TEST(RemoteMediator_cancelledBoundaryRetriesAfterRefresh) {
    auto mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::SkipInitialRefresh;
    auto accessor = CreateAccessor(mock);
    accessor->RequestLoad(LoadType::Prepend, MockState(1));
    accessor->RequestLoad(LoadType::Append, MockState(1));   // queued behind the running prepend
    Advance(50);
    CHECK(mock->NewLoads() == Loads({ { LoadType::Prepend, 1 } }));
    // A refresh cancels the running boundary call.
    accessor->RequestLoad(LoadType::Refresh, MockState(1));
    Advance(50);
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 1 } }));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Prepend, 1 }, { LoadType::Append, 1 } }));
}

TEST(RemoteMediator_queuesBoundaryAfterRefreshFails) {
    auto mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::LaunchInitialRefresh;
    mock->loadCallback = [](LoadType type, const State& s) {
        return type == LoadType::Refresh && s.anchorPosition == 1 ? MediatorResult::Failed(std::runtime_error("load")) : MediatorResult::Succeeded(false);
    };
    auto accessor = CreateAccessor(mock);
    accessor->RequestLoad(LoadType::Prepend, MockState(1));
    accessor->RequestLoad(LoadType::Append, MockState(1));
    accessor->RequestLoad(LoadType::Refresh, MockState(1));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 1 } }));
    const LoadStates failed = accessor->StateFlow().Value();
    CHECK(failed.refresh.IsError() && failed.prepend == kIncomplete && failed.append == kIncomplete);
    accessor->RetryFailed(MockState(2));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 2 }, { LoadType::Prepend, 1 }, { LoadType::Append, 1 } }));
    CHECK(accessor->StateFlow().Value() == Of(kIncomplete, kIncomplete, kIncomplete));
}

TEST(RemoteMediator_refreshEndOfPaginationClearsBoundaryCalls) {
    auto mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::LaunchInitialRefresh;
    mock->loadCallback = [](LoadType, const State&) { return MediatorResult::Succeeded(true); };
    auto accessor = CreateAccessor(mock);
    accessor->RequestLoad(LoadType::Prepend, MockState(1));
    accessor->RequestLoad(LoadType::Append, MockState(1));
    accessor->RequestLoad(LoadType::Refresh, MockState(1));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Refresh, 1 } }));
    CHECK(accessor->StateFlow().Value() == Of(kIncomplete, kComplete, kComplete));
}

// load_reportsPrependLoadState / load_conflatesAppend / load_concurrentBoundaryJobsRunsSerially.
TEST(RemoteMediator_boundaryStatesConflationAndSerial) {
    auto mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::SkipInitialRefresh;
    auto accessor = CreateAccessor(mock);
    accessor->RequestLoad(LoadType::Prepend, MockState(1));
    CHECK(accessor->StateFlow().Value().prepend == LoadState::Loading());
    accessor->RequestLoad(LoadType::Prepend, MockState(2));   // conflated into the queued one
    accessor->RequestLoad(LoadType::Append, MockState(1));
    Advance(50);
    CHECK(mock->NewLoads() == Loads({ { LoadType::Prepend, 1 } }));   // one at a time
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Append, 1 } }));
    CHECK(accessor->StateFlow().Value() == Of(kIncomplete, kIncomplete, kIncomplete));
    // End of pagination blocks that end; an error shows until retried.
    mock->loadCallback = [](LoadType type, const State&) { return type == LoadType::Append ? MediatorResult::Succeeded(true) : MediatorResult::Failed(std::runtime_error("p")); };
    accessor->RequestLoad(LoadType::Append, MockState(3));
    accessor->RequestLoad(LoadType::Prepend, MockState(3));
    AdvanceUntilIdle();
    CHECK(accessor->StateFlow().Value().append == kComplete && accessor->StateFlow().Value().prepend.IsError());
    accessor->RequestLoad(LoadType::Append, MockState(4));   // completed: ignored
    accessor->RequestLoad(LoadType::Prepend, MockState(4));  // in error: ignored until retried
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Append, 3 }, { LoadType::Prepend, 3 } }));
    mock->loadCallback = {};
    accessor->RetryFailed(MockState(5));
    AdvanceUntilIdle();
    CHECK(mock->NewLoads() == Loads({ { LoadType::Prepend, 5 } }));
}

// A Pager with a mediator: the first collection refreshes the remote, the source's ends call the
// mediator, and the events carry its states.
namespace {
    class Numbers : public PagingSource<int, int> {
    public:
        explicit Numbers(int count) : m_Count(count) {}
        rt::Task<Result> Load(Params p) override {
            const int start = p.key.value_or(0);
            const int end = std::min(m_Count, start + p.loadSize);
            Page page;
            for (int i = start; i < end; ++i) page.data.push_back(i);
            if (start > 0) page.prevKey = std::max(0, start - p.loadSize);
            if (end < m_Count) page.nextKey = end;
            co_return page;
        }
        std::optional<int> GetRefreshKey(const State&) override { return std::nullopt; }

    private:
        int m_Count;
    };
}

TEST(RemoteMediator_pagerCallsAtTheEnds) {
    auto mock = std::make_shared<Mock>();
    mock->initializeResult = InitializeAction::LaunchInitialRefresh;
    mock->loadDelay = 10;
    Pager<int, int> pager({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 10 }, [] { return std::make_shared<Numbers>(10); }, std::nullopt, mock);
    std::vector<std::optional<LoadStates>> mediatorStates;
    PagingDataPresenter<int> presenter;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(pager.Flow(), [&](PagingData<int> d) { return presenter.CollectFrom(std::move(d)); }));
    presenter.AddLoadStateListener([&](const CombinedLoadStates& s) { mediatorStates.push_back(s.mediator); });
    AdvanceUntilIdle();
    // LaunchInitialRefresh: the remote refresh first, then both ends (the source has 10 items).
    const auto loads = mock->NewLoads();
    CHECK(!loads.empty() && loads.front().first == LoadType::Refresh);
    CHECK(std::count_if(loads.begin(), loads.end(), [](const auto& l) { return l.first == LoadType::Append; }) == 1);
    CHECK(std::count_if(loads.begin(), loads.end(), [](const auto& l) { return l.first == LoadType::Prepend; }) == 1);
    CHECK(presenter.Snapshot().items.size() == 10);
    // The mediator's states reached the presenter: loading at some point, idle at the end.
    CHECK(!mediatorStates.empty() && mediatorStates.back() && mediatorStates.back()->IsIdle());
    CHECK(std::any_of(mediatorStates.begin(), mediatorStates.end(), [](const auto& s) { return s && s->refresh == LoadState::Loading(); }));
    // A refresh asked for refreshes the remote again; an invalidation alone does not.
    pager.Refresh();
    AdvanceUntilIdle();
    const auto again = mock->NewLoads();
    CHECK(!again.empty() && again.front().first == LoadType::Refresh);
    job.Cancel();
    AdvanceUntilIdle();
}
