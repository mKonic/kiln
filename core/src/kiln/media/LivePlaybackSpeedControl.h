// kiln — live playback speed control: media3's DefaultLivePlaybackSpeedControl.
#pragma once

#include "kiln/media/MediaItem.h"

#include <cstdint>
#include <functional>

namespace kiln::media {

    // Keeps a live stream at its target offset from the live edge by playing slightly faster or
    // slower (DefaultLivePlaybackSpeedControl). The target starts at the live configuration's (or
    // an override's, after a seek), grows on each rebuffer and moves back towards the ideal as far
    // as the buffer safely allows.
    class LivePlaybackSpeedControl {
    public:
        // DefaultLivePlaybackSpeedControl.Builder's settings, with its defaults.
        struct Options {
            float fallbackMinPlaybackSpeed = 0.97f;   // (0, 1]: when the media gives no minimum
            float fallbackMaxPlaybackSpeed = 1.03f;   // >= 1: when the media gives no maximum
            std::int64_t minUpdateIntervalMs = 1000;  // between speed changes
            // The speed change per second of offset error: speed = 1 + factor * error.
            float proportionalControlFactor = 0.1f;
            std::int64_t maxLiveOffsetErrorMsForUnitSpeed = 20;
            std::int64_t targetLiveOffsetIncrementOnRebufferMs = 500;
            float minPossibleLiveOffsetSmoothingFactor = 0.999f;   // [0, 1)
            // SystemClock.elapsedRealtime (null: a steady clock).
            std::function<std::int64_t()> elapsedRealtimeMs;
        };

        LivePlaybackSpeedControl() : LivePlaybackSpeedControl(Options()) {}
        explicit LivePlaybackSpeedControl(Options options);

        // The live stream's configuration (Timeline.Window.liveConfiguration).
        void SetLiveConfiguration(const MediaItem::LiveConfiguration& configuration);
        // The live offset a seek chose, kept instead of the media's target (kTimeUnset: none).
        void SetTargetLiveOffsetOverrideUs(std::int64_t liveOffsetUs);
        // Playback ran out of buffered media.
        void NotifyRebuffer();
        // The speed to play at now, from the current live offset and buffered duration.
        float GetAdjustedPlaybackSpeed(std::int64_t liveOffsetUs, std::int64_t bufferedDurationUs);
        // The live offset aimed at (kTimeUnset: no speed control).
        std::int64_t GetTargetLiveOffsetUs() const { return m_CurrentTargetLiveOffsetUs; }

    private:
        void MaybeResetTargetLiveOffsetUs();
        void UpdateSmoothedMinPossibleLiveOffsetUs(std::int64_t liveOffsetUs, std::int64_t bufferedDurationUs);
        void AdjustTargetLiveOffsetUs(std::int64_t liveOffsetUs);
        std::int64_t NowMs() const;

        Options m_Options;
        float m_ProportionalControlFactor;   // per microsecond
        std::int64_t m_MaxLiveOffsetErrorUsForUnitSpeed, m_TargetLiveOffsetRebufferDeltaUs;

        std::int64_t m_MediaConfigurationTargetLiveOffsetUs = kTimeUnset;
        std::int64_t m_TargetLiveOffsetOverrideUs = kTimeUnset;
        std::int64_t m_IdealTargetLiveOffsetUs = kTimeUnset;
        std::int64_t m_MinTargetLiveOffsetUs = kTimeUnset;
        std::int64_t m_MaxTargetLiveOffsetUs = kTimeUnset;
        std::int64_t m_CurrentTargetLiveOffsetUs = kTimeUnset;
        float m_MinPlaybackSpeed, m_MaxPlaybackSpeed;
        float m_AdjustedPlaybackSpeed = 1.f;
        std::int64_t m_LastPlaybackSpeedUpdateMs = kTimeUnset;
        std::int64_t m_SmoothedMinPossibleLiveOffsetUs = kTimeUnset;
        std::int64_t m_SmoothedMinPossibleLiveOffsetDeviationUs = kTimeUnset;
    };

}
