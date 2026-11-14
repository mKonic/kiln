#pragma once

// Alarms at a time (AlarmManager, AlarmManagerCompat): a handler of the app's runs at a wall-clock
// time, even with the app closed and the device dozing. For reminders, timers and calendar
// events; deferrable background work belongs in kiln::work.
//
//   alarm::AlarmRegistration reminder("reminder", [](const alarm::Alarm& a) {
//       ui::Notify({ .id = 7, .channelId = "reminders", .title = "Reminder", .text = a.payload });
//   });
//   alarm::Schedule({ .name = "reminder", .triggerAtMillis = at, .payload = "Call Ada" });
//
// Exact alarms (Exact, ExactWhileIdle, AlarmClock) need android.permission.SCHEDULE_EXACT_ALARM
// (the user may turn it off: CanScheduleExactAlarms, OpenExactAlarmSettings) or USE_EXACT_ALARM
// (alarm clock and calendar apps only). Scheduled alarms are kept and set again after a reboot
// (with android.permission.RECEIVE_BOOT_COMPLETED) and when exact alarms are allowed again.

#include <kiln/Function.h>

#include <cstdint>
#include <string>
#include <vector>

namespace kiln::alarm {

    enum class AlarmKind {
        Inexact,             // AlarmManager.set: around the time, batched; not while dozing
        InexactWhileIdle,    // setAndAllowWhileIdle
        Window,              // setWindow: within [trigger, trigger + windowLengthMillis]
        Exact,               // setExact: at the time; not while dozing
        ExactWhileIdle,      // setExactAndAllowWhileIdle: at the time, dozing or not
        AlarmClock,          // setAlarmClock: the user's alarm, shown in the status bar and lock screen
    };

    struct Alarm {
        // One alarm per name: scheduling a name again replaces it. The handler with this name runs.
        std::string name;
        std::int64_t triggerAtMillis = 0;   // wall-clock time (ms since the epoch, UTC)
        AlarmKind kind = AlarmKind::ExactWhileIdle;
        std::int64_t windowLengthMillis = 0;   // Window
        std::string payload;                   // handed to the handler
        // AlarmClock: what tapping the alarm in the status bar opens (a deep link; empty: the app).
        std::string showUri;
        bool operator==(const Alarm&) const = default;
    };

    bool IsExact(AlarmKind kind);

    // Sets the alarm (replacing one of the same name). False when an exact alarm is not allowed
    // (CanScheduleExactAlarms) or the platform refused it.
    bool Schedule(const Alarm& alarm);
    void Cancel(const std::string& name);
    std::vector<Alarm> Pending();
    // AlarmManager.canScheduleExactAlarms (true before Android 12, and where there is no platform).
    bool CanScheduleExactAlarms();
    // Settings' page where the user allows exact alarms (ACTION_REQUEST_SCHEDULE_EXACT_ALARM).
    void OpenExactAlarmSettings();

    // A handler, run on the main thread when its alarm goes off (in a process started for it if
    // the app was not running). Register with a static AlarmRegistration so it exists then.
    using AlarmHandler = kiln::Function<void(const Alarm&)>;
    void RegisterAlarmHandler(std::string name, AlarmHandler handler);
    struct AlarmRegistration {
        AlarmRegistration(std::string name, AlarmHandler handler) { RegisterAlarmHandler(std::move(name), std::move(handler)); }
    };
    // From the platform: the alarm went off. False when no handler has its name.
    bool Fire(const Alarm& alarm);

    // The platform's alarms (Android installs AlarmManager's). Without one, alarms run in the
    // process on the main dispatcher's clock (the desktop, tests).
    struct AlarmHost {
        kiln::Function<bool(const Alarm&)> schedule;
        kiln::Function<void(const std::string& name)> cancel;
        kiln::Function<std::vector<Alarm>()> pending;
        kiln::Function<bool()> canScheduleExact;
        kiln::Function<void()> openExactSettings;
    };
    void SetAlarmHost(AlarmHost host);

    // The alarm as bytes (the platform keeps it with the pending intent and across reboots).
    std::vector<std::uint8_t> Encode(const Alarm& alarm);
    Alarm Decode(const std::vector<std::uint8_t>& bytes);

}
