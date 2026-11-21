// Paging (paging-common's PagingStateTest, PageFetcherSnapshotTest, PagingDataPresenterTest and
// CachedPageEventFlowTest cases; paging-compose's LazyPagingItemsTest): loading around what is
// read, load states, retry, invalidation, drops over maxSize, cachedIn and content.
#include "Check.h"

#include "kiln/paging/LazyPagingItems.h"
#include "kiln/paging/Pager.h"
#include "kiln/ui/LayoutNode.h"

#include <numeric>

using namespace kiln;
using namespace kiln::paging;

namespace {
    void Pump(int rounds = 8) {
        for (int i = 0; i < rounds; ++i) rt::Dispatcher::Main().RunPending();
    }

    // TestPagingSource: the items 0..count-1 keyed by position, with placeholders counted.
    struct Log {
        std::vector<std::string> loads;
        int sources = 0;
        bool failNext = false, invalidNext = false, async = false;
    };
    class TestSource : public PagingSource<int, int> {
    public:
        TestSource(std::shared_ptr<Log> log, int count) : m_Log(std::move(log)), m_Count(count) { ++m_Log->sources; }
        rt::Task<Result> Load(Params params) override {
            const char* type = params.type == LoadType::Refresh ? "refresh" : params.type == LoadType::Prepend ? "prepend" : "append";
            m_Log->loads.push_back(std::string(type) + " " + (params.key ? std::to_string(*params.key) : "null") + " " + std::to_string(params.loadSize));
            if (m_Log->async) co_await rt::Yield();
            if (std::exchange(m_Log->failNext, false)) co_return LoadError::Of(std::runtime_error("load failed"));
            if (std::exchange(m_Log->invalidNext, false)) co_return LoadInvalid{};
            int start, end;
            if (params.type == LoadType::Prepend) {
                end = *params.key + 1;
                start = std::max(0, end - params.loadSize);
            } else {
                start = params.key.value_or(0);
                end = std::min(m_Count, start + params.loadSize);
            }
            Page page;
            for (int i = start; i < end; ++i) page.data.push_back(i);
            if (start > 0) page.prevKey = start - 1;
            if (end < m_Count) page.nextKey = end;
            page.itemsBefore = start;
            page.itemsAfter = m_Count - end;
            co_return page;
        }
        std::optional<int> GetRefreshKey(const State& state) override {
            return state.anchorPosition;
        }
        bool JumpingSupported() const override { return true; }

    private:
        std::shared_ptr<Log> m_Log;
        int m_Count;
    };

    struct Fixture {
        std::shared_ptr<Log> log = std::make_shared<Log>();
        std::shared_ptr<TestSource> current;
        Pager<int, int> pager;
        std::vector<PagingDataEvent<int>::Kind> changes;
        PagingDataPresenter<int> presenter{ [this](const PagingDataEvent<int>& e) { changes.push_back(e.kind); } };
        rt::Job job;

        explicit Fixture(PagingConfig config, int count = 100, std::optional<int> initialKey = {})
            : pager(config, [this, count]() -> std::shared_ptr<PagingSource<int, int>> { return current = std::make_shared<TestSource>(log, count); }, initialKey) {}
        void Collect() {
            job = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(pager.Flow(), [this](PagingData<int> d) { return presenter.CollectFrom(std::move(d)); }));
            Pump();
        }
        std::vector<int> Items() const { return presenter.Snapshot().items; }
        const CombinedLoadStates& States() const { return *presenter.LoadStateFlow().Value(); }
        std::vector<int> Range(int from, int to) const {
            std::vector<int> v(std::size_t(to - from));
            std::iota(v.begin(), v.end(), from);
            return v;
        }
    };
}

TEST(PagingConfigFillsDefaultsAndChecks) {
    const PagingConfig c = PagingConfig{ .pageSize = 10 }.Resolved();
    CHECK(c.prefetchDistance == 10 && c.initialLoadSize == 30 && c.enablePlaceholders && c.maxSize == kMaxSizeUnbounded);
    bool threw = false;
    try { PagingConfig{ .pageSize = 10, .prefetchDistance = 0, .enablePlaceholders = false }.Resolved(); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { PagingConfig{ .pageSize = 10, .prefetchDistance = 10, .maxSize = 20 }.Resolved(); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(PagingStateFindsTheClosestItemAndPage) {
    PagingState<int, int> state;
    state.pages = { { { 10, 11 }, 9, 12 }, { {}, 11, 12 }, { { 12, 13, 14 }, 11, {} } };
    state.leadingPlaceholderCount = 10;
    CHECK(!state.IsEmpty() && *state.FirstItemOrNull() == 10 && *state.LastItemOrNull() == 14);
    CHECK(*state.ClosestItemToPosition(3) == 10);     // in the placeholders before
    CHECK(*state.ClosestItemToPosition(11) == 11);
    CHECK(*state.ClosestItemToPosition(13) == 13);
    CHECK(*state.ClosestItemToPosition(40) == 14);    // past the end
    CHECK(state.ClosestPageToPosition(3) == &state.pages[0] && state.ClosestPageToPosition(13) == &state.pages[2]);
    CHECK(*state.ClosestItemAroundPosition(11, [](int v) { return v % 2 == 0; }) == 10);
    CHECK(*state.ClosestItemAroundPosition(12, [](int v) { return v == 14; }) == 14);
    CHECK(state.ClosestItemAroundPosition(12, [](int) { return false; }) == nullptr);
    PagingState<int, int> empty;
    CHECK(empty.IsEmpty() && empty.ClosestItemToPosition(0) == nullptr && empty.FirstItemOrNull() == nullptr);
}

TEST(PagingLoadsTheFirstPageThenAroundWhatIsRead) {
    for (const bool async : { false, true }) {
        Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 20 });
        f.log->async = async;
        CHECK(f.presenter.Size() == 0);
        f.Collect();
        CHECK((f.log->loads == std::vector<std::string>{ "refresh null 20" }));
        CHECK(f.Items() == f.Range(0, 20) && f.presenter.Size() == 100);   // 80 placeholders after
        CHECK(f.States().refresh == LoadState::NotLoading(false));
        CHECK(f.States().prepend == LoadState::NotLoading(true));          // nothing before item 0
        CHECK(f.States().append == LoadState::NotLoading(false));
        CHECK(f.presenter.Peek(50) == nullptr && f.presenter.Peek(3) && *f.presenter.Peek(3) == 3);

        // Reading far from the end loads nothing; within prefetchDistance of it, the next page.
        CHECK(*f.presenter.Get(5) == 5);
        Pump();
        CHECK(f.log->loads.size() == 1);
        CHECK(*f.presenter.Get(16) == 16);
        Pump();
        CHECK((f.log->loads.back() == "append 20 10") && f.log->loads.size() == 2 && f.Items() == f.Range(0, 30));
        // A placeholder: loads until the read position has prefetchDistance items beyond it.
        CHECK(f.presenter.Get(44) == nullptr);
        Pump(20);
        CHECK(f.Items() == f.Range(0, 50) && *f.presenter.Peek(44) == 44);
        CHECK(f.States().append == LoadState::NotLoading(false));
        CHECK(f.changes.front() == PagingDataEvent<int>::Kind::Refresh && f.changes.back() == PagingDataEvent<int>::Kind::Append);
    }
}

TEST(PagingLoadsBothWaysFromAnInitialKeyAndReachesTheEnds) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 3, .initialLoadSize = 10 }, 35, 20);
    f.Collect();
    CHECK(f.Items() == f.Range(20, 30) && f.presenter.Size() == 35);
    CHECK(f.States().prepend == LoadState::NotLoading(false) && f.States().append == LoadState::NotLoading(false));
    CHECK(*f.presenter.Get(21) == 21);
    Pump();
    CHECK(f.Items() == f.Range(10, 30) && (f.log->loads.back() == "prepend 19 10"));
    f.presenter.Get(28);
    Pump();
    CHECK(f.Items() == f.Range(10, 35) && f.States().append == LoadState::NotLoading(true));
    f.presenter.Get(10);
    Pump();
    CHECK(f.Items() == f.Range(0, 35) && f.States().prepend == LoadState::NotLoading(true));
    const std::size_t loads = f.log->loads.size();
    f.presenter.Get(0);
    f.presenter.Get(34);
    Pump();
    CHECK(f.log->loads.size() == loads);   // both ends reached: nothing more to load
}

TEST(PagingReportsErrorsAndRetries) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 10 });
    f.log->failNext = true;
    f.Collect();
    CHECK(f.States().refresh.IsError() && f.States().refresh.ErrorMessage() == "load failed" && f.States().HasError());
    CHECK(f.Items().empty());
    f.presenter.Retry();
    Pump();
    CHECK(f.States().refresh == LoadState::NotLoading(false) && f.Items() == f.Range(0, 10));

    f.log->failNext = true;
    f.presenter.Get(8);
    Pump();
    CHECK(f.States().append.IsError() && f.Items() == f.Range(0, 10));
    // Reading again does not load while the error stands; retry does.
    const std::size_t loads = f.log->loads.size();
    f.presenter.Get(9);
    Pump();
    CHECK(f.log->loads.size() == loads);
    f.presenter.Retry();
    Pump();
    CHECK(f.States().append == LoadState::NotLoading(false) && f.Items() == f.Range(0, 20));
}

TEST(PagingStartsOverWhenTheSourceIsInvalidated) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 2, .initialLoadSize = 10 });
    f.Collect();
    f.presenter.Get(7);
    Pump();
    auto first = f.current;
    CHECK(f.log->sources == 1 && !first->Invalid());
    first->Invalidate();
    Pump();
    // A new source, loaded around the last read position (GetRefreshKey: the anchor).
    CHECK(f.log->sources == 2 && first->Invalid() && f.current != first);
    CHECK((f.log->loads.back() == "refresh 7 10") && f.Items() == f.Range(7, 17) && f.presenter.Size() == 100);
    CHECK(f.changes.back() == PagingDataEvent<int>::Kind::Refresh);

    // The presenter's refresh does the same; the Pager's reloads what is loaded.
    f.presenter.Get(12);
    Pump();
    f.presenter.Refresh();
    Pump();
    CHECK(f.log->sources == 3 && (f.log->loads.back() == "refresh 12 10"));
    f.presenter.Get(19);
    Pump();
    const int loaded = int(f.Items().size());
    f.pager.Refresh();
    Pump();
    CHECK(f.log->sources == 4 && (f.log->loads.back() == "refresh 12 " + std::to_string(loaded)) && int(f.Items().size()) == loaded);

    // A load that answers Invalid invalidates the source too.
    f.log->invalidNext = true;
    f.presenter.Get(f.presenter.Snapshot().placeholdersBefore + loaded - 1);
    Pump();
    CHECK(f.log->sources == 5 && f.States().refresh == LoadState::NotLoading(false));
}

TEST(PagingAppendsAndPrependsOnRequest) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 1, .initialLoadSize = 10 }, 100, 40);
    f.Collect();
    CHECK(f.Items() == f.Range(40, 50));
    f.pager.Append();
    Pump();
    CHECK(f.Items() == f.Range(40, 60));
    f.pager.Prepend();
    Pump();
    CHECK(f.Items() == f.Range(30, 60));
}

TEST(PagingDropsPagesOverMaxSize) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 10, .maxSize = 30 });
    f.Collect();
    for (const int read : { 8, 18, 28 }) {
        f.presenter.Get(read);
        Pump();
    }
    // Four pages would be 40 items: the first was dropped, and counts as placeholders again.
    CHECK(f.Items() == f.Range(10, 40) && f.presenter.Snapshot().placeholdersBefore == 10 && f.presenter.Size() == 100);
    CHECK(std::find(f.changes.begin(), f.changes.end(), PagingDataEvent<int>::Kind::DropPrepend) != f.changes.end());
    CHECK(f.States().prepend == LoadState::NotLoading(false));
    // Reading back toward the start loads it again and drops from the end.
    f.presenter.Get(12);
    Pump();
    CHECK(f.Items() == f.Range(0, 30) && f.presenter.Snapshot().placeholdersAfter == 70);
}

TEST(PagingJumpsWhenAnAccessIsFarPastTheLoadedItems) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 10, .jumpThreshold = 20 });
    f.Collect();
    CHECK(f.Items() == f.Range(0, 10));
    f.presenter.Get(70);   // 60 placeholders past the loaded items
    Pump(20);
    CHECK(f.log->sources == 2 && (f.log->loads.back() == "refresh 70 10") && f.Items() == f.Range(70, 80));
}

TEST(PagingDataMapsFiltersAndPresentsStaticLists) {
    PagingDataPresenter<std::string> presenter;
    auto data = PagingData<int>::From({ 1, 2, 3, 4, 5, 6 })
        .Filter([](int v) { return v % 2 == 0; })
        .Map([](int v) { return "n" + std::to_string(v); });
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), presenter.CollectFrom(data));
    Pump();
    CHECK((presenter.Snapshot().items == std::vector<std::string>{ "n2", "n4", "n6" }));
    CHECK(data.CachedEvent() && data.CachedEvent()->pages.front().data.size() == 3);

    // Through a pager: hints still reach the source's positions after a filter.
    auto log = std::make_shared<Log>();
    Pager<int, int> pager({ .pageSize = 10, .prefetchDistance = 2, .initialLoadSize = 10 }, [log]() -> std::shared_ptr<PagingSource<int, int>> { return std::make_shared<TestSource>(log, 50); });
    PagingDataPresenter<int> evens;
    auto mapped = rt::Map(pager.Flow(), [](const PagingData<int>& d) { return d.Filter([](int v) { return v % 2 == 0; }); });
    rt::Job job2 = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(mapped, [&](PagingData<int> d) { return evens.CollectFrom(std::move(d)); }));
    Pump();
    CHECK((evens.Snapshot().items == std::vector<int>{ 0, 2, 4, 6, 8 }));
    evens.Get(4);   // the last presented item: source index 8, within prefetch of the end
    Pump();
    CHECK(evens.Snapshot().items.size() == 10 && log->loads.size() == 2);
}

TEST(CachedInSharesWhatIsLoaded) {
    auto log = std::make_shared<Log>();
    auto scope = std::make_shared<rt::CoroutineScope>();
    Pager<int, int> pager({ .pageSize = 10, .prefetchDistance = 3, .initialLoadSize = 10 }, [log]() -> std::shared_ptr<PagingSource<int, int>> { return std::make_shared<TestSource>(log, 60); });
    PagingDataFlow<int> cached = CachedIn(pager.Flow(), scope);
    CHECK(!cached.Replay() && log->loads.empty());   // lazily: nothing until collected

    auto first = std::make_unique<PagingDataPresenter<int>>();
    rt::Job job1 = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(cached.flow, [&](PagingData<int> d) { return first->CollectFrom(std::move(d)); }));
    Pump();
    first->Get(8);
    Pump();
    CHECK(first->Snapshot().items.size() == 20 && log->loads.size() == 2 && log->sources == 1);

    // The first collector goes away (a screen left); loading and its data stay with the scope.
    job1.Cancel();
    first.reset();
    CHECK(cached.Replay() && cached.Replay()->CachedEvent() && cached.Replay()->CachedEvent()->pages.size() == 2);
    // A new presenter shows the data at once, then keeps loading on the same source.
    PagingDataPresenter<int> second({}, cached.Replay());
    CHECK(second.Snapshot().items.size() == 20 && second.Size() == 60);
    rt::Job job2 = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(cached.flow, [&](PagingData<int> d) { return second.CollectFrom(std::move(d)); }));
    Pump();
    CHECK(second.Snapshot().items.size() == 20 && log->loads.size() == 2 && log->sources == 1);
    second.Get(18);
    Pump();
    CHECK(second.Snapshot().items.size() == 30 && log->loads.size() == 3);
    // Two at once see the same events.
    PagingDataPresenter<int> third({}, cached.Replay());
    rt::Job job3 = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(cached.flow, [&](PagingData<int> d) { return third.CollectFrom(std::move(d)); }));
    Pump();
    second.Get(28);
    Pump();
    CHECK(second.Snapshot().items.size() == 40 && third.Snapshot().items.size() == 40 && log->loads.size() == 4);

    // Without cachedIn a generation is collected once.
    Pager<int, int> bare({ .pageSize = 10 }, [log]() -> std::shared_ptr<PagingSource<int, int>> { return std::make_shared<TestSource>(log, 60); });
    std::optional<PagingData<int>> data;
    rt::Job grab = rt::Launch(rt::Dispatcher::Main(), bare.Flow().Collect([&](const PagingData<int>& d) { data = d; }));
    PagingDataPresenter<int> a, b;
    rt::Job ja = rt::Launch(rt::Dispatcher::Main(), a.CollectFrom(*data));
    Pump();
    bool twice = false;
    rt::Job jb = rt::Launch(rt::Dispatcher::Main(), [](PagingDataPresenter<int>* p, PagingData<int> d, bool* twice) -> rt::Task<> {
        try { co_await p->CollectFrom(std::move(d)); } catch (const std::logic_error&) { *twice = true; }
    }(&b, *data, &twice));
    Pump();
    CHECK(twice);
    scope->jobs.clear();
}

TEST(LazyPagingItemsPresentPagedDataInContent) {
    auto log = std::make_shared<Log>();
    auto scope = std::make_shared<rt::CoroutineScope>();
    Pager<int, int> pager({ .pageSize = 10, .prefetchDistance = 3, .initialLoadSize = 10 }, [log]() -> std::shared_ptr<PagingSource<int, int>> { return std::make_shared<TestSource>(log, 40); });
    PagingDataFlow<int> flow = CachedIn(pager.Flow(), scope);
    int count = -1;
    rt::State<int> readAt(0);
    std::optional<int> got;
    bool refreshLoading = false, appendEnd = false;
    std::uint64_t key0 = 0, key39 = 0;
    LazyPagingItems<int>* items = nullptr;
    auto settle = [] {
        for (int i = 0; i < 6; ++i) {
            rt::Dispatcher::Main().RunPending();
            rt::Observation::ApplyChanges();
            rt::Composition::RecomposeAll();
        }
    };
    {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        composition.SetContent([&] {
            auto& lazy = CollectAsLazyPagingItems(flow);
            items = &lazy;
            count = lazy.ItemCount();
            refreshLoading = lazy.LoadState().refresh.IsLoading();
            appendEnd = lazy.LoadState().append.endOfPaginationReached;
            const int read = readAt.Get();
            if (read < count) {
                const int* v = lazy.Get(read);
                got = v ? std::optional<int>(*v) : std::nullopt;
            }
            auto keyOf = ItemKey(lazy, [](const int& v) { return v; });
            if (count > 0) {
                key0 = keyOf(0).value;
                key39 = keyOf(39).value;
            }
        });
        rt::Composition::RecomposeAll();
        CHECK(count == 0 && refreshLoading);
        settle();
        CHECK(count == 40 && !refreshLoading && got == std::optional<int>(0) && !appendEnd);
        CHECK(key0 == rt::Key(0).value && key39 != rt::Key(39).value);   // an item's key, a placeholder's
        // Reading near the end of what is loaded brings the next page into content.
        readAt.Set(9);
        settle();
        CHECK(items->ItemSnapshot().items.size() == 20 && got == std::optional<int>(9));
        readAt.Set(39);
        settle();
        settle();
        CHECK(got == std::optional<int>(39) && appendEnd && key39 == rt::Key(39).value);
        composition.SetContent([] {});
        rt::Composition::RecomposeAll();
    }
    // Composed again: the cached data is there in the first composition.
    {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        int firstCount = -1;
        composition.SetContent([&] {
            auto& lazy = CollectAsLazyPagingItems(flow);
            if (firstCount < 0) firstCount = lazy.ItemCount();
        });
        rt::Composition::RecomposeAll();
        CHECK(firstCount == 40);
        composition.SetContent([] {});
        rt::Composition::RecomposeAll();
    }
    scope->jobs.clear();
}

// InvalidatingPagingSourceFactoryTest.
TEST(InvalidatingPagingSourceFactory_invalidatesWhatItMade) {
    auto log = std::make_shared<Log>();
    InvalidatingPagingSourceFactory<int, int> factory([log] { return std::make_shared<TestSource>(log, 10); });
    for (int i = 0; i < 4; ++i) factory();
    CHECK_EQ(factory.PagingSources().size(), 4u);
    // skipInvalidatedPagingSources: one already invalid is not invalidated again.
    factory.PagingSources()[0]->Invalidate();
    int calls = 0;
    for (const auto& s : factory.PagingSources()) s->RegisterInvalidatedCallback([&] { ++calls; });
    CHECK_EQ(calls, 1);   // the invalid one calls at once
    factory.Invalidate();
    CHECK_EQ(calls, 4);
    CHECK(factory.PagingSources().empty());   // invalidateRemoveFromList
    // A source made while invalidating (a new generation) is not invalidated in the same pass.
    std::function<void()> next = [&] { factory()->RegisterInvalidatedCallback([&] { next(); }); };
    next();
    factory.Invalidate();
    CHECK_EQ(factory.PagingSources().size(), 1u);
    CHECK(!factory.PagingSources()[0]->Invalid());
}

// PagingDataPresenterTest: addLoadStateListener (current states at once, then each change, until
// removed), onPagesUpdatedFlow, and a refresh's lists.
TEST(PagingPresenterListenersAndRefreshLists) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 20 });
    std::vector<CombinedLoadStates> heard;
    std::shared_ptr<const PlaceholderPaddedList<int>> before, after;
    PagingDataPresenter<int> presenter([&](const PagingDataEvent<int>& e) {
        if (e.kind == PagingDataEvent<int>::Kind::Refresh) {
            before = e.previousList;
            after = e.newList;
        }
    });
    const int id = presenter.AddLoadStateListener([&](const CombinedLoadStates& s) { heard.push_back(s); });
    CHECK(heard.empty());   // no states yet
    int updates = 0;
    rt::Job counting = rt::Launch(rt::Dispatcher::Main(), presenter.OnPagesUpdatedFlow().Collect([&](const bool&) { ++updates; }));
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), rt::CollectLatest<PagingData<int>>(f.pager.Flow(), [&](PagingData<int> d) { return presenter.CollectFrom(std::move(d)); }));
    Pump();
    CHECK(!heard.empty() && heard.back().refresh == LoadState::NotLoading(false));
    CHECK(before && before->Size() == 0 && after && after->DataCount() == 20 && after->placeholdersAfter == 80 && after->GetItem(3) == 3);
    CHECK(updates >= 1);
    const std::size_t count = heard.size();
    int late = 0;
    presenter.AddLoadStateListener([&](const CombinedLoadStates&) { ++late; });
    CHECK_EQ(late, 1);   // the current states at once
    presenter.RemoveLoadStateListener(id);
    presenter.Get(16);
    Pump();
    CHECK_EQ(heard.size(), count);
    CHECK(late > 1);   // append went loading and back
    job.Cancel();
    counting.Cancel();
    Pump();
}

// ItemSnapshotListFlowTest: the list after each change, with what a filter leaves.
TEST(PagingItemSnapshotListFlow) {
    std::vector<ItemSnapshotList<int>> lists;
    auto flow = rt::FlowOf<PagingData<int>>(PagingData<int>::From({ 1, 2, 3, 4 }).Filter([](int v) { return v % 2 == 0; }));
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), AsItemSnapshotListFlow(flow).Collect([&](const ItemSnapshotList<int>& l) { lists.push_back(l); }));
    Pump();
    CHECK(lists.size() == 1 && lists[0].items == std::vector<int>({ 2, 4 }));
    // A Pager's data: the refresh, then what a read appends.
    Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 20 });
    std::vector<ItemSnapshotList<int>> paged;
    rt::Job job2 = rt::Launch(rt::Dispatcher::Main(), AsItemSnapshotListFlow(f.pager.Flow()).Collect([&](const ItemSnapshotList<int>& l) { paged.push_back(l); }));
    Pump();
    CHECK(!paged.empty() && paged.back().DataCount() == 20 && paged.back().placeholdersAfter == 80);
    job.Cancel();
    job2.Cancel();
    Pump();
}

// awaitNotLoading: returns once the refresh settles.
namespace {
    // Parameters, not captures: a coroutine lambda's captures die with the temporary closure.
    rt::Task<> AwaitInto(const PagingDataPresenter<int>* presenter, std::optional<CombinedLoadStates>* out, bool* done) {
        *out = co_await AwaitNotLoading(presenter->LoadStateFlow());
        *done = true;
    }
}
TEST(PagingAwaitNotLoading) {
    Fixture f({ .pageSize = 10, .prefetchDistance = 5, .initialLoadSize = 20 });
    f.log->async = true;
    std::optional<CombinedLoadStates> settled;
    bool done = false;
    rt::Job waiting = rt::Launch(rt::Dispatcher::Main(), AwaitInto(&f.presenter, &settled, &done));
    f.Collect();
    for (int i = 0; i < 50 && !done; ++i) {
        rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(5));
        Pump();
    }
    CHECK(done && settled && settled->refresh == LoadState::NotLoading(false));
    Page<int, int> page{ { 1, 2, 3 } };
    int sum = 0;
    for (int v : page) sum += v;
    CHECK_EQ(sum, 6);
}
