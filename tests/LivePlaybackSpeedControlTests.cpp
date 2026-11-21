// Ported from media3-exoplayer DefaultLivePlaybackSpeedControlTest.
#include "Check.h"

#include "kiln/media/LivePlaybackSpeedControl.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace kiln::media;
using LPSC = LivePlaybackSpeedControl;
using Config = MediaItem::LiveConfiguration;

namespace {
    // ShadowSystemClock: a clock the test advances.
    struct Clock {
        std::shared_ptr<std::int64_t> now = std::make_shared<std::int64_t>(1000);
        void Advance(std::int64_t ms) const { *now += ms; }
    };
    LPSC Make(const Clock& clock, LPSC::Options o = {}) {
        auto now = clock.now;
        o.elapsedRealtimeMs = [now] { return *now; };
        return LPSC(std::move(o));
    }
    Config Cfg(std::int64_t target, std::int64_t min, std::int64_t max, float minSpeed, float maxSpeed) { return {target, min, max, minSpeed, maxSpeed}; }
    // The usual configuration of the target offset tests.
    Config Standard() { return Cfg(42, 5, 400, 0.95f, 1.05f); }
    Config Large() { return Cfg(42'000, 5'000, 400'000, 0.9f, 1.1f); }
    Config Unset(std::int64_t target) { return Cfg(target, kTimeUnset, kTimeUnset, kRateUnset, kRateUnset); }
    LPSC::Options Rebuffer(std::int64_t incrementMs, std::int64_t minUpdateMs = 1000) {
        LPSC::Options o;
        o.targetLiveOffsetIncrementOnRebufferMs = incrementMs;
        o.minUpdateIntervalMs = minUpdateMs;
        return o;
    }
}

TEST(LiveSpeed_getTargetLiveOffsetUs_returnsUnset) {
    Clock clock;
    CHECK_EQ(Make(clock).GetTargetLiveOffsetUs(), kTimeUnset);
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterSetLiveConfiguration_returnsMediaLiveOffset) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetLiveConfiguration(Standard());
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(42'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_targetGreaterThanMax_returnsMax) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetLiveConfiguration(Cfg(4321, 5, 400, 0.95f, 1.05f));
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(400'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_targetLessThanMin_returnsMin) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetLiveConfiguration(Cfg(3, 5, 400, 0.95f, 1.05f));
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(5'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterOverride_returnsOverride) {
    for (std::int64_t over : {std::int64_t(321'000), std::int64_t(123'456'789), std::int64_t(3'141)}) {
        Clock clock;
        LPSC c = Make(clock);
        c.SetTargetLiveOffsetOverrideUs(over);
        c.SetLiveConfiguration(Standard());
        CHECK_EQ(c.GetTargetLiveOffsetUs(), over);   // even beyond max or below min
    }
}

TEST(LiveSpeed_getTargetLiveOffsetUs_overrideWithoutMediaConfiguration_returnsUnset) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetTargetLiveOffsetOverrideUs(123'456'789);
    CHECK_EQ(c.GetTargetLiveOffsetUs(), kTimeUnset);
}

TEST(LiveSpeed_getTargetLiveOffsetUs_withUnitSpeed_returnsUnset) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetLiveConfiguration(Cfg(42, kTimeUnset, kTimeUnset, 1.f, 1.f));
    CHECK_EQ(c.GetTargetLiveOffsetUs(), kTimeUnset);
}

TEST(LiveSpeed_getTargetLiveOffsetUs_overrideWithTimeUnset_returnsMediaLiveOffset) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetTargetLiveOffsetOverrideUs(123'456'789);
    c.SetLiveConfiguration(Standard());
    c.SetTargetLiveOffsetOverrideUs(kTimeUnset);
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(42'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterNotifyRebuffer_returnsIncreasedTargetOffset) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3));
    c.SetLiveConfiguration(Standard());
    const std::int64_t before = c.GetTargetLiveOffsetUs();
    c.NotifyRebuffer();
    CHECK_EQ(c.GetTargetLiveOffsetUs() - before, std::int64_t(3'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterRepeatedNotifyRebuffer_returnsMaxLiveOffset) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3));
    c.SetLiveConfiguration(Standard());
    std::vector<std::int64_t> offsets;
    for (int i = 0; i < 500; ++i) {
        offsets.push_back(c.GetTargetLiveOffsetUs());
        c.NotifyRebuffer();
    }
    CHECK(std::is_sorted(offsets.begin(), offsets.end()));
    CHECK_EQ(offsets.back(), std::int64_t(400'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterNotifyRebufferWithIncrementOfZero_returnsOriginal) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(0));
    c.SetLiveConfiguration(Standard());
    c.NotifyRebuffer();
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(42'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterNotifyRebufferAndOverride_returnsOverride) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3));
    c.SetLiveConfiguration(Standard());
    c.NotifyRebuffer();
    c.SetTargetLiveOffsetOverrideUs(321'000);
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(321'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterNotifyRebufferAndSameOffsetConfiguration_keepsIncrease) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3));
    c.SetLiveConfiguration(Standard());
    const std::int64_t before = c.GetTargetLiveOffsetUs();
    c.NotifyRebuffer();
    c.SetLiveConfiguration(Cfg(42, 3, 450, 0.9f, 1.1f));
    CHECK_EQ(c.GetTargetLiveOffsetUs() - before, std::int64_t(3'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_afterNotifyRebufferAndNewOffsetConfiguration_returnsNewOffset) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3));
    c.SetLiveConfiguration(Standard());
    c.NotifyRebuffer();
    c.SetLiveConfiguration(Cfg(39, 3, 450, 0.9f, 1.1f));
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(39'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_largeBuffer_decreasesToIdealTarget) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3'000, 100));
    c.SetLiveConfiguration(Large());
    c.NotifyRebuffer();
    const std::int64_t afterRebuffer = c.GetTargetLiveOffsetUs();
    c.GetAdjustedPlaybackSpeed(45'000'000, 9'000'000);
    const std::int64_t afterOne = c.GetTargetLiveOffsetUs();
    for (int i = 0; i < 500; ++i) {
        clock.Advance(100);
        c.GetAdjustedPlaybackSpeed(45'000'000, 9'000'000);
    }
    const std::int64_t afterMany = c.GetTargetLiveOffsetUs();
    CHECK(afterOne < afterRebuffer);
    CHECK(afterMany < afterOne);
    CHECK_EQ(afterMany, std::int64_t(42'000'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_smallBuffer_decreasesToSafeTarget) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3'000, 100));
    c.SetLiveConfiguration(Large());
    c.NotifyRebuffer();
    const std::int64_t afterRebuffer = c.GetTargetLiveOffsetUs();
    c.GetAdjustedPlaybackSpeed(45'000'000, 1'000'000);
    const std::int64_t afterOne = c.GetTargetLiveOffsetUs();
    for (int i = 0; i < 500; ++i) {
        clock.Advance(100);
        const std::int64_t noiseUs = ((i % 10) - 5LL) * 1'000;
        c.GetAdjustedPlaybackSpeed(45'000'000, 1'000'000 + noiseUs);
    }
    const std::int64_t afterMany = c.GetTargetLiveOffsetUs();
    CHECK(afterOne < afterRebuffer);
    CHECK(afterMany < afterOne);
    CHECK(afterMany > 44'005'000);   // at least the minimum buffered position
}

TEST(LiveSpeed_getTargetLiveOffsetUs_liveOffsetAroundTarget_returnsSafeTarget) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetLiveConfiguration(Large());
    const std::int64_t before = c.GetTargetLiveOffsetUs();
    for (int i = 0; i < 500; ++i) {
        clock.Advance(100);
        const std::int64_t noiseUs = ((i % 10) - 5LL) * 1'000;
        c.GetAdjustedPlaybackSpeed(49'000'000, 7'000'000 + noiseUs);
    }
    clock.Advance(100);
    c.GetAdjustedPlaybackSpeed(49'000'000, 7'000'000);
    CHECK_EQ(before, std::int64_t(42'000'000));
    CHECK(c.GetTargetLiveOffsetUs() > 42'005'000);
}

TEST(LiveSpeed_getTargetLiveOffsetUs_smoothingFactorOfZero_ignoresSafeTarget) {
    Clock clock;
    LPSC::Options o;
    o.minPossibleLiveOffsetSmoothingFactor = 0.f;
    LPSC c = Make(clock, o);
    c.SetLiveConfiguration(Large());
    const std::int64_t before = c.GetTargetLiveOffsetUs();
    for (int i = 0; i < 500; ++i) {
        clock.Advance(100);
        const std::int64_t noiseUs = ((i % 10) - 5LL) * 1'000;
        c.GetAdjustedPlaybackSpeed(49'000'000, 7'000'000 + noiseUs);
    }
    clock.Advance(100);
    c.GetAdjustedPlaybackSpeed(49'000'000, 7'000'000);
    CHECK_EQ(before, std::int64_t(42'000'000));
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(42'000'000));
}

TEST(LiveSpeed_getTargetLiveOffsetUs_liveOffsetLessThanTarget_returnsCurrentTarget) {
    Clock clock;
    LPSC c = Make(clock, Rebuffer(3'000, 100));
    c.SetLiveConfiguration(Large());
    const std::int64_t before = c.GetTargetLiveOffsetUs();
    c.GetAdjustedPlaybackSpeed(39'000'000, 1'000'000);
    CHECK_EQ(before, std::int64_t(42'000'000));
    CHECK_EQ(c.GetTargetLiveOffsetUs(), std::int64_t(42'000'000));
}

TEST(LiveSpeed_adjustPlaybackSpeed_liveOffsetMatchesTarget_returnsUnitSpeed) {
    Clock clock;
    LPSC c = Make(clock);
    c.SetLiveConfiguration(Unset(2'000));
    CHECK(c.GetAdjustedPlaybackSpeed(2'000'000, 1'000'000) == 1.f);
}

TEST(LiveSpeed_adjustPlaybackSpeed_withinErrorMargin_returnsUnitSpeed) {
    Clock clock;
    LPSC::Options o;
    o.maxLiveOffsetErrorMsForUnitSpeed = 5;
    LPSC c = Make(clock, o);
    c.SetLiveConfiguration(Unset(2'000));
    CHECK(c.GetAdjustedPlaybackSpeed(2'000'000 - 5'000 + 1, 1'000'000) == 1.f);
    CHECK(c.GetAdjustedPlaybackSpeed(2'000'000 + 5'000 - 1, 1'000'000) == 1.f);
}

TEST(LiveSpeed_adjustPlaybackSpeed_liveOffsetGreaterThanTarget_returnsAdjustedSpeed) {
    Clock clock;
    LPSC::Options o;
    o.proportionalControlFactor = 0.01f;
    LPSC c = Make(clock, o);
    c.SetLiveConfiguration(Unset(2'000));
    const float speed = c.GetAdjustedPlaybackSpeed(2'500'000, 1'000'000);
    CHECK(speed == 1.f + 0.01f * (2.5f - 2.f));
    CHECK(speed > 1.f);
}

TEST(LiveSpeed_adjustPlaybackSpeed_liveOffsetLowerThanTarget_returnsAdjustedSpeed) {
    Clock clock;
    LPSC::Options o;
    o.proportionalControlFactor = 0.01f;
    LPSC c = Make(clock, o);
    c.SetTargetLiveOffsetOverrideUs(2'000'000);
    c.SetLiveConfiguration(Unset(2'000));
    const float speed = c.GetAdjustedPlaybackSpeed(1'500'000, 1'000'000);
    CHECK(speed == 1.f + 0.01f * (1.5f - 2.f));
    CHECK(speed < 1.f);
}

TEST(LiveSpeed_adjustPlaybackSpeed_clampedToFallbackSpeeds) {
    Clock clock;
    LPSC::Options o;
    o.fallbackMaxPlaybackSpeed = 1.5f;
    LPSC fast = Make(clock, o);
    fast.SetLiveConfiguration(Unset(2'000));
    CHECK(fast.GetAdjustedPlaybackSpeed(999'999'999'999LL, 999'999'999'999LL) == 1.5f);
    LPSC::Options p;
    p.fallbackMinPlaybackSpeed = 0.5f;
    LPSC slow = Make(clock, p);
    slow.SetLiveConfiguration(Unset(2'000));
    CHECK(slow.GetAdjustedPlaybackSpeed(-999'999'999'999LL, 1'000'000) == 0.5f);
}

TEST(LiveSpeed_adjustPlaybackSpeed_clampedToMediaSpeeds) {
    Clock clock;
    LPSC::Options o;
    o.fallbackMaxPlaybackSpeed = 1.5f;
    LPSC fast = Make(clock, o);
    fast.SetLiveConfiguration(Cfg(2'000, kTimeUnset, kTimeUnset, kRateUnset, 2.f));
    CHECK(fast.GetAdjustedPlaybackSpeed(999'999'999'999LL, 999'999'999'999LL) == 2.f);
    LPSC::Options p;
    p.fallbackMinPlaybackSpeed = 0.5f;
    LPSC slow = Make(clock, p);
    slow.SetLiveConfiguration(Cfg(2'000, kTimeUnset, kTimeUnset, 0.2f, kRateUnset));
    CHECK(slow.GetAdjustedPlaybackSpeed(-999'999'999'999LL, 1'000'000) == 0.2f);
}

TEST(LiveSpeed_adjustPlaybackSpeed_repeatedCallWithinMinUpdateInterval_returnsSameSpeed) {
    Clock clock;
    LPSC::Options o;
    o.minUpdateIntervalMs = 123;
    LPSC c = Make(clock, o);
    c.SetLiveConfiguration(Unset(2'000));
    const float s1 = c.GetAdjustedPlaybackSpeed(1'500'000, 1'000'000);
    clock.Advance(122);
    const float s2 = c.GetAdjustedPlaybackSpeed(2'500'000, 1'000'000);
    clock.Advance(2);
    const float s3 = c.GetAdjustedPlaybackSpeed(2'500'000, 1'000'000);
    CHECK(s1 == s2);
    CHECK(s3 != s2);
}

TEST(LiveSpeed_adjustPlaybackSpeed_afterSameOffsetConfiguration_returnsSameSpeed) {
    Clock clock;
    LPSC::Options o;
    o.minUpdateIntervalMs = 123;
    LPSC c = Make(clock, o);
    c.SetLiveConfiguration(Unset(2'000));
    const float s1 = c.GetAdjustedPlaybackSpeed(1'500'000, 1'000'000);
    c.SetLiveConfiguration(Unset(2'000));
    CHECK(c.GetAdjustedPlaybackSpeed(2'500'000, 1'000'000) == s1);
}

TEST(LiveSpeed_adjustPlaybackSpeed_afterNewOffsetConfigurationOverrideOrRebuffer_updatesAgain) {
    for (int variant = 0; variant < 3; ++variant) {
        Clock clock;
        LPSC::Options o;
        o.minUpdateIntervalMs = 123;
        LPSC c = Make(clock, o);
        c.SetLiveConfiguration(Unset(2'000));
        const float s1 = c.GetAdjustedPlaybackSpeed(1'500'000, 1'000'000);
        if (variant == 0) c.SetLiveConfiguration(Unset(1'000));
        else if (variant == 1) c.SetTargetLiveOffsetOverrideUs(2'000'001);
        else c.NotifyRebuffer();
        CHECK(c.GetAdjustedPlaybackSpeed(2'500'000, 1'000'000) != s1);
    }
}
