#pragma once

// media3's TimestampAdjuster and HLS TimestampAdjusterProvider: sample timestamps moved onto the
// presentation timeline, with MPEG-TS's 33-bit PTS unwrapped against the last one seen. In shared
// mode the first chunk allowed to initialize sets the offset that every track of the same
// discontinuity sequence then uses.

#include "kiln/media/stream/StreamUtil.h"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace kiln::media::stream {

    class TimestampAdjuster {
    public:
        // No offset: timestamps pass through unchanged.
        static constexpr std::int64_t MODE_NO_OFFSET = INT64_MAX;
        // The first sample timestamp comes from SharedInitializeOrWait.
        static constexpr std::int64_t MODE_SHARED = INT64_MAX - 1;

        explicit TimestampAdjuster(std::int64_t firstSampleTimestampUs) { Reset(firstSampleTimestampUs); }

        // Shared mode: a caller that may initialize records the timestamp its next sample should
        // have; one that may not waits until another did (timeoutMs 0: without limit). Returns
        // false when the wait timed out.
        bool SharedInitializeOrWait(bool canInitialize, std::int64_t nextSampleTimestampUs, std::int64_t timeoutMs);
        // Shared mode without waiting: whether the calling thread may adjust now (the adjuster is
        // initialized, or it may initialize and recorded its timestamp).
        bool TrySharedInitialize(bool canInitialize, std::int64_t nextSampleTimestampUs);

        std::int64_t GetFirstSampleTimestampUs() const;
        std::int64_t GetLastAdjustedTimestampUs() const;
        std::int64_t GetTimestampOffsetUs() const;
        void Reset(std::int64_t firstSampleTimestampUs);
        // A 90 kHz PTS, unwrapped to the 33-bit wrap closest to the last timestamp.
        std::int64_t AdjustTsTimestamp(std::int64_t pts90Khz);
        // A 90 kHz PTS, unwrapped to the first wrap not before the last timestamp.
        std::int64_t AdjustTsTimestampGreaterThanPreviousTimestamp(std::int64_t pts90Khz);
        std::int64_t AdjustSampleTimestamp(std::int64_t timeUs);
        bool IsInitialized() const;

        static std::int64_t PtsToUs(std::int64_t pts) { return ScaleLargeTimestamp(pts, kMicrosPerSecond, 90000); }
        static std::int64_t UsToWrappedPts(std::int64_t us) { return UsToNonWrappedPts(us) % kMaxPtsPlusOne; }
        static std::int64_t UsToNonWrappedPts(std::int64_t us) { return ScaleLargeTimestamp(us, 90000, kMicrosPerSecond); }

    private:
        static constexpr std::int64_t kMaxPtsPlusOne = 0x200000000LL;
        std::int64_t AdjustSampleTimestampLocked(std::int64_t timeUs);

        mutable std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::int64_t m_FirstSampleTimestampUs = 0;
        std::int64_t m_TimestampOffsetUs = kTimeUnset;
        std::int64_t m_LastUnadjustedTimestampUs = kTimeUnset;
        std::map<std::thread::id, std::int64_t> m_NextSampleTimestampUs;   // the ThreadLocal
    };

    // TimestampAdjusterProvider: one shared adjuster per discontinuity sequence.
    class TimestampAdjusterProvider {
    public:
        std::shared_ptr<TimestampAdjuster> GetAdjuster(int discontinuitySequence);
        void Reset();

    private:
        std::mutex m_Mutex;
        std::map<int, std::shared_ptr<TimestampAdjuster>> m_Adjusters;
    };

}
