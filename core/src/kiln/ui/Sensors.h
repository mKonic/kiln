#pragma once

// Sensors (the NDK's ASensorManager, android.hardware.Sensor's types): events delivered on the
// main thread while subscribed.
//
//   auto tilt = ui::SensorEvents(ui::SensorType::Accelerometer);          // a flow
//   auto& last = rt::CollectAsState(tilt, ui::SensorEvent{});             // in content

#include "kiln/runtime/Flow.h"
#include <kiln/Function.h>

#include <array>
#include <cstdint>
#include <memory>

namespace kiln::ui {

    // Sensor.TYPE_*.
    enum class SensorType : int {
        Accelerometer = 1, MagneticField = 2, Gyroscope = 4, Light = 5, Pressure = 6, Proximity = 8, Gravity = 9,
        LinearAcceleration = 10, RotationVector = 11, RelativeHumidity = 12, AmbientTemperature = 13,
        GameRotationVector = 15, StepDetector = 18, StepCounter = 19, GeomagneticRotationVector = 20, HeartRate = 21,
    };
    // SensorManager.SENSOR_DELAY_*: microseconds between events.
    inline constexpr int kSensorDelayFastest = 0, kSensorDelayGame = 20000, kSensorDelayUi = 66667, kSensorDelayNormal = 200000;

    struct SensorEvent {
        SensorType type = SensorType::Accelerometer;
        std::int64_t timestampNanos = 0;   // SystemClock.elapsedRealtimeNanos's clock
        std::array<float, 16> values{};    // as SensorEvent.values (x, y, z for the motion sensors)
        int valueCount = 0;
        int accuracy = 0;                  // SENSOR_STATUS_*
        bool operator==(const SensorEvent&) const = default;
    };

    bool HasSensor(SensorType type);

    // Events for as long as the subscription lives; empty when there is no such sensor.
    class SensorSubscription {
    public:
        SensorSubscription() = default;
        explicit SensorSubscription(int id) : m_Id(id) {}
        SensorSubscription(SensorSubscription&& o) noexcept : m_Id(std::exchange(o.m_Id, -1)) {}
        SensorSubscription& operator=(SensorSubscription&& o) noexcept { Reset(); m_Id = std::exchange(o.m_Id, -1); return *this; }
        ~SensorSubscription() { Reset(); }
        explicit operator bool() const { return m_Id >= 0; }
        void Reset();
    private:
        int m_Id = -1;
    };
    SensorSubscription ListenToSensor(SensorType type, int samplingPeriodUs, kiln::Function<void(const SensorEvent&)> listener);

    // The sensor's events as a flow (the newest kept when the collector is slow); it ends at once
    // when there is no such sensor.
    rt::Flow<SensorEvent> SensorEvents(SensorType type, int samplingPeriodUs = kSensorDelayUi);

    struct SensorHost {
        kiln::Function<bool(SensorType)> has;
        // Returns an id, or -1.
        kiln::Function<int(SensorType, int samplingPeriodUs, kiln::Function<void(const SensorEvent&)>)> start;
        kiln::Function<void(int id)> stop;
    };
    void SetSensorHost(SensorHost host);

}
