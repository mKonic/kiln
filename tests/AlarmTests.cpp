// Alarms: handlers by name, the in-process host on the dispatcher's clock, exact-alarm gating.
#include "Check.h"

#include "kiln/runtime/Task.h"
#include "kiln/work/Alarm.h"

#include <chrono>

using namespace kiln;
using namespace kiln::alarm;

namespace {
    std::int64_t Now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    struct VirtualTime {
        bool was = rt::Dispatcher::Main().VirtualTime();
        VirtualTime() { rt::Dispatcher::Main().SetVirtualTime(true); }
        ~VirtualTime() {
            rt::Dispatcher::Main().RunPending();
            rt::Dispatcher::Main().SetVirtualTime(was);
        }
        void Advance(std::int64_t ms) {
            rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(ms));
            rt::Dispatcher::Main().RunPending();
        }
    };
}

TEST(AlarmsRunTheirHandlerAtTheTime) {
    VirtualTime t;
    std::vector<std::string> fired;
    AlarmRegistration r("test-reminder", [&](const Alarm& a) { fired.push_back(a.payload); });
    CHECK(Schedule({ .name = "test-reminder", .triggerAtMillis = Now() + 1000, .payload = "first" }));
    CHECK(Pending().size() == 1 && Pending()[0].payload == "first");
    // Scheduling the name again replaces it.
    CHECK(Schedule({ .name = "test-reminder", .triggerAtMillis = Now() + 3000, .payload = "second" }));
    t.Advance(1500);
    CHECK(fired.empty() && Pending().size() == 1);
    t.Advance(2000);
    CHECK(fired == std::vector<std::string>{ "second" } && Pending().empty());
    // A time already past goes off at once; cancelled, nothing does.
    CHECK(Schedule({ .name = "test-reminder", .triggerAtMillis = Now() - 5000, .payload = "late" }));
    t.Advance(0);
    CHECK(fired.back() == "late");
    Schedule({ .name = "test-reminder", .triggerAtMillis = Now() + 1000, .payload = "never" });
    Cancel("test-reminder");
    t.Advance(2000);
    CHECK(fired.size() == 2 && Pending().empty());
    // No handler: reported, nothing runs.
    CHECK(!Fire({ .name = "test-nobody" }));
}

TEST(AlarmsGoToThePlatformAndNeedExactPermission) {
    std::vector<Alarm> scheduled;
    bool exactAllowed = false;
    int settings = 0;
    SetAlarmHost({ .schedule = [&](const Alarm& a) { scheduled.push_back(a); return true; },
                   .cancel = [&](const std::string& n) { std::erase_if(scheduled, [&](const Alarm& a) { return a.name == n; }); },
                   .pending = [&] { return scheduled; },
                   .canScheduleExact = [&] { return exactAllowed; },
                   .openExactSettings = [&] { ++settings; } });
    CHECK(!CanScheduleExactAlarms());
    CHECK(!Schedule({ .name = "a", .triggerAtMillis = 1, .kind = AlarmKind::ExactWhileIdle }) && scheduled.empty());
    CHECK(Schedule({ .name = "a", .triggerAtMillis = 1, .kind = AlarmKind::InexactWhileIdle }) && scheduled.size() == 1);
    OpenExactAlarmSettings();
    exactAllowed = true;
    CHECK(Schedule({ .name = "b", .triggerAtMillis = 2, .kind = AlarmKind::AlarmClock, .showUri = "app://alarms" }));
    Cancel("a");
    CHECK(Pending().size() == 1 && Pending()[0].name == "b" && settings == 1);
    SetAlarmHost({});
    const Alarm a{ .name = "x", .triggerAtMillis = 1'700'000'000'000, .kind = AlarmKind::Window, .windowLengthMillis = 60'000,
                   .payload = "p", .showUri = "u" };
    CHECK(Decode(Encode(a)) == a);
    CHECK(IsExact(AlarmKind::AlarmClock) && !IsExact(AlarmKind::Window));
}
