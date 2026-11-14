// Vibration, sensors, location and the biometric prompt over fake hosts: the checks, the flows
// and the cancellation each API promises.
#include "Check.h"

#include "kiln/ui/Biometric.h"
#include "kiln/ui/Location.h"
#include "kiln/ui/Sensors.h"
#include "kiln/ui/Vibration.h"

#include <map>

using namespace kiln;
using namespace kiln::ui;

TEST(VibrationChecksPatternsAndUsesTheHost) {
    SetVibrationHost({});
    CHECK(!Vibrate(VibrationEffect::OneShot(50)) && !HasVibrator());
    std::vector<VibrationEffect> played;
    int cancels = 0;
    SetVibrationHost({ .vibrate = [&](const VibrationEffect& e) { played.push_back(e); return true; }, .cancel = [&] { ++cancels; },
                       .hasVibrator = [] { return true; } });
    CHECK(Vibrate(VibrationEffect::Waveform({ 0, 40, 60, 40 }, { 0, 255, 0, 128 }, 1)));
    CHECK(!Vibrate(VibrationEffect::Waveform({ 0, 40 }, { 255 })));          // amplitudes do not match
    CHECK(!Vibrate(VibrationEffect::Waveform({ 0, 40 }, {}, 2)));            // repeat past the end
    CHECK(Vibrate(VibrationEffect::Compose({ { VibrationEffect::PrimitiveClick, 0.5f, 0 }, { VibrationEffect::PrimitiveTick, 1, 20 } })));
    CancelVibration();
    CHECK(played.size() == 2 && played[1].primitives.size() == 2 && cancels == 1 && HasVibrator());
    SetVibrationHost({});
}

TEST(SensorEventsFlowWhileCollected) {
    std::map<int, kiln::Function<void(const SensorEvent&)>> listeners;
    int next = 0;
    SetSensorHost({
        .has = [](SensorType t) { return t == SensorType::Accelerometer; },
        .start = [&](SensorType t, int, kiln::Function<void(const SensorEvent&)> l) { if (t != SensorType::Accelerometer) return -1; listeners[next] = std::move(l); return next++; },
        .stop = [&](int id) { listeners.erase(id); },
    });
    CHECK(HasSensor(SensorType::Accelerometer) && !HasSensor(SensorType::Light));
    std::vector<float> seen;
    {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), SensorEvents(SensorType::Accelerometer).Collect(
            kiln::Function<void(const SensorEvent&)>([&](const SensorEvent& e) { seen.push_back(e.values[0]); })));
        CHECK(listeners.size() == 1);
        for (float x : { 1.0f, 2.0f }) { SensorEvent e; e.values[0] = x; e.valueCount = 3; listeners.begin()->second(e); }
        CHECK(seen == (std::vector<float>{ 1, 2 }));
    }
    CHECK(listeners.empty());   // collection ended: unsubscribed
    // No such sensor: the flow ends at once.
    bool ended = false;
    rt::Job none = rt::Launch(rt::Dispatcher::Main(), [](bool& ended) -> rt::Task<> {
        co_await SensorEvents(SensorType::Light).Collect(kiln::Function<void(const SensorEvent&)>([](const SensorEvent&) {}));
        ended = true;
    }(ended));
    CHECK(ended);
    SetSensorHost({});
}

TEST(LocationAnswersOnceAndCancels) {
    std::map<int, kiln::Function<void(std::optional<Location>)>> pending;
    std::vector<int> cancelled;
    int next = 0;
    SetLocationHost({
        .lastKnown = [] { return std::optional(Location{ .latitude = 1, .longitude = 2, .provider = "network" }); },
        .current = [&](const std::string&, kiln::Function<void(std::optional<Location>)> done) { pending[next] = std::move(done); return next++; },
        .cancel = [&](int id) { cancelled.push_back(id); },
        .enabled = [] { return true; },
    });
    CHECK(LastKnownLocation() && LastKnownLocation()->latitude == 1 && IsLocationEnabled());
    std::optional<Location> got;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](std::optional<Location>& got) -> rt::Task<> { got = co_await CurrentLocation(); }(got));
    CHECK(!job.Done() && pending.size() == 1);
    pending[0](Location{ .latitude = 45.5, .longitude = -73.6, .accuracy = 12.0f, .provider = "gps" });
    CHECK(job.Done() && got && got->latitude == 45.5 && got->accuracy == std::optional(12.0f) && cancelled.empty());
    // A task cancelled before the fix cancels the request.
    {
        rt::Job waiting = rt::Launch(rt::Dispatcher::Main(), [](std::optional<Location>& got) -> rt::Task<> { got = co_await CurrentLocation(); }(got));
        CHECK(!waiting.Done());
    }
    CHECK(cancelled == std::vector<int>{ 1 });
    SetLocationHost({});
    rt::Job off = rt::Launch(rt::Dispatcher::Main(), [](std::optional<Location>& got) -> rt::Task<> { got = co_await CurrentLocation(); }(got));
    CHECK(off.Done() && !got);
}

TEST(BiometricPromptResultsAndCancellation) {
    CHECK(CanAuthenticate() == -2);
    kiln::Function<void(BiometricResult)> answer;
    std::vector<int> cancelled;
    BiometricPromptInfo shown;
    SetBiometricHost({
        .canAuthenticate = [](int a) { return a == kBiometricStrong ? 11 : 0; },
        .authenticate = [&](const BiometricPromptInfo& i, kiln::Function<void(BiometricResult)> done) { shown = i; answer = std::move(done); return 7; },
        .cancel = [&](int id) { cancelled.push_back(id); },
    });
    CHECK(CanAuthenticate() == 0 && CanAuthenticate(kBiometricStrong) == 11);
    BiometricResult r;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](BiometricResult& r) -> rt::Task<> { r = co_await Authenticate({ .title = "Unlock" }); }(r));
    CHECK(shown.title == "Unlock" && !job.Done());
    answer(BiometricResult{ true, 0, "", 2 });
    CHECK(job.Done() && r.succeeded && r.authenticationType == 2 && cancelled.empty());
    {
        rt::Job open = rt::Launch(rt::Dispatcher::Main(), [](BiometricResult& r) -> rt::Task<> { r = co_await Authenticate({ .title = "Again" }); }(r));
    }
    CHECK(cancelled == std::vector<int>{ 7 });
    SetBiometricHost({});
}
