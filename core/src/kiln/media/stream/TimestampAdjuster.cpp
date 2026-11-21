#include "kiln/media/stream/TimestampAdjuster.h"

#include <chrono>
#include <cstdlib>

namespace kiln::media::stream {

    bool TimestampAdjuster::SharedInitializeOrWait(bool canInitialize, std::int64_t nextSampleTimestampUs, std::int64_t timeoutMs) {
        std::unique_lock lock(m_Mutex);
        if (m_TimestampOffsetUs != kTimeUnset) return true;
        if (canInitialize) {
            m_NextSampleTimestampUs[std::this_thread::get_id()] = nextSampleTimestampUs;
            return true;
        }
        auto initialized = [this] { return m_TimestampOffsetUs != kTimeUnset; };
        if (timeoutMs == 0) {
            m_Cv.wait(lock, initialized);
            return true;
        }
        return m_Cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), initialized);
    }

    bool TimestampAdjuster::TrySharedInitialize(bool canInitialize, std::int64_t nextSampleTimestampUs) {
        std::lock_guard lock(m_Mutex);
        if (m_TimestampOffsetUs != kTimeUnset) return true;
        if (!canInitialize) return false;
        m_NextSampleTimestampUs[std::this_thread::get_id()] = nextSampleTimestampUs;
        return true;
    }

    std::int64_t TimestampAdjuster::GetFirstSampleTimestampUs() const {
        std::lock_guard lock(m_Mutex);
        return m_FirstSampleTimestampUs == MODE_NO_OFFSET || m_FirstSampleTimestampUs == MODE_SHARED ? kTimeUnset : m_FirstSampleTimestampUs;
    }

    std::int64_t TimestampAdjuster::GetLastAdjustedTimestampUs() const {
        std::lock_guard lock(m_Mutex);
        if (m_LastUnadjustedTimestampUs != kTimeUnset) return m_LastUnadjustedTimestampUs + m_TimestampOffsetUs;
        return m_FirstSampleTimestampUs == MODE_NO_OFFSET || m_FirstSampleTimestampUs == MODE_SHARED ? kTimeUnset : m_FirstSampleTimestampUs;
    }

    std::int64_t TimestampAdjuster::GetTimestampOffsetUs() const {
        std::lock_guard lock(m_Mutex);
        return m_TimestampOffsetUs;
    }

    void TimestampAdjuster::Reset(std::int64_t firstSampleTimestampUs) {
        std::lock_guard lock(m_Mutex);
        m_FirstSampleTimestampUs = firstSampleTimestampUs;
        m_TimestampOffsetUs = firstSampleTimestampUs == MODE_NO_OFFSET ? 0 : kTimeUnset;
        m_LastUnadjustedTimestampUs = kTimeUnset;
        m_NextSampleTimestampUs.clear();
    }

    std::int64_t TimestampAdjuster::AdjustTsTimestamp(std::int64_t pts90Khz) {
        if (pts90Khz == kTimeUnset) return kTimeUnset;
        std::lock_guard lock(m_Mutex);
        if (m_LastUnadjustedTimestampUs != kTimeUnset) {
            const std::int64_t lastPts = UsToNonWrappedPts(m_LastUnadjustedTimestampUs);
            const std::int64_t closestWrapCount = (lastPts + (kMaxPtsPlusOne / 2)) / kMaxPtsPlusOne;
            const std::int64_t ptsWrapBelow = pts90Khz + (kMaxPtsPlusOne * (closestWrapCount - 1));
            const std::int64_t ptsWrapAbove = pts90Khz + (kMaxPtsPlusOne * closestWrapCount);
            pts90Khz = std::llabs(ptsWrapBelow - lastPts) < std::llabs(ptsWrapAbove - lastPts) ? ptsWrapBelow : ptsWrapAbove;
        }
        return AdjustSampleTimestampLocked(PtsToUs(pts90Khz));
    }

    std::int64_t TimestampAdjuster::AdjustTsTimestampGreaterThanPreviousTimestamp(std::int64_t pts90Khz) {
        if (pts90Khz == kTimeUnset) return kTimeUnset;
        std::lock_guard lock(m_Mutex);
        if (m_LastUnadjustedTimestampUs != kTimeUnset) {
            const std::int64_t lastPts = UsToNonWrappedPts(m_LastUnadjustedTimestampUs);
            const std::int64_t wrapCount = lastPts / kMaxPtsPlusOne;
            const std::int64_t ptsSameWrap = pts90Khz + (kMaxPtsPlusOne * wrapCount);
            const std::int64_t ptsNextWrap = pts90Khz + (kMaxPtsPlusOne * (wrapCount + 1));
            pts90Khz = ptsSameWrap >= lastPts ? ptsSameWrap : ptsNextWrap;
        }
        return AdjustSampleTimestampLocked(PtsToUs(pts90Khz));
    }

    std::int64_t TimestampAdjuster::AdjustSampleTimestamp(std::int64_t timeUs) {
        if (timeUs == kTimeUnset) return kTimeUnset;
        std::lock_guard lock(m_Mutex);
        return AdjustSampleTimestampLocked(timeUs);
    }

    std::int64_t TimestampAdjuster::AdjustSampleTimestampLocked(std::int64_t timeUs) {
        if (m_TimestampOffsetUs == kTimeUnset) {
            std::int64_t desired = m_FirstSampleTimestampUs;
            if (m_FirstSampleTimestampUs == MODE_SHARED) {
                auto it = m_NextSampleTimestampUs.find(std::this_thread::get_id());
                desired = it != m_NextSampleTimestampUs.end() ? it->second : 0;
            }
            m_TimestampOffsetUs = desired - timeUs;
            m_Cv.notify_all();
        }
        m_LastUnadjustedTimestampUs = timeUs;
        return timeUs + m_TimestampOffsetUs;
    }

    bool TimestampAdjuster::IsInitialized() const {
        std::lock_guard lock(m_Mutex);
        return m_TimestampOffsetUs != kTimeUnset;
    }

    std::shared_ptr<TimestampAdjuster> TimestampAdjusterProvider::GetAdjuster(int discontinuitySequence) {
        std::lock_guard lock(m_Mutex);
        auto& adjuster = m_Adjusters[discontinuitySequence];
        if (!adjuster) adjuster = std::make_shared<TimestampAdjuster>(TimestampAdjuster::MODE_SHARED);
        return adjuster;
    }

    void TimestampAdjusterProvider::Reset() {
        std::lock_guard lock(m_Mutex);
        m_Adjusters.clear();
    }

}
