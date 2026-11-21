// Ported from media3-exoplayer-dash UrlTemplateTest, RangedUriTest, SegmentBaseTest and
// DashManifestTest. Expected values are upstream's.
#include "Check.h"

#include "kiln/media/dash/DashManifest.h"

#include <algorithm>
#include <random>

using namespace kiln::media;
using namespace kiln::media::dash;
using kiln::media::stream::kLengthUnset;
using kiln::media::stream::kTimeUnset;

// --- UrlTemplateTest ---------------------------------------------------------------------------

TEST(DashUrlTemplate_realExamples) {
    CHECK_EQ(UrlTemplate::Compile("QualityLevels($Bandwidth$)/Fragments(video=$Time$,format=mpd-time-csf)").BuildUri("abc1", 10, 650000, 5000),
             std::string("QualityLevels(650000)/Fragments(video=5000,format=mpd-time-csf)"));
    CHECK_EQ(UrlTemplate::Compile("$RepresentationID$/$Number$").BuildUri("abc1", 10, 650000, 5000), std::string("abc1/10"));
    CHECK_EQ(UrlTemplate::Compile("chunk_ctvideo_cfm4s_rid$RepresentationID$_cn$Number$_w2073857842_mpd.m4s").BuildUri("abc1", 10, 650000, 5000),
             std::string("chunk_ctvideo_cfm4s_ridabc1_cn10_w2073857842_mpd.m4s"));
}

TEST(DashUrlTemplate_full) {
    CHECK_EQ(UrlTemplate::Compile("$Bandwidth$_a_$RepresentationID$_b_$Time$_c_$Number$").BuildUri("abc1", 10, 650000, 5000),
             std::string("650000_a_abc1_b_5000_c_10"));
}

TEST(DashUrlTemplate_fullWithDollarEscaping) {
    CHECK_EQ(UrlTemplate::Compile("$$$Bandwidth$$$_a$$_$RepresentationID$_b_$Time$_c_$Number$$$").BuildUri("abc1", 10, 650000, 5000),
             std::string("$650000$_a$_abc1_b_5000_c_10$"));
}

TEST(DashUrlTemplate_invalidSubstitution) {
    bool threw = false;
    try {
        UrlTemplate::Compile("$IllegalId$");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(DashUrlTemplate_fullWithMultipleOccurrences) {
    CHECK_EQ(UrlTemplate::Compile("$Bandwidth$_a1_$RepresentationID$_b1_$Time$_c1_$Number$_$Bandwidth$_a2_$RepresentationID$_b2_$Time$_c2_$Number$")
                 .BuildUri("abc1", 10, 650000, 5000),
             std::string("650000_a1_abc1_b1_5000_c1_10_650000_a2_abc1_b2_5000_c2_10"));
}

TEST(DashUrlTemplate_fullWithMultipleOccurrencesAndDollarEscaping) {
    CHECK_EQ(UrlTemplate::Compile("$$$Bandwidth$$$_a1$$_$RepresentationID$_b1_$Time$_c1_$Number$$$_$$$Bandwidth$$$_a2$$_$RepresentationID$_b2_$Time$_c2_$Number$$$")
                 .BuildUri("abc1", 10, 650000, 5000),
             std::string("$650000$_a1$_abc1_b1_5000_c1_10$_$650000$_a2$_abc1_b2_5000_c2_10$"));
}

// --- RangedUriTest -----------------------------------------------------------------------------

namespace {
    const std::string BASE_URI = "http://www.test.com/";
    const std::string PARTIAL_URI = "path/file.ext";
    const std::string FULL_URI = BASE_URI + PARTIAL_URI;

    void AssertMerge(const RangedUri& a, const RangedUri& b, const RangedUri& expected, const std::string& baseUrl) {
        CHECK(a.AttemptMerge(b, baseUrl) == expected);
        CHECK(b.AttemptMerge(a, baseUrl) == expected);
    }
    void AssertNonMerge(const RangedUri& a, const RangedUri& b, const std::string& baseUrl) {
        CHECK(!a.AttemptMerge(b, baseUrl).has_value());
        CHECK(!b.AttemptMerge(a, baseUrl).has_value());
    }
}

TEST(DashRangedUri_merge) { AssertMerge(RangedUri(FULL_URI, 0, 10), RangedUri(FULL_URI, 10, 10), RangedUri(FULL_URI, 0, 20), ""); }

TEST(DashRangedUri_mergeUnbounded) {
    AssertMerge(RangedUri(FULL_URI, 0, 10), RangedUri(FULL_URI, 10, kLengthUnset), RangedUri(FULL_URI, 0, kLengthUnset), "");
}

TEST(DashRangedUri_nonMerge) {
    AssertNonMerge(RangedUri(FULL_URI, 0, 10), RangedUri(FULL_URI, 11, 10), "");
    AssertNonMerge(RangedUri(FULL_URI, 0, 10), RangedUri(FULL_URI, 11, kLengthUnset), "");
    AssertNonMerge(RangedUri(FULL_URI, 0, 11), RangedUri(FULL_URI, 10, 10), "");
    AssertNonMerge(RangedUri(FULL_URI, 0, kLengthUnset), RangedUri(FULL_URI, 10, kLengthUnset), "");
}

TEST(DashRangedUri_mergeWithBaseUri) { AssertMerge(RangedUri(PARTIAL_URI, 0, 10), RangedUri(FULL_URI, 10, 10), RangedUri(FULL_URI, 0, 20), BASE_URI); }

// --- SegmentBaseTest ---------------------------------------------------------------------------

namespace {
    constexpr std::int64_t kPeriodStartUnixTimeUs = 123'000'000'000'000LL;
    SegmentTemplate UnboundedTemplate() {
        return SegmentTemplate(std::nullopt, 1000, 0, 42, kIndexUnset, 2000, std::nullopt, 500'000, std::nullopt, std::nullopt, 6'000'000, kPeriodStartUnixTimeUs);
    }
}

TEST(DashSegmentBase_getFirstAvailableSegmentNum_unboundedSegmentTemplate) {
    const SegmentTemplate t = UnboundedTemplate();
    CHECK_EQ(t.GetFirstAvailableSegmentNum(kTimeUnset, kPeriodStartUnixTimeUs - 10'000'000), 42);
    CHECK_EQ(t.GetFirstAvailableSegmentNum(kTimeUnset, kPeriodStartUnixTimeUs), 42);
    CHECK_EQ(t.GetFirstAvailableSegmentNum(kTimeUnset, kPeriodStartUnixTimeUs + 7'999'999), 42);
    CHECK_EQ(t.GetFirstAvailableSegmentNum(kTimeUnset, kPeriodStartUnixTimeUs + 8'000'000), 43);
    CHECK_EQ(t.GetFirstAvailableSegmentNum(kTimeUnset, kPeriodStartUnixTimeUs + 9'999'999), 43);
    CHECK_EQ(t.GetFirstAvailableSegmentNum(kTimeUnset, kPeriodStartUnixTimeUs + 10'000'000), 44);
}

TEST(DashSegmentBase_getAvailableSegmentCount_unboundedSegmentTemplate) {
    const SegmentTemplate t = UnboundedTemplate();
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs - 10'000'000), 0);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs), 0);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs + 1'499'999), 0);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs + 1'500'000), 1);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs + 7'499'999), 3);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs + 7'500'000), 4);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs + 7'999'999), 4);
    CHECK_EQ(t.GetAvailableSegmentCount(kTimeUnset, kPeriodStartUnixTimeUs + 8'000'000), 3);
}

TEST(DashSegmentBase_getNextSegmentShiftTimeUse_unboundedSegmentTemplate) {
    const SegmentTemplate t = UnboundedTemplate();
    CHECK_EQ(t.GetNextSegmentAvailableTimeUs(kTimeUnset, kPeriodStartUnixTimeUs - 10'000'000), 1'500'000);
    CHECK_EQ(t.GetNextSegmentAvailableTimeUs(kTimeUnset, kPeriodStartUnixTimeUs), 1'500'000);
    CHECK_EQ(t.GetNextSegmentAvailableTimeUs(kTimeUnset, kPeriodStartUnixTimeUs + 1'499'999), 1'500'000);
    CHECK_EQ(t.GetNextSegmentAvailableTimeUs(kTimeUnset, kPeriodStartUnixTimeUs + 1'500'000), 3'500'000);
    CHECK_EQ(t.GetNextSegmentAvailableTimeUs(kTimeUnset, kPeriodStartUnixTimeUs + 17'499'999), 17'500'000);
    CHECK_EQ(t.GetNextSegmentAvailableTimeUs(kTimeUnset, kPeriodStartUnixTimeUs + 17'500'000), 19'500'000);
}

TEST(DashSegmentBase_getSegmentCount_withSegmentTemplate_avoidsIncorrectRounding) {
    const SegmentTemplate t(std::nullopt, 90000, 0, 0, kIndexUnset, 179989, std::nullopt, kTimeUnset, std::nullopt, std::nullopt, kTimeUnset, kTimeUnset);
    CHECK_EQ(t.GetSegmentCount(2931820000LL), 1466);
}

TEST(DashSegmentBase_getSegmentCount_withSegmentTemplate_avoidsOverflow) {
    const SegmentTemplate t(std::nullopt, 1000000, 0, 0, kIndexUnset, 179989, std::nullopt, kTimeUnset, std::nullopt, std::nullopt, kTimeUnset, kTimeUnset);
    CHECK_EQ(t.GetSegmentCount(1618875028000000LL), 8994299808LL);
}

// --- DashManifestTest --------------------------------------------------------------------------

namespace {
    using Reps = std::vector<std::vector<std::vector<std::shared_ptr<const Representation>>>>;

    Reps NewRepresentations(int periodCount, int adaptationSetCounts, int representationCounts) {
        const auto segmentBase = std::make_shared<SingleSegmentBase>();
        Reps r(std::size_t(periodCount), std::vector<std::vector<std::shared_ptr<const Representation>>>{ std::size_t(adaptationSetCounts) });
        for (auto& period : r)
            for (auto& set : period)
                for (int k = 0; k < representationCounts; k++) set.push_back(Representation::NewInstance(0, Format{}, { BaseUrl("") }, segmentBase));
        return r;
    }

    DashManifest NewDashManifest(int duration, std::optional<ServiceDescriptionElement> serviceDescription, std::vector<Period> periods) {
        return DashManifest(0, duration, 1, false, 2, 3, 4, 12345, std::nullopt, UtcTimingElement{ "", "" }, serviceDescription, std::move(periods),
                            { Location("") });
    }
    Period NewPeriod(const std::string& id, int startMs, std::vector<AdaptationSet> adaptationSets) {
        return Period{ id, startMs, std::move(adaptationSets), {}, std::nullopt };
    }
    AdaptationSet NewAdaptationSet(int seed, std::vector<std::shared_ptr<const Representation>> representations) {
        AdaptationSet set;
        set.id = ++seed;
        set.type = TrackType(++seed);
        set.representations = std::move(representations);
        return set;
    }

    void AssertManifestEquals(const DashManifest& expected, const DashManifest& actual) {
        CHECK_EQ(actual.availabilityStartTimeMs, expected.availabilityStartTimeMs);
        CHECK_EQ(actual.durationMs, expected.durationMs);
        CHECK_EQ(actual.minBufferTimeMs, expected.minBufferTimeMs);
        CHECK_EQ(actual.dynamic, expected.dynamic);
        CHECK_EQ(actual.minUpdatePeriodMs, expected.minUpdatePeriodMs);
        CHECK_EQ(actual.timeShiftBufferDepthMs, expected.timeShiftBufferDepthMs);
        CHECK_EQ(actual.suggestedPresentationDelayMs, expected.suggestedPresentationDelayMs);
        CHECK_EQ(actual.publishTimeMs, expected.publishTimeMs);
        CHECK(actual.utcTiming == expected.utcTiming);
        CHECK_EQ(actual.location, expected.location);
        CHECK(actual.locations == expected.locations);
        CHECK_EQ(actual.GetPeriodCount(), expected.GetPeriodCount());
        CHECK(actual.serviceDescription == expected.serviceDescription);
        for (int i = 0; i < expected.GetPeriodCount() && i < actual.GetPeriodCount(); i++) {
            const Period& e = expected.GetPeriod(i);
            const Period& a = actual.GetPeriod(i);
            CHECK_EQ(a.id, e.id);
            CHECK_EQ(a.startMs, e.startMs);
            CHECK_EQ(a.adaptationSets.size(), e.adaptationSets.size());
            for (std::size_t j = 0; j < e.adaptationSets.size() && j < a.adaptationSets.size(); j++) {
                CHECK_EQ(a.adaptationSets[j].id, e.adaptationSets[j].id);
                CHECK(a.adaptationSets[j].type == e.adaptationSets[j].type);
                CHECK(a.adaptationSets[j].accessibilityDescriptors == e.adaptationSets[j].accessibilityDescriptors);
                CHECK(a.adaptationSets[j].representations == e.adaptationSets[j].representations);
            }
        }
    }
}

TEST(DashManifest_copy) {
    const Reps r = NewRepresentations(3, 2, 3);
    const ServiceDescriptionElement serviceDescriptionElement{ 20, 10, 40, 0.9f, 1.1f };
    const DashManifest sourceManifest = NewDashManifest(
        10, serviceDescriptionElement,
        { NewPeriod("1", 1, { NewAdaptationSet(2, r[0][0]), NewAdaptationSet(3, r[0][1]) }),
          NewPeriod("4", 4, { NewAdaptationSet(5, r[1][0]), NewAdaptationSet(6, r[1][1]) }),
          NewPeriod("7", 7, { NewAdaptationSet(8, r[2][0]), NewAdaptationSet(9, r[2][1]) }) });
    std::vector<StreamKey> keys = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 2 }, { 1, 0, 1 }, { 1, 1, 0 }, { 1, 1, 2 }, { 2, 0, 1 }, { 2, 0, 2 }, { 2, 1, 0 } };
    std::shuffle(keys.begin(), keys.end(), std::mt19937(0));
    const DashManifest copyManifest = sourceManifest.Copy(keys);
    const DashManifest expectedManifest = NewDashManifest(
        10, serviceDescriptionElement,
        { NewPeriod("1", 1, { NewAdaptationSet(2, { r[0][0][0], r[0][0][1] }), NewAdaptationSet(3, { r[0][1][2] }) }),
          NewPeriod("4", 4, { NewAdaptationSet(5, { r[1][0][1] }), NewAdaptationSet(6, { r[1][1][0], r[1][1][2] }) }),
          NewPeriod("7", 7, { NewAdaptationSet(8, { r[2][0][1], r[2][0][2] }), NewAdaptationSet(9, { r[2][1][0] }) }) });
    AssertManifestEquals(expectedManifest, copyManifest);
}

TEST(DashManifest_copySameAdaptationIndexButDifferentPeriod) {
    const Reps r = NewRepresentations(2, 1, 1);
    const DashManifest sourceManifest =
        NewDashManifest(10, std::nullopt, { NewPeriod("1", 1, { NewAdaptationSet(2, r[0][0]) }), NewPeriod("4", 4, { NewAdaptationSet(5, r[1][0]) }) });
    const DashManifest copyManifest = sourceManifest.Copy({ { 0, 0, 0 }, { 1, 0, 0 } });
    const DashManifest expectedManifest =
        NewDashManifest(10, std::nullopt, { NewPeriod("1", 1, { NewAdaptationSet(2, r[0][0]) }), NewPeriod("4", 4, { NewAdaptationSet(5, r[1][0]) }) });
    AssertManifestEquals(expectedManifest, copyManifest);
}

TEST(DashManifest_copySkipPeriod) {
    const Reps r = NewRepresentations(3, 2, 3);
    const DashManifest sourceManifest = NewDashManifest(
        10, std::nullopt,
        { NewPeriod("1", 1, { NewAdaptationSet(2, r[0][0]), NewAdaptationSet(3, r[0][1]) }),
          NewPeriod("4", 4, { NewAdaptationSet(5, r[1][0]), NewAdaptationSet(6, r[1][1]) }),
          NewPeriod("7", 7, { NewAdaptationSet(8, r[2][0]), NewAdaptationSet(9, r[2][1]) }) });
    const DashManifest copyManifest = sourceManifest.Copy({ { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 2 }, { 2, 0, 1 }, { 2, 0, 2 }, { 2, 1, 0 } });
    const DashManifest expectedManifest = NewDashManifest(
        7, std::nullopt,
        { NewPeriod("1", 1, { NewAdaptationSet(2, { r[0][0][0], r[0][0][1] }), NewAdaptationSet(3, { r[0][1][2] }) }),
          NewPeriod("7", 4, { NewAdaptationSet(8, { r[2][0][1], r[2][0][2] }), NewAdaptationSet(9, { r[2][1][0] }) }) });
    AssertManifestEquals(expectedManifest, copyManifest);
}
