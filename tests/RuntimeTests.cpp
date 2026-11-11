#include "Check.h"

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/SnapshotFlow.h"
#include "kiln/runtime/Channel.h"

#include <map>
#include <string>

using namespace kiln::rt;

namespace {
    struct Node : TreeNode {
        std::string name;
        std::vector<TreeNode*> children;
        int setChildrenCalls = 0;
        explicit Node(std::string n) : name(std::move(n)) {}
        void SetChildren(std::vector<TreeNode*> c) override { children = std::move(c); ++setChildrenCalls; }
        std::string Dump() const {
            std::string s = name;
            if (!children.empty()) {
                s += "(";
                for (size_t i = 0; i < children.size(); ++i) s += (i ? "," : "") + static_cast<Node*>(children[i])->Dump();
                s += ")";
            }
            return s;
        }
    };

    Node& Emit(Composition& c, std::string name, std::function<void()> content = {}, Key key = {}) {
        return c.Node<Node>([name] { return std::make_unique<Node>(name); },
                            [name](Node& n) { n.name = name; }, std::move(content), key);
    }
}

TEST(StateChangeRecomposesOnlyReaders) {
    Node root("root");
    Composition c(root);
    State<int> a(1), b(10);
    int runsA = 0, runsB = 0, runsRoot = 0;
    c.SetContent([&] {
        ++runsRoot;
        c.Child([&] { ++runsA; Emit(c, "a" + std::to_string(a.Get())); });
        c.Child([&] { ++runsB; Emit(c, "b" + std::to_string(b.Get())); });
    });
    c.Recompose();
    CHECK_EQ(root.Dump(), std::string("root(a1,b10)"));
    a.Set(2);
    c.Recompose();
    CHECK_EQ(root.Dump(), std::string("root(a2,b10)"));
    CHECK_EQ(runsRoot, 1);
    CHECK_EQ(runsA, 2);
    CHECK_EQ(runsB, 1);
    a.Set(2);   // equal value: no change
    CHECK(!c.Recompose());
}

TEST(RememberSurvivesConditionalAbove) {
    Node root("root");
    Composition c(root);
    State<bool> show(true);
    int* remembered = nullptr;
    c.SetContent([&] {
        if (show.Get()) Emit(c, "banner");
        int& counter = c.Remember<int>([] { return 41; });
        remembered = &counter;
        Emit(c, "body");
    });
    c.Recompose();
    *remembered = 42;
    int* before = remembered;
    show.Set(false);
    c.Recompose();
    CHECK_EQ(root.Dump(), std::string("root(body)"));
    CHECK(remembered == before);   // same slot: call-site identity, not position
    CHECK_EQ(*remembered, 42);
}

TEST(KeyedChildrenMoveWithTheirState) {
    Node root("root");
    Composition c(root);
    State<std::vector<int>> items(std::vector<int>{ 1, 2, 3 });
    std::map<int, int*> slots;
    c.SetContent([&] {
        for (int id : items.Get()) {
            c.Child([&, id] {
                slots[id] = &c.Remember<int>([id] { return id * 100; });
                Emit(c, std::to_string(id));
            }, Key(id));
        }
    });
    c.Recompose();
    auto* slot2 = slots[2];
    items.Set({ 3, 2 });
    c.Recompose();
    CHECK_EQ(root.Dump(), std::string("root(3,2)"));
    CHECK(slots[2] == slot2);
    CHECK_EQ(*slots[2], 200);
}

TEST(NodesAreReusedAndRemoved) {
    Node root("root");
    Composition c(root);
    State<int> n(3);
    c.SetContent([&] {
        Emit(c, "list", [&] { for (int i = 0; i < n.Get(); ++i) Emit(c, "item"); });
    });
    c.Recompose();
    auto* list = root.children[0];
    auto* first = static_cast<Node*>(list)->children[0];
    n.Set(1);
    c.Recompose();
    CHECK_EQ(root.Dump(), std::string("root(list(item))"));
    CHECK(root.children[0] == list);
    CHECK(static_cast<Node*>(list)->children[0] == first);
}

TEST(EffectsRunAfterApplyAndDisposeInOrder) {
    Node root("root");
    Composition c(root);
    State<int> key(1);
    State<bool> present(true);
    std::vector<std::string> log;
    c.SetContent([&] {
        if (present.Get()) {
            int k = key.Get();
            c.DisposableEffect(Keys(k), [&, k] {
                log.push_back("start" + std::to_string(k));
                return [&, k] { log.push_back("stop" + std::to_string(k)); };
            });
        }
        c.SideEffect([&] { log.push_back("side"); });
    });
    c.Recompose();
    key.Set(2);
    c.Recompose();
    present.Set(false);
    c.Recompose();
    std::vector<std::string> expect{ "start1", "side", "stop1", "start2", "side", "stop2", "side" };
    CHECK(log == expect);
}

TEST(LocalsRecomposeOnlyReaders) {
    Node root("root");
    Composition c(root);
    Local<int> theme(0);
    State<int> value(1);
    int readerRuns = 0, bystanderRuns = 0;
    int seen = -1;
    c.SetContent([&] {
        c.Provide(theme, value.Get(), [&] {
            c.Child([&] { ++bystanderRuns; }, Key(), nullptr);
            c.Child([&] { ++readerRuns; seen = c.Read(theme); });
        });
    });
    c.Recompose();
    CHECK_EQ(seen, 1);
    // The root re-runs (it read value) and so do its children; with memo keys the bystander skips.
    value.Set(5);
    c.Recompose();
    CHECK_EQ(seen, 5);
    Local<int> unprovided(7);
    int fallback = 0;
    c.SetContent([&] { fallback = c.Read(unprovided); });
    c.Recompose();
    CHECK_EQ(fallback, 7);
}

TEST(MemoKeysSkipChildren) {
    Node root("root");
    Composition c(root);
    State<int> unrelated(0), inner(0);
    int runs = 0;
    c.SetContent([&] {
        unrelated.Get();
        Keys memo(1);
        c.Child([&] { ++runs; inner.Get(); }, Key(), &memo);
    });
    c.Recompose();
    unrelated.Set(1);
    c.Recompose();
    CHECK_EQ(runs, 1);    // parent re-ran, child skipped: same memo, nothing it read changed
    inner.Set(1);
    c.Recompose();
    CHECK_EQ(runs, 2);    // its own read still invalidates it
}

TEST(DerivedValuesAreFreshAndNotify) {
    Node root("root");
    Composition c(root);
    State<int> a(2), b(3);
    Derived<int> sum([&] { return a.Get() + b.Get(); });
    Derived<bool> big([&] { return sum.Get() > 10; });
    int runs = 0;
    bool seenBig = false;
    c.SetContent([&] { ++runs; seenBig = big.Get(); });
    c.Recompose();
    a.Set(4);
    CHECK_EQ(sum.Get(), 7);       // fresh before any apply
    c.Recompose();
    CHECK_EQ(runs, 1);            // big stayed false: reader not re-run
    a.Set(9);
    c.Recompose();
    CHECK(seenBig);
    CHECK_EQ(runs, 2);
}

namespace {
    Task<> Ticker(State<int>& ticks, int n) {
        for (int i = 0; i < n; ++i) {
            co_await NextFrame{};
            ticks.Set(ticks.Peek() + 1);
        }
    }
    Task<int> Twice(int v) { co_await Yield{}; co_return v * 2; }
    Task<> UsesChild(int& out) { out = co_await Twice(21); }
}

TEST(TasksFramesAndCancellation) {
    Dispatcher d;
    State<int> ticks(0);
    Job job = Launch(d, Ticker(ticks, 5));
    d.RunFrame(1);
    d.RunFrame(2);
    CHECK_EQ(ticks.Peek(), 2);
    job.Cancel();
    d.RunFrame(3);
    CHECK_EQ(ticks.Peek(), 2);
    CHECK(!d.HasFrameWaiters());

    int out = 0;
    Job child = Launch(d, UsesChild(out));
    d.RunPending();
    CHECK_EQ(out, 42);
    CHECK(child.Done());
}

TEST(LaunchedEffectRestartsOnKeyChange) {
    Node root("root");
    Dispatcher d;
    Composition c(root, d);
    State<int> key(1);
    State<int> ticks(0);
    c.SetContent([&] {
        int k = key.Get();
        c.LaunchedEffect(Keys(k), [&] { return Ticker(ticks, 100); });
    });
    c.Recompose();
    d.RunFrame(1);
    CHECK_EQ(ticks.Peek(), 1);
    key.Set(2);
    c.Recompose();
    d.RunFrame(2);
    CHECK_EQ(ticks.Peek(), 2);   // exactly one ticker: the old one was cancelled
    c.SetContent([] {});
    c.Recompose();
    d.RunFrame(3);
    CHECK_EQ(ticks.Peek(), 2);   // removed with its group
}

TEST(SnapshotFlowEmitsDistinctValuesAfterChangesApply) {
    Dispatcher& d = Dispatcher::Main();
    State<int> a(1), b(10);
    std::vector<int> seen;
    // The flow object is gone before the task first runs (as when an effect returns it).
    Task<> task = SnapshotFlow<int>([&] { return a.Get() + b.Get(); }).Collect([&](const int& v) { seen.push_back(v); });
    Job job = Launch(d, std::move(task));
    d.RunPending();
    CHECK(seen == std::vector<int>{ 11 });
    a.Set(2);
    d.RunPending();
    CHECK(seen.size() == 1u);   // nothing until the change applies
    Observation::ApplyChanges();
    d.RunPending();
    CHECK(seen == (std::vector<int>{ 11, 12 }));
    a.Set(1);
    b.Set(11);   // the same sum: not emitted again
    Observation::ApplyChanges();
    d.RunPending();
    CHECK(seen == (std::vector<int>{ 11, 12 }));
    job.Cancel();
    a.Set(5);
    Observation::ApplyChanges();
    d.RunPending();
    CHECK(seen.size() == 2u);
}

TEST(ConflatedChannelKeepsOnlyTheLatestValue) {   // Channel(CONFLATED)
    Dispatcher& d = Dispatcher::Main();
    auto channel = Channel<int>::Conflated();
    CHECK(channel->TrySend(1));
    CHECK(channel->TrySend(2));   // replaces 1
    std::vector<int> got;
    Job job = Launch(d, [](Channel<int>* c, std::vector<int>* out) -> Task<> {
        while (auto v = co_await c->Next()) out->push_back(*v);
    }(channel.get(), &got));
    d.RunPending();
    CHECK(got == std::vector<int>{ 2 });
    CHECK(channel->TrySend(3));
    d.RunPending();
    CHECK(got == (std::vector<int>{ 2, 3 }));
    Channel<int> bounded(1);
    CHECK(bounded.TrySend(1));
    CHECK(!bounded.TrySend(2));   // the default fails when full
    job.Cancel();
}
