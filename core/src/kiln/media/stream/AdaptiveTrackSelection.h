#pragma once

// media3's ExoTrackSelection for adaptive streams: BaseTrackSelection (a group's tracks by
// decreasing bitrate, with exclusions), FixedTrackSelection, and AdaptiveTrackSelection, which
// picks the variant the bandwidth estimate affords and holds it while the buffer says so.

#include "kiln/Function.h"
#include "kiln/media/TrackSelection.h"
#include "kiln/media/stream/BandwidthMeter.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::media::stream {

    // C.SELECTION_REASON_*.
    inline constexpr int kSelectionReasonUnknown = 0, kSelectionReasonInitial = 1, kSelectionReasonManual = 2, kSelectionReasonAdaptive = 3,
                         kSelectionReasonTrickPlay = 4;

    // TrackGroup: tracks of one content in different encodings.
    struct TrackGroup {
        std::vector<Format> formats;
        int Length() const { return int(formats.size()); }
        const Format& GetFormat(int index) const { return formats.at(std::size_t(index)); }
    };

    // MediaChunk: a loaded or loading segment (its identity is the object).
    struct MediaChunk {
        Format trackFormat;
        int trackSelectionReason = kSelectionReasonUnknown;
        std::int64_t startTimeUs = kTimeUnset;
        std::int64_t endTimeUs = kTimeUnset;
    };
    using MediaChunkQueue = std::vector<std::shared_ptr<const MediaChunk>>;

    // MediaChunkIterator: the segments a track would load next.
    class MediaChunkIterator {
    public:
        virtual ~MediaChunkIterator() = default;
        // Moves to the next chunk; false at the end.
        virtual bool Next() = 0;
        virtual std::int64_t GetChunkStartTimeUs() const = 0;
        virtual std::int64_t GetChunkEndTimeUs() const = 0;
    };

    // BaseTrackSelection, as ExoTrackSelection.
    class BaseTrackSelection {
    public:
        // Integer.compare(second.bitrate, first.bitrate): highest bitrate first.
        using FormatComparator = kiln::Function<int(const Format&, const Format&)>;
        static int DefaultFormatComparator(const Format& first, const Format& second);

        virtual ~BaseTrackSelection() = default;
        const TrackGroup& GetTrackGroup() const { return m_Group; }
        int Length() const { return int(m_Tracks.size()); }
        // Index 0 is the comparator's first (by default the highest bitrate).
        const Format& GetFormat(int index) const { return m_Formats.at(std::size_t(index)); }
        int GetIndexInTrackGroup(int index) const { return m_Tracks.at(std::size_t(index)); }
        // The selection index of a format (C.INDEX_UNSET when none).
        int IndexOf(const Format& format) const;
        int IndexOfTrack(int indexInTrackGroup) const;
        const Format& GetSelectedFormat() const { return m_Formats.at(std::size_t(GetSelectedIndex())); }
        int GetSelectedIndexInTrackGroup() const { return m_Tracks.at(std::size_t(GetSelectedIndex())); }

        virtual void Enable() {}
        virtual void Disable() {}
        virtual void OnPlaybackSpeed(float) {}
        virtual void UpdateSelectedTrack(std::int64_t playbackPositionUs, std::int64_t bufferedDurationUs, std::int64_t availableDurationUs,
                                         const MediaChunkQueue& queue, const std::vector<MediaChunkIterator*>& mediaChunkIterators) = 0;
        virtual int GetSelectedIndex() const = 0;
        virtual int GetSelectionReason() const = 0;
        // How many queued chunks to keep (the rest may be discarded and loaded again in a better format).
        virtual int EvaluateQueueSize(std::int64_t, const MediaChunkQueue& queue) { return int(queue.size()); }
        virtual std::int64_t GetLatestBitrateEstimate() const { return kTimeUnset; }
        // Excludes a track (after a load error) for a while, unless all others are excluded.
        bool ExcludeTrack(int index, std::int64_t exclusionDurationMs);
        bool IsTrackExcluded(int index, std::int64_t nowMs) const { return m_ExcludeUntilTimes.at(std::size_t(index)) > nowMs; }
        void ClearTrackExclusions();

    protected:
        BaseTrackSelection(TrackGroup group, const std::vector<int>& tracks, const FormatComparator& comparator);
        int m_Length;

    private:
        TrackGroup m_Group;
        std::vector<int> m_Tracks;
        std::vector<Format> m_Formats;
        std::vector<std::int64_t> m_ExcludeUntilTimes;
    };

    // FixedTrackSelection: one track.
    class FixedTrackSelection final : public BaseTrackSelection {
    public:
        FixedTrackSelection(TrackGroup group, int track) : BaseTrackSelection(std::move(group), { track }, &DefaultFormatComparator) {}
        void UpdateSelectedTrack(std::int64_t, std::int64_t, std::int64_t, const MediaChunkQueue&, const std::vector<MediaChunkIterator*>&) override {}
        int GetSelectedIndex() const override { return 0; }
        int GetSelectionReason() const override { return kSelectionReasonUnknown; }
    };

    class AdaptiveTrackSelection : public BaseTrackSelection {
    public:
        static constexpr int DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS = 10'000;
        static constexpr int DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS = 25'000;
        static constexpr int DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS = 25'000;
        static constexpr int DEFAULT_MAX_WIDTH_TO_DISCARD = 1279;
        static constexpr int DEFAULT_MAX_HEIGHT_TO_DISCARD = 719;
        static constexpr float DEFAULT_BANDWIDTH_FRACTION = 0.7f;
        static constexpr float DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE = 0.75f;

        // A point of the piecewise-linear map from total bandwidth to this selection's share.
        struct AdaptationCheckpoint {
            std::int64_t totalBandwidth = 0;
            std::int64_t allocatedBandwidth = 0;
            bool operator==(const AdaptationCheckpoint&) const = default;
        };

        // ExoTrackSelection.Definition: a group and the tracks to select among.
        struct Definition {
            TrackGroup group;
            std::vector<int> tracks;
        };

        class Factory {
        public:
            Factory();
            Factory(int minDurationForQualityIncreaseMs, int maxDurationForQualityDecreaseMs, int minDurationToRetainAfterDiscardMs, float bandwidthFraction);
            Factory(int minDurationForQualityIncreaseMs, int maxDurationForQualityDecreaseMs, int minDurationToRetainAfterDiscardMs, int maxWidthToDiscard,
                    int maxHeightToDiscard, float bandwidthFraction, float bufferedFractionToLiveEdgeForQualityIncrease, DefaultBandwidthMeter::Clock clock);
            virtual ~Factory() = default;
            Factory& SetTrackFormatComparator(FormatComparator comparator);
            // A selection per definition (none for an empty one): fixed for one track, adaptive for
            // more, the adaptive ones sharing the bandwidth by checkpoints.
            std::vector<std::unique_ptr<BaseTrackSelection>> CreateTrackSelections(const std::vector<std::optional<Definition>>& definitions,
                                                                                   BandwidthMeter* bandwidthMeter);
            virtual std::unique_ptr<AdaptiveTrackSelection> CreateAdaptiveTrackSelection(const TrackGroup& group, const std::vector<int>& tracks,
                                                                                         BandwidthMeter* bandwidthMeter,
                                                                                         std::vector<AdaptationCheckpoint> adaptationCheckpoints);

        private:
            int m_MinDurationForQualityIncreaseMs, m_MaxDurationForQualityDecreaseMs, m_MinDurationToRetainAfterDiscardMs, m_MaxWidthToDiscard,
                m_MaxHeightToDiscard;
            float m_BandwidthFraction, m_BufferedFractionToLiveEdgeForQualityIncrease;
            DefaultBandwidthMeter::Clock m_Clock;
            FormatComparator m_TrackFormatComparator;
        };

        AdaptiveTrackSelection(TrackGroup group, const std::vector<int>& tracks, BandwidthMeter* bandwidthMeter);
        AdaptiveTrackSelection(TrackGroup group, const std::vector<int>& tracks, BandwidthMeter* bandwidthMeter, std::int64_t minDurationForQualityIncreaseMs,
                               std::int64_t maxDurationForQualityDecreaseMs, std::int64_t minDurationToRetainAfterDiscardMs, int maxWidthToDiscard,
                               int maxHeightToDiscard, float bandwidthFraction, float bufferedFractionToLiveEdgeForQualityIncrease,
                               std::vector<AdaptationCheckpoint> adaptationCheckpoints, DefaultBandwidthMeter::Clock clock,
                               const FormatComparator& trackFormatComparator = &DefaultFormatComparator);

        void Enable() override;
        void Disable() override;
        void OnPlaybackSpeed(float playbackSpeed) override { m_PlaybackSpeed = playbackSpeed; }
        void UpdateSelectedTrack(std::int64_t playbackPositionUs, std::int64_t bufferedDurationUs, std::int64_t availableDurationUs,
                                 const MediaChunkQueue& queue, const std::vector<MediaChunkIterator*>& mediaChunkIterators) override;
        int GetSelectedIndex() const override { return m_SelectedIndex; }
        int GetSelectionReason() const override { return m_Reason; }
        int EvaluateQueueSize(std::int64_t playbackPositionUs, const MediaChunkQueue& queue) override;
        std::int64_t GetLatestBitrateEstimate() const override { return m_LatestBitrateEstimate; }

        // The checkpoints of each definition (empty for one of at most one track).
        static std::vector<std::vector<AdaptationCheckpoint>> GetAdaptationCheckpoints(const std::vector<std::optional<Definition>>& definitions);

    protected:
        virtual bool CanSelectFormat(const Format&, int trackBitrate, std::int64_t effectiveBitrate) const { return trackBitrate <= effectiveBitrate; }
        virtual bool ShouldEvaluateQueueSize(std::int64_t nowMs, const MediaChunkQueue& queue) const;
        virtual std::int64_t GetMinDurationToRetainAfterDiscardUs() const { return m_MinDurationToRetainAfterDiscardUs; }

    private:
        int DetermineIdealSelectedIndex(std::int64_t nowMs, std::int64_t effectiveBitrate) const;
        std::int64_t MinDurationForQualityIncreaseUs(std::int64_t availableDurationUs, std::int64_t chunkDurationUs) const;
        std::int64_t GetNextChunkDurationUs(const std::vector<MediaChunkIterator*>& mediaChunkIterators, const MediaChunkQueue& queue) const;
        static std::int64_t GetLastChunkDurationUs(const MediaChunkQueue& queue);
        std::int64_t GetAllocatedBandwidth(std::int64_t chunkDurationUs);
        std::int64_t GetTotalAllocatableBandwidth(std::int64_t chunkDurationUs);

        BandwidthMeter* m_BandwidthMeter;
        std::int64_t m_MinDurationForQualityIncreaseUs, m_MaxDurationForQualityDecreaseUs, m_MinDurationToRetainAfterDiscardUs;
        int m_MaxWidthToDiscard, m_MaxHeightToDiscard;
        float m_BandwidthFraction, m_BufferedFractionToLiveEdgeForQualityIncrease;
        std::vector<AdaptationCheckpoint> m_AdaptationCheckpoints;
        DefaultBandwidthMeter::Clock m_Clock;
        float m_PlaybackSpeed = 1.0f;
        int m_SelectedIndex = 0;
        int m_Reason = kSelectionReasonUnknown;
        std::int64_t m_LastBufferEvaluationMs = kTimeUnset;
        std::shared_ptr<const MediaChunk> m_LastBufferEvaluationMediaChunk;
        std::int64_t m_LatestBitrateEstimate;
    };

}
