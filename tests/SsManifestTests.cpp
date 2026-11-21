// Ported from media3 exoplayer-smoothstreaming's SsManifestParserTest and SsManifestTest, with
// checks of what the sample manifests hold.
#include "Check.h"
#include "SsManifestTestData.h"

#include "kiln/media/smoothstreaming/SsManifest.h"

#include <algorithm>
#include <random>

using namespace kiln::media;
using namespace kiln::media::ss;
using kiln::media::stream::StreamKey;

namespace {
    template <typename F> bool ThrowsParserException(F&& f) {
        try {
            f();
        } catch (const stream::ParserException&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }
}

TEST(SsManifestParser_withMalformedManifest_throwsParserException) {
    std::mt19937 random(0);
    std::string data(100, '\0');
    for (char& c : data) c = char(random() & 0xff);
    CHECK(ThrowsParserException([&] { SsManifestParser().Parse("http://test.test", data); }));
}

TEST(SsManifestParser_parseSmoothStreamingManifest) {
    SsManifestParser().Parse("https://example.com/test.ismc", ss_test_data::kSampleIsmc1);
    SsManifestParser().Parse("https://example.com/test.ismc", ss_test_data::kSampleIsmc2);
}

TEST(SsManifestParser_populatesFormatLabelWithStreamIndexName) {
    const SsManifest m = SsManifestParser().Parse("https://example.com/test.ismc", ss_test_data::kSampleIsmc1);
    CHECK_EQ(m.streamElements[0].formats[0].label, std::string("video"));
    CHECK_EQ(m.streamElements[0].formats[0].labels.at(0).value, std::string("video"));
}

TEST(SsManifestParser_readsTheSample) {
    const SsManifest m = SsManifestParser().Parse("https://example.com/test.ismc", ss_test_data::kSampleIsmc1);
    CHECK(m.majorVersion == 2 && m.minorVersion == 0 && !m.isLive);
    CHECK_EQ(m.durationUs, std::int64_t(230'000'000));
    CHECK_EQ(m.dvrWindowLengthUs, kTimeUnset);
    CHECK(m.protectionElement && m.protectionElement->uuid == stream::kPlayreadyUuid);
    CHECK_EQ(m.protectionElement->trackEncryptionBoxes.at(0).keyId.size(), std::size_t(16));
    const SsManifest::StreamElement& video = m.streamElements.at(0);
    CHECK(video.type == TrackType::Video && video.maxWidth == 720 && video.maxHeight == 480 && video.timescale == 10'000'000);
    CHECK_EQ(video.formats.size(), std::size_t(2));
    CHECK(video.formats[0].id == "0" && video.formats[0].bitrate == 1'536'000 && video.formats[0].width == 720);
    CHECK(video.formats[0].drmInitData.has_value());   // the protection applies to audio and video
    // Not a start code: the private data stays one buffer.
    CHECK_EQ(video.formats[0].initializationData.size(), std::size_t(1));
    CHECK_EQ(video.chunkCount, 3);
    CHECK_EQ(video.GetStartTimeUs(1), std::int64_t(1'968'000));
    CHECK_EQ(video.GetChunkDurationUs(1), std::int64_t(898'000));
    CHECK_EQ(video.GetChunkDurationUs(2), std::int64_t(898'000));   // the last chunk's own d
    CHECK_EQ(video.GetChunkIndex(2'000'000), 1);
    CHECK_EQ(video.GetChunkIndex(0), 0);
    CHECK_EQ(video.BuildRequestUri(1, 2), std::string("https://example.com/QualityLevels(307200,{CustomAttributes})/Fragments(video=28660000)"));
}

TEST(SsManifestParser_codecPrivateDataAndRepeats) {
    const std::string manifest = R"ISM(<?xml version="1.0"?>
<SmoothStreamingMedia MajorVersion="2" MinorVersion="2" Duration="0" IsLive="TRUE" DVRWindowLength="600000000" LookaheadCount="2">
  <StreamIndex Type="video" Url="QualityLevels({bitrate})/Fragments(video={start time})" Name="v">
    <QualityLevel Index="0" Bitrate="500000" FourCC="AVC1" MaxWidth="640" MaxHeight="360" CodecPrivateData="00000001674D401FE8802802DD80B501010140000003004000000C83C60C44800000000168EBEF20"/>
    <c t="100" d="20000000" r="3"/>
    <c d="10000000"/>
  </StreamIndex>
  <StreamIndex Type="audio" Url="QualityLevels({bitrate})/Fragments(audio={start time})" Language="deu" TimeScale="1000">
    <QualityLevel Index="0" Bitrate="128000" FourCC="XYZW" SamplingRate="48000" Channels="2"/>
    <c t="0" d="2000"/>
  </StreamIndex>
</SmoothStreamingMedia>)ISM";
    const SsManifest m = SsManifestParser().Parse("https://example.com/live.isml/Manifest", manifest);
    CHECK(m.isLive && m.lookAheadCount == 2);
    CHECK_EQ(m.durationUs, kTimeUnset);
    CHECK_EQ(m.dvrWindowLengthUs, std::int64_t(60'000'000));
    const auto& video = m.streamElements.at(0);
    // H.264's private data splits into its NAL units (start codes kept).
    CHECK_EQ(video.formats[0].sampleMimeType, std::string(stream::MimeTypes::VIDEO_H264));
    CHECK_EQ(video.formats[0].initializationData.size(), std::size_t(2));
    CHECK(video.formats[0].initializationData[1].size() == 8 && video.formats[0].initializationData[1][4] == 0x68);
    CHECK(!video.formats[0].drmInitData);
    // r repeats a chunk; a chunk without t follows the one before.
    CHECK_EQ(video.chunkCount, 4);
    CHECK_EQ(video.GetStartTimeUs(2), std::int64_t(4'000'010));
    CHECK_EQ(video.GetStartTimeUs(3), std::int64_t(6'000'010));
    CHECK_EQ(video.GetChunkDurationUs(3), std::int64_t(1'000'000));
    CHECK_EQ(video.BuildRequestUri(0, 1), std::string("https://example.com/live.isml/QualityLevels(500000)/Fragments(video=20000100)"));
    // Audio of an unknown FourCC is AAC; without private data, an AAC LC config from its rate.
    const auto& audio = m.streamElements.at(1);
    CHECK(audio.type == TrackType::Audio && audio.timescale == 1000 && audio.language == "deu");
    CHECK(audio.formats[0].sampleMimeType == stream::MimeTypes::AUDIO_AAC && audio.formats[0].language == "deu");
    CHECK(audio.formats[0].initializationData == (std::vector<std::vector<std::uint8_t>>{ { 0x11, 0x90 } }));
    CHECK_EQ(audio.GetChunkDurationUs(0), std::int64_t(2'000'000));
}

TEST(SsManifestParser_missingFields) {
    CHECK(ThrowsParserException([&] { SsManifestParser().Parse("u", R"(<SmoothStreamingMedia MajorVersion="2" Duration="1"/>)"); }));
    CHECK(ThrowsParserException([&] {
        SsManifestParser().Parse("u", R"(<SmoothStreamingMedia MajorVersion="2" MinorVersion="0" Duration="1"><StreamIndex Url="x"/></SmoothStreamingMedia>)");
    }));
    // A start time that cannot be inferred (the chunk before has d -1, C.INDEX_UNSET).
    CHECK(ThrowsParserException([&] {
        SsManifestParser().Parse("u", R"(<SmoothStreamingMedia MajorVersion="2" MinorVersion="0" Duration="1"><StreamIndex Type="video" Url="x"><c t="0" d="-1"/><c/></StreamIndex></SmoothStreamingMedia>)");
    }));
}

namespace {
    // SsTestUtils.createStreamElement / createSsManifest.
    SsManifest::StreamElement CreateStreamElement(const std::string& name, std::vector<Format> formats) {
        return SsManifest::StreamElement("baseUri", "chunkTemplate", TrackType::Video, "subType", 1000, name, 1024, 768, 1024, 768, "", std::move(formats),
                                         {}, 0);
    }
    SsManifest CreateSsManifest(std::vector<SsManifest::StreamElement> elements) {
        SsManifest::ProtectionElement protection;
        protection.uuid = stream::kUuidNil;
        protection.data = { 0, 1, 2 };
        return SsManifest(1, 2, 1000, 5000, 0, 2, false, protection, std::move(elements));
    }
    std::vector<std::vector<Format>> NewFormats(int elements, int tracks) {
        auto formats = std::vector<std::vector<Format>>(std::size_t(elements));
        for (int i = 0; i < elements; ++i)
            for (int j = 0; j < tracks; ++j) {
                Format f;
                f.id = std::to_string(i) + "." + std::to_string(j);
                f.containerMimeType = stream::MimeTypes::VIDEO_MP4;
                f.sampleMimeType = stream::MimeTypes::VIDEO_H264;
                formats[std::size_t(i)].push_back(f);
            }
        return formats;
    }
    void CheckManifestEquals(const SsManifest& expected, const SsManifest& actual) {
        CHECK(actual.durationUs == expected.durationUs && actual.dvrWindowLengthUs == expected.dvrWindowLengthUs && actual.isLive == expected.isLive);
        CHECK(actual.lookAheadCount == expected.lookAheadCount && actual.majorVersion == expected.majorVersion && actual.minorVersion == expected.minorVersion);
        CHECK(actual.protectionElement == expected.protectionElement);
        CHECK_EQ(actual.streamElements.size(), expected.streamElements.size());
        for (std::size_t i = 0; i < expected.streamElements.size(); ++i) {
            const auto& e = expected.streamElements[i];
            const auto& a = actual.streamElements[i];
            CHECK(a.chunkCount == e.chunkCount && a.displayHeight == e.displayHeight && a.displayWidth == e.displayWidth && a.language == e.language);
            CHECK(a.maxHeight == e.maxHeight && a.maxWidth == e.maxWidth && a.name == e.name && a.subType == e.subType && a.timescale == e.timescale);
            CHECK(a.type == e.type && a.formats == e.formats);
        }
    }
}

TEST(SsManifest_copy) {
    const auto formats = NewFormats(2, 3);
    const SsManifest source = CreateSsManifest({ CreateStreamElement("1", formats[0]), CreateStreamElement("2", formats[1]) });
    std::vector<StreamKey> keys = { { 0, 0, 0 }, { 0, 0, 2 }, { 0, 1, 0 } };
    std::shuffle(keys.begin(), keys.end(), std::mt19937(0));   // keys need not be in order
    const SsManifest copy = source.Copy(keys);
    const SsManifest expected = CreateSsManifest({ CreateStreamElement("1", { formats[0][0], formats[0][2] }), CreateStreamElement("2", { formats[1][0] }) });
    CheckManifestEquals(expected, copy);
}

TEST(SsManifest_copyRemoveStreamElement) {
    const auto formats = NewFormats(2, 3);
    const SsManifest source = CreateSsManifest({ CreateStreamElement("1", formats[0]), CreateStreamElement("2", formats[1]) });
    const SsManifest copy = source.Copy({ { 0, 1, 0 } });
    CheckManifestEquals(CreateSsManifest({ CreateStreamElement("2", { formats[1][0] }) }), copy);
}
