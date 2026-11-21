// Ported from media3 exoplayer-rtsp: RtspMediaTrackTest (SDP media sections to formats and track
// URIs), with checks of the SDP parser's output.
#include "Check.h"

#include "kiln/media/codec/CodecUtil.h"
#include "kiln/media/rtsp/Rtp.h"

using namespace kiln::media;
using namespace kiln::media::rtsp;
using Bytes = std::vector<std::uint8_t>;
using Fmtp = std::map<std::string, std::string>;

namespace {
    const RtspHeaders kDescribeHeaders =
        RtspHeaders::Builder().AddAll(std::vector<std::string>{ "Accept: application/sdp", "CSeq: 3", "Content-Length: 707", "Transport: RTP/AVP;unicast;client_port=65458-65459\r\n" }).Build();
    template <typename F> bool ThrowsInvalidArgument(F&& f) {
        try {
            f();
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }
    MediaDescription::Builder Media(const char* type, int payloadType) {
        MediaDescription::Builder b(type, 0, MediaDescription::RTP_AVP_PROFILE, payloadType);
        b.SetConnection("IN IP4 0.0.0.0");
        return b;
    }
    const Bytes kSps = { 0, 0, 0, 1, 103, 100, 0, 31, 0xAC, 0xD9, 64, 0xF0, 17, 105, 0xB2, 0, 0, 3, 0, 8, 0, 0, 3, 1, 0x9C, 30, 48, 99, 44 };
    const Bytes kPps = { 0, 0, 0, 1, 104, 0xEB, 0xE3, 0xCB, 34, 0xC0 };
    Format H264Format() {
        Format f;
        f.sampleMimeType = stream::MimeTypes::VIDEO_H264;
        f.SetBitrates(500'000, -1);
        f.pixelWidthHeightRatio = 1;
        f.height = 544;
        f.width = 960;
        f.codecs = "avc1.64001F";
        f.colorRange = 1;   // C.COLOR_RANGE_FULL
        f.initializationData = { kSps, kPps };
        return f;
    }
    MediaDescription GenericWithControl(const std::string& control) {
        return Media(MediaDescription::MEDIA_TYPE_AUDIO, 97).SetBitrate(48'000).AddAttribute("rtpmap", "97 AC3/48000/6").AddAttribute("control", control).Build();
    }
}

TEST(RtspMediaTrack_h264) {
    for (const char* tail : { "", ";" }) {   // a trailing semicolon too
        const MediaDescription d = Media(MediaDescription::MEDIA_TYPE_VIDEO, 96)
                                       .SetBitrate(500'000)
                                       .AddAttribute("rtpmap", "96 H264/90000")
                                       .AddAttribute("fmtp", std::string("96 packetization-mode=1;profile-level-id=64001F;sprop-parameter-sets=Z2QAH6zZQPARabIAAAMACAAAAwGcHjBjLA==,aOvjyyLA") + tail)
                                       .AddAttribute("control", "track1")
                                       .Build();
        const RtpPayloadFormat format = RtspMediaTrack::GeneratePayloadFormat(d);
        CHECK(format.format == H264Format());
        CHECK(format.rtpPayloadType == 96 && format.clockRate == 90'000 && format.mediaEncoding == RtpPayloadFormat::RTP_MEDIA_H264);
        CHECK(format.fmtpParameters == (Fmtp{ { "packetization-mode", "1" }, { "profile-level-id", "64001F" }, { "sprop-parameter-sets", "Z2QAH6zZQPARabIAAAMACAAAAwGcHjBjLA==,aOvjyyLA" } }));
    }
    // Without profile-level-id: the codecs string from the SPS.
    const MediaDescription noProfile = Media(MediaDescription::MEDIA_TYPE_VIDEO, 96)
                                           .SetBitrate(500'000)
                                           .AddAttribute("rtpmap", "96 H264/90000")
                                           .AddAttribute("fmtp", "96 packetization-mode=1;sprop-parameter-sets=Z2QAH6zZQPARabIAAAMACAAAAwGcHjBjLA==,aOvjyyLA")
                                           .AddAttribute("control", "track1")
                                           .Build();
    CHECK_EQ(RtspMediaTrack::GeneratePayloadFormat(noProfile).format.codecs, std::string("avc1.64001F"));
    CHECK(ThrowsInvalidArgument([] {
        RtspMediaTrack::GeneratePayloadFormat(Media(MediaDescription::MEDIA_TYPE_VIDEO, 96).SetBitrate(500'000).AddAttribute("rtpmap", "96 H264/90000").AddAttribute("control", "track1").Build());
    }));
    CHECK(ThrowsInvalidArgument([] {
        RtspMediaTrack::GeneratePayloadFormat(Media(MediaDescription::MEDIA_TYPE_VIDEO, 96)
                                                  .SetBitrate(500'000)
                                                  .AddAttribute("rtpmap", "96 H264/90000")
                                                  .AddAttribute("fmtp", "96 packetization-mode=1;profile-level-id=64001F")
                                                  .AddAttribute("control", "track1")
                                                  .Build());
    }));
}

TEST(RtspMediaTrack_staticAudioPayloadTypes) {
    struct Case { int payloadType; const char* mime; int channels, rate, pcmEncoding; const char* encoding; };
    for (const Case& c : { Case{ 0, kMimeAudioMlaw, 1, 8000, -1, RtpPayloadFormat::RTP_MEDIA_PCMU }, Case{ 8, kMimeAudioAlaw, 1, 8000, -1, RtpPayloadFormat::RTP_MEDIA_PCMA },
                           Case{ 10, kMimeAudioRaw, 2, 44100, kEncodingPcm16BitBigEndian, RtpPayloadFormat::RTP_MEDIA_PCM_L16 },
                           Case{ 11, kMimeAudioRaw, 1, 44100, kEncodingPcm16BitBigEndian, RtpPayloadFormat::RTP_MEDIA_PCM_L16 } }) {
        const RtpPayloadFormat format = RtspMediaTrack::GeneratePayloadFormat(Media(MediaDescription::MEDIA_TYPE_AUDIO, c.payloadType).AddAttribute("control", "track2").Build());
        Format expected;
        expected.sampleMimeType = c.mime;
        expected.channelCount = c.channels;
        expected.sampleRate = c.rate;
        expected.pcmEncoding = c.pcmEncoding;
        CHECK(format.format == expected && format.rtpPayloadType == c.payloadType && format.clockRate == c.rate && format.fmtpParameters.empty() && format.mediaEncoding == c.encoding);
    }
}

TEST(RtspMediaTrack_aacAndAc3) {
    const MediaDescription aac = Media(MediaDescription::MEDIA_TYPE_AUDIO, 97)
                                     .SetBitrate(96'000)
                                     .AddAttribute("rtpmap", "97 MPEG4-GENERIC/44100")
                                     .AddAttribute("fmtp", "97 streamtype=5; profile-level-id=1; mode=AAC-hbr; sizelength=13; indexlength=3; indexdeltalength=3; config=1208")
                                     .AddAttribute("control", "track2")
                                     .Build();
    const RtpPayloadFormat format = RtspMediaTrack::GeneratePayloadFormat(aac);
    Format expected;
    expected.sampleMimeType = stream::MimeTypes::AUDIO_AAC;
    expected.channelCount = 1;
    expected.sampleRate = 44100;
    expected.SetBitrates(96'000, -1);
    expected.codecs = "mp4a.40.1";
    expected.initializationData = { codec::AacUtil::BuildAacLcAudioSpecificConfig(44'100, 1) };
    CHECK(format.format == expected && format.rtpPayloadType == 97 && format.clockRate == 44'100);
    CHECK(format.fmtpParameters == (Fmtp{ { "streamtype", "5" }, { "profile-level-id", "1" }, { "mode", "AAC-hbr" }, { "sizelength", "13" }, { "indexlength", "3" },
                                          { "indexdeltalength", "3" }, { "config", "1208" } }));
    for (auto [rtpmap, channels] : { std::pair{ "97 AC3/48000", 6 }, std::pair{ "97 AC3/48000/2", 2 } }) {
        const RtpPayloadFormat ac3 =
            RtspMediaTrack::GeneratePayloadFormat(Media(MediaDescription::MEDIA_TYPE_AUDIO, 97).SetBitrate(48'000).AddAttribute("rtpmap", rtpmap).AddAttribute("control", "track2").Build());
        Format e;
        e.sampleMimeType = stream::MimeTypes::AUDIO_AC3;
        e.channelCount = channels;
        e.sampleRate = 48000;
        e.SetBitrates(48'000, -1);
        CHECK(ac3.format == e && ac3.clockRate == 48000 && ac3.mediaEncoding == RtpPayloadFormat::RTP_MEDIA_AC3);
    }
    // MP4A-LATM without profile-level-id: 30.
    const MediaDescription latm = Media(MediaDescription::MEDIA_TYPE_AUDIO, 96)
                                      .SetBitrate(96'000)
                                      .AddAttribute("rtpmap", "96 MP4A-LATM/44100")
                                      .AddAttribute("fmtp", "96 config=40002410adca00; cpresent=0")
                                      .AddAttribute("control", "track1")
                                      .Build();
    CHECK_EQ(RtspMediaTrack::GeneratePayloadFormat(latm).format.codecs, std::string("mp4a.40.30"));
    CHECK(ThrowsInvalidArgument([] {
        RtspMediaTrack::GeneratePayloadFormat(Media(MediaDescription::MEDIA_TYPE_AUDIO, 97).SetBitrate(96'000).AddAttribute("rtpmap", "97 MPEG4-GENERIC/44100").AddAttribute("control", "track2").Build());
    }));
    for (const char* fmtp : { "97 streamtype=5;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1208",
                              "97 streamtype=5;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1208;profile-level-id=;" })
        CHECK(ThrowsInvalidArgument([&] {
            RtspMediaTrack::GeneratePayloadFormat(Media(MediaDescription::MEDIA_TYPE_AUDIO, 97)
                                                      .SetBitrate(96'000)
                                                      .AddAttribute("rtpmap", "97 MPEG4-GENERIC/44100")
                                                      .AddAttribute("fmtp", fmtp)
                                                      .AddAttribute("control", "track2")
                                                      .Build());
        }));
}

TEST(RtspMediaTrack_trackUris) {
    auto uri = [](const RtspHeaders& headers, const std::string& control) { return RtspMediaTrack(headers, GenericWithControl(control), "rtsp://test.com").uri; };
    CHECK_EQ(uri(kDescribeHeaders, "path1/track2"), std::string("rtsp://test.com/path1/track2"));
    CHECK_EQ(uri(kDescribeHeaders, "rtsp://test.com/foo"), std::string("rtsp://test.com/foo"));
    CHECK_EQ(uri(kDescribeHeaders, "*"), std::string("rtsp://test.com"));
    auto with = [](std::vector<std::string> lines) { return kDescribeHeaders.BuildUpon().AddAll(lines).Build(); };
    CHECK_EQ(uri(with({ "Content-Base: rtsp://test.com/path1" }), "path2/track3"), std::string("rtsp://test.com/path1/path2/track3"));
    CHECK_EQ(uri(with({ "Content-Location: rtsp://test.com/path1" }), "path2/track3"), std::string("rtsp://test.com/path1/path2/track3"));
    CHECK_EQ(uri(with({ "Content-Base: rtsp://test.com/path1", "Content-Location: rtsp://test.com/path2" }), "path2/track3"), std::string("rtsp://test.com/path1/path2/track3"));
    CHECK_EQ(uri(with({ "Content-Base:", "Content-Location: rtsp://test.com/path1" }), "path2/track3"), std::string("rtsp://test.com/path1/path2/track3"));
    CHECK_EQ(uri(with({ "Content-Location:" }), "path2/track3"), std::string("rtsp://test.com/path2/track3"));
    CHECK_EQ(uri(with({ "Content-Base: rtsp://test.com/path1" }), "rtsp://test.com/foo"), std::string("rtsp://test.com/foo"));
}

// The SDP parser on a DESCRIBE body: sessions and media sections with their attributes.
TEST(SessionDescriptionParser_parsesMediaSections) {
    const SessionDescription s = SessionDescriptionParser::Parse(
        "v=0\r\no=- 1606776316530225 1 IN IP4 192.168.2.176\r\ns=Test\r\ni=imax.mkv\r\nt=0 0\r\nb=AS:2000\r\na=range:npt=0-60\r\n"
        "m=video 0 RTP/AVP 96\r\nc=IN IP4 0.0.0.0\r\nb=AS:500\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1\r\na=control:track1\r\n"
        "m=audio 0 RTP/AVP 0\r\na=control:track2\r\nm=application 0 RTP/AVP xyz\r\na=control:track3\r\n");
    CHECK(s.origin == "- 1606776316530225 1 IN IP4 192.168.2.176" && s.sessionName == "Test" && s.sessionInfo == "imax.mkv" && s.timing == "0 0");
    CHECK(s.bitrate == 2'000'000 && s.attributes.at("range") == "npt=0-60");
    CHECK_EQ(s.mediaDescriptionList.size(), std::size_t(2));   // the bad m= line is skipped with its lines
    const MediaDescription& video = s.mediaDescriptionList[0];
    CHECK(video.mediaType == "video" && video.payloadType == 96 && video.bitrate == 500'000 && video.connection == "IN IP4 0.0.0.0");
    CHECK(video.rtpMapAttribute == (MediaDescription::RtpMapAttribute{ 96, "H264", 90000, -1 }) && video.attributes.at("control") == "track1");
    CHECK(video.GetFmtpParametersAsMap() == (Fmtp{ { "packetization-mode", "1" } }));
    const MediaDescription& audio = s.mediaDescriptionList[1];
    CHECK(audio.rtpMapAttribute == (MediaDescription::RtpMapAttribute{ 0, "PCMU", 8000, 1 }));
    bool threw = false;
    try {
        SessionDescriptionParser::Parse("v=1\r\n");
    } catch (const stream::ParserException&) {
        threw = true;
    }
    CHECK(threw);
}
