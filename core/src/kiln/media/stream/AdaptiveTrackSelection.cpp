#include "kiln/media/stream/AdaptiveTrackSelection.h"

#include "kiln/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>

namespace kiln::media::stream {

    namespace {
        constexpr std::int64_t kRateUnsetInt = std::int64_t(INT32_MIN) + 1;   // C.RATE_UNSET_INT
        constexpr std::int64_t MIN_TIME_BETWEEN_BUFFER_REEVALUTATION_MS = 1000;

        std::int64_t SystemElapsedRealtimeMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // Util.getPlayoutDurationForMediaDuration.
        std::int64_t PlayoutDurationForMediaDuration(std::int64_t mediaDuration, float speed) {
            if (speed == 1.0f) return mediaDuration;
            return std::int64_t(std::llround(double(mediaDuration) / double(speed)));
        }
    }

    // --- BaseTrackSelection --------------------------------------------------------------------

    int BaseTrackSelection::DefaultFormatComparator(const Format& first, const Format& second) {
        return second.bitrate < first.bitrate ? -1 : second.bitrate > first.bitrate ? 1 : 0;
    }

    BaseTrackSelection::BaseTrackSelection(TrackGroup group, const std::vector<int>& tracks, const FormatComparator& comparator)
        : m_Length(int(tracks.size())), m_Group(std::move(group)) {
        if (tracks.empty()) throw std::invalid_argument("a track selection needs tracks");
        // Arrays.sort(formats, comparator) is stable; the group index travels with each format
        // (upstream finds it again by reference).
        std::vector<int> order = tracks;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return comparator(m_Group.GetFormat(a), m_Group.GetFormat(b)) < 0; });
        m_Tracks = order;
        for (int track : m_Tracks) m_Formats.push_back(m_Group.GetFormat(track));
        m_ExcludeUntilTimes.assign(m_Tracks.size(), 0);
    }

    int BaseTrackSelection::IndexOf(const Format& format) const {
        for (std::size_t i = 0; i < m_Formats.size(); i++)
            if (m_Formats[i] == format) return int(i);
        return kIndexUnset;
    }

    int BaseTrackSelection::IndexOfTrack(int indexInTrackGroup) const {
        for (std::size_t i = 0; i < m_Tracks.size(); i++)
            if (m_Tracks[i] == indexInTrackGroup) return int(i);
        return kIndexUnset;
    }

    bool BaseTrackSelection::ExcludeTrack(int index, std::int64_t exclusionDurationMs) {
        // SystemClock.elapsedRealtime(), as upstream (not the selection's clock).
        const std::int64_t nowMs = SystemElapsedRealtimeMs();
        bool canExclude = IsTrackExcluded(index, nowMs);
        for (int i = 0; i < Length() && !canExclude; i++) canExclude = i != index && !IsTrackExcluded(i, nowMs);
        if (!canExclude) return false;
        std::int64_t until;
        if (__builtin_add_overflow(nowMs, exclusionDurationMs, &until)) until = INT64_MAX;   // Util.addWithOverflowDefault
        m_ExcludeUntilTimes[std::size_t(index)] = std::max(m_ExcludeUntilTimes[std::size_t(index)], until);
        return true;
    }

    void BaseTrackSelection::ClearTrackExclusions() { std::fill(m_ExcludeUntilTimes.begin(), m_ExcludeUntilTimes.end(), 0); }

    // --- AdaptiveTrackSelection.Factory --------------------------------------------------------

    AdaptiveTrackSelection::Factory::Factory()
        : Factory(DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS, DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS,
                  DEFAULT_BANDWIDTH_FRACTION) {}

    AdaptiveTrackSelection::Factory::Factory(int minDurationForQualityIncreaseMs, int maxDurationForQualityDecreaseMs, int minDurationToRetainAfterDiscardMs,
                                             float bandwidthFraction)
        : Factory(minDurationForQualityIncreaseMs, maxDurationForQualityDecreaseMs, minDurationToRetainAfterDiscardMs, DEFAULT_MAX_WIDTH_TO_DISCARD,
                  DEFAULT_MAX_HEIGHT_TO_DISCARD, bandwidthFraction, DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE, nullptr) {}

    AdaptiveTrackSelection::Factory::Factory(int minDurationForQualityIncreaseMs, int maxDurationForQualityDecreaseMs, int minDurationToRetainAfterDiscardMs,
                                             int maxWidthToDiscard, int maxHeightToDiscard, float bandwidthFraction,
                                             float bufferedFractionToLiveEdgeForQualityIncrease, DefaultBandwidthMeter::Clock clock)
        : m_MinDurationForQualityIncreaseMs(minDurationForQualityIncreaseMs), m_MaxDurationForQualityDecreaseMs(maxDurationForQualityDecreaseMs),
          m_MinDurationToRetainAfterDiscardMs(minDurationToRetainAfterDiscardMs), m_MaxWidthToDiscard(maxWidthToDiscard), m_MaxHeightToDiscard(maxHeightToDiscard),
          m_BandwidthFraction(bandwidthFraction), m_BufferedFractionToLiveEdgeForQualityIncrease(bufferedFractionToLiveEdgeForQualityIncrease),
          m_Clock(std::move(clock)), m_TrackFormatComparator(&DefaultFormatComparator) {}

    AdaptiveTrackSelection::Factory& AdaptiveTrackSelection::Factory::SetTrackFormatComparator(FormatComparator comparator) {
        m_TrackFormatComparator = std::move(comparator);
        return *this;
    }

    std::vector<std::unique_ptr<BaseTrackSelection>> AdaptiveTrackSelection::Factory::CreateTrackSelections(
        const std::vector<std::optional<Definition>>& definitions, BandwidthMeter* bandwidthMeter) {
        const std::vector<std::vector<AdaptationCheckpoint>> adaptationCheckpoints = GetAdaptationCheckpoints(definitions);
        std::vector<std::unique_ptr<BaseTrackSelection>> selections(definitions.size());
        for (std::size_t i = 0; i < definitions.size(); i++) {
            const std::optional<Definition>& definition = definitions[i];
            if (!definition || definition->tracks.empty()) continue;
            if (definition->tracks.size() == 1) selections[i] = std::make_unique<FixedTrackSelection>(definition->group, definition->tracks[0]);
            else selections[i] = CreateAdaptiveTrackSelection(definition->group, definition->tracks, bandwidthMeter, adaptationCheckpoints[i]);
        }
        return selections;
    }

    std::unique_ptr<AdaptiveTrackSelection> AdaptiveTrackSelection::Factory::CreateAdaptiveTrackSelection(const TrackGroup& group, const std::vector<int>& tracks,
                                                                                                          BandwidthMeter* bandwidthMeter,
                                                                                                          std::vector<AdaptationCheckpoint> adaptationCheckpoints) {
        return std::make_unique<AdaptiveTrackSelection>(group, tracks, bandwidthMeter, m_MinDurationForQualityIncreaseMs, m_MaxDurationForQualityDecreaseMs,
                                                        m_MinDurationToRetainAfterDiscardMs, m_MaxWidthToDiscard, m_MaxHeightToDiscard, m_BandwidthFraction,
                                                        m_BufferedFractionToLiveEdgeForQualityIncrease, std::move(adaptationCheckpoints), m_Clock,
                                                        m_TrackFormatComparator);
    }

    // --- AdaptiveTrackSelection ----------------------------------------------------------------

    AdaptiveTrackSelection::AdaptiveTrackSelection(TrackGroup group, const std::vector<int>& tracks, BandwidthMeter* bandwidthMeter)
        : AdaptiveTrackSelection(std::move(group), tracks, bandwidthMeter, DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
                                 DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS, DEFAULT_MAX_WIDTH_TO_DISCARD, DEFAULT_MAX_HEIGHT_TO_DISCARD,
                                 DEFAULT_BANDWIDTH_FRACTION, DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE, {}, nullptr) {}

    AdaptiveTrackSelection::AdaptiveTrackSelection(TrackGroup group, const std::vector<int>& tracks, BandwidthMeter* bandwidthMeter,
                                                   std::int64_t minDurationForQualityIncreaseMs, std::int64_t maxDurationForQualityDecreaseMs,
                                                   std::int64_t minDurationToRetainAfterDiscardMs, int maxWidthToDiscard, int maxHeightToDiscard,
                                                   float bandwidthFraction, float bufferedFractionToLiveEdgeForQualityIncrease,
                                                   std::vector<AdaptationCheckpoint> adaptationCheckpoints, DefaultBandwidthMeter::Clock clock,
                                                   const FormatComparator& trackFormatComparator)
        : BaseTrackSelection(std::move(group), tracks, trackFormatComparator), m_BandwidthMeter(bandwidthMeter) {
        if (minDurationToRetainAfterDiscardMs < minDurationForQualityIncreaseMs) {
            log::Warn("AdaptiveTrackSelection", "Adjusting minDurationToRetainAfterDiscardMs to be at least minDurationForQualityIncreaseMs");
            minDurationToRetainAfterDiscardMs = minDurationForQualityIncreaseMs;
        }
        m_MinDurationForQualityIncreaseUs = minDurationForQualityIncreaseMs * 1000;
        m_MaxDurationForQualityDecreaseUs = maxDurationForQualityDecreaseMs * 1000;
        m_MinDurationToRetainAfterDiscardUs = minDurationToRetainAfterDiscardMs * 1000;
        m_MaxWidthToDiscard = maxWidthToDiscard;
        m_MaxHeightToDiscard = maxHeightToDiscard;
        m_BandwidthFraction = bandwidthFraction;
        m_BufferedFractionToLiveEdgeForQualityIncrease = bufferedFractionToLiveEdgeForQualityIncrease;
        m_AdaptationCheckpoints = std::move(adaptationCheckpoints);
        m_Clock = clock ? std::move(clock) : DefaultBandwidthMeter::Clock(&SystemElapsedRealtimeMs);
        m_LatestBitrateEstimate = kRateUnsetInt;
    }

    void AdaptiveTrackSelection::Enable() {
        m_LastBufferEvaluationMs = kTimeUnset;
        m_LastBufferEvaluationMediaChunk.reset();
    }

    void AdaptiveTrackSelection::Disable() { m_LastBufferEvaluationMediaChunk.reset(); }

    void AdaptiveTrackSelection::UpdateSelectedTrack(std::int64_t, std::int64_t bufferedDurationUs, std::int64_t availableDurationUs, const MediaChunkQueue& queue,
                                                     const std::vector<MediaChunkIterator*>& mediaChunkIterators) {
        const std::int64_t nowMs = m_Clock();
        const std::int64_t chunkDurationUs = GetNextChunkDurationUs(mediaChunkIterators, queue);
        const std::int64_t effectiveBitrate = GetAllocatedBandwidth(chunkDurationUs);
        // Stash the current selection, then make a new one.
        if (m_Reason == kSelectionReasonUnknown) {
            m_Reason = kSelectionReasonInitial;
            m_SelectedIndex = DetermineIdealSelectedIndex(nowMs, effectiveBitrate);
            return;
        }
        int previousSelectedIndex = m_SelectedIndex;
        int previousReason = m_Reason;
        const int formatIndexOfPreviousChunk = queue.empty() ? kIndexUnset : IndexOf(queue.back()->trackFormat);
        if (formatIndexOfPreviousChunk != kIndexUnset) {
            previousSelectedIndex = formatIndexOfPreviousChunk;
            previousReason = queue.back()->trackSelectionReason;
        }
        int newSelectedIndex = DetermineIdealSelectedIndex(nowMs, effectiveBitrate);
        if (newSelectedIndex != previousSelectedIndex && !IsTrackExcluded(previousSelectedIndex, nowMs)) {
            // Revert back to the previous selection if conditions are not suitable for switching.
            const std::int64_t minDurationForQualityIncreaseUs = MinDurationForQualityIncreaseUs(availableDurationUs, chunkDurationUs);
            if (newSelectedIndex < previousSelectedIndex && bufferedDurationUs < minDurationForQualityIncreaseUs) {
                // The selected track is a higher quality, but we have insufficient buffer to safely switch up.
                newSelectedIndex = previousSelectedIndex;
            } else if (newSelectedIndex > previousSelectedIndex && bufferedDurationUs >= m_MaxDurationForQualityDecreaseUs) {
                // The selected track is a lower quality, but we have sufficient buffer to defer switching down for now.
                newSelectedIndex = previousSelectedIndex;
            }
        }
        m_Reason = newSelectedIndex == previousSelectedIndex ? previousReason : kSelectionReasonAdaptive;
        m_SelectedIndex = newSelectedIndex;
    }

    int AdaptiveTrackSelection::EvaluateQueueSize(std::int64_t playbackPositionUs, const MediaChunkQueue& queue) {
        const std::int64_t nowMs = m_Clock();
        if (!ShouldEvaluateQueueSize(nowMs, queue)) return int(queue.size());
        m_LastBufferEvaluationMs = nowMs;
        m_LastBufferEvaluationMediaChunk = queue.empty() ? nullptr : queue.back();
        if (queue.empty()) return 0;
        const int queueSize = int(queue.size());
        const MediaChunk& lastChunk = *queue.back();
        const std::int64_t playoutBufferedDurationBeforeLastChunkUs = PlayoutDurationForMediaDuration(lastChunk.startTimeUs - playbackPositionUs, m_PlaybackSpeed);
        const std::int64_t minDurationToRetainAfterDiscardUs = GetMinDurationToRetainAfterDiscardUs();
        if (playoutBufferedDurationBeforeLastChunkUs < minDurationToRetainAfterDiscardUs) return queueSize;
        const int idealSelectedIndex = DetermineIdealSelectedIndex(nowMs, GetAllocatedBandwidth(GetLastChunkDurationUs(queue)));
        const Format& idealFormat = GetFormat(idealSelectedIndex);
        // Discard from the first SD chunk beyond minDurationToRetainAfterDiscardUs whose resolution
        // and bitrate are both lower than the ideal track.
        for (int i = 0; i < queueSize; i++) {
            const MediaChunk& chunk = *queue[std::size_t(i)];
            const Format& format = chunk.trackFormat;
            const std::int64_t mediaDurationBeforeThisChunkUs = chunk.startTimeUs - playbackPositionUs;
            const std::int64_t playoutDurationBeforeThisChunkUs = PlayoutDurationForMediaDuration(mediaDurationBeforeThisChunkUs, m_PlaybackSpeed);
            if (playoutDurationBeforeThisChunkUs >= minDurationToRetainAfterDiscardUs && format.bitrate < idealFormat.bitrate && format.height != -1 &&
                format.height <= m_MaxHeightToDiscard && format.width != -1 && format.width <= m_MaxWidthToDiscard && format.height < idealFormat.height)
                return i;
        }
        return queueSize;
    }

    bool AdaptiveTrackSelection::ShouldEvaluateQueueSize(std::int64_t nowMs, const MediaChunkQueue& queue) const {
        return m_LastBufferEvaluationMs == kTimeUnset || nowMs - m_LastBufferEvaluationMs >= MIN_TIME_BETWEEN_BUFFER_REEVALUTATION_MS ||
               (!queue.empty() && queue.back() != m_LastBufferEvaluationMediaChunk);
    }

    int AdaptiveTrackSelection::DetermineIdealSelectedIndex(std::int64_t nowMs, std::int64_t effectiveBitrate) const {
        int lowestBitrateAllowedIndex = 0;
        for (int i = 0; i < m_Length; i++) {
            if (nowMs == INT64_MIN || !IsTrackExcluded(i, nowMs)) {
                const Format& format = GetFormat(i);
                if (CanSelectFormat(format, format.bitrate, effectiveBitrate)) return i;
                lowestBitrateAllowedIndex = i;
            }
        }
        return lowestBitrateAllowedIndex;
    }

    std::int64_t AdaptiveTrackSelection::MinDurationForQualityIncreaseUs(std::int64_t availableDurationUs, std::int64_t chunkDurationUs) const {
        if (availableDurationUs == kTimeUnset) return m_MinDurationForQualityIncreaseUs;   // Non-live streams.
        if (chunkDurationUs != kTimeUnset) availableDurationUs -= chunkDurationUs;   // Make sure to leave room for the next chunk.
        const std::int64_t adjusted = std::int64_t(float(availableDurationUs) * m_BufferedFractionToLiveEdgeForQualityIncrease);
        return std::min(adjusted, m_MinDurationForQualityIncreaseUs);
    }

    std::int64_t AdaptiveTrackSelection::GetNextChunkDurationUs(const std::vector<MediaChunkIterator*>& mediaChunkIterators, const MediaChunkQueue& queue) const {
        // Try to get the next chunk duration for the currently selected format.
        if (m_SelectedIndex < int(mediaChunkIterators.size()) && mediaChunkIterators[std::size_t(m_SelectedIndex)]->Next()) {
            const MediaChunkIterator* iterator = mediaChunkIterators[std::size_t(m_SelectedIndex)];
            return iterator->GetChunkEndTimeUs() - iterator->GetChunkStartTimeUs();
        }
        // Try to get the next chunk duration for another format, on the assumption that chunks
        // belonging to different formats are likely to have identical or similar durations.
        for (MediaChunkIterator* iterator : mediaChunkIterators)
            if (iterator->Next()) return iterator->GetChunkEndTimeUs() - iterator->GetChunkStartTimeUs();
        // Try to get chunk duration for last chunk in the queue, on the assumption that the next
        // chunk is likely to have a similar duration.
        return GetLastChunkDurationUs(queue);
    }

    std::int64_t AdaptiveTrackSelection::GetLastChunkDurationUs(const MediaChunkQueue& queue) {
        if (queue.empty()) return kTimeUnset;
        const MediaChunk& lastChunk = *queue.back();
        return lastChunk.startTimeUs != kTimeUnset && lastChunk.endTimeUs != kTimeUnset ? lastChunk.endTimeUs - lastChunk.startTimeUs : kTimeUnset;
    }

    std::int64_t AdaptiveTrackSelection::GetAllocatedBandwidth(std::int64_t chunkDurationUs) {
        const std::int64_t totalBandwidth = GetTotalAllocatableBandwidth(chunkDurationUs);
        if (m_AdaptationCheckpoints.empty()) return totalBandwidth;
        std::size_t nextIndex = 1;
        while (nextIndex < m_AdaptationCheckpoints.size() - 1 && m_AdaptationCheckpoints[nextIndex].totalBandwidth < totalBandwidth) nextIndex++;
        const AdaptationCheckpoint& previous = m_AdaptationCheckpoints[nextIndex - 1];
        const AdaptationCheckpoint& next = m_AdaptationCheckpoints[nextIndex];
        const float fractionBetweenCheckpoints = float(totalBandwidth - previous.totalBandwidth) / float(next.totalBandwidth - previous.totalBandwidth);
        return previous.allocatedBandwidth + std::int64_t(fractionBetweenCheckpoints * float(next.allocatedBandwidth - previous.allocatedBandwidth));
    }

    std::int64_t AdaptiveTrackSelection::GetTotalAllocatableBandwidth(std::int64_t chunkDurationUs) {
        m_LatestBitrateEstimate = m_BandwidthMeter->GetBitrateEstimate();
        const std::int64_t cautiousBandwidthEstimate = std::int64_t(float(m_LatestBitrateEstimate) * m_BandwidthFraction);
        const std::int64_t timeToFirstByteEstimateUs = m_BandwidthMeter->GetTimeToFirstByteEstimateUs();
        if (timeToFirstByteEstimateUs == kTimeUnset || chunkDurationUs == kTimeUnset) return std::int64_t(float(cautiousBandwidthEstimate) / m_PlaybackSpeed);
        const float availableTimeToLoadUs = std::max(float(chunkDurationUs) / m_PlaybackSpeed - float(timeToFirstByteEstimateUs), 0.0f);
        return std::int64_t(float(cautiousBandwidthEstimate) * availableTimeToLoadUs / float(chunkDurationUs));
    }

    namespace {
        using Checkpoint = AdaptiveTrackSelection::AdaptationCheckpoint;

        void AddCheckpoint(std::vector<std::optional<std::vector<Checkpoint>>>& builders, const std::vector<std::int64_t>& checkpointBitrates) {
            const std::int64_t totalBitrate = std::accumulate(checkpointBitrates.begin(), checkpointBitrates.end(), std::int64_t(0));
            for (std::size_t i = 0; i < builders.size(); i++)
                if (builders[i]) builders[i]->push_back({ totalBitrate, checkpointBitrates[i] });
        }
    }

    std::vector<std::vector<AdaptiveTrackSelection::AdaptationCheckpoint>> AdaptiveTrackSelection::GetAdaptationCheckpoints(
        const std::vector<std::optional<Definition>>& definitions) {
        std::vector<std::optional<std::vector<Checkpoint>>> builders;
        for (const std::optional<Definition>& definition : definitions) {
            if (definition && definition->tracks.size() > 1) builders.emplace_back(std::vector<Checkpoint>{ { 0, 0 } });
            else builders.emplace_back();
        }
        // Add a checkpoint at the lowest bitrates, then at each switch point in order.
        std::vector<std::vector<std::int64_t>> trackBitrates(definitions.size());
        for (std::size_t i = 0; i < definitions.size(); i++) {
            if (!definitions[i]) continue;
            for (int track : definitions[i]->tracks) {
                const int bitrate = definitions[i]->group.GetFormat(track).bitrate;
                trackBitrates[i].push_back(bitrate == -1 ? 0 : bitrate);
            }
            std::sort(trackBitrates[i].begin(), trackBitrates[i].end());
        }
        std::vector<int> currentTrackIndices(trackBitrates.size(), 0);
        std::vector<std::int64_t> currentTrackBitrates(trackBitrates.size(), 0);
        for (std::size_t i = 0; i < trackBitrates.size(); i++) currentTrackBitrates[i] = trackBitrates[i].empty() ? 0 : trackBitrates[i][0];
        AddCheckpoint(builders, currentTrackBitrates);
        // getSwitchOrder: switch points by their position on each group's log-bitrate scale
        // (a TreeMultimap: ties keep insertion order).
        std::multimap<double, int> switchPoints;
        for (std::size_t i = 0; i < trackBitrates.size(); i++) {
            if (trackBitrates[i].size() <= 1) continue;
            std::vector<double> logBitrates;
            for (std::int64_t b : trackBitrates[i]) logBitrates.push_back(b == -1 ? 0.0 : std::log(double(b)));
            const double totalBitrateDiff = logBitrates.back() - logBitrates.front();
            for (std::size_t j = 0; j + 1 < logBitrates.size(); j++) {
                const double switchBitrate = 0.5 * (logBitrates[j] + logBitrates[j + 1]);
                const double switchPoint = totalBitrateDiff == 0.0 ? 1.0 : (switchBitrate - logBitrates[0]) / totalBitrateDiff;
                switchPoints.emplace(switchPoint, int(i));
            }
        }
        for (const auto& [point, switchIndex] : switchPoints) {
            const int newTrackIndex = ++currentTrackIndices[std::size_t(switchIndex)];
            currentTrackBitrates[std::size_t(switchIndex)] = trackBitrates[std::size_t(switchIndex)][std::size_t(newTrackIndex)];
            AddCheckpoint(builders, currentTrackBitrates);
        }
        // Add final checkpoint to extrapolate additional bandwidth for adaptive selections.
        for (std::size_t i = 0; i < definitions.size(); i++)
            if (builders[i]) currentTrackBitrates[i] *= 2;
        AddCheckpoint(builders, currentTrackBitrates);
        std::vector<std::vector<Checkpoint>> output;
        for (auto& builder : builders) output.push_back(builder ? std::move(*builder) : std::vector<Checkpoint>{});
        return output;
    }

}
