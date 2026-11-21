// Ported from media3-common UriUtilTest, UtilTest and MimeTypesTest (the parts kiln's stream
// helpers implement). Expected values are upstream's; a null string is "" here.
#include "Check.h"

#include "kiln/media/stream/StreamUtil.h"

#include <string>

using namespace kiln::media;
using namespace kiln::media::stream;
using M = std::string;

TEST(StreamUriUtil_resolveNormal) {
    const std::string base = "http://a/b/c/d;p?q";
    CHECK_EQ(ResolveUri(base, "g:h"), M("g:h"));
    CHECK_EQ(ResolveUri(base, "g"), M("http://a/b/c/g"));
    CHECK_EQ(ResolveUri(base, "g/"), M("http://a/b/c/g/"));
    CHECK_EQ(ResolveUri(base, "/g"), M("http://a/g"));
    CHECK_EQ(ResolveUri(base, "//g"), M("http://g"));
    CHECK_EQ(ResolveUri(base, "//g:80"), M("http://g:80"));
    CHECK_EQ(ResolveUri(base, "?y"), M("http://a/b/c/d;p?y"));
    CHECK_EQ(ResolveUri(base, "g?y"), M("http://a/b/c/g?y"));
    CHECK_EQ(ResolveUri(base, "#s"), M("http://a/b/c/d;p?q#s"));
    CHECK_EQ(ResolveUri(base, "g#s"), M("http://a/b/c/g#s"));
    CHECK_EQ(ResolveUri(base, "g?y#s"), M("http://a/b/c/g?y#s"));
    CHECK_EQ(ResolveUri(base, ";x"), M("http://a/b/c/;x"));
    CHECK_EQ(ResolveUri(base, "g;x"), M("http://a/b/c/g;x"));
    CHECK_EQ(ResolveUri(base, "g;x?y#s"), M("http://a/b/c/g;x?y#s"));
    CHECK_EQ(ResolveUri(base, ""), M("http://a/b/c/d;p?q"));
    CHECK_EQ(ResolveUri(base, "."), M("http://a/b/c/"));
    CHECK_EQ(ResolveUri(base, "./"), M("http://a/b/c/"));
    CHECK_EQ(ResolveUri(base, ".."), M("http://a/b/"));
    CHECK_EQ(ResolveUri(base, "../"), M("http://a/b/"));
    CHECK_EQ(ResolveUri(base, "../g"), M("http://a/b/g"));
    CHECK_EQ(ResolveUri(base, "../.."), M("http://a/"));
    CHECK_EQ(ResolveUri(base, "../../"), M("http://a/"));
    CHECK_EQ(ResolveUri(base, "../../g"), M("http://a/g"));
}

TEST(StreamUriUtil_resolveAbnormal) {
    const std::string base = "http://a/b/c/d;p?q";
    CHECK_EQ(ResolveUri(base, "../../../g"), M("http://a/g"));
    CHECK_EQ(ResolveUri(base, "../../../../g"), M("http://a/g"));
    CHECK_EQ(ResolveUri(base, "/./g"), M("http://a/g"));
    CHECK_EQ(ResolveUri(base, "/../g"), M("http://a/g"));
    CHECK_EQ(ResolveUri(base, "g."), M("http://a/b/c/g."));
    CHECK_EQ(ResolveUri(base, ".g"), M("http://a/b/c/.g"));
    CHECK_EQ(ResolveUri(base, "g.."), M("http://a/b/c/g.."));
    CHECK_EQ(ResolveUri(base, "..g"), M("http://a/b/c/..g"));
    CHECK_EQ(ResolveUri(base, "./../g"), M("http://a/b/g"));
    CHECK_EQ(ResolveUri(base, "./g/."), M("http://a/b/c/g/"));
    CHECK_EQ(ResolveUri(base, "g/./h"), M("http://a/b/c/g/h"));
    CHECK_EQ(ResolveUri(base, "g/../h"), M("http://a/b/c/h"));
    CHECK_EQ(ResolveUri(base, "g;x=1/./y"), M("http://a/b/c/g;x=1/y"));
    CHECK_EQ(ResolveUri(base, "g;x=1/../y"), M("http://a/b/c/y"));
    CHECK_EQ(ResolveUri(base, "g?y/./x"), M("http://a/b/c/g?y/./x"));
    CHECK_EQ(ResolveUri(base, "g?y/../x"), M("http://a/b/c/g?y/../x"));
    CHECK_EQ(ResolveUri(base, "g#s/./x"), M("http://a/b/c/g#s/./x"));
    CHECK_EQ(ResolveUri(base, "g#s/../x"), M("http://a/b/c/g#s/../x"));
    CHECK_EQ(ResolveUri(base, "http:g"), M("http:g"));
}

TEST(StreamUriUtil_resolveAbnormalAdditional) {
    CHECK_EQ(ResolveUri("http://a/b", "c:d/../e"), M("c:e"));
    CHECK_EQ(ResolveUri("a:b", "../c"), M("a:c"));
}

TEST(StreamUriUtil_isAbsolute) {
    CHECK(IsAbsoluteUri("fo://bar"));
    CHECK(!IsAbsoluteUri(""));
    CHECK(!IsAbsoluteUri("      "));
    CHECK(!IsAbsoluteUri("//www.google.com"));
    CHECK(!IsAbsoluteUri("//www.google.com:80"));
    CHECK(!IsAbsoluteUri("/path/to/file"));
    CHECK(!IsAbsoluteUri("path/to/file"));
}

TEST(StreamUtil_parseXsDateTime_returnsParsedDateTimeInMillis) {
    for (const char* separator : { "T", "t", " " }) {
        const std::string s = separator;
        CHECK_EQ(ParseXsDateTime("2014-06-19" + s + "23:07:42"), 1403219262000LL);
        CHECK_EQ(ParseXsDateTime("2014-08-06" + s + "11:00:00Z"), 1407322800000LL);
        CHECK_EQ(ParseXsDateTime("2014-08-06" + s + "11:00:00,000Z"), 1407322800000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55-08:00"), 1411161535000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55+0100"), 1411129135000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55.000-0800"), 1411161535000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55.000-800"), 1411161535000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55-08"), 1411161535000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55.000-8"), 1411161535000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55+01"), 1411129135000LL);
        CHECK_EQ(ParseXsDateTime("2014-09-19" + s + "13:18:55+1"), 1411129135000LL);
    }
}

TEST(StreamUtil_getCodecsOfType) {
    CHECK_EQ(GetCodecsOfType("", TrackType::Video), M(""));
    CHECK_EQ(GetCodecsOfType("avc1.64001e,vp9.63.1", TrackType::Audio), M(""));
    CHECK_EQ(GetCodecsOfType(" vp9.63.1, ec-3 ", TrackType::Audio), M("ec-3"));
    CHECK_EQ(GetCodecsOfType("avc1.61e, vp9.63.1, ec-3 ", TrackType::Video), M("avc1.61e,vp9.63.1"));
    CHECK_EQ(GetCodecsOfType("invalidCodec1, invalidCodec2 ", TrackType::Audio), M(""));
}

TEST(StreamMimeTypes_isText_returnsCorrectResult) {
    for (const char* m : { MimeTypes::TEXT_VTT, MimeTypes::APPLICATION_CEA608, MimeTypes::APPLICATION_CEA708, MimeTypes::APPLICATION_MP4CEA608,
                           MimeTypes::APPLICATION_SUBRIP, MimeTypes::APPLICATION_TTML, MimeTypes::APPLICATION_TX3G, MimeTypes::APPLICATION_MP4VTT,
                           MimeTypes::APPLICATION_VOBSUB, MimeTypes::APPLICATION_PGS, MimeTypes::APPLICATION_DVBSUBS, "text/x-ssa", "text/custom" })
        CHECK(MimeTypes::IsText(m));
    for (const char* m : { MimeTypes::VIDEO_MP4, MimeTypes::VIDEO_H264, MimeTypes::AUDIO_MP4, MimeTypes::AUDIO_AAC, MimeTypes::IMAGE_JPEG, "application/custom" })
        CHECK(!MimeTypes::IsText(m));
}

TEST(StreamMimeTypes_isImage_returnsCorrectResult) {
    for (const char* m : { MimeTypes::IMAGE_JPEG, "image/png", "image/heif", "image/heic", "image/avif", "image/bmp", "image/webp", "image/custom",
                           "application/x-image-uri" })
        CHECK(MimeTypes::IsImage(m));
    for (const char* m : { MimeTypes::VIDEO_MP4, MimeTypes::AUDIO_AAC, "application/custom" }) CHECK(!MimeTypes::IsImage(m));
}

TEST(StreamMimeTypes_getTrackType_returnsCorrectResult) {
    CHECK(MimeTypes::GetTrackType(MimeTypes::VIDEO_H264) == TrackType::Video);
    CHECK(MimeTypes::GetTrackType("video/custom") == TrackType::Video);
    CHECK(MimeTypes::GetTrackType(MimeTypes::AUDIO_AAC) == TrackType::Audio);
    CHECK(MimeTypes::GetTrackType("audio/custom") == TrackType::Audio);
    CHECK(MimeTypes::GetTrackType("text/x-ssa") == TrackType::Text);
    CHECK(MimeTypes::GetTrackType("text/custom") == TrackType::Text);
    CHECK(MimeTypes::GetTrackType(MimeTypes::IMAGE_JPEG) == TrackType::Image);
    CHECK(MimeTypes::GetTrackType("image/custom") == TrackType::Image);
    CHECK(MimeTypes::GetTrackType(MimeTypes::APPLICATION_CEA608) == TrackType::Text);
    CHECK(MimeTypes::GetTrackType(MimeTypes::APPLICATION_EMSG) == TrackType::Metadata);
    CHECK(MimeTypes::GetTrackType("application/vnd.dvb.ait") == TrackType::Metadata);
    CHECK(MimeTypes::GetTrackType("application/custom") == TrackType::Unknown);
}

TEST(StreamMimeTypes_getMediaMimeType_fromValidCodecs_returnsCorrectMimeType) {
    const std::pair<const char*, const char*> cases[] = {
        { "avc1", MimeTypes::VIDEO_H264 },          { "avc1.42E01E", MimeTypes::VIDEO_H264 },      { "avc1.42E01F", MimeTypes::VIDEO_H264 },
        { "avc1.4D401F", MimeTypes::VIDEO_H264 },   { "avc1.4D4028", MimeTypes::VIDEO_H264 },      { "avc1.640028", MimeTypes::VIDEO_H264 },
        { "avc1.640029", MimeTypes::VIDEO_H264 },   { "avc3", MimeTypes::VIDEO_H264 },             { "hev1", MimeTypes::VIDEO_H265 },
        { "hvc1", MimeTypes::VIDEO_H265 },          { "vp08", MimeTypes::VIDEO_VP8 },              { "vp8", MimeTypes::VIDEO_VP8 },
        { "vp09", MimeTypes::VIDEO_VP9 },           { "vp9", MimeTypes::VIDEO_VP9 },               { "ac-3", MimeTypes::AUDIO_AC3 },
        { "dac3", MimeTypes::AUDIO_AC3 },           { "dec3", MimeTypes::AUDIO_E_AC3 },            { "ec-3", MimeTypes::AUDIO_E_AC3 },
        { "ec+3", MimeTypes::AUDIO_E_AC3_JOC },     { "dtsc", MimeTypes::AUDIO_DTS },              { "dtse", MimeTypes::AUDIO_DTS_EXPRESS },
        { "dtsh", MimeTypes::AUDIO_DTS_HD },        { "dtsl", MimeTypes::AUDIO_DTS_HD },           { "dtsx", MimeTypes::AUDIO_DTS_UHD_P2 },
        { "opus", MimeTypes::AUDIO_OPUS },          { "vorbis", MimeTypes::AUDIO_VORBIS },         { "mp4a", MimeTypes::AUDIO_AAC },
        { "mp4a.40.02", MimeTypes::AUDIO_AAC },     { "mp4a.40.05", MimeTypes::AUDIO_AAC },        { "mp4a.40.2", MimeTypes::AUDIO_AAC },
        { "mp4a.40.5", MimeTypes::AUDIO_AAC },      { "mp4a.40.29", MimeTypes::AUDIO_AAC },        { "mp4a.66", MimeTypes::AUDIO_AAC },
        { "mp4a.67", MimeTypes::AUDIO_AAC },        { "mp4a.68", MimeTypes::AUDIO_AAC },           { "mp4a.69", MimeTypes::AUDIO_MPEG },
        { "mp4a.6B", MimeTypes::AUDIO_MPEG },       { "mp4a.a5", MimeTypes::AUDIO_AC3 },           { "mp4a.A5", MimeTypes::AUDIO_AC3 },
        { "mp4a.a6", MimeTypes::AUDIO_E_AC3 },      { "mp4a.A6", MimeTypes::AUDIO_E_AC3 },         { "mp4a.A9", MimeTypes::AUDIO_DTS },
        { "mp4a.AC", MimeTypes::AUDIO_DTS },        { "mp4a.AA", MimeTypes::AUDIO_DTS_HD },        { "mp4a.AB", MimeTypes::AUDIO_DTS_HD },
        { "mp4a.AD", MimeTypes::AUDIO_OPUS },       { "wvtt", MimeTypes::TEXT_VTT },               { "stpp.", MimeTypes::APPLICATION_TTML },
        { "stpp.ttml.im1t", MimeTypes::APPLICATION_TTML }, { "eia608.", MimeTypes::APPLICATION_CEA608 }, { "cea608", MimeTypes::APPLICATION_CEA608 },
        { "cea708", MimeTypes::APPLICATION_CEA708 },
    };
    for (const auto& [codec, mime] : cases) CHECK_EQ(MimeTypes::GetMediaMimeType(codec), M(mime));
}

TEST(StreamMimeTypes_getMimeTypeFromMp4ObjectType) {
    const std::pair<int, const char*> valid[] = {
        { 0x60, MimeTypes::VIDEO_MPEG2 }, { 0x61, MimeTypes::VIDEO_MPEG2 }, { 0x20, MimeTypes::VIDEO_MP4V },   { 0x21, MimeTypes::VIDEO_H264 },
        { 0x23, MimeTypes::VIDEO_H265 },  { 0x6B, MimeTypes::AUDIO_MPEG },  { 0x40, MimeTypes::AUDIO_AAC },    { 0x66, MimeTypes::AUDIO_AAC },
        { 0x67, MimeTypes::AUDIO_AAC },   { 0x68, MimeTypes::AUDIO_AAC },   { 0xA5, MimeTypes::AUDIO_AC3 },    { 0xA6, MimeTypes::AUDIO_E_AC3 },
        { 0xA9, MimeTypes::AUDIO_DTS },   { 0xAC, MimeTypes::AUDIO_DTS },   { 0xAA, MimeTypes::AUDIO_DTS_HD }, { 0xAB, MimeTypes::AUDIO_DTS_HD },
        { 0xAD, MimeTypes::AUDIO_OPUS },
    };
    for (const auto& [type, mime] : valid) CHECK_EQ(MimeTypes::GetMimeTypeFromMp4ObjectType(type), M(mime));
    for (int type : { 0, 0x600, 0x01, -1 }) CHECK(MimeTypes::GetMimeTypeFromMp4ObjectType(type).empty());
}
