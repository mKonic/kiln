#include "kiln/media/stream/BandwidthMeter.h"

#include "kiln/media/stream/BandwidthCountryGroups.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#ifdef __ANDROID__
#include "kiln/jvm/Jvm.h"
#endif

namespace kiln::media::stream {

    // --- SlidingPercentile ---------------------------------------------------------------------

    void SlidingPercentile::Reset() {
        m_Samples.clear();
        m_SortOrder = -1;
        m_NextSampleIndex = 0;
        m_TotalWeight = 0;
    }

    void SlidingPercentile::SortByIndex() {
        if (m_SortOrder != 1) {
            std::stable_sort(m_Samples.begin(), m_Samples.end(), [](const Sample& a, const Sample& b) { return a.index < b.index; });
            m_SortOrder = 1;
        }
    }

    void SlidingPercentile::SortByValue() {
        if (m_SortOrder != 0) {
            // Float.compare: NaN after everything, -0 before +0.
            std::stable_sort(m_Samples.begin(), m_Samples.end(), [](const Sample& a, const Sample& b) {
                if (std::isnan(a.value) || std::isnan(b.value)) return !std::isnan(a.value) && std::isnan(b.value);
                if (a.value == b.value) return std::signbit(a.value) && !std::signbit(b.value);
                return a.value < b.value;
            });
            m_SortOrder = 0;
        }
    }

    void SlidingPercentile::AddSample(int weight, float value) {
        SortByIndex();
        m_Samples.push_back({ m_NextSampleIndex++, weight, value });
        m_TotalWeight += weight;
        while (m_TotalWeight > m_MaxWeight) {
            const int excessWeight = m_TotalWeight - m_MaxWeight;
            Sample& oldestSample = m_Samples.front();
            if (oldestSample.weight <= excessWeight) {
                m_TotalWeight -= oldestSample.weight;
                m_Samples.erase(m_Samples.begin());
            } else {
                oldestSample.weight -= excessWeight;
                m_TotalWeight -= excessWeight;
            }
        }
    }

    float SlidingPercentile::GetPercentile(float percentile) {
        SortByValue();
        const float desiredWeight = percentile * float(m_TotalWeight);
        int accumulatedWeight = 0;
        for (const Sample& sample : m_Samples) {
            accumulatedWeight += sample.weight;
            if (float(accumulatedWeight) >= desiredWeight) return sample.value;
        }
        return m_Samples.empty() ? std::nanf("") : m_Samples.back().value;
    }

    // --- NetworkTypeObserver -------------------------------------------------------------------

    NetworkTypeObserver& NetworkTypeObserver::Instance() {
        static NetworkTypeObserver instance;
        return instance;
    }

    NetworkType NetworkTypeObserver::GetNetworkType() const {
        std::lock_guard lock(m_Mutex);
        return m_Type;
    }

    int NetworkTypeObserver::Register(kiln::Function<void(NetworkType)> listener) {
        auto holder = std::make_shared<kiln::Function<void(NetworkType)>>(std::move(listener));
        std::lock_guard dispatch(m_DispatchMutex);
        bool initialized;
        NetworkType type;
        int id;
        {
            std::lock_guard lock(m_Mutex);
            id = m_NextId++;
            m_Listeners.emplace(id, holder);
            initialized = m_Initialized;
            type = m_Type;
        }
        if (initialized) (*holder)(type);
        return id;
    }

    void NetworkTypeObserver::Unregister(int id) {
        std::lock_guard dispatch(m_DispatchMutex);
        std::lock_guard lock(m_Mutex);
        m_Listeners.erase(id);
    }

    void NetworkTypeObserver::Update(NetworkType type) {
        std::lock_guard dispatch(m_DispatchMutex);
        std::vector<std::shared_ptr<kiln::Function<void(NetworkType)>>> listeners;
        {
            std::lock_guard lock(m_Mutex);
            if (m_Initialized && m_Type == type) return;
            m_Initialized = true;
            m_Type = type;
            for (const auto& [id, listener] : m_Listeners) listeners.push_back(listener);
        }
        for (const auto& listener : listeners) (*listener)(type);
    }

    std::string GetCountryCode() {
        std::string code;
#ifdef __ANDROID__
        try {
            JNIEnv* env = jvm::Env();
            jvm::Global<jclass> c = jvm::FindClass("dev/kiln/KilnNetworkType");
            jmethodID m = env->GetStaticMethodID(c.get(), "countryCode", "()Ljava/lang/String;");
            jvm::Local<jstring> s(static_cast<jstring>(env->CallStaticObjectMethod(c.get(), m)), env);
            jvm::Check(env);
            if (s) {
                const char* chars = env->GetStringUTFChars(s.get(), nullptr);
                code = chars;
                env->ReleaseStringUTFChars(s.get(), chars);
            }
        } catch (const std::exception&) {
            code.clear();
        }
#else
        // Locale.getDefault().getCountry(): the locale's region (LANG=de_AT.UTF-8 -> AT).
        for (const char* var : { "LC_ALL", "LC_MESSAGES", "LANG" }) {
            const char* v = std::getenv(var);
            if (!v || !*v) continue;
            std::string locale = v;
            const std::size_t underscore = locale.find('_');
            if (underscore != std::string::npos) code = locale.substr(underscore + 1, 2);
            break;
        }
#endif
        std::transform(code.begin(), code.end(), code.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        return code;
    }

    // --- DefaultBandwidthMeter -----------------------------------------------------------------

    const std::vector<std::int64_t> DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATES_WIFI = { 4'300'000, 3'200'000, 2'400'000, 1'700'000, 860'000 };
    const std::vector<std::int64_t> DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATES_2G = { 1'500'000, 980'000, 750'000, 520'000, 290'000 };
    const std::vector<std::int64_t> DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATES_3G = { 2'000'000, 1'300'000, 1'000'000, 860'000, 610'000 };
    const std::vector<std::int64_t> DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATES_4G = { 2'500'000, 1'700'000, 1'200'000, 970'000, 680'000 };
    const std::vector<std::int64_t> DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATES_5G_NSA = { 4'700'000, 2'800'000, 2'100'000, 1'700'000, 980'000 };
    const std::vector<std::int64_t> DefaultBandwidthMeter::DEFAULT_INITIAL_BITRATE_ESTIMATES_5G_SA = { 2'700'000, 2'000'000, 1'600'000, 1'300'000, 1'000'000 };

    std::int64_t GetInitialBitrateEstimateForCountry(const std::string& countryCode, NetworkType networkType) {
        const detail::CountryGroups* groups = &detail::kDefaultCountryGroups;
        const auto it = std::lower_bound(detail::kCountryGroups.begin(), detail::kCountryGroups.end(), countryCode,
                                         [](const detail::CountryGroups& g, const std::string& code) { return std::string(g.code) < code; });
        if (it != detail::kCountryGroups.end() && countryCode == it->code) groups = &*it;
        using M = DefaultBandwidthMeter;
        switch (networkType) {
            case NetworkType::Wifi:
            case NetworkType::Ethernet: return M::DEFAULT_INITIAL_BITRATE_ESTIMATES_WIFI.at(groups->wifi);
            case NetworkType::Cellular2G: return M::DEFAULT_INITIAL_BITRATE_ESTIMATES_2G.at(groups->g2);
            case NetworkType::Cellular3G: return M::DEFAULT_INITIAL_BITRATE_ESTIMATES_3G.at(groups->g3);
            case NetworkType::Cellular4G: return M::DEFAULT_INITIAL_BITRATE_ESTIMATES_4G.at(groups->g4);
            case NetworkType::Cellular5GNSA: return M::DEFAULT_INITIAL_BITRATE_ESTIMATES_5G_NSA.at(groups->g5Nsa);
            case NetworkType::Cellular5GSA: return M::DEFAULT_INITIAL_BITRATE_ESTIMATES_5G_SA.at(groups->g5Sa);
            default: return M::DEFAULT_INITIAL_BITRATE_ESTIMATE;
        }
    }

    namespace {
        std::int64_t ElapsedRealtimeMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    DefaultBandwidthMeter::Builder::Builder() : Builder(&NetworkTypeObserver::Instance(), [] { return GetCountryCode(); }) {}

    DefaultBandwidthMeter::Builder DefaultBandwidthMeter::Builder::WithoutContext() { return Builder(nullptr, nullptr); }

    DefaultBandwidthMeter::Builder::Builder(NetworkTypeObserver* observer, kiln::Function<std::string()> countryCode)
        : m_Observer(observer), m_CountryCode(std::move(countryCode)) {
        m_InitialBitrateEstimates[NetworkType::Unknown] = DEFAULT_INITIAL_BITRATE_ESTIMATE;
        for (NetworkType t : { NetworkType::Wifi, NetworkType::Cellular2G, NetworkType::Cellular3G, NetworkType::Cellular4G, NetworkType::Cellular5GNSA,
                               NetworkType::Cellular5GSA, NetworkType::Ethernet })
            m_InitialBitrateEstimates[t] = kTimeUnset;
    }

    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetSlidingWindowMaxWeight(int slidingWindowMaxWeight) {
        m_SlidingWindowMaxWeight = slidingWindowMaxWeight;
        return *this;
    }
    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetInitialBitrateEstimate(std::int64_t initialBitrateEstimate) {
        for (auto& [type, estimate] : m_InitialBitrateEstimates) estimate = initialBitrateEstimate;
        return *this;
    }
    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetInitialBitrateEstimate(NetworkType networkType, std::int64_t initialBitrateEstimate) {
        m_InitialBitrateEstimates[networkType] = initialBitrateEstimate;
        return *this;
    }
    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetInitialBitrateEstimate(const std::string& countryCode) {
        std::string upper = countryCode;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
        for (auto& [type, estimate] : m_InitialBitrateEstimates) estimate = GetInitialBitrateEstimateForCountry(upper, type);
        return *this;
    }
    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetClock(Clock clock) {
        m_Clock = std::move(clock);
        return *this;
    }
    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetResetOnNetworkTypeChange(bool resetOnNetworkTypeChange) {
        m_ResetOnNetworkTypeChange = resetOnNetworkTypeChange;
        return *this;
    }
    DefaultBandwidthMeter::Builder& DefaultBandwidthMeter::Builder::SetInitialBitrateSupplier(InitialBitrateSupplier initialBitrateSupplier) {
        m_InitialBitrateSupplier = std::move(initialBitrateSupplier);
        return *this;
    }
    std::unique_ptr<DefaultBandwidthMeter> DefaultBandwidthMeter::Builder::Build() { return std::unique_ptr<DefaultBandwidthMeter>(new DefaultBandwidthMeter(*this)); }

    DefaultBandwidthMeter::DefaultBandwidthMeter(Builder& b)
        : m_Observer(b.m_Observer), m_CountryCodeProvider(std::move(b.m_CountryCode)), m_InitialBitrateEstimates(b.m_InitialBitrateEstimates),
          m_Clock(b.m_Clock ? std::move(b.m_Clock) : Clock(&ElapsedRealtimeMs)), m_ResetOnNetworkTypeChange(b.m_ResetOnNetworkTypeChange),
          m_InitialBitrateSupplier(std::move(b.m_InitialBitrateSupplier)), m_SlidingPercentile(b.m_SlidingWindowMaxWeight) {
        if (m_Observer) {
            m_NetworkType = m_Observer->GetNetworkType();
            m_BitrateEstimate = GetInitialBitrateEstimateForNetworkType(m_NetworkType);
            m_ObserverRegistration = m_Observer->Register([this](NetworkType type) { OnNetworkTypeChanged(type); });
        } else {
            m_NetworkType = NetworkType::Unknown;
            m_BitrateEstimate = DEFAULT_INITIAL_BITRATE_ESTIMATE;
        }
    }

    DefaultBandwidthMeter::~DefaultBandwidthMeter() {
        if (m_Observer) m_Observer->Unregister(m_ObserverRegistration);
    }

    void DefaultBandwidthMeter::SetNetworkTypeOverride(NetworkType networkType) {
        std::lock_guard lock(m_Mutex);
        m_NetworkTypeOverride = networkType;
        m_NetworkTypeOverrideSet = true;
        OnNetworkTypeChanged(networkType);
    }

    std::int64_t DefaultBandwidthMeter::GetBitrateEstimate() {
        std::lock_guard lock(m_Mutex);
        return m_BitrateEstimate;
    }

    void DefaultBandwidthMeter::AddEventListener(EventListener listener) {
        std::lock_guard lock(m_Mutex);
        m_Listeners.push_back(std::move(listener));
    }

    void DefaultBandwidthMeter::OnTransferStart(bool isNetwork, bool mightNotUseFullNetworkSpeed) {
        if (!isNetwork || mightNotUseFullNetworkSpeed) return;
        std::lock_guard lock(m_Mutex);
        if (m_StreamCount == 0) m_SampleStartTimeMs = m_Clock();
        m_StreamCount++;
    }

    void DefaultBandwidthMeter::OnBytesTransferred(bool isNetwork, int bytesTransferred, bool mightNotUseFullNetworkSpeed) {
        if (!isNetwork || mightNotUseFullNetworkSpeed) return;
        std::lock_guard lock(m_Mutex);
        m_SampleBytesTransferred += bytesTransferred;
    }

    void DefaultBandwidthMeter::OnTransferEnd(bool isNetwork, bool mightNotUseFullNetworkSpeed) {
        if (!isNetwork || mightNotUseFullNetworkSpeed) return;
        std::lock_guard lock(m_Mutex);
        if (m_StreamCount <= 0) throw std::logic_error("transfer end without a start");
        const std::int64_t nowMs = m_Clock();
        const int sampleElapsedTimeMs = int(nowMs - m_SampleStartTimeMs);
        m_TotalElapsedTimeMs += sampleElapsedTimeMs;
        m_TotalBytesTransferred += m_SampleBytesTransferred;
        if (sampleElapsedTimeMs > 0) {
            const float bitsPerSecond = (float(m_SampleBytesTransferred) * 8000.0f) / float(sampleElapsedTimeMs);
            m_SlidingPercentile.AddSample(int(std::sqrt(double(m_SampleBytesTransferred))), bitsPerSecond);
            if (m_TotalElapsedTimeMs >= 2000 || m_TotalBytesTransferred >= 512 * 1024)   // ELAPSED_MILLIS / BYTES_TRANSFERRED_FOR_ESTIMATE
                m_BitrateEstimate = std::int64_t(m_SlidingPercentile.GetPercentile(0.5f));
            MaybeNotifyBandwidthSample(sampleElapsedTimeMs, m_SampleBytesTransferred, m_BitrateEstimate);
            m_SampleStartTimeMs = nowMs;
            m_SampleBytesTransferred = 0;
        }
        m_StreamCount--;
    }

    void DefaultBandwidthMeter::OnNetworkTypeChanged(NetworkType networkType) {
        std::lock_guard lock(m_Mutex);
        if (m_NetworkType != NetworkType::Unknown && !m_ResetOnNetworkTypeChange) return;
        if (m_NetworkTypeOverrideSet) networkType = m_NetworkTypeOverride;
        if (m_NetworkType == networkType && m_CountryCode) return;
        m_NetworkType = networkType;
        if (networkType == NetworkType::Offline || networkType == NetworkType::Unknown || networkType == NetworkType::Other) return;
        if (!m_CountryCode) m_CountryCode = m_CountryCodeProvider ? m_CountryCodeProvider() : std::string();
        m_BitrateEstimate = GetInitialBitrateEstimateForNetworkType(networkType);
        const std::int64_t nowMs = m_Clock();
        const int sampleElapsedTimeMs = m_StreamCount > 0 ? int(nowMs - m_SampleStartTimeMs) : 0;
        MaybeNotifyBandwidthSample(sampleElapsedTimeMs, m_SampleBytesTransferred, m_BitrateEstimate);
        m_SampleStartTimeMs = nowMs;
        m_SampleBytesTransferred = 0;
        m_TotalBytesTransferred = 0;
        m_TotalElapsedTimeMs = 0;
        m_SlidingPercentile.Reset();
    }

    void DefaultBandwidthMeter::MaybeNotifyBandwidthSample(int elapsedMs, std::int64_t bytesTransferred, std::int64_t bitrateEstimate) {
        if (elapsedMs == 0 && bytesTransferred == 0 && bitrateEstimate == m_LastReportedBitrateEstimate) return;
        m_LastReportedBitrateEstimate = bitrateEstimate;
        for (auto& listener : m_Listeners) listener(elapsedMs, bytesTransferred, bitrateEstimate);
    }

    std::int64_t DefaultBandwidthMeter::GetInitialBitrateEstimateForNetworkType(NetworkType networkType) const {
        if (m_InitialBitrateSupplier) {
            const std::int64_t supplierEstimate = m_InitialBitrateSupplier(networkType);
            if (supplierEstimate != kTimeUnset) return supplierEstimate;
        }
        std::optional<std::int64_t> initialBitrateEstimate;
        if (const auto it = m_InitialBitrateEstimates.find(networkType); it != m_InitialBitrateEstimates.end()) {
            initialBitrateEstimate = it->second;
            if (*initialBitrateEstimate == kTimeUnset) initialBitrateEstimate = GetInitialBitrateEstimateForCountry(m_CountryCode.value_or(""), networkType);
        } else if (const auto unknown = m_InitialBitrateEstimates.find(NetworkType::Unknown); unknown != m_InitialBitrateEstimates.end()) {
            initialBitrateEstimate = unknown->second;
        }
        return initialBitrateEstimate.value_or(DEFAULT_INITIAL_BITRATE_ESTIMATE);
    }

}
