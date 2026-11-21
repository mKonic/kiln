// Ported from media3-exoplayer-dash DashManifestParserTest.java, over upstream's sample MPDs.
// Expected values are upstream's; a null string is "" here. Left out:
// parse_withUpstreamError_throwsUpstreamException (the parser reads a string, not a DataSource).
#include "Check.h"
#include "DashManifestTestData.h"

#include "kiln/media/dash/DashManifest.h"

#include <cstdlib>

using namespace kiln::media;
using namespace kiln::media::dash;
namespace MimeTypes = kiln::media::stream::MimeTypes;
using kiln::media::stream::XmlPullParser;
using namespace dash_test_data;

namespace {
    const std::string kTestMpd = "https://example.com/test.mpd";

    std::shared_ptr<DashManifest> Parse(std::string_view mpd, const std::string& uri = kTestMpd) { return DashManifestParser().Parse(uri, std::string(mpd)); }

    const std::string NEXT_TAG_NAME = "Next";
    const std::string NEXT_TAG = "<" + NEXT_TAG_NAME + "/>";

    void AssertNextTag(XmlPullParser& xpp) {
        xpp.Next();
        CHECK_EQ(xpp.GetEventType(), int(XmlPullParser::START_TAG));
        CHECK_EQ(xpp.GetName(), NEXT_TAG_NAME);
    }

    std::vector<std::uint8_t> Bytes(const std::string& s) { return { s.begin(), s.end() }; }

    std::int64_t GetAvailabilityTimeOffsetUs(const AdaptationSet& adaptationSet) {
        CHECK(!adaptationSet.representations.empty());
        const auto& representation = adaptationSet.representations.at(0);
        CHECK(representation->kind == Representation::Kind::MultiSegment);
        return static_cast<const MultiSegmentRepresentation&>(*representation).segmentBase->availabilityTimeOffsetUs;
    }

    const DashSegmentIndex& IndexOf(const Representation& representation) {
        CHECK(representation.kind == Representation::Kind::MultiSegment);
        return static_cast<const MultiSegmentRepresentation&>(representation);
    }

    std::vector<Descriptor> Cea608(const std::string& value) { return { Descriptor{ "urn:scte:dash:cc:cea-608:2015", value, "" } }; }
    std::vector<Descriptor> Cea708(const std::string& value) { return { Descriptor{ "urn:scte:dash:cc:cea-708:2015", value, "" } }; }
}

TEST(DashManifestParser_parse_withMalformedManifest_throwsParserExceptionForMalformedContent) {
    std::string random;
    std::srand(0);
    for (int i = 0; i < 100; i++) random += char(std::rand() & 0xFF);   // FakeDataSet.setRandomData(uri, 100)
    bool threw = false;
    try {
        Parse(random, "http://test.test");
    } catch (const stream::ParserException&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(DashManifestParser_parseMediaPresentationDescription) {
    Parse(kLive);
    Parse(kUnknownMimeType);
}

TEST(DashManifestParser_parseMediaPresentationDescription_segmentTemplate) {
    auto manifest = Parse(kSegmentTemplate);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    const Period& period = manifest->GetPeriod(0);
    CHECK_EQ(period.adaptationSets.size(), 2u);
    int checked = 0;
    for (const AdaptationSet& adaptationSet : period.adaptationSets) {
        for (const auto& representation : adaptationSet.representations) {
            if (representation->kind == Representation::Kind::MultiSegment) {
                const auto& multi = static_cast<const MultiSegmentRepresentation&>(*representation);
                const RangedUri uri = multi.GetSegmentUrl(multi.GetFirstSegmentNum());
                CHECK(uri.ResolveUriString(representation->baseUrls.at(0).url).find("redirector.googlevideo.com") != std::string::npos);
                checked++;
            }
        }
    }
    CHECK(checked > 0);
}

TEST(DashManifestParser_parseMediaPresentationDescription_eventStream) {
    auto manifest = Parse(kEventStream);
    const Period& period = manifest->GetPeriod(0);
    CHECK_EQ(period.eventStreams.size(), 4u);
    const EventStream& eventStream1 = period.eventStreams[0];
    CHECK_EQ(eventStream1.events.size(), 1u);
    CHECK(eventStream1.events[0] == (EventMessage{ "urn:uuid:XYZY", "call", 10000, 0, Bytes("+ 1 800 10101010") }));
    CHECK_EQ(eventStream1.presentationTimesUs[0], 0);
    const EventStream& eventStream2 = period.eventStreams[1];
    CHECK_EQ(eventStream2.events.size(), 1u);
    CHECK(eventStream2.events[0] == (EventMessage{ "urn:uuid:with-pto", "pto-4s", 10000, 0, Bytes("pt=1s") }));
    CHECK_EQ(eventStream2.presentationTimesUs[0], 1000000);
    const EventStream& eventStream3 = period.eventStreams[2];
    CHECK_EQ(eventStream3.events.size(), 1u);
    const EventMessage expectedEvent3{ "urn:dvb:iptv:cpm:2014", "", 1500000, 1,
                                       Bytes("<![CDATA[<BroadcastEvent>\n"
                                             "      <Program crid=\"crid://broadcaster.example.com/ABCDEF\"/>\n"
                                             "      <InstanceDescription>\n"
                                             "      <Title xml:lang=\"en\">The title</Title>\n"
                                             "      <Synopsis xml:lang=\"en\" length=\"medium\">"
                                             "The description</Synopsis>\n"
                                             "      <ParentalGuidance>\n"
                                             "      <mpeg7:ParentalRating href=\"urn:dvb:iptv:rating:2014:15\"/>\n"
                                             "      <mpeg7:Region>GB</mpeg7:Region>\n"
                                             "      </ParentalGuidance>\n"
                                             "      </InstanceDescription>\n"
                                             "      </BroadcastEvent>]]>") };
    CHECK(eventStream3.events[0] == expectedEvent3);
    CHECK_EQ(eventStream3.presentationTimesUs[0], 300000000);
    const EventStream& eventStream4 = period.eventStreams[3];
    CHECK_EQ(eventStream4.events.size(), 1u);
    const EventMessage expectedEvent4{ "urn:scte:scte35:2014:xml+bin", "", 1000000, 2,
                                       Bytes("<scte35:Signal>\n"
                                             "         <scte35:Binary>\n"
                                             "         /DAIAAAAAAAAAAAQAAZ/I0VniQAQAgBDVUVJQAAAAH+cAAAAAA==\n"
                                             "         </scte35:Binary>\n"
                                             "       </scte35:Signal>") };
    CHECK(eventStream4.events[0] == expectedEvent4);
    CHECK_EQ(eventStream4.presentationTimesUs[0], 1000000000);
}

TEST(DashManifestParser_parseMediaPresentationDescription_programInformation) {
    auto manifest = Parse(kLive);
    CHECK(manifest->programInformation == (ProgramInformation{ "MediaTitle", "MediaSource", "MediaCopyright", "www.example.com", "enUs" }));
}

TEST(DashManifestParser_parseMediaPresentationDescription_locationRedirectRelative) {
    auto manifest = Parse(kLiveLocationRedirectRelative, "https://example.com/a/b/test.mpd");
    CHECK_EQ(manifest->location, std::string("https://example.com/a/relative/redirect.mpd"));
}

TEST(DashManifestParser_parseMediaPresentationDescription_locationRedirectAbsolute) {
    auto manifest = Parse(kLiveLocationRedirectAbsolute, "https://example.com/a/b/test.mpd");
    CHECK_EQ(manifest->location, std::string("https://example2.com/absolute/redirect.mpd"));
}

TEST(DashManifestParser_parseMediaPresentationDescription_images) {
    auto manifest = Parse(kImages);
    const AdaptationSet& adaptationSet = manifest->GetPeriod(0).adaptationSets.at(0);
    const Format& format0 = adaptationSet.representations.at(0)->format;
    const Format& format1 = adaptationSet.representations.at(1)->format;
    CHECK_EQ(format0.sampleMimeType, std::string("image/jpeg"));
    CHECK_EQ(format0.width, 320);
    CHECK_EQ(format0.height, 180);
    CHECK_EQ(format0.tileCountHorizontal, 12);
    CHECK_EQ(format0.tileCountVertical, 16);
    CHECK_EQ(format1.sampleMimeType, std::string("image/jpeg"));
    CHECK_EQ(format1.width, 640);
    CHECK_EQ(format1.height, 360);
    CHECK_EQ(format1.tileCountHorizontal, 2);
    CHECK_EQ(format1.tileCountVertical, 4);
}

TEST(DashManifestParser_parseMediaPresentationDescription_labels) {
    auto manifest = Parse(kLabels);
    const auto& adaptationSets = manifest->GetPeriod(0).adaptationSets;
    const Format& audio = adaptationSets.at(0).representations.at(0)->format;
    const Format& video = adaptationSets.at(1).representations.at(0)->format;
    CHECK_EQ(audio.label, std::string("audio label"));
    CHECK_EQ(video.label, std::string("video label"));
    CHECK_EQ(audio.labels.size(), 1u);
    CHECK_EQ(audio.labels.at(0).value, std::string("audio label"));
    CHECK_EQ(video.labels.size(), 1u);
    CHECK_EQ(video.labels.at(0).value, std::string("video label"));
}

TEST(DashManifestParser_parseMediaPresentationDescription_text) {
    auto manifest = Parse(kText);
    const auto& adaptationSets = manifest->GetPeriod(0).adaptationSets;
    const Format* format = &adaptationSets.at(0).representations.at(0)->format;
    CHECK_EQ(format->containerMimeType, std::string(MimeTypes::APPLICATION_MP4));
    CHECK_EQ(format->sampleMimeType, std::string(MimeTypes::APPLICATION_TTML));
    CHECK_EQ(format->codecs, std::string("stpp.ttml.im1t"));
    CHECK_EQ(format->roleFlags, kRoleFlagSubtitle);
    CHECK_EQ(format->selectionFlags, kSelectionFlagForced);
    CHECK(adaptationSets.at(1).type == TrackType::Text);
    format = &adaptationSets.at(1).representations.at(0)->format;
    CHECK_EQ(format->roleFlags, kRoleFlagSubtitle);
    CHECK_EQ(format->selectionFlags, kSelectionFlagForced);
    format = &adaptationSets.at(2).representations.at(0)->format;
    CHECK_EQ(format->containerMimeType, std::string(MimeTypes::APPLICATION_TTML));
    CHECK_EQ(format->sampleMimeType, std::string(MimeTypes::APPLICATION_TTML));
    CHECK(format->codecs.empty());
    CHECK_EQ(format->roleFlags, 0);
    CHECK(adaptationSets.at(2).type == TrackType::Text);
}

TEST(DashManifestParser_parseMediaPresentationDescription_standaloneTextIgnoresPresentationTimeOffset) {
    auto manifest = Parse(kTextPresentationTimeOffset);
    const auto& adaptationSets = manifest->GetPeriod(0).adaptationSets;
    const Representation& ttml = *adaptationSets.at(0).representations.at(0);
    CHECK_EQ(ttml.format.containerMimeType, std::string(MimeTypes::APPLICATION_TTML));
    CHECK_EQ(ttml.presentationTimeOffsetUs, 0);
    CHECK_EQ(IndexOf(ttml).GetTimeUs(IndexOf(ttml).GetFirstSegmentNum()), 2'000'000);
    const Representation& vtt = *adaptationSets.at(1).representations.at(0);
    CHECK_EQ(vtt.format.containerMimeType, std::string(MimeTypes::TEXT_VTT));
    CHECK_EQ(vtt.presentationTimeOffsetUs, 0);
    CHECK_EQ(IndexOf(vtt).GetTimeUs(IndexOf(vtt).GetFirstSegmentNum()), 3'000'000);
    const Representation& mp4 = *adaptationSets.at(2).representations.at(0);
    CHECK_EQ(mp4.format.containerMimeType, std::string(MimeTypes::APPLICATION_MP4));
    CHECK_EQ(mp4.presentationTimeOffsetUs, 5'000'000);
    CHECK_EQ(IndexOf(mp4).GetTimeUs(IndexOf(mp4).GetFirstSegmentNum()), 0);
}

TEST(DashManifestParser_parseMediaPresentationDescription_trickPlay) {
    auto manifest = Parse(kTrickPlay);
    const auto& adaptationSets = manifest->GetPeriod(0).adaptationSets;
    const AdaptationSet* adaptationSet = &adaptationSets.at(0);
    CHECK(adaptationSet->essentialProperties.empty());
    CHECK(adaptationSet->supplementalProperties.empty());
    CHECK_EQ(adaptationSet->representations.at(0)->format.roleFlags, 0);
    adaptationSet = &adaptationSets.at(1);
    CHECK(adaptationSet->essentialProperties.empty());
    CHECK(adaptationSet->supplementalProperties.empty());
    CHECK_EQ(adaptationSet->representations.at(0)->format.roleFlags, 0);
    adaptationSet = &adaptationSets.at(2);
    CHECK_EQ(adaptationSet->essentialProperties.size(), 1u);
    CHECK_EQ(adaptationSet->essentialProperties.at(0).schemeIdUri, std::string("http://dashif.org/guidelines/trickmode"));
    CHECK_EQ(adaptationSet->essentialProperties.at(0).value, std::string("0"));
    CHECK(adaptationSet->supplementalProperties.empty());
    CHECK_EQ(adaptationSet->representations.at(0)->format.roleFlags, kRoleFlagTrickPlay);
    adaptationSet = &adaptationSets.at(3);
    CHECK(adaptationSet->essentialProperties.empty());
    CHECK_EQ(adaptationSet->supplementalProperties.size(), 1u);
    CHECK_EQ(adaptationSet->supplementalProperties.at(0).schemeIdUri, std::string("http://dashif.org/guidelines/trickmode"));
    CHECK_EQ(adaptationSet->supplementalProperties.at(0).value, std::string("1"));
    CHECK_EQ(adaptationSet->representations.at(0)->format.roleFlags, kRoleFlagTrickPlay);
}

TEST(DashManifestParser_parseMediaPresentationDescription_dolbyVisionProfile10) {
    auto manifest = Parse(kDolby);
    const AdaptationSet& adaptationSet = manifest->GetPeriod(0).adaptationSets.at(0);
    CHECK_EQ(adaptationSet.representations.size(), 2u);
    const Format& format = adaptationSet.representations.at(0)->format;
    CHECK_EQ(format.colorSpace, stream::kColorSpaceBt2020);
    CHECK_EQ(format.colorTransfer, stream::kColorTransferSt2084);
    CHECK_EQ(format.colorRange, stream::kColorRangeFull);
    CHECK_EQ(adaptationSet.supplementalProperties.size(), 1u);
    CHECK_EQ(adaptationSet.supplementalProperties.at(0).schemeIdUri, std::string("urn:dolby:dash:dolby-vision:2018"));
    CHECK_EQ(adaptationSet.supplementalProperties.at(0).value, std::string("10.1"));
}

TEST(DashManifestParser_parseSegmentTimeline_repeatCount) {
    XmlPullParser xpp("<SegmentTimeline><S d=\"96000\" r=\"2\"/><S d=\"48000\" r=\"0\"/></SegmentTimeline>" + NEXT_TAG);
    xpp.Next();
    const auto elements = DashManifestParser().ParseSegmentTimeline(xpp, 48000, 10000);
    CHECK(elements == (std::vector<SegmentTimelineElement>{ { 0, 96000 }, { 96000, 96000 }, { 192000, 96000 }, { 288000, 48000 } }));
    AssertNextTag(xpp);
}

TEST(DashManifestParser_parseSegmentTimeline_singleUndefinedRepeatCount) {
    XmlPullParser xpp("<SegmentTimeline><S d=\"96000\" r=\"-1\"/></SegmentTimeline>" + NEXT_TAG);
    xpp.Next();
    const auto elements = DashManifestParser().ParseSegmentTimeline(xpp, 48000, 10000);
    CHECK(elements == (std::vector<SegmentTimelineElement>{ { 0, 96000 }, { 96000, 96000 }, { 192000, 96000 }, { 288000, 96000 }, { 384000, 96000 } }));
    AssertNextTag(xpp);
}

TEST(DashManifestParser_parseSegmentTimeline_timeOffsetsAndUndefinedRepeatCount) {
    XmlPullParser xpp("<SegmentTimeline><S t=\"0\" d=\"96000\" r=\"-1\"/><S t=\"192000\" d=\"48000\" r=\"-1\"/></SegmentTimeline>" + NEXT_TAG);
    xpp.Next();
    const auto elements = DashManifestParser().ParseSegmentTimeline(xpp, 48000, 10000);
    CHECK(elements == (std::vector<SegmentTimelineElement>{ { 0, 96000 },
                                                            { 96000, 96000 },
                                                            { 192000, 48000 },
                                                            { 240000, 48000 },
                                                            { 288000, 48000 },
                                                            { 336000, 48000 },
                                                            { 384000, 48000 },
                                                            { 432000, 48000 } }));
    AssertNextTag(xpp);
}

TEST(DashManifestParser_parseLabel) {
    XmlPullParser xpp("<Label lang=\"en\">test label</Label>" + NEXT_TAG);
    xpp.Next();
    const Label label = DashManifestParser().ParseLabel(xpp);
    CHECK_EQ(label.language, std::string("en"));
    CHECK_EQ(label.value, std::string("test label"));
    AssertNextTag(xpp);
}

TEST(DashManifestParser_parseLabel_noLang) {
    XmlPullParser xpp("<Label>test label</Label>" + NEXT_TAG);
    xpp.Next();
    const Label label = DashManifestParser().ParseLabel(xpp);
    CHECK(label.language.empty());
    AssertNextTag(xpp);
}

TEST(DashManifestParser_parseLabel_noText) {
    XmlPullParser xpp("<Label/>" + NEXT_TAG);
    xpp.Next();
    const Label label = DashManifestParser().ParseLabel(xpp);
    CHECK(label.value.empty());
    AssertNextTag(xpp);
}

TEST(DashManifestParser_parseCea608AccessibilityChannel) {
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("CC1=eng")), 1);
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("CC2=eng")), 2);
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("CC3=eng")), 3);
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("CC4=eng")), 4);
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("")), -1);   // null and "" alike
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("CC0=eng")), -1);
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("CC5=eng")), -1);
    CHECK_EQ(DashManifestParser::ParseCea608AccessibilityChannel(Cea608("Wrong format")), -1);
}

TEST(DashManifestParser_parseCea708AccessibilityChannel) {
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("1=lang:eng")), 1);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("2=lang:eng")), 2);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("3=lang:eng")), 3);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("62=lang:eng")), 62);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("63=lang:eng")), 63);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("")), -1);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("0=lang:eng")), -1);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("64=lang:eng")), -1);
    CHECK_EQ(DashManifestParser::ParseCea708AccessibilityChannel(Cea708("Wrong format")), -1);
}

TEST(DashManifestParser_parsePeriodAssetIdentifier) {
    auto manifest = Parse(kAssetIdentifier);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    const Period& period = manifest->GetPeriod(0);
    CHECK(period.assetIdentifier.has_value());
    CHECK_EQ(period.assetIdentifier->schemeIdUri, std::string("urn:org:dashif:asset-id:2013"));
    CHECK_EQ(period.assetIdentifier->value, std::string("md:cid:EIDR:10.5240%2f0EFB-02CD-126E-8092-1E49-W"));
    CHECK_EQ(period.assetIdentifier->id, std::string("uniqueId"));
}

TEST(DashManifestParser_parseEssentialAndSupplementalProperties) {
    auto manifest = Parse(kEssentialSupplementalProperties);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    CHECK_EQ(manifest->GetPeriod(0).adaptationSets.size(), 1u);
    const AdaptationSet& adaptationSet = manifest->GetPeriod(0).adaptationSets.at(0);
    CHECK_EQ(adaptationSet.representations.size(), 2u);
    const Representation& representation0 = *adaptationSet.representations.at(0);
    const Representation& representation1 = *adaptationSet.representations.at(1);
    CHECK(representation0.kind == Representation::Kind::SingleSegment);
    CHECK(representation1.kind == Representation::Kind::MultiSegment);
    const std::string essential = "urn:mpeg:dash:essential-scheme:2050", supplemental = "urn:mpeg:dash:supplemental-scheme:2050";
    CHECK(adaptationSet.essentialProperties == (std::vector<Descriptor>{ { essential, "adaptationEssential", "" } }));
    CHECK(adaptationSet.supplementalProperties == (std::vector<Descriptor>{ { supplemental, "adaptationSupplemental", "" } }));
    CHECK_EQ(representation0.essentialProperties.size(), 2u);
    CHECK_EQ(representation0.essentialProperties.at(0).schemeIdUri, essential);
    CHECK_EQ(representation0.essentialProperties.at(0).value, std::string("adaptationEssential"));
    CHECK_EQ(representation0.essentialProperties.at(1).schemeIdUri, essential);
    CHECK_EQ(representation0.essentialProperties.at(1).value, std::string("representationEssential"));
    CHECK_EQ(representation0.supplementalProperties.size(), 2u);
    CHECK_EQ(representation0.supplementalProperties.at(0).schemeIdUri, supplemental);
    CHECK_EQ(representation0.supplementalProperties.at(0).value, std::string("adaptationSupplemental"));
    CHECK_EQ(representation0.supplementalProperties.at(1).schemeIdUri, supplemental);
    CHECK_EQ(representation0.supplementalProperties.at(1).value, std::string("representationSupplemental"));
    CHECK_EQ(representation1.essentialProperties.size(), 2u);
    CHECK_EQ(representation1.essentialProperties.at(1).schemeIdUri, essential);
    CHECK_EQ(representation1.essentialProperties.at(1).value, std::string("representationEssential"));
    CHECK_EQ(representation1.supplementalProperties.size(), 2u);
    CHECK_EQ(representation1.supplementalProperties.at(1).schemeIdUri, supplemental);
    CHECK_EQ(representation1.supplementalProperties.at(1).value, std::string("representationSupplemental"));
}

TEST(DashManifestParser_availabilityTimeOffset_staticManifest_setToTimeUnset) {
    auto manifest = Parse(kText);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    const auto& adaptationSets = manifest->GetPeriod(0).adaptationSets;
    CHECK_EQ(adaptationSets.size(), 3u);
    for (const AdaptationSet& set : adaptationSets) CHECK_EQ(GetAvailabilityTimeOffsetUs(set), stream::kTimeUnset);
}

TEST(DashManifestParser_availabilityTimeOffset_dynamicManifest_valuesInBaseUrl_setsCorrectValues) {
    auto manifest = Parse(kAvailabilityTimeOffsetBaseUrl);
    CHECK_EQ(manifest->GetPeriodCount(), 2);
    const auto& adaptationSets0 = manifest->GetPeriod(0).adaptationSets;
    const auto& adaptationSets1 = manifest->GetPeriod(1).adaptationSets;
    CHECK_EQ(adaptationSets0.size(), 4u);
    CHECK_EQ(adaptationSets1.size(), 1u);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(0)), 5'000'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(1)), 4'321'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(2)), 9'876'543);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(3)), stream::kTimeUnset);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets1.at(0)), 0);
}

TEST(DashManifestParser_availabilityTimeOffset_dynamicManifest_valuesInSegmentTemplate_setsCorrectValues) {
    auto manifest = Parse(kAvailabilityTimeOffsetSegmentTemplate);
    CHECK_EQ(manifest->GetPeriodCount(), 2);
    const auto& adaptationSets0 = manifest->GetPeriod(0).adaptationSets;
    const auto& adaptationSets1 = manifest->GetPeriod(1).adaptationSets;
    CHECK_EQ(adaptationSets0.size(), 4u);
    CHECK_EQ(adaptationSets1.size(), 1u);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(0)), 2'000'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(1)), 3'210'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(2)), 1'230'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(3)), 100'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets1.at(0)), 9'999'000);
}

TEST(DashManifestParser_availabilityTimeOffset_dynamicManifest_valuesInSegmentList_setsCorrectValues) {
    auto manifest = Parse(kAvailabilityTimeOffsetSegmentList);
    CHECK_EQ(manifest->GetPeriodCount(), 2);
    const auto& adaptationSets0 = manifest->GetPeriod(0).adaptationSets;
    const auto& adaptationSets1 = manifest->GetPeriod(1).adaptationSets;
    CHECK_EQ(adaptationSets0.size(), 4u);
    CHECK_EQ(adaptationSets1.size(), 1u);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(0)), 2'000'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(1)), 3'210'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(2)), 1'230'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets0.at(3)), 100'000);
    CHECK_EQ(GetAvailabilityTimeOffsetUs(adaptationSets1.at(0)), 9'999'000);
}

TEST(DashManifestParser_baseUrl_absoluteBaseUrls_usesClosestBaseUrl) {
    auto manifest = Parse(kAvailabilityTimeOffsetBaseUrl);
    const auto& adaptationSets0 = manifest->GetPeriod(0).adaptationSets;
    auto first = [&](int set) -> const BaseUrl& { return adaptationSets0.at(std::size_t(set)).representations.at(0)->baseUrls.at(0); };
    CHECK_EQ(first(0).serviceLocation, std::string("period0"));
    CHECK_EQ(first(0).priority, 2);
    CHECK_EQ(first(0).weight, 20);
    CHECK_EQ(first(1).serviceLocation, std::string("adaptationSet1"));
    CHECK_EQ(first(1).priority, 3);
    CHECK_EQ(first(1).weight, 30);
    CHECK_EQ(first(2).serviceLocation, std::string("representation2"));
    CHECK_EQ(first(2).priority, 4);
    CHECK_EQ(first(2).weight, 40);
    CHECK_EQ(first(3).serviceLocation, std::string("http://video-foo.com/baseUrl/adaptationSet3"));
    CHECK_EQ(first(3).priority, 1);
    CHECK_EQ(first(3).weight, 1);
    CHECK_EQ(first(3).url, std::string("http://video-foo.com/baseUrl/representation3"));
}

namespace {
    bool EndsWith(const std::string& s, const std::string& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }
}

TEST(DashManifestParser_baseUrl_multipleBaseUrls_correctParsingAndUnfolding) {
    auto manifest = Parse(kMultipleBaseUrls);
    const auto& sets = manifest->GetPeriod(0).adaptationSets;
    const auto& audioBaseUrls = sets.at(0).representations.at(0)->baseUrls;
    CHECK_EQ(audioBaseUrls.size(), 6u);
    const char* audioSuffixes[] = { "/baseUrl/a/media/audio", "/baseUrl/b/media/audio", "/baseUrl/c/media/audio",
                                    "/baseUrl/a/files/audio", "/baseUrl/b/files/audio", "/baseUrl/c/files/audio" };
    const char* audioLocations[] = { "a", "b", "c", "a", "b", "c" };
    for (std::size_t i = 0; i < 6 && i < audioBaseUrls.size(); i++) {
        CHECK(EndsWith(audioBaseUrls[i].url, audioSuffixes[i]));
        CHECK_EQ(audioBaseUrls[i].serviceLocation, std::string(audioLocations[i]));
    }
    const auto& videoBaseUrls = sets.at(1).representations.at(0)->baseUrls;
    CHECK_EQ(videoBaseUrls.size(), 7u);
    const char* videoSuffixes[] = { "/baseUrl/a/media/video", "/baseUrl/b/media/video", "/baseUrl/c/media/video", "/baseUrl/a/files/video",
                                    "/baseUrl/b/files/video", "/baseUrl/c/files/video", "/baseUrl/d/alternative/" };
    const char* videoLocations[] = { "a", "b", "c", "a", "b", "c", "d" };
    for (std::size_t i = 0; i < 7 && i < videoBaseUrls.size(); i++) {
        CHECK(EndsWith(videoBaseUrls[i].url, videoSuffixes[i]));
        CHECK_EQ(videoBaseUrls[i].serviceLocation, std::string(videoLocations[i]));
    }
    const auto& textBaseUrls = sets.at(2).representations.at(0)->baseUrls;
    CHECK_EQ(textBaseUrls.size(), 1u);
    CHECK(EndsWith(textBaseUrls.at(0).url, "/baseUrl/e/text/"));
    CHECK_EQ(textBaseUrls.at(0).serviceLocation, std::string("e"));
}

TEST(DashManifestParser_baseUrl_relativeBaseUrlsNoDvbNamespace_hasDifferentPrioritiesAndServiceLocation) {
    auto manifest = Parse(kRelativeBaseUrlsDvbProfileNotDeclared);
    const auto& baseUrls = manifest->GetPeriod(0).adaptationSets.at(0).representations.at(0)->baseUrls;
    CHECK_EQ(baseUrls.at(0).priority, BaseUrl::PRIORITY_UNSET);
    CHECK_EQ(baseUrls.at(1).priority, BaseUrl::PRIORITY_UNSET);
    CHECK(baseUrls.at(0).serviceLocation != baseUrls.at(1).serviceLocation);
}

TEST(DashManifestParser_baseUrl_relativeBaseUrlsWithDvbNamespace_inheritsPrioritiesAndServiceLocation) {
    auto manifest = Parse(kRelativeBaseUrlsDvbProfileDeclared);
    const auto& baseUrls = manifest->GetPeriod(0).adaptationSets.at(0).representations.at(0)->baseUrls;
    CHECK_EQ(baseUrls.at(0).priority, baseUrls.at(1).priority);
    CHECK_EQ(baseUrls.at(0).serviceLocation, baseUrls.at(1).serviceLocation);
}

TEST(DashManifestParser_serviceDescriptionElement_allValuesSet) {
    auto manifest = Parse(kServiceDescriptionLowLatency);
    CHECK(manifest->serviceDescription.has_value());
    CHECK_EQ(manifest->serviceDescription->targetOffsetMs, 20'000);
    CHECK_EQ(manifest->serviceDescription->minOffsetMs, 1'000);
    CHECK_EQ(manifest->serviceDescription->maxOffsetMs, 30'000);
    CHECK_EQ(manifest->serviceDescription->minPlaybackSpeed, 0.1f);
    CHECK_EQ(manifest->serviceDescription->maxPlaybackSpeed, 99.0f);
}

TEST(DashManifestParser_serviceDescriptionElement_onlyPlaybackRates_latencyValuesUnset) {
    auto manifest = Parse(kServiceDescriptionLowLatencyOnlyPlaybackRates);
    CHECK(manifest->serviceDescription.has_value());
    CHECK_EQ(manifest->serviceDescription->targetOffsetMs, stream::kTimeUnset);
    CHECK_EQ(manifest->serviceDescription->minOffsetMs, stream::kTimeUnset);
    CHECK_EQ(manifest->serviceDescription->maxOffsetMs, stream::kTimeUnset);
    CHECK_EQ(manifest->serviceDescription->minPlaybackSpeed, 0.1f);
    CHECK_EQ(manifest->serviceDescription->maxPlaybackSpeed, 99.0f);
}

TEST(DashManifestParser_serviceDescriptionElement_onlyTargetLatency_playbackRatesAndMinMaxLatencyUnset) {
    auto manifest = Parse(kServiceDescriptionLowLatencyOnlyTargetLatency);
    CHECK(manifest->serviceDescription.has_value());
    CHECK_EQ(manifest->serviceDescription->targetOffsetMs, 20'000);
    CHECK_EQ(manifest->serviceDescription->minOffsetMs, stream::kTimeUnset);
    CHECK_EQ(manifest->serviceDescription->maxOffsetMs, stream::kTimeUnset);
    CHECK_EQ(manifest->serviceDescription->minPlaybackSpeed, kRateUnset);
    CHECK_EQ(manifest->serviceDescription->maxPlaybackSpeed, kRateUnset);
}

TEST(DashManifestParser_serviceDescriptionElement_noServiceDescription_isNullInManifest) {
    CHECK(!Parse(kAvailabilityTimeOffsetSegmentList)->serviceDescription.has_value());
}

TEST(DashManifestParser_contentProtections_withClearKeyLicenseUrl) {
    auto manifest = Parse(kClearKeyLicenseUrl);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    const Period& period = manifest->GetPeriod(0);
    CHECK_EQ(period.adaptationSets.size(), 2u);
    CHECK_EQ(period.adaptationSets.at(0).representations.size(), 1u);
    CHECK_EQ(period.adaptationSets.at(1).representations.size(), 1u);
    const Format& format0 = period.adaptationSets.at(0).representations.at(0)->format;
    const Format& format1 = period.adaptationSets.at(1).representations.at(0)->format;
    CHECK_EQ(format0.drmInitData.value().schemeType, std::string("cenc"));
    CHECK_EQ(format1.drmInitData.value().schemeType, std::string("cenc"));
    CHECK_EQ(format0.drmInitData->schemeDatas.size(), 1u);
    CHECK_EQ(format1.drmInitData->schemeDatas.size(), 1u);
    CHECK(format0.drmInitData->schemeDatas.at(0).uuid == stream::kClearkeyUuid);
    CHECK(format1.drmInitData->schemeDatas.at(0).uuid == stream::kClearkeyUuid);
    CHECK_EQ(format0.drmInitData->schemeDatas.at(0).licenseServerUrl, std::string("https://testserver1.test/AcquireLicense"));
    CHECK_EQ(format1.drmInitData->schemeDatas.at(0).licenseServerUrl, std::string("https://testserver2.test/AcquireLicense"));
}

TEST(DashManifestParser_contentProtection_withDashIfLicenseUrl) {
    auto manifest = Parse(kDashifLicenseUrl);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    const Period& period = manifest->GetPeriod(0);
    CHECK_EQ(period.adaptationSets.size(), 2u);
    const Format& format0 = period.adaptationSets.at(0).representations.at(0)->format;
    const Format& format1 = period.adaptationSets.at(1).representations.at(0)->format;
    CHECK_EQ(format0.drmInitData.value().schemeType, std::string("cenc"));
    CHECK_EQ(format1.drmInitData.value().schemeType, std::string("cenc"));
    CHECK_EQ(format0.drmInitData->schemeDatas.size(), 2u);
    CHECK_EQ(format1.drmInitData->schemeDatas.size(), 2u);
    CHECK(format0.drmInitData->schemeDatas.at(0).uuid == stream::kWidevineUuid);
    CHECK(format1.drmInitData->schemeDatas.at(0).uuid == stream::kWidevineUuid);
    CHECK_EQ(format0.drmInitData->schemeDatas.at(0).licenseServerUrl, std::string("https://testserver1.test/AcquireLicense"));
    CHECK_EQ(format1.drmInitData->schemeDatas.at(0).licenseServerUrl, std::string("https://testserver2.test/AcquireLicense"));
}

TEST(DashManifestParser_supplementalCodecs) {
    auto manifest = Parse(kSupplementalCodecs);
    CHECK_EQ(manifest->GetPeriodCount(), 1);
    const Period& period = manifest->GetPeriod(0);
    CHECK_EQ(period.adaptationSets.size(), 1u);
    CHECK_EQ(period.adaptationSets.at(0).representations.size(), 1u);
    const Format& format = period.adaptationSets.at(0).representations.at(0)->format;
    CHECK_EQ(format.sampleMimeType, std::string(MimeTypes::VIDEO_DOLBY_VISION));
    CHECK_EQ(format.codecs, std::string("dvh1.08.03"));
}

TEST(DashManifestParser_parse_multipleLocationsWithAbsoluteUrls_parsedCorrectly) {
    auto manifest = Parse(kMultipleLocationsAbsolute);
    CHECK_EQ(manifest->locations.size(), 2u);
    CHECK_EQ(manifest->locations.at(0).url, std::string("https://example.com/location1/manifest.mpd"));
    CHECK_EQ(manifest->locations.at(0).serviceLocation, std::string("https://example.com/location1/manifest.mpd"));
    CHECK_EQ(manifest->locations.at(1).url, std::string("https://example.com/location2/manifest.mpd"));
    CHECK_EQ(manifest->locations.at(1).serviceLocation, std::string("loc2"));
    CHECK_EQ(manifest->location, std::string("https://example.com/location2/manifest.mpd"));
}

TEST(DashManifestParser_parse_multipleLocationsWithRelativeUrls_parsedCorrectly) {
    auto manifest = Parse(kMultipleLocationsRelative);
    CHECK_EQ(manifest->locations.size(), 2u);
    CHECK_EQ(manifest->locations.at(0).url, std::string("https://example.com/location1/manifest.mpd"));
    CHECK_EQ(manifest->locations.at(0).serviceLocation, std::string("https://example.com/location1/manifest.mpd"));
    CHECK_EQ(manifest->locations.at(1).url, std::string("https://example.com/location2/manifest.mpd"));
    CHECK_EQ(manifest->locations.at(1).serviceLocation, std::string("loc2"));
    CHECK_EQ(manifest->location, std::string("https://example.com/location2/manifest.mpd"));
}
