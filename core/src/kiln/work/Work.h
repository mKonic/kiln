#pragma once

// Background work that runs when its constraints hold, even after the app's process has gone:
// WorkManager's model (androidx work-runtime) over JobScheduler. Each run is one JobScheduler job
// that carries its WorkSpec, the way WorkManager's SystemJobScheduler converts one
// (SystemJobInfoConverter); retries and periods are scheduled again from WorkSpec's
// calculateNextRunTime.
//
//   // At namespace scope: workers are found by name in a process the system started for them.
//   kiln::work::WorkerRegistration sync("sync", [](kiln::work::WorkContext c) -> kiln::rt::Task<kiln::work::Result> {
//       try { co_await net::Fetch({ .url = c.input }); } catch (const net::HttpError&) { co_return kiln::work::Result::Retry(); }
//       co_return kiln::work::Result::Success();
//   });
//
//   kiln::work::Enqueue("sync-notes", { .worker = "sync", .input = url,
//                                       .constraints = { .requiredNetwork = kiln::work::NetworkType::Connected },
//                                       .repeatIntervalMillis = 60 * 60 * 1000 });
//
// A worker runs on the main dispatcher; it can move work off it with rt::WithContext. When the
// system stops it (a constraint no longer holds, or its time ran out) its task is cancelled and
// the job runs again later.

#include "kiln/runtime/Task.h"
#include <kiln/Function.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiln::work {

    // The network a job needs (NetworkType).
    enum class NetworkType { NotRequired, Connected, Unmetered, NotRoaming, Metered, TemporarilyUnmetered };

    struct Constraints {
        NetworkType requiredNetwork = NetworkType::NotRequired;
        bool requiresCharging = false;
        bool requiresDeviceIdle = false;      // no backoff then (JobScheduler allows only one)
        bool requiresBatteryNotLow = false;
        bool requiresStorageNotLow = false;
        bool operator==(const Constraints&) const = default;
    };

    enum class BackoffPolicy { Exponential, Linear };

    // WorkRequest's limits.
    inline constexpr std::int64_t kDefaultBackoffDelayMillis = 30000;
    inline constexpr std::int64_t kMinBackoffMillis = 10000;
    inline constexpr std::int64_t kMaxBackoffMillis = 5 * 60 * 60 * 1000;
    inline constexpr std::int64_t kMinPeriodicIntervalMillis = 15 * 60 * 1000;
    inline constexpr std::int64_t kMinPeriodicFlexMillis = 5 * 60 * 1000;

    struct WorkRequest {
        // The registered worker that runs it, and what it is given.
        std::string worker;
        std::string input;
        Constraints constraints;
        std::int64_t initialDelayMillis = 0;
        // After Result::Retry: the delay before the next attempt, times the attempt (linear) or
        // doubled each attempt (exponential), at most kMaxBackoffMillis. Clamped to
        // [kMinBackoffMillis, kMaxBackoffMillis].
        BackoffPolicy backoffPolicy = BackoffPolicy::Exponential;
        std::int64_t backoffDelayMillis = kDefaultBackoffDelayMillis;
        // Periodic work: runs once per interval (at least kMinPeriodicIntervalMillis), within the
        // flex window at its end (at least kMinPeriodicFlexMillis; 0: the whole interval).
        std::int64_t repeatIntervalMillis = 0;
        std::int64_t flexMillis = 0;
        // Runs as soon as possible, ahead of other jobs (a one-time job without a delay).
        bool expedited = false;
        bool operator==(const WorkRequest&) const = default;
    };

    // What happens to an existing unique work of the same name.
    enum class ExistingWorkPolicy {
        Replace,   // cancels it (stopping a run in progress) and enqueues the new one
        Keep,      // leaves it, and drops the new one
    };

    struct WorkContext {
        std::string name;        // the unique work's name
        std::string worker;
        std::string input;
        int runAttemptCount = 0; // earlier attempts of this run that returned Retry or were stopped
        int periodCount = 0;     // periods completed (periodic work)
        bool periodic = false;
    };

    struct Result {
        enum class Kind { Success, Failure, Retry };
        Kind kind = Kind::Success;
        static Result Success() { return { Kind::Success }; }
        // Done, unsuccessfully (periodic work still runs next period).
        static Result Failure() { return { Kind::Failure }; }
        // Run again after the backoff delay.
        static Result Retry() { return { Kind::Retry }; }
        bool operator==(const Result&) const = default;
    };

    using Worker = kiln::Function<rt::Task<Result>(WorkContext)>;
    void RegisterWorker(std::string name, Worker worker);
    // Registers a worker as the library loads (a static object), so it exists in a process the
    // system started to run a job.
    struct WorkerRegistration {
        WorkerRegistration(std::string name, Worker worker) { RegisterWorker(std::move(name), std::move(worker)); }
    };

    void Enqueue(std::string uniqueName, WorkRequest request, ExistingWorkPolicy policy = ExistingWorkPolicy::Replace);
    void Cancel(const std::string& uniqueName);
    void CancelAll();

    struct WorkInfo {
        std::string name;
        std::string worker;
        bool running = false;
        bool periodic = false;
        int runAttemptCount = 0;
        int periodCount = 0;
        std::int64_t nextRunTimeMillis = 0;   // wall-clock milliseconds
    };
    // The unique works that are scheduled or running.
    std::vector<WorkInfo> Works();
    std::optional<WorkInfo> Find(const std::string& uniqueName);

    // --- what the platform's scheduler sees --------------------------------------------------

    // One scheduled run: the request and where it is in its retries and periods (WorkSpec).
    struct WorkSpec {
        std::string name;
        WorkRequest request;
        int runAttemptCount = 0;
        int periodCount = 0;
        std::int64_t lastEnqueueTime = 0;   // wall-clock milliseconds
        bool Periodic() const { return request.repeatIntervalMillis != 0; }
        bool operator==(const WorkSpec&) const = default;
    };
    // WorkSpec.calculateNextRunTime: when the run is due (wall-clock milliseconds).
    std::int64_t NextRunTime(const WorkSpec& spec);
    // The request with WorkRequest's and PeriodicWorkRequest's clamps applied.
    WorkRequest Clamped(WorkRequest request);
    // The JobScheduler job id of a unique work: stable across processes, from its name.
    int JobId(std::string_view uniqueName);
    std::vector<std::uint8_t> Encode(const WorkSpec& spec);
    // Throws when the bytes are not a WorkSpec.
    WorkSpec Decode(const std::vector<std::uint8_t>& bytes);

    struct WorkHost {
        // Schedules the run after delayMillis, replacing a job of the same JobId.
        kiln::Function<void(const WorkSpec&, std::int64_t delayMillis)> schedule;
        kiln::Function<void(int jobId)> cancel;
        // The scheduled runs.
        kiln::Function<std::vector<WorkSpec>()> pending;
        // A started job is done (reschedule: the system runs it again with its own backoff).
        kiln::Function<void(int jobId, bool reschedule)> finished;
        // Wall-clock milliseconds.
        kiln::Function<std::int64_t()> now;
    };
    // The platform's scheduler (Android: JobScheduler). The default runs work in this process on
    // the main dispatcher, ignoring constraints (the desktop, tests); an empty host restores it.
    void SetWorkHost(WorkHost host);

    // The host: a scheduled job starts (main thread). Runs its worker; when it finishes,
    // schedules what comes next and calls host.finished.
    void StartJob(const WorkSpec& spec);
    // The host: the system stops a job early. Cancels its worker; returns whether the system
    // should run it again.
    bool StopJob(int jobId);

}
