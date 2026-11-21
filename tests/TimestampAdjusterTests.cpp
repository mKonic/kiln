// Ported from media3-common TimestampAdjusterTest.
#include "Check.h"

#include "kiln/media/stream/TimestampAdjuster.h"

using namespace kiln::media::stream;
using TA = TimestampAdjuster;

namespace {
    constexpr std::int64_t kWrap = 0x200000000LL;
}

TEST(TimestampAdjuster_adjustSampleTimestamp_fromZero) {
    TA adjuster(0);
    const std::int64_t first = adjuster.AdjustSampleTimestamp(2000);
    const std::int64_t second = adjuster.AdjustSampleTimestamp(6000);
    CHECK_EQ(first, std::int64_t(0));
    CHECK_EQ(second, std::int64_t(4000));
}

TEST(TimestampAdjuster_adjustSampleTimestamp_fromNonZero) {
    TA adjuster(1000);
    const std::int64_t first = adjuster.AdjustSampleTimestamp(2000);
    const std::int64_t second = adjuster.AdjustSampleTimestamp(6000);
    CHECK_EQ(first, std::int64_t(1000));
    CHECK_EQ(second, std::int64_t(5000));
}

TEST(TimestampAdjuster_adjustSampleTimestamp_noOffset) {
    TA adjuster(TA::MODE_NO_OFFSET);
    CHECK_EQ(adjuster.AdjustSampleTimestamp(2000), std::int64_t(2000));
    CHECK_EQ(adjuster.AdjustSampleTimestamp(6000), std::int64_t(6000));
}

TEST(TimestampAdjuster_adjustSampleTimestamp_afterResetToNoOffset) {
    TA adjuster(0);
    adjuster.AdjustSampleTimestamp(1000);
    adjuster.Reset(TA::MODE_NO_OFFSET);
    CHECK_EQ(adjuster.AdjustSampleTimestamp(2000), std::int64_t(2000));
    CHECK_EQ(adjuster.AdjustSampleTimestamp(6000), std::int64_t(6000));
}

TEST(TimestampAdjuster_adjustSampleTimestamp_afterResetToDifferentStartTime) {
    TA adjuster(0);
    adjuster.AdjustSampleTimestamp(1000);
    adjuster.Reset(5000);
    CHECK_EQ(adjuster.AdjustSampleTimestamp(2000), std::int64_t(5000));
    CHECK_EQ(adjuster.AdjustSampleTimestamp(6000), std::int64_t(9000));
}

TEST(TimestampAdjuster_adjustTsTimestamp_closeToWraparoundFollowedBySlightlySmallerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap - 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    const std::int64_t second = adjuster.AdjustTsTimestamp(kWrap - 180'000);
    CHECK_EQ(second, first - 1'000'000);
}

TEST(TimestampAdjuster_adjustTsTimestamp_closeToWraparoundFollowedBySlightlyLargerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap - 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    const std::int64_t second = adjuster.AdjustTsTimestamp(kWrap - 45'000);
    CHECK_EQ(second, first + 500'000);
}

TEST(TimestampAdjuster_adjustTsTimestamp_closeToWraparoundFollowedByMuchSmallerValue_assumesWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap - 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    const std::int64_t second = adjuster.AdjustTsTimestamp(90'000);
    CHECK_EQ(second, first + 2'000'000);
}

TEST(TimestampAdjuster_adjustTsTimestamp_justBeyondWraparoundFollowedBySlightlySmallerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap + 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(90'000);
    const std::int64_t second = adjuster.AdjustTsTimestamp(45'000);
    CHECK_EQ(second, first - 500'000);
}

TEST(TimestampAdjuster_adjustTsTimestamp_justBeyondWraparoundFollowedBySlightlyLargerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap + 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(90'000);
    const std::int64_t second = adjuster.AdjustTsTimestamp(180'000);
    CHECK_EQ(second, first + 1'000'000);
}

TEST(TimestampAdjuster_adjustTsTimestamp_justBeyondWraparoundFollowedByMuchLargerValue_assumesWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap + 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(90'000);
    const std::int64_t second = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    CHECK_EQ(second, first - 2'000'000);
}

TEST(TimestampAdjuster_adjustTsTimestampGreaterThanPreviousTimestamp_closeToWraparoundFollowedBySlightlySmallerValue_assumesWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap - 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    const std::int64_t second = adjuster.AdjustTsTimestampGreaterThanPreviousTimestamp(kWrap - 180'000);
    CHECK(second - first > 0x100000000LL);
}

TEST(TimestampAdjuster_adjustTsTimestampGreaterThanPreviousTimestamp_closeToWraparoundFollowedBySlightlyLargerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap - 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    const std::int64_t second = adjuster.AdjustTsTimestampGreaterThanPreviousTimestamp(kWrap - 45'000);
    CHECK_EQ(second, first + 500'000);
}

TEST(TimestampAdjuster_adjustTsTimestampGreaterThanPreviousTimestamp_closeToWraparoundFollowedByMuchSmallerValue_assumesWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap - 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(kWrap - 90'000);
    const std::int64_t second = adjuster.AdjustTsTimestampGreaterThanPreviousTimestamp(90'000);
    CHECK_EQ(second, first + 2'000'000);
}

TEST(TimestampAdjuster_adjustTsTimestampGreaterThanPreviousTimestamp_justBeyondWraparoundFollowedBySlightlySmallerValue_assumesWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap + 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(90'000);
    const std::int64_t second = adjuster.AdjustTsTimestampGreaterThanPreviousTimestamp(45'000);
    CHECK(second - first > 0x100000000LL);
}

TEST(TimestampAdjuster_adjustTsTimestampGreaterThanPreviousTimestamp_justBeyondWraparoundFollowedBySlightlyLargerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap + 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(90'000);
    const std::int64_t second = adjuster.AdjustTsTimestampGreaterThanPreviousTimestamp(180'000);
    CHECK_EQ(second, first + 1'000'000);
}

TEST(TimestampAdjuster_adjustTsTimestampGreaterThanPreviousTimestamp_justBeyondWraparoundFollowedByMuchLargerValue_doesNotAssumeWraparound) {
    TA adjuster(TA::PtsToUs(3 * kWrap + 90'000));
    const std::int64_t first = adjuster.AdjustTsTimestamp(90'000);
    const std::int64_t second = adjuster.AdjustTsTimestampGreaterThanPreviousTimestamp(kWrap - 90'000);
    CHECK(second - first > 0x100000000LL);
}

TEST(TimestampAdjuster_usToWrappedPts_usTimestampCloseToOverflow_doesntOverflow) {
    CHECK_EQ(TA::UsToNonWrappedPts(std::int64_t(1) << 52), std::int64_t(405323966463344LL));
}

// kiln: the shared mode HLS uses (TimestampAdjusterProvider, sharedInitializeOrWait).
TEST(TimestampAdjuster_shared_primaryInitializes_othersFollow) {
    TimestampAdjusterProvider provider;
    auto adjuster = provider.GetAdjuster(3);
    CHECK(provider.GetAdjuster(3) == adjuster);
    CHECK(!adjuster->TrySharedInitialize(false, 0));
    CHECK(adjuster->TrySharedInitialize(true, 10'000'000));
    CHECK_EQ(adjuster->AdjustTsTimestamp(900'000), std::int64_t(10'000'000));   // 10 s of PTS -> the segment start
    CHECK(adjuster->TrySharedInitialize(false, 0));
    CHECK_EQ(adjuster->AdjustTsTimestamp(990'000), std::int64_t(11'000'000));
    provider.Reset();
    CHECK(provider.GetAdjuster(3) != adjuster);
}
