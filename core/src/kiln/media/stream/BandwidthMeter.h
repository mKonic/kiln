#pragma once

// media3's bandwidth estimation: SlidingPercentile, NetworkTypeObserver (C.NETWORK_TYPE_*) and
// DefaultBandwidthMeter, which turns network transfers into a bitrate estimate for adaptive
// track selection.

#include "kiln/Function.h"
#include "kiln/media/MediaItem.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

namespace kiln::media::stream {

    // SlidingPercentile: the weighted percentile of the most recent samples, up to a total weight.
    class SlidingPercentile {
    public:
        explicit SlidingPercentile(int maxWeight) : m_MaxWeight(maxWeight) {}
        void Reset();
        void AddSample(int weight, float value);
        // NaN without samples.
        float GetPercentile(float percentile);

    private:
        struct Sample {
            int index, weight;
            float value;
        };
        void SortByIndex();
        void SortByValue();
        int m_MaxWeight;
        std::vector<Sample> m_Samples;
        int m_SortOrder = -1;   // -1 none, 0 by value, 1 by index
        int m_NextSampleIndex = 0;
        int m_TotalWeight = 0;
    };

    // C.NetworkType.
    enum class NetworkType { Unknown = 0, Offline = 1, Wifi = 2, Cellular2G = 3, Cellular3G = 4, Cellular4G = 5, CellularUnknown = 6, Ethernet = 7, Other = 8, Cellular5GSA = 9,
                             Cellular5GNSA = 10 };

    // NetworkTypeObserver: the device's network type. The platform reports it (Android: the
    // active network and, for cellular, its generation); without a report it is Unknown.
    class NetworkTypeObserver {
    public:
        static NetworkTypeObserver& Instance();
        NetworkType GetNetworkType() const;
        // The listener hears the type now (once one was reported) and each change.
        int Register(kiln::Function<void(NetworkType)> listener);
        void Unregister(int id);
        // From the platform (and tests): the network type changed.
        void Update(NetworkType type);

    private:
        mutable std::mutex m_Mutex;
        // Held while listeners run, and by Unregister: once it returns, the listener is not running.
        std::recursive_mutex m_DispatchMutex;
        NetworkType m_Type = NetworkType::Unknown;
        bool m_Initialized = false;
        int m_NextId = 1;
        std::map<int, std::shared_ptr<kiln::Function<void(NetworkType)>>> m_Listeners;
    };

    // Util.getCountryCode: the network's country (TelephonyManager), else the locale's, upper case.
    std::string GetCountryCode();

    // TransferListener: told about each transfer a data source makes.
    class TransferListener {
    public:
        virtual ~TransferListener() = default;
        virtual void OnTransferStart(bool isNetwork, bool mightNotUseFullNetworkSpeed = false) = 0;
        virtual void OnBytesTransferred(bool isNetwork, int bytesTransferred, bool mightNotUseFullNetworkSpeed = false) = 0;
        virtual void OnTransferEnd(bool isNetwork, bool mightNotUseFullNetworkSpeed = false) = 0;
    };

    // BandwidthMeter: what adaptive track selection asks of an estimator.
    class BandwidthMeter {
    public:
        virtual ~BandwidthMeter() = default;
        // Bits per second.
        virtual std::int64_t GetBitrateEstimate() = 0;
        virtual std::int64_t GetTimeToFirstByteEstimateUs() { return kTimeUnset; }
        // What the data sources report their transfers to (none: they are not measured).
        virtual TransferListener* GetTransferListener() { return nullptr; }
    };

    // DefaultBandwidthMeter: the median of recent network transfers' throughput (weighted by the
    // square root of their size), starting from an estimate for the network type and country.
    class DefaultBandwidthMeter final : public BandwidthMeter, public TransferListener {
    public:
        static constexpr std::int64_t DEFAULT_INITIAL_BITRATE_ESTIMATE = 1'000'000;
        static constexpr int DEFAULT_SLIDING_WINDOW_MAX_WEIGHT = 2000;
        static const std::vector<std::int64_t> DEFAULT_INITIAL_BITRATE_ESTIMATES_WIFI, DEFAULT_INITIAL_BITRATE_ESTIMATES_2G,
            DEFAULT_INITIAL_BITRATE_ESTIMATES_3G, DEFAULT_INITIAL_BITRATE_ESTIMATES_4G, DEFAULT_INITIAL_BITRATE_ESTIMATES_5G_NSA,
            DEFAULT_INITIAL_BITRATE_ESTIMATES_5G_SA;

        using InitialBitrateSupplier = kiln::Function<std::int64_t(NetworkType)>;
        // elapsedRealtime in milliseconds.
        using Clock = kiln::Function<std::int64_t()>;
        // A bandwidth sample: elapsed milliseconds, bytes, and the estimate after it.
        using EventListener = kiln::Function<void(int, std::int64_t, std::int64_t)>;

        class Builder {
        public:
            // Observes NetworkTypeObserver::Instance() and GetCountryCode().
            Builder();
            // Builder(null): no network observation (Unknown, the default estimate).
            static Builder WithoutContext();
            // Observes this observer, with this country (tests).
            Builder(NetworkTypeObserver* observer, kiln::Function<std::string()> countryCode);
            Builder& SetSlidingWindowMaxWeight(int slidingWindowMaxWeight);
            Builder& SetInitialBitrateEstimate(std::int64_t initialBitrateEstimate);
            Builder& SetInitialBitrateEstimate(NetworkType networkType, std::int64_t initialBitrateEstimate);
            // The defaults for a country (ISO 3166-1 alpha-2).
            Builder& SetInitialBitrateEstimate(const std::string& countryCode);
            Builder& SetClock(Clock clock);
            Builder& SetResetOnNetworkTypeChange(bool resetOnNetworkTypeChange);
            Builder& SetInitialBitrateSupplier(InitialBitrateSupplier initialBitrateSupplier);
            std::unique_ptr<DefaultBandwidthMeter> Build();

        private:
            friend class DefaultBandwidthMeter;
            NetworkTypeObserver* m_Observer = nullptr;
            kiln::Function<std::string()> m_CountryCode;
            std::map<NetworkType, std::int64_t> m_InitialBitrateEstimates;
            int m_SlidingWindowMaxWeight = DEFAULT_SLIDING_WINDOW_MAX_WEIGHT;
            Clock m_Clock;
            bool m_ResetOnNetworkTypeChange = true;
            InitialBitrateSupplier m_InitialBitrateSupplier;
        };

        ~DefaultBandwidthMeter() override;
        // Treats the network as this type from now on.
        void SetNetworkTypeOverride(NetworkType networkType);
        std::int64_t GetBitrateEstimate() override;
        void AddEventListener(EventListener listener);

        // TransferListener: a transfer from the network (isNetwork) whose speed counts unless it
        // might not use the network's full speed.
        void OnTransferStart(bool isNetwork, bool mightNotUseFullNetworkSpeed = false) override;
        void OnBytesTransferred(bool isNetwork, int bytesTransferred, bool mightNotUseFullNetworkSpeed = false) override;
        void OnTransferEnd(bool isNetwork, bool mightNotUseFullNetworkSpeed = false) override;
        TransferListener* GetTransferListener() override { return this; }

    private:
        explicit DefaultBandwidthMeter(Builder& builder);
        void OnNetworkTypeChanged(NetworkType networkType);
        void MaybeNotifyBandwidthSample(int elapsedMs, std::int64_t bytesTransferred, std::int64_t bitrateEstimate);
        std::int64_t GetInitialBitrateEstimateForNetworkType(NetworkType networkType) const;

        std::recursive_mutex m_Mutex;
        NetworkTypeObserver* m_Observer;
        int m_ObserverRegistration = 0;
        kiln::Function<std::string()> m_CountryCodeProvider;
        std::map<NetworkType, std::int64_t> m_InitialBitrateEstimates;
        Clock m_Clock;
        bool m_ResetOnNetworkTypeChange;
        InitialBitrateSupplier m_InitialBitrateSupplier;
        SlidingPercentile m_SlidingPercentile;
        std::vector<EventListener> m_Listeners;
        int m_StreamCount = 0;
        std::int64_t m_SampleStartTimeMs = 0;
        std::int64_t m_SampleBytesTransferred = 0;
        std::int64_t m_TotalElapsedTimeMs = 0;
        std::int64_t m_TotalBytesTransferred = 0;
        std::int64_t m_BitrateEstimate = 0;
        std::int64_t m_LastReportedBitrateEstimate = 0;
        NetworkType m_NetworkType = NetworkType::Unknown;
        bool m_NetworkTypeOverrideSet = false;
        NetworkType m_NetworkTypeOverride = NetworkType::Unknown;
        std::optional<std::string> m_CountryCode;
    };

    // The default initial estimate for a country (upper case) and network type.
    std::int64_t GetInitialBitrateEstimateForCountry(const std::string& countryCode, NetworkType networkType);

}
