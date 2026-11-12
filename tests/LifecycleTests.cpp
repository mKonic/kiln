#include "Check.h"

#include "kiln/lifecycle/Lifecycle.h"
#include "kiln/lifecycle/LifecycleCompose.h"
#include "kiln/lifecycle/RepeatOnLifecycle.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/runtime/Channel.h"
#include "kiln/runtime/SnapshotFlow.h"

#include <string>
#include <vector>

using namespace kiln;
using namespace kiln::lifecycle;

namespace {

    // A DefaultLifecycleObserver that logs "name:event" and runs an optional hook per event
    // (LifecycleRegistryTest's TestObserver with overridden callbacks).
    struct Log { std::vector<std::string> lines; };

    class TestObserver final : public DefaultLifecycleObserver {
    public:
        TestObserver(Log& log, std::string name) : m_Log(&log), m_Name(std::move(name)) {}
        std::function<void()> onCreate, onStart, onResume, onPause, onStop, onDestroy;
        void OnCreate(LifecycleOwner&) override { Record("create", onCreate); }
        void OnStart(LifecycleOwner&) override { Record("start", onStart); }
        void OnResume(LifecycleOwner&) override { Record("resume", onResume); }
        void OnPause(LifecycleOwner&) override { Record("pause", onPause); }
        void OnStop(LifecycleOwner&) override { Record("stop", onStop); }
        void OnDestroy(LifecycleOwner&) override { Record("destroy", onDestroy); }
    private:
        void Record(const char* what, const std::function<void()>& hook) {
            m_Log->lines.push_back(m_Name + ":" + what);
            if (hook) hook();
        }
        Log* m_Log;
        std::string m_Name;
    };

    struct Owner final : LifecycleOwner {
        LifecycleRegistry registry{ *this };
        Lifecycle& GetLifecycle() override { return registry; }
        void Resume() {
            registry.HandleLifecycleEvent(Event::OnCreate);
            registry.HandleLifecycleEvent(Event::OnStart);
            registry.HandleLifecycleEvent(Event::OnResume);
        }
    };

    using Lines = std::vector<std::string>;
}

TEST(LifecycleRegistryOrdersEventsLikeUpstream) {
    // removeWhileTraversing: an observer removed by an earlier one's callback is not called.
    {
        Owner o; Log log;
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        o1->onCreate = [&] { o.registry.RemoveObserver(o2); };
        o.registry.AddObserver(o1);
        o.registry.AddObserver(o2);
        o.registry.HandleLifecycleEvent(Event::OnCreate);
        CHECK(log.lines == Lines{ "1:create" });
    }
    // constructionDestruction2: events sent from a callback while the observer is added.
    {
        Owner o; Log log;
        o.Resume();
        auto ob = std::make_shared<TestObserver>(log, "o");
        ob->onStart = [&] {
            o.registry.HandleLifecycleEvent(Event::OnPause);
            o.registry.HandleLifecycleEvent(Event::OnStop);
            o.registry.HandleLifecycleEvent(Event::OnDestroy);
        };
        o.registry.AddObserver(ob);
        CHECK(log.lines == (Lines{ "o:create", "o:start", "o:stop", "o:destroy" }));
    }
    // addDuringTraversing: one added during a pass waits for the observers before it.
    {
        Owner o; Log log;
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        auto o3 = std::make_shared<TestObserver>(log, "3");
        o1->onStart = [&] { o.registry.AddObserver(o3); };
        o.registry.AddObserver(o1);
        o.registry.AddObserver(o2);
        o.registry.HandleLifecycleEvent(Event::OnCreate);
        o.registry.HandleLifecycleEvent(Event::OnStart);
        CHECK(log.lines == (Lines{ "1:create", "2:create", "1:start", "3:create", "2:start", "3:start" }));
    }
    // addDuringAddition.
    {
        Owner o; Log log;
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        auto o3 = std::make_shared<TestObserver>(log, "3");
        o2->onCreate = [&] { o.registry.AddObserver(o3); };
        o1->onResume = [&] { o.registry.AddObserver(o2); };
        o.registry.AddObserver(o1);
        o.Resume();
        CHECK(log.lines == (Lines{ "1:create", "1:start", "1:resume", "2:create", "2:start", "2:resume", "3:create", "3:start", "3:resume" }));
    }
    // downEventsRemoveAll: down events go newest first; removing everyone stops the pass.
    {
        Owner o; Log log;
        o.Resume();
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        auto o3 = std::make_shared<TestObserver>(log, "3");
        o2->onStop = [&] {
            o.registry.RemoveObserver(o3);
            o.registry.RemoveObserver(std::shared_ptr<LifecycleObserver>(o2));
            o.registry.RemoveObserver(o1);
            CHECK_EQ(o.registry.ObserverCount(), 0);
        };
        o.registry.AddObserver(o1);
        o.registry.AddObserver(o2);
        o.registry.AddObserver(o3);
        log.lines.clear();
        o.registry.HandleLifecycleEvent(Event::OnPause);
        o.registry.HandleLifecycleEvent(Event::OnStop);
        o.registry.HandleLifecycleEvent(Event::OnPause);
        CHECK(log.lines == (Lines{ "3:pause", "2:pause", "1:pause", "3:stop", "2:stop" }));
    }
    // removeCascade.
    {
        Owner o; Log log;
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        auto o3 = std::make_shared<TestObserver>(log, "3");
        auto o4 = std::make_shared<TestObserver>(log, "4");
        o2->onStart = [&] { o.registry.RemoveObserver(o2); };
        o1->onResume = [&] {
            o.registry.RemoveObserver(o1);
            o.registry.AddObserver(o2);
            o.registry.AddObserver(o3);
            o.registry.AddObserver(o4);
        };
        o.Resume();
        o.registry.AddObserver(o1);
        CHECK(log.lines == (Lines{ "1:create", "1:start", "1:resume", "2:create", "2:start", "3:create", "3:start", "4:create", "4:start",
                                   "3:resume", "4:resume" }));
    }
    // changeStateDuringDescending.
    {
        Owner o; Log log;
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        o1->onPause = [&] {
            o.registry.HandleLifecycleEvent(Event::OnResume);
            o.registry.AddObserver(o2);
        };
        o.Resume();
        o.registry.AddObserver(o1);
        log.lines.clear();
        o.registry.HandleLifecycleEvent(Event::OnPause);
        CHECK(log.lines == (Lines{ "1:pause", "2:create", "2:start", "1:resume", "2:resume" }));
    }
    // siblingRemovalLimitationCheck2.
    {
        Owner o; Log log;
        auto o1 = std::make_shared<TestObserver>(log, "1");
        auto o2 = std::make_shared<TestObserver>(log, "2");
        auto o3 = std::make_shared<TestObserver>(log, "3");
        auto o4 = std::make_shared<TestObserver>(log, "4");
        o3->onCreate = [&] { o.registry.RemoveObserver(o2); };
        o1->onStart = [&] { o.registry.AddObserver(o2); };
        o1->onResume = [&] {
            o.registry.AddObserver(o3);
            o.registry.AddObserver(o4);
        };
        o.Resume();
        o.registry.AddObserver(o1);
        CHECK(log.lines == (Lines{ "1:create", "1:start", "2:create", "1:resume", "3:create", "3:start", "4:create", "4:start", "3:resume",
                                   "4:resume" }));
    }
}

TEST(LifecycleRegistryStatesObserversAndScope) {
    Owner o; Log log;
    // Moving Initialized straight to Destroyed is refused.
    bool threw = false;
    try { o.registry.SetCurrentState(State::Destroyed); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw && o.registry.CurrentState() == State::Initialized);

    // An observer that is both kinds gets the per-event callback first, then the event.
    struct Both final : DefaultLifecycleObserver, LifecycleEventObserver {
        Log* log;
        void OnStart(LifecycleOwner&) override { log->lines.push_back("onStart"); }
        void OnStateChanged(LifecycleOwner&, Event e) override { log->lines.push_back(ToString(e)); }
    };
    auto both = std::make_shared<Both>();
    both->log = &log;
    o.registry.AddObserver(both);
    o.registry.AddObserver(both);   // the same observer twice is added once
    CHECK_EQ(o.registry.ObserverCount(), 1);

    // currentStateFlow follows; the scope's tasks end on destroy.
    const auto& flow = o.registry.CurrentStateFlow();
    auto scope = LifecycleScope(o);
    rt::Channel<int> never;
    bool ended = false;
    scope->Launch([](rt::Channel<int>& ch, bool& ended) -> rt::Task<> {
        struct End { bool* e; ~End() { *e = true; } } end{ &ended };
        co_await ch.Next();
    }(never, ended));
    std::vector<Event> events;
    auto collect = rt::Launch(rt::Dispatcher::Main(), EventFlow(o.registry).Collect([&](Event e) { events.push_back(e); }));

    o.registry.SetCurrentState(State::Started);   // sends OnCreate then OnStart
    CHECK(log.lines == (Lines{ "ON_CREATE", "onStart", "ON_START" }));
    CHECK(flow.Peek() == State::Started && IsAtLeast(o.registry.CurrentState(), State::Created));
    CHECK(!ended && scope->IsActive());
    o.registry.HandleLifecycleEvent(Event::OnStop);
    o.registry.HandleLifecycleEvent(Event::OnDestroy);
    rt::Dispatcher::Main().RunPending();
    CHECK(ended && !scope->IsActive() && collect.Done());
    CHECK(events == (std::vector<Event>{ Event::OnCreate, Event::OnStart, Event::OnStop, Event::OnDestroy }));
    CHECK_EQ(o.registry.ObserverCount(), 0);

    // subscribeToDead: an observer added after destroy gets nothing; leaving Destroyed throws.
    log.lines.clear();
    o.registry.AddObserver(std::make_shared<TestObserver>(log, "late"));
    CHECK(log.lines.empty());
    threw = false;
    try { o.registry.SetCurrentState(State::Created); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    // Event helpers.
    CHECK(UpTo(State::Started) == Event::OnStart && DownFrom(State::Resumed) == Event::OnPause && !UpFrom(State::Resumed));
    CHECK(TargetState(Event::OnStop) == State::Created && DownTo(State::Destroyed) == Event::OnDestroy);
}

TEST(RepeatOnLifecycleAndWithStateFollowTheLifecycle) {
    Owner o;
    auto& main = rt::Dispatcher::Main();
    // repeatOnLifecycle(STARTED): a block per start, cancelled on stop; returns on destroy.
    int starts = 0, ends = 0;
    bool returned = false;
    rt::Channel<int> never;
    auto repeat = rt::Launch(main, [](Owner& o, int& starts, int& ends, bool& returned, rt::Channel<int>& never) -> rt::Task<> {
        co_await RepeatOnLifecycle(o, State::Started, [&]() -> rt::Task<> {
            ++starts;
            struct End { int* e; ~End() { ++*e; } } end{ &ends };
            co_await never.Next();
        });
        returned = true;
    }(o, starts, ends, returned, never));

    // withStarted: runs inside the event that reaches STARTED, before later observers.
    std::vector<std::string> order;
    std::optional<int> got;
    auto with = rt::Launch(main, [](Owner& o, std::vector<std::string>& order, std::optional<int>& got) -> rt::Task<> {
        got = co_await WithStarted(o, [&] { order.push_back("block"); return 7; });
    }(o, order, got));
    auto later = AddObserver(o.registry, [&](LifecycleOwner&, Event e) { if (e == Event::OnStart) order.push_back("later"); });

    o.registry.HandleLifecycleEvent(Event::OnCreate);
    CHECK(starts == 0 && !got);
    o.registry.HandleLifecycleEvent(Event::OnStart);
    CHECK(starts == 1 && ends == 0 && got == 7);
    CHECK(order == (std::vector<std::string>{ "block", "later" }));
    o.registry.HandleLifecycleEvent(Event::OnResume);
    o.registry.HandleLifecycleEvent(Event::OnPause);
    CHECK(starts == 1 && ends == 0);
    o.registry.HandleLifecycleEvent(Event::OnStop);
    CHECK(starts == 1 && ends == 1);
    o.registry.HandleLifecycleEvent(Event::OnStart);
    CHECK(starts == 2 && ends == 1);

    // flowWithLifecycle over a snapshotFlow: collected only while RESUMED.
    rt::State<int> source{ 1 };
    std::vector<int> seen;
    auto flowJob = rt::Launch(main, FlowWithLifecycle(rt::SnapshotFlow<int>([&] { return source.Get(); }), o.registry, State::Resumed)
                                        .Collect([&](const int& v) { seen.push_back(v); }));
    CHECK(seen.empty());
    o.registry.HandleLifecycleEvent(Event::OnResume);
    CHECK(seen == std::vector<int>{ 1 });
    source.Set(2);
    rt::Observation::ApplyChanges();
    main.RunPending();
    CHECK(seen == (std::vector<int>{ 1, 2 }));
    o.registry.HandleLifecycleEvent(Event::OnPause);
    source.Set(3);
    rt::Observation::ApplyChanges();
    main.RunPending();
    CHECK(seen == (std::vector<int>{ 1, 2 }));

    // withResumed waiting when the lifecycle is destroyed: LifecycleDestroyedException.
    bool destroyedThrown = false;
    auto waiting = rt::Launch(main, [](Owner& o, bool& thrown) -> rt::Task<> {
        try { co_await WithResumed(o, [] {}); } catch (const LifecycleDestroyedException&) { thrown = true; }
    }(o, destroyedThrown));
    o.registry.HandleLifecycleEvent(Event::OnStop);
    o.registry.HandleLifecycleEvent(Event::OnDestroy);
    main.RunPending();
    CHECK(destroyedThrown && returned && ends == 2 && flowJob.Done());
}

TEST(LifecycleEffectsFollowTheWindowLifecycle) {
    ui::Owner window;
    window.SetDensity({ 1, 1 });
    rt::Composition comp(window.Root(), rt::Dispatcher::Main());
    auto host = std::make_shared<Owner>();
    host->registry.SetCurrentState(State::Created);
    SetHostLifecycleOwner(window, host);

    rt::State<bool> shown{ true };
    rt::State<int> key{ 0 };
    rt::State<int> source{ 1 };
    std::vector<std::string> log;
    LifecycleOwnerPtr seen, child;
    std::function<void()> click;
    rt::State<int>* collected = nullptr;
    comp.SetContent([&] { rt::Composition::Current()->Provide(ui::LocalOwner, &window, [&] {
        seen = CurrentLifecycleOwner();
        if (!shown.Get()) return;
        LifecycleEventEffect(Event::OnStart, [&] { log.push_back("event:start"); });
        LifecycleResumeEffect(rt::Keys(key.Get()), [&](LifecycleResumePauseEffectScope& s) {
            log.push_back("resume");
            return s.OnPauseOrDispose([&](LifecycleOwner&) { log.push_back("pauseOrDispose"); });
        });
        LifecycleStartEffect(rt::Keys(0), [&](LifecycleStartStopEffectScope& s) {
            log.push_back("start");
            return s.OnStopOrDispose([&](LifecycleOwner&) { log.push_back("stopOrDispose"); });
        });
        child = RememberLifecycleOwner(State::Started);
        click = DropUnlessResumed([&] { log.push_back("click"); });
        collected = &CollectAsStateWithLifecycle(rt::SnapshotFlow<int>([&] { return source.Get(); }), 0);
    }); });
    auto pump = [&] {
        rt::Observation::ApplyChanges();
        rt::Dispatcher::Main().RunPending();
        comp.Recompose();
        rt::Dispatcher::Main().RunPending();
    };
    pump();
    CHECK(seen == host && log.empty() && child->GetLifecycle().CurrentState() == State::Created && collected->Peek() == 0);
    click();
    CHECK(log.empty());   // dropped: not resumed

    host->registry.HandleLifecycleEvent(Event::OnStart);
    host->registry.HandleLifecycleEvent(Event::OnResume);
    pump();
    CHECK(log == (std::vector<std::string>{ "event:start", "start", "resume" }));
    CHECK(child->GetLifecycle().CurrentState() == State::Started);   // capped
    CHECK(collected->Peek() == 1);
    source.Set(5);
    pump();
    CHECK(collected->Peek() == 5);
    click();
    CHECK(log.back() == "click");

    // A key change disposes and restarts the resume effect (still resumed: runs again at once).
    log.clear();
    key.Set(1);
    pump();
    CHECK(log == (std::vector<std::string>{ "pauseOrDispose", "resume" }));

    // Stopping: pause then stop blocks; collection stops below STARTED.
    log.clear();
    host->registry.HandleLifecycleEvent(Event::OnPause);
    host->registry.HandleLifecycleEvent(Event::OnStop);
    source.Set(9);
    pump();
    CHECK(log == (std::vector<std::string>{ "pauseOrDispose", "stopOrDispose" }) && collected->Peek() == 5);
    CHECK(child->GetLifecycle().CurrentState() == State::Created);

    // Leaving composition destroys the child owner and disposes without a second stop block.
    log.clear();
    auto gone = child;
    shown.Set(false);
    pump();
    CHECK(log.empty() && gone->GetLifecycle().CurrentState() == State::Destroyed);
}
