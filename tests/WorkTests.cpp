// Background work: WorkSpec.calculateNextRunTime (androidx work-runtime's WorkSpecTest), the
// request clamps, and the scheduler flow against a fake platform host: retries with backoff,
// periods, unique-work policies, the system stopping a job.
#include "Check.h"

#include "kiln/work/Work.h"
#include <kiln/kiln.h>

#include <map>

using namespace kiln;
using namespace kiln::work;

namespace {
    constexpr std::int64_t kInitialDelay = 900;
    constexpr std::int64_t kBackoffDelay = kMinBackoffMillis + 2000;
    constexpr std::int64_t kLastEnqueue = 10000;
    constexpr std::int64_t kFlex = kMinPeriodicFlexMillis + 5000;
    constexpr std::int64_t kInterval = kMinPeriodicIntervalMillis + 5000;

    WorkSpec Spec(WorkRequest r, int attempts = 0, int periods = 0) {
        return { "w", Clamped(std::move(r)), attempts, periods, kLastEnqueue };
    }

    // JobScheduler as the tests see it: scheduled jobs, finished calls, a settable clock.
    struct FakeScheduler {
        std::map<int, std::pair<WorkSpec, std::int64_t>> jobs;   // id: spec, delay
        std::vector<std::pair<int, bool>> finished;
        std::int64_t now = 1000000;
        FakeScheduler() {
            WorkHost h;
            h.schedule = [this](const WorkSpec& s, std::int64_t delay) { jobs[JobId(s.name)] = { s, delay }; };
            h.cancel = [this](int id) { jobs.erase(id); };
            h.pending = [this] {
                std::vector<WorkSpec> v;
                for (auto& [id, j] : jobs) v.push_back(j.first);
                return v;
            };
            h.finished = [this](int id, bool reschedule) { finished.push_back({ id, reschedule }); };
            h.now = [this] { return now; };
            SetWorkHost(std::move(h));
        }
        // The system runs the job.
        void Start(const std::string& name) {
            WorkSpec s = jobs.at(JobId(name)).first;
            jobs.erase(JobId(name));
            StartJob(s);
            rt::Dispatcher::Main().RunPending();
        }
        ~FakeScheduler() {
            CancelAll();
            rt::Dispatcher::Main().RunPending();
            SetWorkHost({});
        }
    };
}

TEST(WorkNextRunTimeFollowsWorkSpec) {
    // One-off: the initial delay.
    CHECK(NextRunTime(Spec({ .initialDelayMillis = kInitialDelay })) == kLastEnqueue + kInitialDelay);
    // Periodic, first run: at once, or at the flex window's start.
    CHECK(NextRunTime(Spec({ .repeatIntervalMillis = kInterval, .flexMillis = kFlex })) == kLastEnqueue + kInterval - kFlex);
    CHECK(NextRunTime(Spec({ .repeatIntervalMillis = kInterval, .flexMillis = kInterval })) == kLastEnqueue);
    // Later runs: an interval after the last.
    CHECK(NextRunTime(Spec({ .repeatIntervalMillis = kInterval, .flexMillis = kFlex }, 0, 1)) == kLastEnqueue + kInterval);
    CHECK(NextRunTime(Spec({ .repeatIntervalMillis = kInterval, .flexMillis = kInterval }, 0, 1)) == kLastEnqueue + kInterval);
    // With an initial delay.
    CHECK(NextRunTime(Spec({ .initialDelayMillis = 10000, .repeatIntervalMillis = kInterval, .flexMillis = kInterval })) == kLastEnqueue + 10000);
    CHECK(NextRunTime(Spec({ .initialDelayMillis = 10000, .repeatIntervalMillis = kInterval, .flexMillis = kFlex })) == kLastEnqueue + 10000 + kInterval - kFlex);
    // Backoff: exponential grows, linear steps evenly, both at most kMaxBackoffMillis.
    auto backoff = [](BackoffPolicy p, std::int64_t delay, int attempt) {
        return NextRunTime(Spec({ .backoffPolicy = p, .backoffDelayMillis = delay }, attempt));
    };
    std::int64_t e1 = backoff(BackoffPolicy::Exponential, kBackoffDelay, 1), e2 = backoff(BackoffPolicy::Exponential, kBackoffDelay, 2),
                 e3 = backoff(BackoffPolicy::Exponential, kBackoffDelay, 3);
    CHECK(e1 == kLastEnqueue + kBackoffDelay && e3 - e2 > e2 - e1 && e2 == kLastEnqueue + 2 * kBackoffDelay);
    std::int64_t l1 = backoff(BackoffPolicy::Linear, kBackoffDelay, 1), l2 = backoff(BackoffPolicy::Linear, kBackoffDelay, 2),
                 l3 = backoff(BackoffPolicy::Linear, kBackoffDelay, 3);
    CHECK(l2 - l1 == l3 - l2 && l3 == kLastEnqueue + 3 * kBackoffDelay);
    CHECK(backoff(BackoffPolicy::Linear, kMaxBackoffMillis + 1, 1) == kLastEnqueue + kMaxBackoffMillis);
    CHECK(backoff(BackoffPolicy::Exponential, kMaxBackoffMillis + 1, 1) == kLastEnqueue + kMaxBackoffMillis);
    CHECK(backoff(BackoffPolicy::Exponential, kBackoffDelay, 30) == kLastEnqueue + kMaxBackoffMillis);
}

TEST(WorkRequestsAreClampedAndEncoded) {
    WorkRequest r = Clamped({ .backoffDelayMillis = 1, .repeatIntervalMillis = 1000, .flexMillis = 10, .expedited = true });
    CHECK(r.backoffDelayMillis == kMinBackoffMillis && r.repeatIntervalMillis == kMinPeriodicIntervalMillis);
    CHECK(r.flexMillis == kMinPeriodicFlexMillis && !r.expedited);
    CHECK(Clamped({ .repeatIntervalMillis = kInterval }).flexMillis == kInterval);   // no flex: the whole interval
    CHECK(Clamped({ .initialDelayMillis = 5, .expedited = true }).expedited == false);
    CHECK(Clamped({ .expedited = true }).expedited);

    WorkSpec s{ "sync ✓", { .worker = "sync", .input = std::string("a\0b", 3),
                            .constraints = { .requiredNetwork = NetworkType::Unmetered, .requiresCharging = true, .requiresStorageNotLow = true },
                            .initialDelayMillis = 7, .backoffPolicy = BackoffPolicy::Linear, .backoffDelayMillis = 12345,
                            .repeatIntervalMillis = kInterval, .flexMillis = kFlex }, 2, 5, 99 };
    CHECK(Decode(Encode(s)) == s);
    std::vector<std::uint8_t> bad = Encode(s);
    bad.push_back(0);
    bool threw = false;
    try { (void)Decode(bad); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // Job ids: stable, in the high range, distinct for different names.
    CHECK(JobId("sync") == JobId("sync") && JobId("sync") != JobId("sync2") && JobId("sync") >= 0x40000000);
}

TEST(WorkRetriesBacksOffAndRepeats) {
    FakeScheduler sched;
    std::vector<WorkContext> runs;
    std::vector<Result> answers = { Result::Retry(), Result::Retry(), Result::Success() };
    RegisterWorker("flaky", [&](WorkContext c) -> rt::Task<Result> {
        runs.push_back(c);
        Result r = answers[std::min(runs.size(), answers.size()) - 1];
        co_return r;
    });
    Enqueue("once", { .worker = "flaky", .input = "in", .initialDelayMillis = 5000, .backoffDelayMillis = 20000 });
    CHECK(sched.jobs.size() == 1 && sched.jobs.at(JobId("once")).second == 5000);
    auto info = Find("once");
    CHECK(info && !info->running && info->worker == "flaky" && info->nextRunTimeMillis == sched.now + 5000);

    // Retry: scheduled again after the backoff, its attempt counted; the system's job is done.
    sched.now += 5000;
    sched.Start("once");
    CHECK(runs.size() == 1 && runs[0].input == "in" && runs[0].runAttemptCount == 0 && runs[0].name == "once");
    CHECK(sched.finished.size() == 1 && sched.finished[0] == std::make_pair(JobId("once"), false));
    CHECK(sched.jobs.at(JobId("once")).second == 20000 && sched.jobs.at(JobId("once")).first.runAttemptCount == 1);
    sched.now += 20000;
    sched.Start("once");
    CHECK(runs.size() == 2 && runs[1].runAttemptCount == 1 && sched.jobs.at(JobId("once")).second == 40000);   // exponential
    sched.now += 40000;
    sched.Start("once");
    // Success: a one-time work is gone.
    CHECK(runs.size() == 3 && sched.jobs.empty() && !Find("once") && sched.finished.size() == 3);

    // Periodic: each run schedules the next an interval later; Failure still repeats.
    runs.clear();
    answers = { Result::Failure() };
    Enqueue("hourly", { .worker = "flaky", .repeatIntervalMillis = 60 * 60 * 1000 });
    CHECK(sched.jobs.at(JobId("hourly")).second == 0);
    sched.Start("hourly");
    CHECK(runs.size() == 1 && runs[0].periodic && runs[0].periodCount == 0);
    CHECK(sched.jobs.at(JobId("hourly")).second == 60 * 60 * 1000 && sched.jobs.at(JobId("hourly")).first.periodCount == 1);
    sched.Start("hourly");
    CHECK(runs.size() == 2 && runs[1].periodCount == 1);
}

TEST(WorkUniquePoliciesAndStops) {
    FakeScheduler sched;
    int started = 0, cancelled = 0;
    RegisterWorker("slow", [&](WorkContext) -> rt::Task<Result> {
        struct Guard { int& n; bool done = false; ~Guard() { if (!done) ++n; } } guard{ cancelled };
        ++started;
        co_await rt::Delay(60000);
        guard.done = true;
        co_return Result::Success();
    });
    Enqueue("u", { .worker = "slow", .input = "first", .initialDelayMillis = 1000 });
    Enqueue("u", { .worker = "slow", .input = "second" }, ExistingWorkPolicy::Keep);
    CHECK(sched.jobs.at(JobId("u")).first.request.input == "first");
    Enqueue("u", { .worker = "slow", .input = "third" });
    CHECK(sched.jobs.size() == 1 && sched.jobs.at(JobId("u")).first.request.input == "third");

    // The system stops a running job: its task is cancelled, and the system runs it again.
    sched.Start("u");
    CHECK(started == 1 && Find("u") && Find("u")->running);
    CHECK(StopJob(JobId("u")) && cancelled == 1 && !Find("u"));
    CHECK(!StopJob(JobId("u")));
    // Replacing a running work stops it.
    Enqueue("u", { .worker = "slow" });
    sched.Start("u");
    Enqueue("u", { .worker = "slow", .input = "next", .initialDelayMillis = 10 });
    CHECK(cancelled == 2 && sched.jobs.at(JobId("u")).first.request.input == "next");
    // Cancel removes the scheduled job; an unknown worker fails its job.
    Cancel("u");
    CHECK(sched.jobs.empty());
    Enqueue("x", { .worker = "nobody" });
    sched.Start("x");
    CHECK(sched.finished.back() == std::make_pair(JobId("x"), false) && sched.jobs.empty());
}

TEST(WorkRunsInProcessWithoutAPlatform) {
    // The default host: work runs on the main dispatcher when it is due.
    auto& d = rt::Dispatcher::Main();
    bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    int runs = 0;
    RegisterWorker("local", [&](WorkContext) -> rt::Task<Result> {
        ++runs;
        co_return runs < 2 ? Result::Retry() : Result::Success();
    });
    Enqueue("l", { .worker = "local", .initialDelayMillis = 100 });
    d.RunPending();
    CHECK(runs == 0 && Find("l"));
    d.AdvanceTimeBy(std::chrono::milliseconds(100));
    d.RunPending();
    d.RunPending();
    CHECK(runs == 1 && Find("l") && Find("l")->runAttemptCount == 1);
    d.AdvanceTimeBy(std::chrono::milliseconds(kDefaultBackoffDelayMillis));
    d.RunPending();
    d.RunPending();
    CHECK(runs == 2 && !Find("l"));
    d.SetVirtualTime(wasVirtual);
}

TEST(WorkFromTheCApi) {
    FakeScheduler sched;
    struct Seen { std::string name, input; int attempt = -1; kiln_work_done* done = nullptr; } seen;
    kiln_fn_worker fn{};
    fn.user = &seen;
    fn.call = [](void* user, const char* name, const char* input, int32_t attempt, kiln_work_done* done) {
        auto* s = static_cast<Seen*>(user);
        *s = { name, input, attempt, done };
    };
    kiln_work_register_worker("c", fn);
    kiln_work_request r{};
    r.worker = "c";
    r.input = "from C";
    r.network = KILN_WORK_NETWORK_UNMETERED;
    kiln_work_enqueue("cwork", &r, false);
    CHECK(kiln_work_is_scheduled("cwork"));
    const WorkSpec& spec = sched.jobs.at(JobId("cwork")).first;
    CHECK(spec.request.constraints.requiredNetwork == NetworkType::Unmetered && spec.request.backoffDelayMillis == kDefaultBackoffDelayMillis);
    sched.Start("cwork");
    CHECK(seen.name == "cwork" && seen.input == "from C" && seen.attempt == 0 && seen.done && Find("cwork")->running);
    // The answer arrives later (another thread may send it); the run finishes on the dispatcher.
    kiln_work_complete(seen.done, KILN_WORK_RETRY);
    CHECK(Find("cwork")->running);
    rt::Dispatcher::Main().RunPending();
    CHECK(!Find("cwork")->running && sched.jobs.at(JobId("cwork")).first.runAttemptCount == 1);
    sched.Start("cwork");
    kiln_work_complete(seen.done, KILN_WORK_SUCCESS);
    rt::Dispatcher::Main().RunPending();
    CHECK(!kiln_work_is_scheduled("cwork") && seen.attempt == 1);
    // A run stopped before its answer: the late answer is dropped.
    kiln_work_enqueue("cwork", &r, false);
    sched.Start("cwork");
    CHECK(StopJob(JobId("cwork")));
    kiln_work_complete(seen.done, KILN_WORK_SUCCESS);
    rt::Dispatcher::Main().RunPending();
    CHECK(!kiln_work_is_scheduled("cwork"));
}
