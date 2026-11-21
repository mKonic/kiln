// kiln — live playback speed control (DefaultLivePlaybackSpeedControl).
#include "kiln/media/LivePlaybackSpeedControl.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace kiln::media {

    namespace {
        // Util.msToUs (keeping unset values unset).
        std::int64_t MsToUs(std::int64_t ms) { return ms == kTimeUnset ? kTimeUnset : ms * 1000; }
        template <typename T> T Constrain(T value, T min, T max) { return std::max(min, std::min(value, max)); }
        // As Java's float arithmetic on a long.
        std::int64_t Smooth(std::int64_t smoothed, std::int64_t value, float factor) {
            return std::int64_t(factor * float(smoothed) + (1.f - factor) * float(value));
        }
    }

    LivePlaybackSpeedControl::LivePlaybackSpeedControl(Options options)
        : m_Options(std::move(options)),
          m_ProportionalControlFactor(m_Options.proportionalControlFactor / 1'000'000.f),
          m_MaxLiveOffsetErrorUsForUnitSpeed(m_Options.maxLiveOffsetErrorMsForUnitSpeed * 1000),
          m_TargetLiveOffsetRebufferDeltaUs(m_Options.targetLiveOffsetIncrementOnRebufferMs * 1000),
          m_MinPlaybackSpeed(m_Options.fallbackMinPlaybackSpeed),
          m_MaxPlaybackSpeed(m_Options.fallbackMaxPlaybackSpeed) {}

    std::int64_t LivePlaybackSpeedControl::NowMs() const {
        if (m_Options.elapsedRealtimeMs) return m_Options.elapsedRealtimeMs();
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void LivePlaybackSpeedControl::SetLiveConfiguration(const MediaItem::LiveConfiguration& c) {
        m_MediaConfigurationTargetLiveOffsetUs = MsToUs(c.targetOffsetMs);
        m_MinTargetLiveOffsetUs = MsToUs(c.minOffsetMs);
        m_MaxTargetLiveOffsetUs = MsToUs(c.maxOffsetMs);
        m_MinPlaybackSpeed = c.minPlaybackSpeed != kRateUnset ? c.minPlaybackSpeed : m_Options.fallbackMinPlaybackSpeed;
        m_MaxPlaybackSpeed = c.maxPlaybackSpeed != kRateUnset ? c.maxPlaybackSpeed : m_Options.fallbackMaxPlaybackSpeed;
        // No adjustments when the speed cannot change.
        if (m_MinPlaybackSpeed == 1.f && m_MaxPlaybackSpeed == 1.f) m_MediaConfigurationTargetLiveOffsetUs = kTimeUnset;
        MaybeResetTargetLiveOffsetUs();
    }

    void LivePlaybackSpeedControl::SetTargetLiveOffsetOverrideUs(std::int64_t liveOffsetUs) {
        m_TargetLiveOffsetOverrideUs = liveOffsetUs;
        MaybeResetTargetLiveOffsetUs();
    }

    void LivePlaybackSpeedControl::NotifyRebuffer() {
        if (m_CurrentTargetLiveOffsetUs == kTimeUnset) return;
        m_CurrentTargetLiveOffsetUs += m_TargetLiveOffsetRebufferDeltaUs;
        if (m_MaxTargetLiveOffsetUs != kTimeUnset && m_CurrentTargetLiveOffsetUs > m_MaxTargetLiveOffsetUs) m_CurrentTargetLiveOffsetUs = m_MaxTargetLiveOffsetUs;
        m_LastPlaybackSpeedUpdateMs = kTimeUnset;
    }

    float LivePlaybackSpeedControl::GetAdjustedPlaybackSpeed(std::int64_t liveOffsetUs, std::int64_t bufferedDurationUs) {
        if (m_MediaConfigurationTargetLiveOffsetUs == kTimeUnset) return 1.f;
        UpdateSmoothedMinPossibleLiveOffsetUs(liveOffsetUs, bufferedDurationUs);
        const std::int64_t now = NowMs();
        if (m_LastPlaybackSpeedUpdateMs != kTimeUnset && now - m_LastPlaybackSpeedUpdateMs < m_Options.minUpdateIntervalMs) return m_AdjustedPlaybackSpeed;
        m_LastPlaybackSpeedUpdateMs = now;
        AdjustTargetLiveOffsetUs(liveOffsetUs);
        const std::int64_t errorUs = liveOffsetUs - m_CurrentTargetLiveOffsetUs;
        if (std::llabs(errorUs) < m_MaxLiveOffsetErrorUsForUnitSpeed) {
            m_AdjustedPlaybackSpeed = 1.f;
        } else {
            const float calculated = 1.f + m_ProportionalControlFactor * float(errorUs);
            m_AdjustedPlaybackSpeed = Constrain(calculated, m_MinPlaybackSpeed, m_MaxPlaybackSpeed);
        }
        return m_AdjustedPlaybackSpeed;
    }

    void LivePlaybackSpeedControl::MaybeResetTargetLiveOffsetUs() {
        std::int64_t ideal = kTimeUnset;
        if (m_MediaConfigurationTargetLiveOffsetUs != kTimeUnset) {
            if (m_TargetLiveOffsetOverrideUs != kTimeUnset) {
                ideal = m_TargetLiveOffsetOverrideUs;
            } else {
                ideal = m_MediaConfigurationTargetLiveOffsetUs;
                if (m_MinTargetLiveOffsetUs != kTimeUnset && ideal < m_MinTargetLiveOffsetUs) ideal = m_MinTargetLiveOffsetUs;
                if (m_MaxTargetLiveOffsetUs != kTimeUnset && ideal > m_MaxTargetLiveOffsetUs) ideal = m_MaxTargetLiveOffsetUs;
            }
        }
        if (m_IdealTargetLiveOffsetUs == ideal) return;
        m_IdealTargetLiveOffsetUs = ideal;
        m_CurrentTargetLiveOffsetUs = ideal;
        m_SmoothedMinPossibleLiveOffsetUs = kTimeUnset;
        m_SmoothedMinPossibleLiveOffsetDeviationUs = kTimeUnset;
        m_LastPlaybackSpeedUpdateMs = kTimeUnset;
    }

    void LivePlaybackSpeedControl::UpdateSmoothedMinPossibleLiveOffsetUs(std::int64_t liveOffsetUs, std::int64_t bufferedDurationUs) {
        const std::int64_t minPossibleUs = liveOffsetUs - bufferedDurationUs;
        const float factor = m_Options.minPossibleLiveOffsetSmoothingFactor;
        if (m_SmoothedMinPossibleLiveOffsetUs == kTimeUnset) {
            m_SmoothedMinPossibleLiveOffsetUs = minPossibleUs;
            m_SmoothedMinPossibleLiveOffsetDeviationUs = 0;
        } else {
            // The maximum keeps the upper bound of what is safely possible, not the average.
            m_SmoothedMinPossibleLiveOffsetUs = std::max(minPossibleUs, Smooth(m_SmoothedMinPossibleLiveOffsetUs, minPossibleUs, factor));
            const std::int64_t deviationUs = std::llabs(minPossibleUs - m_SmoothedMinPossibleLiveOffsetUs);
            m_SmoothedMinPossibleLiveOffsetDeviationUs = Smooth(m_SmoothedMinPossibleLiveOffsetDeviationUs, deviationUs, factor);
        }
    }

    void LivePlaybackSpeedControl::AdjustTargetLiveOffsetUs(std::int64_t liveOffsetUs) {
        // A safe distance (3 standard deviations) from the minimum possible live offset.
        const std::int64_t safeOffsetUs = m_SmoothedMinPossibleLiveOffsetUs + 3 * m_SmoothedMinPossibleLiveOffsetDeviationUs;
        if (m_CurrentTargetLiveOffsetUs > safeOffsetUs) {
            // Room to decrease towards the ideal or safe offset, limited so the speed change is
            // the same as the largest one when slowing down towards the target.
            const std::int64_t minUpdateIntervalUs = m_Options.minUpdateIntervalMs * 1000;
            const std::int64_t decrementToOffsetCurrentSpeedUs = std::int64_t((m_AdjustedPlaybackSpeed - 1.f) * float(minUpdateIntervalUs));
            const std::int64_t decrementToIncreaseSpeedUs = std::int64_t((m_MaxPlaybackSpeed - 1.f) * float(minUpdateIntervalUs));
            const std::int64_t maxDecrementUs = decrementToOffsetCurrentSpeedUs + decrementToIncreaseSpeedUs;
            m_CurrentTargetLiveOffsetUs = std::max({safeOffsetUs, m_IdealTargetLiveOffsetUs, m_CurrentTargetLiveOffsetUs - maxDecrementUs});
        } else {
            // Stay just below the safe offset, but no further than slowing down gradually from the
            // current offset allows.
            const std::int64_t offsetWhenSlowingDownNowUs =
                liveOffsetUs - std::int64_t(std::max(0.f, m_AdjustedPlaybackSpeed - 1.f) / m_ProportionalControlFactor);
            m_CurrentTargetLiveOffsetUs = Constrain(offsetWhenSlowingDownNowUs, m_CurrentTargetLiveOffsetUs, safeOffsetUs);
            if (m_MaxTargetLiveOffsetUs != kTimeUnset && m_CurrentTargetLiveOffsetUs > m_MaxTargetLiveOffsetUs) m_CurrentTargetLiveOffsetUs = m_MaxTargetLiveOffsetUs;
        }
    }

}
