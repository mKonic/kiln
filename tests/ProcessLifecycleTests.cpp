// ProcessLifecycleOwner (lifecycle-process): start / resume with the first activity, pause / stop
// kTimeoutMs after the last, nothing across an activity recreated in between (upstream's
// ProcessOwnerTest cases, on the main dispatcher's time).
#include "Check.h"

#include "kiln/lifecycle/ProcessLifecycleOwner.h"

#include <chrono>
#include <string>
#include <vector>

using namespace kiln;
using namespace kiln::lifecycle;

namespace {
    struct Log {
        std::vector<std::string> events;
        std::shared_ptr<LifecycleEventObserver> observer;
        explicit Log(Lifecycle& l) {
            observer = AddObserver(l, [this](LifecycleOwner&, Event e) { events.push_back(ToString(e)); });
        }
    };
    void Advance(int ms) {
        rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(ms));
        rt::Dispatcher::Main().RunPending();
    }
}

TEST(ProcessLifecycleFollowsTheFirstAndLastActivity) {
    ProcessLifecycleOwner owner;
    CHECK(owner.GetLifecycle().CurrentState() == State::Created);
    Log log(owner.GetLifecycle());
    log.events.clear();   // the catch-up to CREATED
    // One activity: start, resume.
    owner.ActivityStarted();
    owner.ActivityResumed();
    CHECK(owner.GetLifecycle().CurrentState() == State::Resumed);
    // A second one comes over the first (navigation): nothing for the process.
    owner.ActivityStarted();
    owner.ActivityPaused();
    owner.ActivityResumed();
    owner.ActivityStopped();
    Advance(ProcessLifecycleOwner::kTimeoutMs + 50);
    CHECK(owner.GetLifecycle().CurrentState() == State::Resumed);
    // The last one pauses and stops: the events wait for the timeout.
    owner.ActivityPaused();
    owner.ActivityStopped();
    CHECK(owner.GetLifecycle().CurrentState() == State::Resumed);
    Advance(ProcessLifecycleOwner::kTimeoutMs - 100);
    CHECK(owner.GetLifecycle().CurrentState() == State::Resumed);
    Advance(150);
    CHECK(owner.GetLifecycle().CurrentState() == State::Created);
    CHECK((log.events == std::vector<std::string>{ "ON_START", "ON_RESUME", "ON_PAUSE", "ON_STOP" }));
}

TEST(ProcessLifecycleIgnoresAnActivityRecreatedWithinTheTimeout) {
    ProcessLifecycleOwner owner;
    Log log(owner.GetLifecycle());
    owner.ActivityStarted();
    owner.ActivityResumed();
    log.events.clear();
    // A configuration change: paused, stopped, then the new instance starts and resumes.
    owner.ActivityPaused();
    owner.ActivityStopped();
    Advance(100);
    owner.ActivityStarted();
    owner.ActivityResumed();
    Advance(ProcessLifecycleOwner::kTimeoutMs + 50);
    CHECK(log.events.empty() && owner.GetLifecycle().CurrentState() == State::Resumed);
    // Paused only (a dialog-themed activity on top): ON_PAUSE after the timeout, no ON_STOP.
    owner.ActivityPaused();
    Advance(ProcessLifecycleOwner::kTimeoutMs + 50);
    CHECK((log.events == std::vector<std::string>{ "ON_PAUSE" }) && owner.GetLifecycle().CurrentState() == State::Started);
    owner.ActivityResumed();
    CHECK((log.events == std::vector<std::string>{ "ON_PAUSE", "ON_RESUME" }));
}
