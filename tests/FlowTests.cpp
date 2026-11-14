// Flows (kotlinx.coroutines flow/): the cases of its operator, StateFlow, SharedFlow and
// sharing tests, on the main dispatcher with virtual time.
#include "Check.h"

#include "kiln/runtime/Flow.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

using namespace kiln;
using namespace kiln::rt;

namespace {
    // Runs `flow` to completion and returns what it emitted.
    template<typename T>
    std::vector<T> Collected(Flow<T> flow) {
        auto out = std::make_shared<std::vector<T>>();
        Job job = Launch(Dispatcher::Main(), [](Flow<T> f, std::shared_ptr<std::vector<T>> o) -> Task<> { *o = co_await ToList(f); }(flow, out));
        for (int i = 0; i < 100 && !job.Done(); ++i) {
            Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(100));
            Dispatcher::Main().RunPending();
        }
        return *out;
    }
    void Advance(int ms) {
        Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(ms));
        Dispatcher::Main().RunPending();
    }
    Flow<int> Counting(int n, std::shared_ptr<int> emitted, int delayMs = 0) {
        return Flow<int>::Create([n, emitted, delayMs](FlowCollector<int> emit) -> Task<> {
            for (int i = 1; i <= n; ++i) {
                if (delayMs) co_await Delay(delayMs);
                ++*emitted;
                co_await emit(i);
            }
        });
    }
}

TEST(FlowOperatorsTransformInOrder) {
    auto n = std::make_shared<int>(0);
    CHECK((Collected(Map(Filter(Counting(6, n), [](int i) { return i % 2 == 0; }), [](int i) { return i * 10; })) == std::vector<int>{ 20, 40, 60 }));
    CHECK((Collected(Scan(FlowOf(1, 2, 3), 0, [](int acc, int v) { return acc + v; })) == std::vector<int>{ 0, 1, 3, 6 }));
    CHECK((Collected(DistinctUntilChanged(FlowOf(1, 1, 2, 2, 1))) == std::vector<int>{ 1, 2, 1 }));
    CHECK((Collected(Drop(FlowOf(1, 2, 3, 4), 2)) == std::vector<int>{ 3, 4 }));
    CHECK((Collected(MapNotNull(FlowOf(1, 2, 3), [](int i) { return i == 2 ? std::nullopt : std::optional<std::string>(std::to_string(i)); }))
           == std::vector<std::string>{ "1", "3" }));
    std::vector<int> seen;
    CHECK((Collected(OnEach(FlowOf(5, 6), [&](int i) { seen.push_back(i); })) == std::vector<int>{ 5, 6 }));
    CHECK((seen == std::vector<int>{ 5, 6 }));
    CHECK((Collected(Transform<int, int>(FlowOf(1, 2), [](int v, FlowCollector<int> emit) -> Task<> { co_await emit(v); co_await emit(-v); }))
           == std::vector<int>{ 1, -1, 2, -2 }));
    CHECK((Collected(OnStart<int>(FlowOf(2), [](FlowCollector<int> emit) -> Task<> { co_await emit(1); })) == std::vector<int>{ 1, 2 }));
}

TEST(FlowTakeStopsTheUpstream) {
    auto n = std::make_shared<int>(0);
    CHECK((Collected(Take(Counting(100, n), 3)) == std::vector<int>{ 1, 2, 3 }));
    CHECK(*n == 3);   // upstream stopped after the third
    *n = 0;
    std::optional<int> first;
    Job job = Launch(Dispatcher::Main(), [](std::shared_ptr<int> c, std::optional<int>* out) -> Task<> { *out = co_await First(Counting(50, c)); }(n, &first));
    Dispatcher::Main().RunPending();
    CHECK(first == std::optional<int>(1) && *n == 1);
    std::optional<int> found;
    Job job2 = Launch(Dispatcher::Main(), [](std::optional<int>* out) -> Task<> {
        *out = co_await FirstOrNull(FlowOf(1, 4, 9, 16), [](const int& v) { return v > 5; });
    }(&found));
    Dispatcher::Main().RunPending();
    CHECK(found == std::optional<int>(9));
}

TEST(FlowCatchHandlesUpstreamFailuresOnly) {
    auto failing = Flow<int>::Create([](FlowCollector<int> emit) -> Task<> {
        co_await emit(1);
        throw std::runtime_error("boom");
    });
    auto recovered = Catch<int>(failing, [](std::exception_ptr, FlowCollector<int> emit) -> Task<> { co_await emit(-1); });
    CHECK((Collected(recovered) == std::vector<int>{ 1, -1 }));
    std::exception_ptr completedWith;
    bool completed = false;
    auto watched = OnCompletion<int>(FlowOf(1), [&](std::exception_ptr e) { completed = true; completedWith = e; });
    CHECK((Collected(watched) == std::vector<int>{ 1 }));
    CHECK(completed && !completedWith);
    // A downstream failure passes through catch.
    bool rethrown = false;
    Job job = Launch(Dispatcher::Main(), [](Flow<int> f, bool* out) -> Task<> {
        try {
            co_await f.Collect(FlowCollector<int>([](int) -> Task<> { throw std::logic_error("downstream"); }));
        } catch (const std::logic_error&) {
            *out = true;
        }
    }(Catch<int>(FlowOf(1), [](std::exception_ptr, FlowCollector<int>) -> Task<> { co_return; }), &rethrown));
    Dispatcher::Main().RunPending();
    CHECK(rethrown);
}

TEST(FlowCombineZipAndMerge) {
    auto a = std::make_shared<int>(0), b = std::make_shared<int>(0);
    // a: 1 @100, 2 @200, 3 @300; b: 10 @150, 20 @300.
    auto fa = Counting(3, a, 100);
    auto fb = Map(Counting(2, b, 150), [](int i) { return i * 10; });
    const auto combined = Collected(Combine(fa, fb, [](int x, int y) { return x + y; }));
    CHECK(!combined.empty() && combined.front() == 11 && combined.back() == 23);
    CHECK((Collected(Zip(FlowOf(1, 2, 3), FlowOf<std::string>("a", "b"), [](int i, const std::string& s) { return std::to_string(i) + s; }))
           == std::vector<std::string>{ "1a", "2b" }));
    auto merged = Collected(Merge<int>({ FlowOf(1, 2), FlowOf(3) }));
    std::sort(merged.begin(), merged.end());
    CHECK((merged == std::vector<int>{ 1, 2, 3 }));
}

TEST(FlowDebounceFlatMapLatestAndConflate) {
    // Values at 0, 50, 300 ms with a 100 ms debounce: 2 (after the burst) and 3 (at completion).
    auto bursts = Flow<int>::Create([](FlowCollector<int> emit) -> Task<> {
        co_await emit(1);
        co_await Delay(50);
        co_await emit(2);
        co_await Delay(250);
        co_await emit(3);
    });
    CHECK((Collected(Debounce(bursts, 100)) == std::vector<int>{ 2, 3 }));
    // flatMapLatest: a new upstream value cancels the previous inner flow.
    auto queries = Flow<int>::Create([](FlowCollector<int> emit) -> Task<> {
        co_await emit(1);
        co_await Delay(150);
        co_await emit(2);
    });
    auto results = FlatMapLatest(queries, [](const int& q) {
        return Flow<std::string>::Create([q](FlowCollector<std::string> emit) -> Task<> {
            co_await emit(std::to_string(q) + ":start");
            co_await Delay(200);
            co_await emit(std::to_string(q) + ":done");
        });
    });
    CHECK((Collected(results) == std::vector<std::string>{ "1:start", "2:start", "2:done" }));
    // conflate: a slow collector sees the latest, and always the last.
    std::vector<int> slow;
    Job job = Launch(Dispatcher::Main(), [](std::vector<int>* out) -> Task<> {
        co_await Conflate(FlowOf(1, 2, 3, 4)).Collect(FlowCollector<int>([out](int v) -> Task<> {
            return [](std::vector<int>* o, int value) -> Task<> {
                o->push_back(value);
                co_await Delay(100);
            }(out, v);
        }));
    }(&slow));
    for (int i = 0; i < 10; ++i) Advance(100);
    // A synchronous upstream finishes before the collector runs: only the last is certain.
    CHECK(!slow.empty() && slow.back() == 4 && slow.size() < 4);
}

TEST(MutableStateFlowEmitsDistinctLatestValues) {
    MutableStateFlow<int> state(1);
    std::vector<int> seen;
    Job job = Launch(Dispatcher::Main(), state.Collect([&](const int& v) { seen.push_back(v); }));
    Dispatcher::Main().RunPending();
    CHECK((seen == std::vector<int>{ 1 }) && state.SubscriptionCount() == 1);
    state.SetValue(2);
    state.SetValue(2);   // equal: nothing
    Dispatcher::Main().RunPending();
    CHECK((seen == std::vector<int>{ 1, 2 }));
    state.Update([](int v) { return v + 10; });
    CHECK(!state.CompareAndSet(5, 6));
    CHECK(state.CompareAndSet(12, 13));
    Dispatcher::Main().RunPending();
    CHECK(seen.back() == 13 && state.Value() == 13);
    job.Cancel();
    CHECK(state.SubscriptionCount() == 0);
}

TEST(MutableSharedFlowReplaysToNewCollectors) {
    MutableSharedFlow<std::string> events(2);
    events.TryEmit("a");
    events.TryEmit("b");
    events.TryEmit("c");
    std::vector<std::string> seen;
    Job job = Launch(Dispatcher::Main(), events.Collect([&](const std::string& v) { seen.push_back(v); }));
    Dispatcher::Main().RunPending();
    CHECK((seen == std::vector<std::string>{ "b", "c" }));
    events.TryEmit("d");
    Dispatcher::Main().RunPending();
    CHECK(seen.back() == "d" && events.SubscriptionCount() == 1);
    MutableSharedFlow<int> plain;
    plain.TryEmit(1);   // no collectors, no replay: gone
    std::vector<int> got;
    Job job2 = Launch(Dispatcher::Main(), plain.Collect([&](const int& v) { got.push_back(v); }));
    Dispatcher::Main().RunPending();
    plain.TryEmit(2);
    Dispatcher::Main().RunPending();
    CHECK((got == std::vector<int>{ 2 }));
}

TEST(StateInSharesTheUpstreamByItsPolicy) {
    auto starts = std::make_shared<int>(0);
    auto source = Flow<int>::Create([starts](FlowCollector<int> emit) -> Task<> {
        ++*starts;
        for (int i = 1;; ++i) {
            co_await emit(i);
            co_await Delay(100);
        }
    });
    auto scope = std::make_shared<CoroutineScope>();
    // WhileSubscribed(500): starts with the first collector, stops 500 ms after the last.
    auto state = StateIn(source, scope, SharingStarted::WhileSubscribed(500), 0);
    Dispatcher::Main().RunPending();
    CHECK(*starts == 0 && state.Value() == 0);
    std::vector<int> a, b;
    Job ja = Launch(Dispatcher::Main(), state.Collect([&](const int& v) { a.push_back(v); }));
    Job jb = Launch(Dispatcher::Main(), state.Collect([&](const int& v) { b.push_back(v); }));
    Dispatcher::Main().RunPending();
    for (int i = 0; i < 5; ++i) Advance(50);   // timers run in steps, as time passes
    CHECK(*starts == 1);   // one upstream for both
    CHECK(state.Value() >= 3);
    ja.Cancel();
    jb.Cancel();
    for (int i = 0; i < 20; ++i) Advance(50);   // past the 500 ms timeout
    const int stopped = state.Value();
    for (int i = 0; i < 10; ++i) Advance(50);
    CHECK(state.Value() == stopped);   // the upstream stopped
    // A new collector starts it again.
    std::vector<int> c;
    Job jc = Launch(Dispatcher::Main(), state.Collect([&](const int& v) { c.push_back(v); }));
    Dispatcher::Main().RunPending();
    CHECK(*starts == 2);
    jc.Cancel();
    // Eagerly: running before any collector; a cancelled scope stops it.
    auto eager = StateIn(source, scope, SharingStarted::Eagerly(), -1);
    Dispatcher::Main().RunPending();
    CHECK(eager.Value() == 1);
    auto jobs = std::move(scope->jobs);
    jobs.clear();   // the scope cancelled
    Advance(300);
    CHECK(eager.Value() == 1);
}

TEST(CollectLatestCancelsThePreviousHandler) {
    std::vector<std::string> log;
    Job job = Launch(Dispatcher::Main(), [](std::vector<std::string>* out) -> Task<> {
        auto values = Flow<int>::Create([](FlowCollector<int> emit) -> Task<> {
            co_await emit(1);
            co_await Delay(50);
            co_await emit(2);
        });
        co_await CollectLatest<int>(values, [out](int v) -> Task<> {
            out->push_back("start " + std::to_string(v));
            co_await Delay(100);
            out->push_back("end " + std::to_string(v));
        });
    }(&log));
    for (int i = 0; i < 5; ++i) Advance(100);
    CHECK((log == std::vector<std::string>{ "start 1", "start 2", "end 2" }));
}

#include "kiln/ui/LayoutNode.h"

TEST(CollectAsStateFollowsAFlowWhileComposed) {
    MutableStateFlow<int> count(5);
    std::vector<int> seen;
    {
        ui::Owner owner;
        Composition composition{ owner.Root(), Dispatcher::Main() };
        composition.SetContent([&] { seen.push_back(CollectAsState(count).Get()); });
        Composition::RecomposeAll();
        Dispatcher::Main().RunPending();
        CHECK(!seen.empty() && seen.back() == 5);
        count.SetValue(6);
        Dispatcher::Main().RunPending();
        Observation::ApplyChanges();
        Composition::RecomposeAll();
        CHECK(seen.back() == 6);
        const std::size_t compositions = seen.size();
        Composition::RecomposeAll();
        CHECK(seen.size() == compositions);   // the collection did not restart and recompose
        composition.SetContent([] {});
        Composition::RecomposeAll();
    }
    CHECK(count.SubscriptionCount() == 0);
}
