#include "kiln/work/Work.h"

#include "kiln/Log.h"
#include "kiln/jvm/Codec.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <mutex>

namespace kiln::work {

    namespace {
        std::mutex& RegistryMutex() {
            static std::mutex m;
            return m;
        }
        std::map<std::string, Worker>& Registry() {
            static std::map<std::string, Worker> r;
            return r;
        }

        // Runs in progress, by job id (main thread).
        struct Running {
            WorkSpec spec;
            rt::Job job;
        };
        std::map<int, std::unique_ptr<Running>>& Runs() {
            static std::map<int, std::unique_ptr<Running>> r;
            return r;
        }

        std::int64_t SystemNow() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // The default host: the work runs in this process when it is due.
        struct InProcess {
            std::map<int, WorkSpec> pending;
            std::map<int, std::pair<std::uint64_t, rt::Job>> timers;   // by id: its generation, its wait
            std::uint64_t generation = 0;
        };
        InProcess& Local() {
            static InProcess p;
            return p;
        }
        rt::Task<> RunLater(WorkSpec spec, std::int64_t delay, std::uint64_t generation) {
            co_await rt::Delay(delay);
            int id = JobId(spec.name);
            Local().pending.erase(id);
            // Dropped after this resume, unless the run scheduled the next one meanwhile.
            rt::Dispatcher::Main().Post([id, generation] {
                auto it = Local().timers.find(id);
                if (it != Local().timers.end() && it->second.first == generation) Local().timers.erase(it);
            });
            StartJob(spec);
        }
        WorkHost InProcessHost() {
            WorkHost h;
            h.schedule = [](const WorkSpec& spec, std::int64_t delay) {
                int id = JobId(spec.name);
                Local().pending[id] = spec;
                std::uint64_t generation = ++Local().generation;
                Local().timers[id] = { generation, rt::Launch(rt::Dispatcher::Main(), RunLater(spec, delay, generation)) };
            };
            h.cancel = [](int id) {
                Local().pending.erase(id);
                Local().timers.erase(id);
            };
            h.pending = [] {
                std::vector<WorkSpec> out;
                for (const auto& [id, s] : Local().pending) out.push_back(s);
                return out;
            };
            h.finished = [](int, bool) {};
            h.now = SystemNow;
            return h;
        }
        WorkHost& Host() {
            static WorkHost h = InProcessHost();
            return h;
        }
        std::int64_t Now() { return Host().now ? Host().now() : SystemNow(); }

        void Schedule(const WorkSpec& spec) {
            std::int64_t delay = std::max<std::int64_t>(NextRunTime(spec) - Now(), 0);
            Host().schedule(spec, delay);
        }

        // What follows a finished run (WorkerWrapper's handleResult / resetPeriodic /
        // rescheduleAndResolve).
        void Finish(int id, Result result) {
            auto it = Runs().find(id);
            if (it == Runs().end()) return;
            WorkSpec spec = std::move(it->second->spec);
            // The job's task has ended: drop it after this resume returns.
            rt::Dispatcher::Main().Post([run = std::shared_ptr<Running>(std::move(it->second))] {});
            Runs().erase(it);
            Host().finished(id, false);
            spec.lastEnqueueTime = Now();
            if (result.kind == Result::Kind::Retry) {
                ++spec.runAttemptCount;
                Schedule(spec);
            } else if (spec.Periodic()) {
                spec.runAttemptCount = 0;
                ++spec.periodCount;
                Schedule(spec);
            }
        }

        rt::Task<> Run(int id, Worker worker, WorkContext context) {
            Result result = Result::Failure();
            const std::string name = context.worker;
            try {
                result = co_await worker(std::move(context));
            } catch (const std::exception& e) {
                log::Error("kiln", "work: worker {} failed: {}", name, e.what());
                result = Result::Failure();
            }
            Finish(id, result);
        }

        void StopRun(int id) {
            auto it = Runs().find(id);
            if (it == Runs().end()) return;
            Runs().erase(it);   // cancels the worker's task
        }
    }

    void RegisterWorker(std::string name, Worker worker) {
        std::lock_guard lock(RegistryMutex());
        Registry()[std::move(name)] = std::move(worker);
    }

    WorkRequest Clamped(WorkRequest r) {
        r.backoffDelayMillis = std::clamp(r.backoffDelayMillis, kMinBackoffMillis, kMaxBackoffMillis);
        r.initialDelayMillis = std::max<std::int64_t>(r.initialDelayMillis, 0);
        if (r.repeatIntervalMillis != 0) {
            // PeriodicWorkRequest / WorkSpec.setPeriodic.
            r.repeatIntervalMillis = std::max(r.repeatIntervalMillis, kMinPeriodicIntervalMillis);
            std::int64_t flex = r.flexMillis == 0 ? r.repeatIntervalMillis : r.flexMillis;
            r.flexMillis = std::clamp(flex, kMinPeriodicFlexMillis, r.repeatIntervalMillis);
            r.expedited = false;   // expedited work is one-time
        } else {
            r.flexMillis = 0;
        }
        if (r.initialDelayMillis > 0) r.expedited = false;
        return r;
    }

    std::int64_t NextRunTime(const WorkSpec& s) {
        const WorkRequest& r = s.request;
        if (s.runAttemptCount > 0) {
            // Backed off: WorkSpec.calculateNextRunTime's isBackedOff branch.
            std::int64_t delay = r.backoffPolicy == BackoffPolicy::Linear
                ? r.backoffDelayMillis * s.runAttemptCount
                : std::int64_t(std::scalbn(float(r.backoffDelayMillis), s.runAttemptCount - 1));
            return s.lastEnqueueTime + std::min(delay, kMaxBackoffMillis);
        }
        if (s.Periodic()) {
            // The first run is not delayed by the interval, only by the initial delay; with a flex
            // shorter than the interval it waits for the flex window.
            std::int64_t schedule = s.periodCount == 0 ? s.lastEnqueueTime + r.initialDelayMillis
                                                       : s.lastEnqueueTime + r.repeatIntervalMillis;
            if (r.flexMillis != r.repeatIntervalMillis && s.periodCount == 0) schedule += r.repeatIntervalMillis - r.flexMillis;
            return schedule;
        }
        return s.lastEnqueueTime + r.initialDelayMillis;
    }

    int JobId(std::string_view name) {
        // FNV-1a, in [0x40000000, 0x7FFFFFFF]: away from the small ids apps pick by hand.
        std::uint32_t h = 2166136261u;
        for (char c : name) {
            h ^= std::uint8_t(c);
            h *= 16777619u;
        }
        return int((h & 0x3FFFFFFFu) | 0x40000000u);
    }

    std::vector<std::uint8_t> Encode(const WorkSpec& s) {
        jvm::Writer w;
        w.I32(1);   // the format's version
        w.Str(s.name);
        const WorkRequest& r = s.request;
        w.Str(r.worker);
        w.Str(r.input);
        w.I32(int(r.constraints.requiredNetwork));
        w.Bool(r.constraints.requiresCharging);
        w.Bool(r.constraints.requiresDeviceIdle);
        w.Bool(r.constraints.requiresBatteryNotLow);
        w.Bool(r.constraints.requiresStorageNotLow);
        w.I64(r.initialDelayMillis);
        w.I32(int(r.backoffPolicy));
        w.I64(r.backoffDelayMillis);
        w.I64(r.repeatIntervalMillis);
        w.I64(r.flexMillis);
        w.Bool(r.expedited);
        w.I32(s.runAttemptCount);
        w.I32(s.periodCount);
        w.I64(s.lastEnqueueTime);
        return w.Take();
    }

    WorkSpec Decode(const std::vector<std::uint8_t>& bytes) {
        jvm::Reader rd(bytes);
        if (rd.I32() != 1) throw jvm::CodecError("work: an unknown WorkSpec format");
        WorkSpec s;
        s.name = rd.Str();
        WorkRequest& r = s.request;
        r.worker = rd.Str();
        r.input = rd.Str();
        int network = rd.I32();
        if (network < 0 || network > int(NetworkType::TemporarilyUnmetered)) throw jvm::CodecError("work: an unknown network type");
        r.constraints.requiredNetwork = NetworkType(network);
        r.constraints.requiresCharging = rd.Bool();
        r.constraints.requiresDeviceIdle = rd.Bool();
        r.constraints.requiresBatteryNotLow = rd.Bool();
        r.constraints.requiresStorageNotLow = rd.Bool();
        r.initialDelayMillis = rd.I64();
        r.backoffPolicy = rd.I32() == int(BackoffPolicy::Linear) ? BackoffPolicy::Linear : BackoffPolicy::Exponential;
        r.backoffDelayMillis = rd.I64();
        r.repeatIntervalMillis = rd.I64();
        r.flexMillis = rd.I64();
        r.expedited = rd.Bool();
        s.runAttemptCount = rd.I32();
        s.periodCount = rd.I32();
        s.lastEnqueueTime = rd.I64();
        if (rd.Remaining() != 0) throw jvm::CodecError("work: bytes left after the WorkSpec");
        return s;
    }

    void Enqueue(std::string name, WorkRequest request, ExistingWorkPolicy policy) {
        int id = JobId(name);
        if (policy == ExistingWorkPolicy::Keep && Find(name)) return;
        StopRun(id);
        WorkSpec spec;
        spec.name = std::move(name);
        spec.request = Clamped(std::move(request));
        spec.lastEnqueueTime = Now();
        Schedule(spec);
    }

    void Cancel(const std::string& name) {
        int id = JobId(name);
        StopRun(id);
        Host().cancel(id);
    }

    void CancelAll() {
        std::vector<int> ids;
        for (const auto& [id, run] : Runs()) ids.push_back(id);
        for (const WorkSpec& s : Host().pending()) ids.push_back(JobId(s.name));
        for (int id : ids) {
            StopRun(id);
            Host().cancel(id);
        }
    }

    std::vector<WorkInfo> Works() {
        std::map<std::string, WorkInfo> out;
        auto info = [](const WorkSpec& s, bool running) {
            return WorkInfo{ s.name, s.request.worker, running, s.Periodic(), s.runAttemptCount, s.periodCount, NextRunTime(s) };
        };
        for (const WorkSpec& s : Host().pending()) out[s.name] = info(s, false);
        for (const auto& [id, run] : Runs()) out[run->spec.name] = info(run->spec, true);
        std::vector<WorkInfo> v;
        for (auto& [name, i] : out) v.push_back(std::move(i));
        return v;
    }

    std::optional<WorkInfo> Find(const std::string& name) {
        for (WorkInfo& i : Works()) if (i.name == name) return std::move(i);
        return std::nullopt;
    }

    void SetWorkHost(WorkHost host) {
        if (!host.schedule) host = InProcessHost();
        if (!host.now) host.now = SystemNow;
        Host() = std::move(host);
    }

    void StartJob(const WorkSpec& spec) {
        int id = JobId(spec.name);
        Worker worker;
        {
            std::lock_guard lock(RegistryMutex());
            auto it = Registry().find(spec.request.worker);
            if (it != Registry().end()) worker = it->second;
        }
        StopRun(id);
        if (!worker) {
            log::Error("kiln", "work: no worker named {} (register it with a static work::WorkerRegistration)", spec.request.worker);
            Host().finished(id, false);
            return;
        }
        auto run = std::make_unique<Running>();
        run->spec = spec;
        Running* r = run.get();
        Runs()[id] = std::move(run);
        WorkContext context{ spec.name, spec.request.worker, spec.request.input, spec.runAttemptCount, spec.periodCount, spec.Periodic() };
        // Launched after it is recorded: a worker that finishes at once finds its run.
        r->job = rt::Launch(rt::Dispatcher::Main(), Run(id, std::move(worker), std::move(context)));
    }

    bool StopJob(int id) {
        bool running = Runs().count(id) != 0;
        StopRun(id);
        return running;
    }

}
