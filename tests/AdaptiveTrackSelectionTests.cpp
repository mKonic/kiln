// Ported from media3-exoplayer AdaptiveTrackSelectionTest.java. Expected values are upstream's.
// The Mockito bandwidth meter is a fake that returns its queued estimates in turn.
#include "Check.h"

#include "kiln/media/stream/AdaptiveTrackSelection.h"
#include "kiln/media/stream/StreamUtil.h"

#include <algorithm>
#include <deque>

using namespace kiln::media;
using namespace kiln::media::stream;
using ATS = AdaptiveTrackSelection;
using Checkpoint = ATS::AdaptationCheckpoint;

namespace {
    constexpr std::int64_t TEST_CHUNK_DURATION_US = 2'000'000;

    // when(meter.getBitrateEstimate()).thenReturn(a, b, ...): each call takes the next, the last repeats.
    struct FakeBandwidthMeter : BandwidthMeter {
        std::deque<std::int64_t> estimates{ 0 };
        std::int64_t timeToFirstByteUs = kTimeUnset;
        void ThenReturn(std::initializer_list<std::int64_t> values) { estimates.assign(values); }
        std::int64_t GetBitrateEstimate() override {
            const std::int64_t v = estimates.front();
            if (estimates.size() > 1) estimates.pop_front();
            return v;
        }
        std::int64_t GetTimeToFirstByteEstimateUs() override { return timeToFirstByteUs; }
    };

    struct FakeClock {
        std::int64_t now = 0;
        DefaultBandwidthMeter::Clock AsClock() {
            return [this] { return now; };
        }
    };

    struct FixedChunkIterator : MediaChunkIterator {
        std::int64_t durationUs;
        bool advanced = false;
        explicit FixedChunkIterator(std::int64_t d) : durationUs(d) {}
        // BaseMediaChunkIterator(fromIndex 0, toIndex 0): one chunk.
        bool Next() override {
            if (advanced) return false;
            advanced = true;
            return true;
        }
        std::int64_t GetChunkStartTimeUs() const override { return 123'456'789; }
        std::int64_t GetChunkEndTimeUs() const override { return 123'456'789 + durationUs; }
    };

    struct Iterators {
        std::vector<std::unique_ptr<FixedChunkIterator>> owned;
        std::vector<MediaChunkIterator*> pointers;
        Iterators(const TrackGroup& group, std::int64_t chunkDurationUs) {
            for (int i = 0; i < group.Length(); i++) {
                owned.push_back(std::make_unique<FixedChunkIterator>(chunkDurationUs));
                pointers.push_back(owned.back().get());
            }
        }
    };

    Format VideoFormat(int bitrate, int width, int height) {
        Format f;
        f.sampleMimeType = MimeTypes::VIDEO_H264;
        f.SetBitrates(bitrate, -1);   // setAverageBitrate
        f.width = width;
        f.height = height;
        return f;
    }

    std::shared_ptr<const MediaChunk> Chunk(const Format& format, std::int64_t startUs, std::int64_t endUs, int reason = kSelectionReasonAdaptive) {
        // FakeMediaChunk's default reason is C.SELECTION_REASON_ADAPTIVE.
        return std::make_shared<MediaChunk>(MediaChunk{ format, reason, startUs, endUs });
    }

    std::vector<int> AllTracks(const TrackGroup& group) {
        std::vector<int> v(std::size_t(group.Length()));
        for (int i = 0; i < group.Length(); i++) v[std::size_t(i)] = i;
        return v;
    }

    BaseTrackSelection::FormatComparator InOrder(std::vector<Format> ordered) {
        return [ordered](const Format& a, const Format& b) {
            const auto ia = std::find(ordered.begin(), ordered.end(), a) - ordered.begin();
            const auto ib = std::find(ordered.begin(), ordered.end(), b) - ordered.begin();
            return ia < ib ? -1 : ia > ib ? 1 : 0;
        };
    }

    struct Fixture {
        FakeBandwidthMeter meter;
        FakeClock clock;

        std::unique_ptr<ATS> Prepare(std::unique_ptr<ATS> selection) {
            selection->Enable();
            Iterators it(selection->GetTrackGroup(), TEST_CHUNK_DURATION_US);
            selection->UpdateSelectedTrack(0, 0, kTimeUnset, {}, it.pointers);
            return selection;
        }
        std::unique_ptr<ATS> Make(const TrackGroup& group, std::int64_t minIncreaseMs, std::int64_t maxDecreaseMs, std::int64_t retainMs, int maxWidth,
                                  int maxHeight, float fraction, float liveFraction, std::vector<Checkpoint> checkpoints = {},
                                  const BaseTrackSelection::FormatComparator& comparator = &BaseTrackSelection::DefaultFormatComparator) {
            return Prepare(std::make_unique<ATS>(group, AllTracks(group), &meter, minIncreaseMs, maxDecreaseMs, retainMs, maxWidth, maxHeight, fraction,
                                                 liveFraction, std::move(checkpoints), clock.AsClock(), comparator));
        }
        std::unique_ptr<ATS> WithBandwidthFraction(const TrackGroup& g, float fraction) {
            return Make(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
                        ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS, ATS::DEFAULT_MAX_WIDTH_TO_DISCARD, ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, fraction,
                        ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE);
        }
        std::unique_ptr<ATS> WithMinDurationForQualityIncreaseMs(const TrackGroup& g, std::int64_t ms) {
            return Make(g, ms, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS, ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS,
                        ATS::DEFAULT_MAX_WIDTH_TO_DISCARD, ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, 1.0f, ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE);
        }
        std::unique_ptr<ATS> Default(const TrackGroup& g) { return WithMinDurationForQualityIncreaseMs(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS); }
        std::unique_ptr<ATS> WithMaxDurationForQualityDecreaseMs(const TrackGroup& g, std::int64_t ms) {
            return Make(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, ms, ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS,
                        ATS::DEFAULT_MAX_WIDTH_TO_DISCARD, ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, 1.0f, ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE);
        }
        std::unique_ptr<ATS> WithMinTimeBetweenBufferReevaluationMs(const TrackGroup& g, std::int64_t retainMs) {
            return Make(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS, retainMs,
                        ATS::DEFAULT_MAX_WIDTH_TO_DISCARD, ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, 1.0f, ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE);
        }
        std::unique_ptr<ATS> WithMaxResolutionToDiscard(const TrackGroup& g, int maxWidth, int maxHeight) {
            return Make(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
                        ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS, maxWidth, maxHeight, 1.0f,
                        ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE);
        }
        std::unique_ptr<ATS> WithBufferedFractionToLiveEdge(const TrackGroup& g, float liveFraction, std::int64_t minIncreaseMs) {
            return Make(g, minIncreaseMs, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS, ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS,
                        ATS::DEFAULT_MAX_WIDTH_TO_DISCARD, ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, 1.0f, liveFraction);
        }
        std::unique_ptr<ATS> WithAdaptationCheckpoints(const TrackGroup& g, std::vector<Checkpoint> checkpoints) {
            return Make(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
                        ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS, ATS::DEFAULT_MAX_WIDTH_TO_DISCARD, ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, 1.0f,
                        ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE, std::move(checkpoints));
        }
        std::unique_ptr<ATS> WithFormatComparatorAndDurations(const TrackGroup& g, std::int64_t minIncreaseMs, std::int64_t maxDecreaseMs,
                                                              const BaseTrackSelection::FormatComparator& comparator) {
            return Make(g, minIncreaseMs, maxDecreaseMs, ATS::DEFAULT_MIN_DURATION_TO_RETAIN_AFTER_DISCARD_MS, ATS::DEFAULT_MAX_WIDTH_TO_DISCARD,
                        ATS::DEFAULT_MAX_HEIGHT_TO_DISCARD, 1.0f, ATS::DEFAULT_BUFFERED_FRACTION_TO_LIVE_EDGE_FOR_QUALITY_INCREASE, {}, comparator);
        }
        std::unique_ptr<ATS> WithFormatComparator(const TrackGroup& g, const BaseTrackSelection::FormatComparator& comparator) {
            return WithFormatComparatorAndDurations(g, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, ATS::DEFAULT_MAX_DURATION_FOR_QUALITY_DECREASE_MS,
                                                    comparator);
        }
        void Update(ATS& s, std::int64_t bufferedUs, std::int64_t availableUs, const MediaChunkQueue& queue, const TrackGroup& g,
                    std::int64_t chunkUs = TEST_CHUNK_DURATION_US) {
            Iterators it(g, chunkUs);
            s.UpdateSelectedTrack(0, bufferedUs, availableUs, queue, it.pointers);
        }
    };

    const Format format1 = VideoFormat(500, 320, 240);
    const Format format2 = VideoFormat(1000, 640, 480);
    const Format format3 = VideoFormat(2000, 960, 720);
    const TrackGroup group123{ { format1, format2, format3 } };
}

TEST(AdaptiveTrackSelection_initial_updateSelectedTrack_selectsHighestBitrateWithinBandwidth) {
    Fixture f;
    f.meter.ThenReturn({ 1000 });
    auto s = f.WithBandwidthFraction(group123, 1.0f);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_initial_updateSelectedTrack_selectsHighestBitrateWithinBandwidthFraction) {
    Fixture f;
    f.meter.ThenReturn({ 2000 });
    auto s = f.WithBandwidthFraction(group123, 0.5f);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_initial_updateSelectedTrack_selectsHighestBitrateWithinBandwidthAndTimeToFirstByte) {
    Fixture f;
    f.meter.ThenReturn({ 2000 });
    f.meter.timeToFirstByteUs = 1'000'000;
    auto s = f.WithBandwidthFraction(group123, 1.0f);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_initial_updateSelectedTrack_returnsCorrectLatestBitrateEstimate) {
    Fixture f;
    f.meter.ThenReturn({ 2000 });
    auto s = f.WithBandwidthFraction(TrackGroup{ { format1, format2 } }, 0.5f);
    CHECK_EQ(s->GetLatestBitrateEstimate(), 2000);
}

TEST(AdaptiveTrackSelection_updateSelectedTrackDoNotSwitchUpIfNotBufferedEnough) {
    Fixture f;
    f.meter.ThenReturn({ 1000, 2000 });
    auto s = f.WithMinDurationForQualityIncreaseMs(group123, 10'000);
    f.Update(*s, 9'999'000, kTimeUnset, {}, group123);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_updateSelectedTrackSwitchUpIfBufferedEnough) {
    Fixture f;
    f.meter.ThenReturn({ 1000, 2000 });
    auto s = f.WithMinDurationForQualityIncreaseMs(group123, 10'000);
    f.Update(*s, 10'000'000, kTimeUnset, {}, group123);
    CHECK(s->GetSelectedFormat() == format3);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonAdaptive);
}

TEST(AdaptiveTrackSelection_updateSelectedTrack_liveStream_switchesUpWhenBufferedFractionToLiveEdgeReached) {
    Fixture f;
    f.meter.ThenReturn({ 1000, 2000 });
    auto s = f.WithBufferedFractionToLiveEdge(group123, 0.75f, 5000);
    f.Update(*s, 1'600'000, 5'600'000, {}, group123, 2'000'000);
    CHECK(s->GetSelectedFormat() == format2);
    f.Update(*s, 3'600'000, 5'600'000, {}, group123, 2'000'000);
    CHECK(s->GetSelectedFormat() == format3);
}

TEST(AdaptiveTrackSelection_initial_updateSelectedTrack_usesFactoryProvidedTrackFormatComparatorOrder) {
    Fixture f;
    ATS::Factory factory(10'000, 25'000, 25'000, 1.0f);
    factory.SetTrackFormatComparator(InOrder({ format2, format3, format1 }));
    f.meter.ThenReturn({ 2000 });
    auto s = f.Prepare(factory.CreateAdaptiveTrackSelection(group123, { 0, 1, 2 }, &f.meter, {}));
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_initial_updateSelectedTrack_withNoSelectableFormat_fallsBackToLowestPriorityFormat) {
    Fixture f;
    f.meter.ThenReturn({ 499 });
    auto s = f.WithFormatComparator(group123, InOrder({ format2, format1, format3 }));
    CHECK(s->GetSelectedFormat() == format3);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_updateSelectedTrackDoNotSwitchDownIfBufferedEnough) {
    Fixture f;
    f.meter.ThenReturn({ 1000, 500 });
    auto s = f.WithMaxDurationForQualityDecreaseMs(group123, 25'000);
    f.Update(*s, 25'000'000, kTimeUnset, {}, group123);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_updateSelectedTrackSwitchDownIfNotBufferedEnough) {
    Fixture f;
    f.meter.ThenReturn({ 1000, 500 });
    auto s = f.WithMaxDurationForQualityDecreaseMs(group123, 25'000);
    f.Update(*s, 24'999'000, kTimeUnset, {}, group123);
    CHECK(s->GetSelectedFormat() == format1);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonAdaptive);
}

TEST(AdaptiveTrackSelection_evaluateQueueSizeReturnQueueSizeIfBandwidthIsNotImproved) {
    Fixture f;
    const MediaChunkQueue queue = { Chunk(format1, 0, 10'000'000), Chunk(format1, 10'000'000, 20'000'000), Chunk(format1, 20'000'000, 30'000'000) };
    f.meter.ThenReturn({ 500 });
    auto s = f.Default(group123);
    CHECK_EQ(s->EvaluateQueueSize(0, queue), 3);
}

TEST(AdaptiveTrackSelection_evaluateQueueSizeDoNotReevaluateUntilAfterMinTimeBetweenBufferReevaluation) {
    Fixture f;
    const MediaChunkQueue queue = { Chunk(format1, 0, 10'000'000), Chunk(format1, 10'000'000, 20'000'000), Chunk(format1, 20'000'000, 30'000'000) };
    f.meter.ThenReturn({ 500 });
    auto s = f.WithMinTimeBetweenBufferReevaluationMs(group123, 15'000);
    const int initialQueueSize = s->EvaluateQueueSize(0, queue);
    f.clock.now += 999;
    f.meter.ThenReturn({ 1000 });
    int newSize = s->EvaluateQueueSize(0, queue);
    CHECK_EQ(newSize, initialQueueSize);
    f.clock.now += 1;
    newSize = s->EvaluateQueueSize(0, queue);
    CHECK(newSize < initialQueueSize);
}

TEST(AdaptiveTrackSelection_evaluateQueueSizeRetainMoreThanMinimumDurationAfterDiscard) {
    Fixture f;
    const MediaChunkQueue queue = { Chunk(format1, 0, 10'000'000), Chunk(format1, 10'000'000, 20'000'000), Chunk(format1, 20'000'000, 30'000'000) };
    f.meter.ThenReturn({ 500 });
    auto s = f.WithMinTimeBetweenBufferReevaluationMs(group123, 15'000);
    CHECK_EQ(s->EvaluateQueueSize(0, queue), 3);
    f.clock.now += 2000;
    f.meter.ThenReturn({ 1000 });
    CHECK_EQ(s->EvaluateQueueSize(0, queue), 2);
}

TEST(AdaptiveTrackSelection_evaluateQueueSizeDiscardChunksLessThanOrEqualToMaximumResolution) {
    Fixture f;
    const MediaChunkQueue queue = { Chunk(format2, 0, 10'000'000), Chunk(format2, 10'000'000, 20'000'000), Chunk(format2, 20'000'000, 30'000'000),
                                    Chunk(format2, 30'000'000, 40'000'000), Chunk(format1, 40'000'000, 50'000'000) };
    f.meter.ThenReturn({ 500 });
    auto s = f.WithMaxResolutionToDiscard(group123, 320, 240);
    CHECK_EQ(s->EvaluateQueueSize(0, queue), 5);
    f.clock.now += 2000;
    f.meter.ThenReturn({ 2000 });
    CHECK_EQ(s->EvaluateQueueSize(0, queue), 4);
}

TEST(AdaptiveTrackSelection_updateSelectedTrack_usesFormatOfLastChunkInTheQueueForSelection) {
    Fixture f;
    const TrackGroup group{ { format1, format2 } };
    auto s = ATS::Factory(10'000, 10'000, 25'000, 1.0f).CreateAdaptiveTrackSelection(group, { 0, 1 }, &f.meter, {});
    f.meter.ThenReturn({ 1000 });
    s = f.Prepare(std::move(s));
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
    const MediaChunkQueue queue = { Chunk(format2, 0, 2'000'000, kSelectionReasonInitial), Chunk(format2, 2'000'000, 4'000'000, kSelectionReasonInitial) };
    f.meter.ThenReturn({ 500 });
    f.Update(*s, 4'000'000, kTimeUnset, queue, group);
    CHECK(s->GetSelectedFormat() == format1);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonAdaptive);
    f.meter.ThenReturn({ 1000 });
    f.Update(*s, 4'000'000, kTimeUnset, queue, group);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_updateSelectedTrack_withQueueOfUnknownFormats_doesntThrow) {
    Fixture f;
    const TrackGroup group{ { format1, format2 } };
    auto s = f.Prepare(f.Default(group));
    const MediaChunkQueue queue = { Chunk(VideoFormat(42, 300, 123), 0, 2'000'000) };
    f.Update(*s, 2'000'000, kTimeUnset, queue, group);
    CHECK(s->GetSelectedFormat() == format1 || s->GetSelectedFormat() == format2);
}

TEST(AdaptiveTrackSelection_updateSelectedTrack_withCustomFormatOrder_usesCustomOrderForSwitchDirection) {
    Fixture f;
    f.meter.ThenReturn({ 2000 });
    auto s = f.WithFormatComparatorAndDurations(group123, 10'000, 25'000, InOrder({ format1, format2, format3 }));
    f.Update(*s, 4'000'000, kTimeUnset, { Chunk(format2, 0, 2'000'000, kSelectionReasonInitial) }, group123);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_updateSelectedTrack_withCustomFormatOrder_defersSwitchDownWhenBufferedEnough) {
    Fixture f;
    f.meter.ThenReturn({ 1000, 500 });
    auto s = f.WithFormatComparatorAndDurations(group123, ATS::DEFAULT_MIN_DURATION_FOR_QUALITY_INCREASE_MS, 25'000, InOrder({ format2, format1, format3 }));
    f.Update(*s, 25'000'000, kTimeUnset, {}, group123);
    CHECK(s->GetSelectedFormat() == format2);
    CHECK_EQ(s->GetSelectionReason(), kSelectionReasonInitial);
}

TEST(AdaptiveTrackSelection_updateSelectedTrack_withAdaptationCheckpoints_usesOnlyAllocatedBandwidth) {
    Fixture f;
    const Format format0 = VideoFormat(100, 160, 120);
    const Format f3 = VideoFormat(1500, 1024, 768);
    const TrackGroup group{ { format0, format1, format2, f3 } };
    auto s = f.Prepare(f.WithAdaptationCheckpoints(group, { { 0, 0 }, { 1500, 750 }, { 3000, 750 }, { 4000, 1250 }, { 5000, 1300 } }));
    const std::pair<std::int64_t, const Format*> steps[] = { { 1, &format0 },    { 999, &format0 },  { 1000, &format1 }, { 2499, &format1 },
                                                             { 3500, &format2 }, { 8999, &format2 }, { 9000, &f3 } };
    for (const auto& [estimate, expected] : steps) {
        f.meter.ThenReturn({ estimate });
        f.Update(*s, 999'999'999'999LL, kTimeUnset, {}, group);
        CHECK(s->GetSelectedFormat() == *expected);
    }
}

namespace {
    Format Average(int bitrate) {
        Format f;
        f.SetBitrates(bitrate, -1);
        return f;
    }
    // A Factory that records the checkpoints it creates selections with.
    struct RecordingFactory : ATS::Factory {
        std::vector<std::vector<Checkpoint>> checkPoints;
        std::unique_ptr<ATS> CreateAdaptiveTrackSelection(const TrackGroup& group, const std::vector<int>& tracks, BandwidthMeter* meter,
                                                          std::vector<Checkpoint> checkpoints) override {
            checkPoints.push_back(checkpoints);
            return ATS::Factory::CreateAdaptiveTrackSelection(group, tracks, meter, std::move(checkpoints));
        }
    };
}

TEST(AdaptiveTrackSelection_builderCreateTrackSelections_withSingleAdaptiveGroup_usesCorrectAdaptationCheckpoints) {
    FakeBandwidthMeter meter;
    const TrackGroup trackGroupMultipleFixed{ { Average(500), Average(1000) } };
    const TrackGroup trackGroupAdaptive{ { Average(2000), Average(3000), Average(4000), Average(5000) } };
    RecordingFactory factory;
    factory.CreateTrackSelections({ std::nullopt, ATS::Definition{ trackGroupMultipleFixed, { 0 } }, std::nullopt,
                                    ATS::Definition{ trackGroupMultipleFixed, { 1 } }, ATS::Definition{ trackGroupAdaptive, { 1, 2, 3 } } },
                                  &meter);
    CHECK_EQ(factory.checkPoints.size(), 1u);
    CHECK(factory.checkPoints.at(0) == (std::vector<Checkpoint>{ { 0, 0 }, { 4500, 3000 }, { 5500, 4000 }, { 6500, 5000 }, { 11500, 10000 } }));
}

TEST(AdaptiveTrackSelection_builderCreateTrackSelections_withMultipleAdaptiveGroups_usesCorrectAdaptationCheckpoints) {
    FakeBandwidthMeter meter;
    const TrackGroup trackGroup1{ { Average(500), Average(1000) } };
    const TrackGroup trackGroup2{ { Average(250), Average(500), Average(1250), Average(2000) } };
    const TrackGroup fixedGroup{ { Average(5000) } };
    RecordingFactory factory;
    factory.CreateTrackSelections({ std::nullopt, ATS::Definition{ trackGroup1, { 0, 1 } }, ATS::Definition{ fixedGroup, { 0 } },
                                    ATS::Definition{ trackGroup2, { 0, 1, 2 } }, std::nullopt },
                                  &meter);
    CHECK_EQ(factory.checkPoints.size(), 2u);
    CHECK(factory.checkPoints.at(0) == (std::vector<Checkpoint>{ { 0, 0 }, { 5750, 500 }, { 6000, 500 }, { 6500, 1000 }, { 7250, 1000 }, { 9500, 2000 } }));
    CHECK(factory.checkPoints.at(1) == (std::vector<Checkpoint>{ { 0, 0 }, { 5750, 250 }, { 6000, 500 }, { 6500, 500 }, { 7250, 1250 }, { 9500, 2500 } }));
}
