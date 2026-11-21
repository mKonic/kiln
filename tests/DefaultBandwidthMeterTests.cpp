// Ported from media3-exoplayer DefaultBandwidthMeterTest.java. Robolectric's connectivity and
// telephony shadows become a NetworkTypeObserver the test reports to and a country it sets.
// Left out: defaultInitialBitrateEstimate_for5gSa_forFastCountry_isGreaterThanEstimateForSlowCountry
// (@Ignore upstream).
#include "Check.h"

#include "kiln/media/stream/BandwidthMeter.h"

#include <atomic>
#include <thread>

using namespace kiln::media;
using namespace kiln::media::stream;

namespace {
    const std::string FAST_COUNTRY_ISO = "TW";
    const std::string SLOW_COUNTRY_ISO = "PG";

    // java.util.Random.
    struct JavaRandom {
        std::uint64_t seed;
        explicit JavaRandom(std::int64_t s) : seed((std::uint64_t(s) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1)) {}
        int Next(int bits) {
            seed = (seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
            return int(std::int64_t(seed >> (48 - bits)));
        }
        int NextInt(int bound) {
            int r = Next(31);
            const int m = bound - 1;
            if ((bound & m) == 0) return int((std::int64_t(bound) * std::int64_t(r)) >> 31);
            for (int u = r; u - (r = u % bound) + m < 0; u = Next(31)) {}
            return r;
        }
    };

    struct Fixture {
        NetworkTypeObserver observer;
        std::string country = "non-existent-country-to-force-default-values";
        DefaultBandwidthMeter::Builder Builder() {
            return DefaultBandwidthMeter::Builder(&observer, [this] { return country; });
        }
        std::int64_t EstimateFor(NetworkType type) {
            observer.Update(type);
            return Builder().Build()->GetBitrateEstimate();
        }
    };

    std::vector<std::int64_t> SimulateTransfers(DefaultBandwidthMeter& meter, std::atomic<std::int64_t>& clock, int count, JavaRandom& random) {
        std::vector<std::int64_t> estimates(static_cast<std::size_t>(count));
        for (int i = 0; i < count; i++) {
            meter.OnTransferStart(true);
            clock += random.NextInt(5000);
            meter.OnBytesTransferred(true, random.NextInt(5 * 1024 * 1024));
            meter.OnTransferEnd(true);
            estimates[std::size_t(i)] = meter.GetBitrateEstimate();
        }
        return estimates;
    }
    std::vector<std::int64_t> SimulateTransfers(DefaultBandwidthMeter& meter, std::atomic<std::int64_t>& clock, int count) {
        JavaRandom random(0);
        return SimulateTransfers(meter, clock, count, random);
    }
}

TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forWifi_isGreaterThanEstimateFor2G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Wifi) > f.EstimateFor(NetworkType::Cellular2G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forWifi_isGreaterThanEstimateFor3G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Wifi) > f.EstimateFor(NetworkType::Cellular3G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forEthernet_isGreaterThanEstimateFor2G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Ethernet) > f.EstimateFor(NetworkType::Cellular2G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forEthernet_isGreaterThanEstimateFor3G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Ethernet) > f.EstimateFor(NetworkType::Cellular3G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for4G_isGreaterThanEstimateFor2G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Cellular4G) > f.EstimateFor(NetworkType::Cellular2G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for4G_isGreaterThanEstimateFor3G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Cellular4G) > f.EstimateFor(NetworkType::Cellular3G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for3G_isGreaterThanEstimateFor2G) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Cellular3G) > f.EstimateFor(NetworkType::Cellular2G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for5gNsa_isGreaterThanEstimateFor4g) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Cellular5GNSA) > f.EstimateFor(NetworkType::Cellular4G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for5gSa_isGreaterThanEstimateFor3g) {
    Fixture f;
    CHECK(f.EstimateFor(NetworkType::Cellular5GSA) > f.EstimateFor(NetworkType::Cellular3G));
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forOffline_isReasonable) {
    Fixture f;
    const std::int64_t e = f.EstimateFor(NetworkType::Offline);
    CHECK(e > 100'000 && e < 50'000'000);
}

namespace {
    void FastGreaterThanSlow(NetworkType type) {
        Fixture f;
        f.observer.Update(type);
        f.country = FAST_COUNTRY_ISO;
        const std::int64_t fast = f.Builder().Build()->GetBitrateEstimate();
        f.country = SLOW_COUNTRY_ISO;
        const std::int64_t slow = f.Builder().Build()->GetBitrateEstimate();
        CHECK(fast > slow);
    }
}

TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forWifi_forFastCountry_isGreaterThanEstimateForSlowCountry) { FastGreaterThanSlow(NetworkType::Wifi); }
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_forEthernet_forFastCountry_isGreaterThanEstimateForSlowCountry) {
    FastGreaterThanSlow(NetworkType::Ethernet);
}
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for2G_forFastCountry_isGreaterThanEstimateForSlowCountry) { FastGreaterThanSlow(NetworkType::Cellular2G); }
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for3G_forFastCountry_isGreaterThanEstimateForSlowCountry) { FastGreaterThanSlow(NetworkType::Cellular3G); }
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for4g_forFastCountry_isGreaterThanEstimateForSlowCountry) { FastGreaterThanSlow(NetworkType::Cellular4G); }
TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_for5gNsa_forFastCountry_isGreaterThanEstimateForSlowCountry) {
    FastGreaterThanSlow(NetworkType::Cellular5GNSA);
}

TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_whileConnectedToNetwork_setsInitialEstimate) {
    Fixture f;
    f.observer.Update(NetworkType::Wifi);
    CHECK_EQ(f.Builder().SetInitialBitrateEstimate(123456789).Build()->GetBitrateEstimate(), 123456789);
}

TEST(DefaultBandwidthMeter_initialBitrateSupplier_setsInitialEstimate) {
    Fixture f;
    f.observer.Update(NetworkType::Wifi);
    CHECK_EQ(f.Builder().SetInitialBitrateSupplier([](NetworkType) -> std::int64_t { return 123456789; }).Build()->GetBitrateEstimate(), 123456789);
}

TEST(DefaultBandwidthMeter_initialBitrateSupplier_returnsTimeUnset_fallsBackToDefault) {
    Fixture f;
    f.observer.Update(NetworkType::Wifi);
    const std::int64_t e = f.Builder().SetInitialBitrateSupplier([](NetworkType) { return kTimeUnset; }).Build()->GetBitrateEstimate();
    CHECK(e != kTimeUnset);
    CHECK(e > 0);
}

TEST(DefaultBandwidthMeter_initialBitrateSupplier_onNetworkTypeOverride_setsInitialEstimate) {
    Fixture f;
    f.observer.Update(NetworkType::Wifi);
    const std::int64_t expected4gBitrate = 987654321;
    auto meter = f.Builder()
                     .SetInitialBitrateSupplier([&](NetworkType type) { return type == NetworkType::Cellular4G ? expected4gBitrate : kTimeUnset; })
                     .Build();
    CHECK(meter->GetBitrateEstimate() != expected4gBitrate);
    meter->SetNetworkTypeOverride(NetworkType::Cellular4G);
    CHECK_EQ(meter->GetBitrateEstimate(), expected4gBitrate);
}

TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_whileOffline_setsInitialEstimate) {
    Fixture f;
    f.observer.Update(NetworkType::Offline);
    CHECK_EQ(f.Builder().SetInitialBitrateEstimate(123456789).Build()->GetBitrateEstimate(), 123456789);
}

namespace {
    // initialBitrateEstimateOverwrite_for<X>_whileConnectedTo<Y>: set for X, connected to Y.
    std::int64_t OverwriteFor(NetworkType forType, NetworkType connectedTo) {
        Fixture f;
        f.observer.Update(connectedTo);
        return f.Builder().SetInitialBitrateEstimate(forType, 123456789).Build()->GetBitrateEstimate();
    }
}

TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forWifi_whileConnectedToWifi_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Wifi, NetworkType::Wifi), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forWifi_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Wifi, NetworkType::Cellular2G) != 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forEthernet_whileConnectedToEthernet_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Ethernet, NetworkType::Ethernet), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forEthernet_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Wifi, NetworkType::Cellular2G) != 123456789);   // upstream sets the Wi-Fi estimate here
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for2G_whileConnectedTo2G_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Cellular2G, NetworkType::Cellular2G), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for2G_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Cellular2G, NetworkType::Wifi) != 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for3G_whileConnectedTo3G_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Cellular3G, NetworkType::Cellular3G), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for3G_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Cellular3G, NetworkType::Wifi) != 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for4G_whileConnectedTo4G_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Cellular4G, NetworkType::Cellular4G), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for4G_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Cellular4G, NetworkType::Wifi) != 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for5gNsa_whileConnectedTo5gNsa_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Cellular5GNSA, NetworkType::Cellular5GNSA), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for5gNsa_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Cellular5GNSA, NetworkType::Cellular4G) != 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for5gSa_whileConnectedTo5gSa_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Cellular5GSA, NetworkType::Cellular5GSA), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_for5gSa_whileConnectedToOtherNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Cellular5GSA, NetworkType::Wifi) != 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forOffline_whileOffline_setsInitialEstimate) {
    CHECK_EQ(OverwriteFor(NetworkType::Offline, NetworkType::Offline), 123456789);
}
TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forOffline_whileConnectedToNetwork_doesNotSetInitialEstimate) {
    CHECK(OverwriteFor(NetworkType::Offline, NetworkType::Wifi) != 123456789);
}

TEST(DefaultBandwidthMeter_initialBitrateEstimateOverwrite_forCountry_usesDefaultValuesForCountry) {
    Fixture f;
    f.country = SLOW_COUNTRY_ISO;
    const std::int64_t slow = f.Builder().Build()->GetBitrateEstimate();
    f.country = FAST_COUNTRY_ISO;
    const std::int64_t fastWithSlowOverwrite = f.Builder().SetInitialBitrateEstimate(SLOW_COUNTRY_ISO).Build()->GetBitrateEstimate();
    CHECK_EQ(fastWithSlowOverwrite, slow);
}

TEST(DefaultBandwidthMeter_networkTypeOverride_updatesBitrateEstimate) {
    Fixture f;
    f.observer.Update(NetworkType::Ethernet);
    auto meter = f.Builder().Build();
    const std::int64_t ethernet = meter->GetBitrateEstimate();
    meter->SetNetworkTypeOverride(NetworkType::Cellular2G);
    CHECK(ethernet > meter->GetBitrateEstimate());
}

TEST(DefaultBandwidthMeter_networkTypeOverride_doesFullReset) {
    Fixture f;
    std::atomic<std::int64_t> clock{ 0 };
    f.observer.Update(NetworkType::Ethernet);
    auto meter = f.Builder().SetClock([&] { return clock.load(); }).Build();
    const std::vector<std::int64_t> withNewInstance = SimulateTransfers(*meter, clock, 100);
    f.observer.Update(NetworkType::Cellular2G);
    meter = f.Builder().SetClock([&] { return clock.load(); }).Build();
    SimulateTransfers(*meter, clock, 100);
    meter->SetNetworkTypeOverride(NetworkType::Ethernet);
    const std::vector<std::int64_t> afterReset = SimulateTransfers(*meter, clock, 100);
    CHECK(afterReset == withNewInstance);
}

TEST(DefaultBandwidthMeter_defaultInitialBitrateEstimate_withoutContext_isReasonable) {
    const std::int64_t e = DefaultBandwidthMeter::Builder::WithoutContext().Build()->GetBitrateEstimate();
    CHECK(e > 100'000 && e < 50'000'000);
}

TEST(DefaultBandwidthMeter_getBitrateEstimate_withSimultaneousTransferEvents_receivesUpdatedValues) {
    Fixture f;
    std::atomic<std::int64_t> clock{ 0 };
    auto meter = f.Builder().SetClock([&] { return clock.load(); }).Build();
    std::atomic<bool> bitrateEstimateUpdated{ false };
    std::thread thread([&] {
        JavaRandom random(0);
        while (!bitrateEstimateUpdated) SimulateTransfers(*meter, clock, 100, random);
    });
    std::int64_t current = meter->GetBitrateEstimate();
    while (!bitrateEstimateUpdated) {
        const std::int64_t next = meter->GetBitrateEstimate();
        if (next != current) {
            current = next;
            bitrateEstimateUpdated = true;
        }
    }
    thread.join();
    CHECK(bitrateEstimateUpdated.load());
}
