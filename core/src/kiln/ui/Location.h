#pragma once

// Location (android.location.LocationManager, without Play services). The app declares and
// requests android.permission.ACCESS_FINE_LOCATION or ACCESS_COARSE_LOCATION; without them
// every call answers nothing.
//
//   std::optional<ui::Location> here = co_await ui::CurrentLocation();

#include "kiln/runtime/Flow.h"
#include "kiln/runtime/Task.h"
#include <kiln/Function.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace kiln::ui {

    struct Location {
        double latitude = 0, longitude = 0;
        std::optional<double> altitude;           // metres above WGS84
        std::optional<float> accuracy;            // metres, 68% confidence
        std::optional<float> speed, bearing;      // m/s, degrees
        std::int64_t timeMillis = 0;              // UTC
        std::string provider;                     // "gps", "network", "fused"
        bool operator==(const Location&) const = default;
    };

    // LocationManager providers.
    inline constexpr const char* kGpsProvider = "gps";
    inline constexpr const char* kNetworkProvider = "network";
    inline constexpr const char* kFusedProvider = "fused";

    // getLastKnownLocation: the newest location any provider has, at once (may be old).
    std::optional<Location> LastKnownLocation();
    // getCurrentLocation: a fresh fix from the provider (the best enabled one by default), or
    // nothing after its timeout (30 s) or without permission. Cancelling the task cancels it.
    rt::Task<std::optional<Location>> CurrentLocation(std::string provider = {});
    // requestLocationUpdates: fixes at most every intervalMs, and only after moving
    // minDistanceMeters; ends at once without permission.
    rt::Flow<Location> LocationUpdates(std::string provider = {}, std::int64_t intervalMs = 5000, float minDistanceMeters = 0);
    bool IsLocationEnabled();

    struct LocationHost {
        kiln::Function<std::optional<Location>()> lastKnown;
        // Calls back once (nullopt: none); returns an id for cancel, or -1 when it could not start.
        kiln::Function<int(const std::string& provider, kiln::Function<void(std::optional<Location>)>)> current;
        kiln::Function<int(const std::string& provider, std::int64_t intervalMs, float minDistance, kiln::Function<void(const Location&)>)> updates;
        kiln::Function<void(int id)> cancel;
        kiln::Function<bool()> enabled;
    };
    void SetLocationHost(LocationHost host);

}
