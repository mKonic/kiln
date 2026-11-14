#include "kiln/work/Alarm.h"

#include "kiln/Log.h"
#include "kiln/jvm/Codec.h"
#include "kiln/runtime/Task.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>

namespace kiln::alarm {

    namespace {
        struct Registry {
            std::mutex mutex;
            std::map<std::string, std::shared_ptr<AlarmHandler>> handlers;
            AlarmHost host;
            // The in-process host's alarms.
            std::map<std::string, std::pair<Alarm, rt::Job>> local;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
        AlarmHost Host() {
            std::lock_guard lock(R().mutex);
            return R().host;
        }
        std::int64_t NowMillis() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }
        rt::Task<> RunAt(Alarm a) {
            // In steps of at most ~24 days: a time far off would overflow the clock's nanoseconds.
            const std::int64_t now = NowMillis();
            std::int64_t remaining = a.triggerAtMillis > now ? a.triggerAtMillis - now : 0;
            while (remaining > 0) {
                const std::int64_t step = std::min<std::int64_t>(remaining, std::int64_t(1) << 31);
                co_await rt::Delay(step);
                remaining -= step;
            }
            Alarm fired = a;
            {
                std::lock_guard lock(R().mutex);
                auto it = R().local.find(a.name);
                // A one-shot alarm: gone once it fires (the job, this frame, is released last).
                if (it != R().local.end()) {
                    rt::Job keep = std::move(it->second.second);
                    R().local.erase(it);
                    rt::Dispatcher::Main().Post([keep = std::make_shared<rt::Job>(std::move(keep))] {});
                }
            }
            Fire(fired);
        }
    }

    bool IsExact(AlarmKind k) { return k == AlarmKind::Exact || k == AlarmKind::ExactWhileIdle || k == AlarmKind::AlarmClock; }

    void SetAlarmHost(AlarmHost host) {
        std::lock_guard lock(R().mutex);
        R().host = std::move(host);
    }

    void RegisterAlarmHandler(std::string name, AlarmHandler handler) {
        std::lock_guard lock(R().mutex);
        R().handlers[std::move(name)] = std::make_shared<AlarmHandler>(std::move(handler));
    }

    bool Fire(const Alarm& a) {
        std::shared_ptr<AlarmHandler> h;
        {
            std::lock_guard lock(R().mutex);
            auto it = R().handlers.find(a.name);
            if (it != R().handlers.end()) h = it->second;
        }
        if (!h) {
            log::Error("kiln", "alarm {} went off with no handler (register it with a static alarm::AlarmRegistration)", a.name);
            return false;
        }
        (*h)(a);
        return true;
    }

    bool CanScheduleExactAlarms() {
        AlarmHost h = Host();
        return h.canScheduleExact ? h.canScheduleExact() : true;
    }

    void OpenExactAlarmSettings() {
        AlarmHost h = Host();
        if (h.openExactSettings) h.openExactSettings();
    }

    bool Schedule(const Alarm& a) {
        if (a.name.empty()) throw std::invalid_argument("alarm::Schedule: the alarm needs a name");
        if (IsExact(a.kind) && !CanScheduleExactAlarms()) {
            log::Warn("kiln", "alarm {}: exact alarms are not allowed (SCHEDULE_EXACT_ALARM)", a.name);
            return false;
        }
        AlarmHost h = Host();
        if (h.schedule) return h.schedule(a);
        Cancel(a.name);
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), RunAt(a));
        std::lock_guard lock(R().mutex);
        if (!job.Done()) R().local[a.name] = { a, std::move(job) };
        return true;
    }

    void Cancel(const std::string& name) {
        AlarmHost h = Host();
        if (h.cancel) {
            h.cancel(name);
            return;
        }
        rt::Job job;
        {
            std::lock_guard lock(R().mutex);
            auto it = R().local.find(name);
            if (it == R().local.end()) return;
            job = std::move(it->second.second);
            R().local.erase(it);
        }
        job.Cancel();
    }

    std::vector<Alarm> Pending() {
        AlarmHost h = Host();
        if (h.pending) return h.pending();
        std::lock_guard lock(R().mutex);
        std::vector<Alarm> out;
        for (const auto& [name, entry] : R().local) out.push_back(entry.first);
        return out;
    }

    std::vector<std::uint8_t> Encode(const Alarm& a) {
        jvm::Writer w;
        w.I32(1);   // format
        w.Str(a.name);
        w.I64(a.triggerAtMillis);
        w.I32(int(a.kind));
        w.I64(a.windowLengthMillis);
        w.Str(a.payload);
        w.Str(a.showUri);
        return w.Take();
    }

    Alarm Decode(const std::vector<std::uint8_t>& bytes) {
        jvm::Reader r(bytes);
        if (r.I32() != 1) throw std::invalid_argument("alarm: unknown format");
        Alarm a;
        a.name = r.Str();
        a.triggerAtMillis = r.I64();
        a.kind = AlarmKind(r.I32());
        a.windowLengthMillis = r.I64();
        a.payload = r.Str();
        a.showUri = r.Str();
        return a;
    }

}
