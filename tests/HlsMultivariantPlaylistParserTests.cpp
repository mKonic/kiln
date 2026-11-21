// Ported from media3-exoplayer-hls HlsMultivariantPlaylistParserTest.java. Expected values are
// upstream's; a null string is "" here.
#include "Check.h"

#include "kiln/media/hls/HlsPlaylist.h"

#include <string>

using namespace kiln::media;
using namespace kiln::media::hls;

namespace {

    const std::string PLAYLIST_URI = "https://example.com/test.m3u8";

    const std::string PLAYLIST_SIMPLE = " #EXTM3U \n"
                                        "\n"
                                        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                        "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                        "http://example.com/low.m3u8\n"
                                        "\n"
                                        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"mp4a.40.2 , avc1.66.30 \"\n"
                                        "http://example.com/spaces_in_codecs.m3u8\n"
                                        "\n"
                                        "#EXT-X-STREAM-INF:BANDWIDTH=2560000,FRAME-RATE=25,RESOLUTION=384x160\n"
                                        "http://example.com/mid.m3u8\n"
                                        "\n"
                                        "#EXT-X-STREAM-INF:BANDWIDTH=7680000,FRAME-RATE=29.997\n"
                                        "http://example.com/hi.m3u8\n"
                                        "\n"
                                        "#EXT-X-STREAM-INF:BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n"
                                        "http://example.com/audio-only.m3u8";
    const std::string PLAYLIST_WITH_AVG_BANDWIDTH = " #EXTM3U \n"
                                                    "\n"
                                                    "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                    "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                                    "http://example.com/low.m3u8\n"
                                                    "\n"
                                                    "#EXT-X-STREAM-INF:BANDWIDTH=1280000,AVERAGE-BANDWIDTH=1270000,"
                                                    "CODECS=\"mp4a.40.2 , avc1.66.30 \"\n"
                                                    "http://example.com/spaces_in_codecs.m3u8\n";
    const std::string PLAYLIST_WITH_SCORE = " #EXTM3U \n"
                                            "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"avc1.66.30\","
                                            "RESOLUTION=304x128,SCORE=1.5\n"
                                            "http://example.com/low.m3u8\n"
                                            "#EXT-X-STREAM-INF:BANDWIDTH=8940000,CODECS=\"avc1.66.30\","
                                            "RESOLUTION=1920x1080,SCORE=2.0\n"
                                            "http://example.com/high.m3u8\n";
    const std::string PLAYLIST_WITH_DOLBY_VISION = " #EXTM3U \n"
                                                   "\n"
                                                   "#EXT-X-STREAM-INF:BANDWIDTH=8500000,AVERAGE-BANDWIDTH=6000000,"
                                                   "CODECS=\"dvh1.10.05\",RESOLUTION=1920x1080,VIDEO-RANGE=PQ\n"
                                                   "http://example.com/high_hdr.m3u8\n";
    const std::string STEERING_VARIANTS =
        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,PATHWAY-ID=\"CDN-A\",STABLE-VARIANT-ID=\"Video1\"\n"
        "http://example.com/low.m3u8\n"
        "\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,PATHWAY-ID=\"CDN-B\",STABLE-VARIANT-ID=\"Video1\"\n"
        "http://backup.example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_CONTENT_STEERING_INFO_ABSOLUTE_SERVER_URI =
        " #EXTM3U \n\n#EXT-X-CONTENT-STEERING:SERVER-URI=\"https://test.com/steering?video=001\",PATHWAY-ID=\"CDN-A\"\n\n" + STEERING_VARIANTS;
    const std::string PLAYLIST_WITH_CONTENT_STEERING_INFO_RELATIVE_SERVER_URI =
        " #EXTM3U \n\n#EXT-X-CONTENT-STEERING:SERVER-URI=\"/steering?video=001\",PATHWAY-ID=\"CDN-A\"\n\n" + STEERING_VARIANTS;
    const std::string PLAYLIST_WITH_CONTENT_STEERING_AND_VARIABLE_SUBSTITUTION =
        " #EXTM3U \n"
        "\n"
        "#EXT-X-DEFINE:NAME=\"steering_path\",VALUE=\"/steering?video=001\"\n"
        "#EXT-X-DEFINE:NAME=\"cdn_id\",VALUE=\"CDN-A\"\n"
        "\n"
        "#EXT-X-CONTENT-STEERING:SERVER-URI=\"{$steering_path}\",PATHWAY-ID=\"{$cdn_id}\"\n"
        "\n" +
        STEERING_VARIANTS;
    const std::string PLAYLIST_WITH_CONTENT_STEERING_INFO_NO_INITIAL_PATHWAY_ID =
        " #EXTM3U \n\n#EXT-X-CONTENT-STEERING:SERVER-URI=\"/steering?video=001\"\n\n" + STEERING_VARIANTS;
    const std::string PLAYLIST_WITH_CONTENT_STEERING_INFO_NO_SERVER_URI = " #EXTM3U \n\n#EXT-X-CONTENT-STEERING:PATHWAY-ID=\"CDN-A\"\n\n" + STEERING_VARIANTS;
    const std::string PLAYLIST_WITH_CONTENT_STEERING_INFO_APPEARING_TWICE =
        " #EXTM3U \n"
        "\n"
        "#EXT-X-CONTENT-STEERING:SERVER-URI=\"/steering?video=001\",PATHWAY-ID=\"CDN-A\"\n"
        "\n"
        "#EXT-X-CONTENT-STEERING:SERVER-URI=\"/steering?video=001\",PATHWAY-ID=\"CDN-B\"\n"
        "\n" +
        STEERING_VARIANTS;
    const std::string PLAYLIST_WITH_PATHWAY_ID_AND_STABLE_VARIANT_ID =
        " #EXTM3U \n"
        "\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,PATHWAY-ID=\"CDN-A\",STABLE-VARIANT-ID=\"Video1\"\n"
        "http://example.com/low.m3u8\n"
        "\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8940000,CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=1920x1080,PATHWAY-ID=\"CDN-A\",STABLE-VARIANT-ID=\"Video2\"\n"
        "http://example.com/high.m3u8\n"
        "\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,PATHWAY-ID=\"CDN-B\",STABLE-VARIANT-ID=\"Video1\"\n"
        "http://backup.example.com/low.m3u8\n"
        "\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8940000,CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=1920x1080,PATHWAY-ID=\"CDN-B\",STABLE-VARIANT-ID=\"Video2\"\n"
        "http://backup.example.com/high.m3u8\n";
    const std::string PLAYLIST_WITH_INVALID_HEADER = "#EXTMU3\n"
                                                     "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                     "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                                     "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_CC = " #EXTM3U \n"
                                         "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS,GROUP-ID=\"cc1\","
                                         "LANGUAGE=\"es\",NAME=\"\fEng\",INSTREAM-ID=\"SERVICE4\"\n"
                                         "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                         "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                         "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_CHANNELS_ATTRIBUTE = " #EXTM3U \n"
                                                         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",CHANNELS=\"6\",NAME=\"Eng6\","
                                                         "URI=\"something.m3u8\"\n"
                                                         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",CHANNELS=\"2/6\",NAME=\"Eng26\","
                                                         "URI=\"something2.m3u8\"\n"
                                                         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"Eng\","
                                                         "URI=\"something3.m3u8\"\n"
                                                         "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                         "CODECS=\"mp4a.40.2,avc1.66.30\",AUDIO=\"audio\",RESOLUTION=304x128\n"
                                                         "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITHOUT_CC = " #EXTM3U \n"
                                            "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS,GROUP-ID=\"cc1\","
                                            "LANGUAGE=\"es\",NAME=\"Eng\",INSTREAM-ID=\"SERVICE4\"\n"
                                            "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                            "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,"
                                            "CLOSED-CAPTIONS=NONE\n"
                                            "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_SUBTITLES = " #EXTM3U \n"
                                                "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",URI=\"s1/en/prog_index.m3u8\","
                                                "LANGUAGE=\"es\",NAME=\"Eng\"\n"
                                                "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                                "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_SUBTITLES_NO_URI = " #EXTM3U \n"
                                                       "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\","
                                                       "LANGUAGE=\"es\",NAME=\"Eng\"\n"
                                                       "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                       "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                                       "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_SUBTITLES_STABLE_RENDITION_ID = " #EXTM3U \n"
                                                                    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",URI=\"s1/en/prog_index.m3u8\","
                                                                    "LANGUAGE=\"es\",NAME=\"Eng\",STABLE-RENDITION-ID=\"Subtitles-Eng\"\n"
                                                                    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",URI=\"s1/fr/prog_index.m3u8\","
                                                                    "LANGUAGE=\"fr\",NAME=\"Fra\",STABLE-RENDITION-ID=\"Subtitles-Fra\"\n"
                                                                    "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                                    "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,STABLE-VARIANT-ID=\"Video\"\n"
                                                                    "http://example.com/low.m3u8\n";
    const std::string PLAYLIST_WITH_AUDIO_MEDIA_TAG = "#EXTM3U\n"
                                                      "#EXT-X-STREAM-INF:BANDWIDTH=2227464,CODECS=\"avc1.640020,mp4a.40.2\",AUDIO=\"aud1\"\n"
                                                      "uri1.m3u8\n"
                                                      "#EXT-X-STREAM-INF:BANDWIDTH=8178040,CODECS=\"avc1.64002a,mp4a.40.2\",AUDIO=\"aud1\"\n"
                                                      "uri2.m3u8\n"
                                                      "#EXT-X-STREAM-INF:BANDWIDTH=2448841,CODECS=\"avc1.640020,ac-3\",AUDIO=\"aud2\"\n"
                                                      "uri1.m3u8\n"
                                                      "#EXT-X-STREAM-INF:BANDWIDTH=8399417,CODECS=\"avc1.64002a,ac-3\",AUDIO=\"aud2\"\n"
                                                      "uri2.m3u8\n"
                                                      "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",LANGUAGE=\"en\",NAME=\"English\","
                                                      "AUTOSELECT=YES,DEFAULT=YES,CHANNELS=\"2\",URI=\"a1/prog_index.m3u8\"\n"
                                                      "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud2\",LANGUAGE=\"en\",NAME=\"English\","
                                                      "AUTOSELECT=YES,DEFAULT=YES,CHANNELS=\"6\",URI=\"a2/prog_index.m3u8\"\n";
    const std::string PLAYLIST_WITH_AUDIO_STABLE_RENDITION_ID =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8178040,CODECS=\"avc1.64002a,mp4a.40.2\",AUDIO=\"aud1\",STABLE-VARIANT-ID=\"Video1\"\n"
        "uri1.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2448841,CODECS=\"avc1.640020,ac-3\",AUDIO=\"aud2\",STABLE-VARIANT-ID=\"Video2\"\n"
        "uri2.m3u8\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",LANGUAGE=\"en\",NAME=\"English\",STABLE-RENDITION-ID=\"Audio1\",AUTOSELECT=YES,DEFAULT=YES,CHANNELS=\"2\",URI=\"a1/prog_index.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud2\",LANGUAGE=\"en\",NAME=\"English\",STABLE-RENDITION-ID=\"Audio2\",AUTOSELECT=YES,DEFAULT=YES,CHANNELS=\"6\",URI=\"a2/prog_index.m3u8\"\n";
    const std::string PLAYLIST_WITH_INDEPENDENT_SEGMENTS = " #EXTM3U\n"
                                                           "\n"
                                                           "#EXT-X-INDEPENDENT-SEGMENTS\n"
                                                           "\n"
                                                           "#EXT-X-STREAM-INF:BANDWIDTH=1280000,"
                                                           "CODECS=\"mp4a.40.2,avc1.66.30\",RESOLUTION=304x128\n"
                                                           "http://example.com/low.m3u8\n"
                                                           "\n"
                                                           "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"mp4a.40.2 , avc1.66.30 \"\n"
                                                           "http://example.com/spaces_in_codecs.m3u8\n";
    const std::string PLAYLIST_WITH_VARIABLE_SUBSTITUTION = " #EXTM3U \n"
                                                            "\n"
                                                            "#EXT-X-DEFINE:NAME=\"codecs\",VALUE=\"mp4a.40.5\"\n"
                                                            "#EXT-X-DEFINE:NAME=\"tricky\",VALUE=\"This/{$nested}/reference/shouldnt/work\"\n"
                                                            "#EXT-X-DEFINE:NAME=\"nested\",VALUE=\"This should not be inserted\"\n"
                                                            "#EXT-X-STREAM-INF:BANDWIDTH=65000,CODECS=\"{$codecs}\"\n"
                                                            "http://example.com/{$tricky}\n";
    const std::string PLAYLIST_WITH_QUERY_PARAM_SUBSTITUTION = " #EXTM3U \n"
                                                               "\n"
                                                               "#EXT-X-DEFINE:QUERYPARAM=\"path\",VALUE=\"\"\n"
                                                               "#EXT-X-DEFINE:QUERYPARAM=\"codecs\",VALUE=\"\"\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=65000,CODECS=\"{$codecs}\"\n"
                                                               "http://example.com/{$path}\n";
    const std::string PLAYLIST_WITH_DUPLICATE_VARIABLE_AND_QUERY_PARAM_NAMES = " #EXTM3U \n"
                                                                               "\n"
                                                                               "#EXT-X-DEFINE:NAME=\"path\",VALUE=\"path/to/glory\"\n"
                                                                               "#EXT-X-DEFINE:QUERYPARAM=\"path\",VALUE=\"\"\n"
                                                                               "#EXT-X-STREAM-INF:BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n"
                                                                               "http://example.com/{$path}\n";
    const std::string PLAYLIST_WITH_DUPLICATE_VARIABLE_NAMES = " #EXTM3U \n"
                                                               "\n"
                                                               "#EXT-X-DEFINE:NAME=\"var_name\",VALUE=\"path/to/glory\"\n"
                                                               "#EXT-X-DEFINE:NAME=\"var_name\",VALUE=\"\"\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n"
                                                               "http://example.com/{$path}\n";
    const std::string PLAYLIST_WITH_DUPLICATE_QUERY_PARAM_NAMES = " #EXTM3U \n"
                                                                  "\n"
                                                                  "#EXT-X-DEFINE:QUERYPARAM=\"query_param\",VALUE=\"\"\n"
                                                                  "#EXT-X-DEFINE:QUERYPARAM=\"query_param\",VALUE=\"\"\n"
                                                                  "#EXT-X-STREAM-INF:BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n"
                                                                  "http://example.com/{$path}\n";
    const std::string PLAYLIST_WITH_MATCHING_STREAM_INF_URLS = "#EXTM3U\n"
                                                               "#EXT-X-VERSION:6\n"
                                                               "\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=2227464,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
                                                               "v5/prog_index.m3u8\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=6453202,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
                                                               "v8/prog_index.m3u8\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=5054232,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
                                                               "v7/prog_index.m3u8\n"
                                                               "\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=2448841,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud2\",SUBTITLES=\"sub1\"\n"
                                                               "v5/prog_index.m3u8\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=8399417,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud2\",SUBTITLES=\"sub1\"\n"
                                                               "v9/prog_index.m3u8\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=5275609,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud2\",SUBTITLES=\"sub1\"\n"
                                                               "v7/prog_index.m3u8\n"
                                                               "\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=2256841,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud3\",SUBTITLES=\"sub1\"\n"
                                                               "v5/prog_index.m3u8\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=8207417,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud3\",SUBTITLES=\"sub1\"\n"
                                                               "v9/prog_index.m3u8\n"
                                                               "#EXT-X-STREAM-INF:BANDWIDTH=6482579,"
                                                               "CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud3\",SUBTITLES=\"sub1\"\n"
                                                               "v8/prog_index.m3u8\n"
                                                               "\n"
                                                               "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",NAME=\"English\",URI=\"a1/index.m3u8\"\n"
                                                               "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud2\",NAME=\"English\",URI=\"a2/index.m3u8\"\n"
                                                               "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud3\",NAME=\"English\",URI=\"a3/index.m3u8\"\n"
                                                               "\n"
                                                               "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS,"
                                                               "GROUP-ID=\"cc1\",NAME=\"English\",INSTREAM-ID=\"CC1\"\n"
                                                               "\n"
                                                               "#EXT-X-MEDIA:TYPE=SUBTITLES,"
                                                               "GROUP-ID=\"sub1\",NAME=\"English\",URI=\"s1/en/prog_index.m3u8\"\n";
    const std::string PLAYLIST_WITH_TTML_SUBTITLE =
        " #EXTM3U\n"
        "\n"
        "#EXT-X-VERSION:6\n"
        "\n"
        "#EXT-X-INDEPENDENT-SEGMENTS\n"
        "\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"stpp.ttml.im1t,mp4a.40.2,avc1.66.30\",RESOLUTION=304x128,AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
        "http://example.com/low.m3u8\n"
        "\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",NAME=\"English\",URI=\"a1/index.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",NAME=\"English\",AUTOSELECT=YES,DEFAULT=YES,URI=\"s1/en/prog_index.m3u8\"\n";
    const std::string PLAYLIST_WITH_IFRAME_VARIANTS =
        "#EXTM3U\n"
        "#EXT-X-VERSION:5\n"
        "#EXT-X-MEDIA:URI=\"AUDIO_English/index.m3u8\",TYPE=AUDIO,GROUP-ID=\"audio-aac\",LANGUAGE=\"en\",NAME=\"English\",AUTOSELECT=YES\n"
        "#EXT-X-MEDIA:URI=\"AUDIO_Spanish/index.m3u8\",TYPE=AUDIO,GROUP-ID=\"audio-aac\",LANGUAGE=\"es\",NAME=\"Spanish\",AUTOSELECT=YES\n"
        "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS,GROUP-ID=\"cc1\",LANGUAGE=\"en\",NAME=\"English\",AUTOSELECT=YES,DEFAULT=YES,INSTREAM-ID=\"CC1\"\n"
        "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=400000,RESOLUTION=480x320,CODECS=\"mp4a.40.2,avc1.640015\",AUDIO=\"audio-aac\",CLOSED-CAPTIONS=\"cc1\"\n"
        "400000/index.m3u8\n"
        "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1000000,RESOLUTION=848x480,CODECS=\"mp4a.40.2,avc1.64001f\",AUDIO=\"audio-aac\",CLOSED-CAPTIONS=\"cc1\"\n"
        "1000000/index.m3u8\n"
        "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=3220000,RESOLUTION=1280x720,CODECS=\"mp4a.40.2,avc1.64001f\",AUDIO=\"audio-aac\",CLOSED-CAPTIONS=\"cc1\"\n"
        "3220000/index.m3u8\n"
        "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=8940000,RESOLUTION=1920x1080,CODECS=\"mp4a.40.2,avc1.640028\",AUDIO=\"audio-aac\",CLOSED-CAPTIONS=\"cc1\"\n"
        "8940000/index.m3u8\n"
        "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=1313400,RESOLUTION=1920x1080,CODECS=\"avc1.640028\",URI=\"iframe_1313400/index.m3u8\"\n";
    const std::string PLAYLIST_WITH_IFRAME_VARIANT_SCORE = "#EXTM3U\n"
                                                           "#EXT-X-STREAM-INF:BANDWIDTH=8940000,RESOLUTION=1920x1080,"
                                                           "CODECS=\"avc1.640028\"\n"
                                                           "8940000/index.m3u8\n"
                                                           "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=1313400,SCORE=4.5,"
                                                           "RESOLUTION=1920x1080,CODECS=\"avc1.640028\","
                                                           "URI=\"iframe_1313400/index.m3u8\"\n";

    std::shared_ptr<HlsMultivariantPlaylist> Parse(const std::string& uri, const std::string& playlist) {
        return std::dynamic_pointer_cast<HlsMultivariantPlaylist>(HlsPlaylistParser().Parse(uri, playlist));
    }

    // The ParserException message, or "" when parsing succeeds.
    std::string ParseError(const std::string& uri, const std::string& playlist) {
        try {
            Parse(uri, playlist);
        } catch (const stream::ParserException& e) {
            return e.what();
        }
        return "";
    }
    bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

    HlsTrackMetadataEntry::VariantInfo VariantInfo(int peakBitrate, const std::string& audioGroupId) {
        return { -1, peakBitrate, "", audioGroupId, "sub1", "cc1" };
    }

}

TEST(HlsMultivariantPlaylistParser_withSimple_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_SIMPLE);
    const auto& variants = playlist->variants;
    CHECK_EQ(variants.size(), 5u);
    CHECK(!playlist->muxedCaptionFormats.has_value());
    CHECK_EQ(variants[0].format.bitrate, 1280000);
    CHECK_EQ(variants[0].format.codecs, std::string("mp4a.40.2,avc1.66.30"));
    CHECK_EQ(variants[0].format.width, 304);
    CHECK_EQ(variants[0].format.height, 128);
    CHECK_EQ(variants[0].url, std::string("http://example.com/low.m3u8"));
    CHECK(variants[0].pathwayId.empty());
    CHECK(variants[0].stableVariantId.empty());
    CHECK_EQ(variants[1].format.bitrate, 1280000);
    CHECK_EQ(variants[1].format.codecs, std::string("mp4a.40.2 , avc1.66.30 "));
    CHECK_EQ(variants[1].url, std::string("http://example.com/spaces_in_codecs.m3u8"));
    CHECK_EQ(variants[2].format.bitrate, 2560000);
    CHECK(variants[2].format.codecs.empty());
    CHECK_EQ(variants[2].format.width, 384);
    CHECK_EQ(variants[2].format.height, 160);
    CHECK_EQ(variants[2].format.frameRate, 25.0f);
    CHECK_EQ(variants[2].url, std::string("http://example.com/mid.m3u8"));
    CHECK_EQ(variants[3].format.bitrate, 7680000);
    CHECK(variants[3].format.codecs.empty());
    CHECK_EQ(variants[3].format.width, -1);
    CHECK_EQ(variants[3].format.height, -1);
    CHECK_EQ(variants[3].format.frameRate, 29.997f);
    CHECK_EQ(variants[3].url, std::string("http://example.com/hi.m3u8"));
    CHECK_EQ(variants[4].format.bitrate, 65000);
    CHECK_EQ(variants[4].format.codecs, std::string("mp4a.40.5"));
    CHECK_EQ(variants[4].format.width, -1);
    CHECK_EQ(variants[4].format.height, -1);
    CHECK_EQ(variants[4].format.frameRate, -1.0f);
    CHECK_EQ(variants[4].url, std::string("http://example.com/audio-only.m3u8"));
    for (const auto& v : variants) CHECK(v.pathwayId.empty() && v.stableVariantId.empty());
    CHECK(!playlist->contentSteeringInfo.has_value());
}

TEST(HlsMultivariantPlaylistParser_withoutScore_setsSelectionPriorityToNoValue) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_SIMPLE);
    for (const auto& variant : parsed->variants) CHECK_EQ(variant.format.selectionPriority, -1.0f);
}

TEST(HlsMultivariantPlaylistParser_withAverageBandwidth_success) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_AVG_BANDWIDTH);
    const auto& variants = parsed->variants;
    CHECK_EQ(variants[0].format.bitrate, 1280000);
    CHECK_EQ(variants[1].format.bitrate, 1280000);
}

TEST(HlsMultivariantPlaylistParser_withScore_success) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_SCORE);
    const auto& variants = parsed->variants;
    CHECK_EQ(variants[0].format.selectionPriority, 1.5f);
    CHECK_EQ(variants[1].format.selectionPriority, 2.0f);
}

TEST(HlsMultivariantPlaylistParser_withDolbyVisionProfile10_success) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_DOLBY_VISION);
    const auto& format = parsed->variants[0].format;
    CHECK_EQ(format.colorSpace, stream::kColorSpaceBt2020);
    CHECK_EQ(format.colorTransfer, stream::kColorTransferSt2084);
    CHECK_EQ(format.colorRange, stream::kColorRangeFull);
}

TEST(HlsMultivariantPlaylistParser_hasContentSteeringInfoWithAbsoluteServerUri_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_CONTENT_STEERING_INFO_ABSOLUTE_SERVER_URI);
    CHECK(playlist->contentSteeringInfo.has_value());
    CHECK_EQ(playlist->contentSteeringInfo->serverUri, std::string("https://test.com/steering?video=001"));
    CHECK_EQ(playlist->contentSteeringInfo->pathwayId, std::string("CDN-A"));
}

TEST(HlsMultivariantPlaylistParser_hasContentSteeringInfoWithRelativeServerUri_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_CONTENT_STEERING_INFO_RELATIVE_SERVER_URI);
    CHECK(playlist->contentSteeringInfo.has_value());
    CHECK_EQ(playlist->contentSteeringInfo->serverUri, std::string("https://example.com/steering?video=001"));
    CHECK_EQ(playlist->contentSteeringInfo->pathwayId, std::string("CDN-A"));
}

TEST(HlsMultivariantPlaylistParser_contentSteeringWithVariableSubstitution_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_CONTENT_STEERING_AND_VARIABLE_SUBSTITUTION);
    CHECK(playlist->contentSteeringInfo.has_value());
    CHECK_EQ(playlist->contentSteeringInfo->serverUri, std::string("https://example.com/steering?video=001"));
    CHECK_EQ(playlist->contentSteeringInfo->pathwayId, std::string("CDN-A"));
}

TEST(HlsMultivariantPlaylistParser_hasContentSteeringInfoNoInitialPathwayId_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_CONTENT_STEERING_INFO_NO_INITIAL_PATHWAY_ID);
    CHECK(playlist->contentSteeringInfo.has_value());
    CHECK_EQ(playlist->contentSteeringInfo->serverUri, std::string("https://example.com/steering?video=001"));
    CHECK(playlist->contentSteeringInfo->pathwayId.empty());
}

TEST(HlsMultivariantPlaylistParser_contentSteeringInfoNoServerUri_throwsException) {
    CHECK(!ParseError(PLAYLIST_URI, PLAYLIST_WITH_CONTENT_STEERING_INFO_NO_SERVER_URI).empty());
}

TEST(HlsMultivariantPlaylistParser_contentSteeringInfoAppearingTwice_throwsException) {
    CHECK(Contains(ParseError(PLAYLIST_URI, PLAYLIST_WITH_CONTENT_STEERING_INFO_APPEARING_TWICE),
                   "#EXT-X-CONTENT-STEERING tag must not appear more than once"));
}

TEST(HlsMultivariantPlaylistParser_withPathwayIdAndStableVariantId_success) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_PATHWAY_ID_AND_STABLE_VARIANT_ID);
    const auto& variants = parsed->variants;
    CHECK_EQ(variants[0].pathwayId, std::string("CDN-A"));
    CHECK_EQ(variants[0].stableVariantId, std::string("Video1"));
    CHECK_EQ(variants[1].pathwayId, std::string("CDN-A"));
    CHECK_EQ(variants[1].stableVariantId, std::string("Video2"));
    CHECK_EQ(variants[2].pathwayId, std::string("CDN-B"));
    CHECK_EQ(variants[2].stableVariantId, std::string("Video1"));
    CHECK_EQ(variants[3].pathwayId, std::string("CDN-B"));
    CHECK_EQ(variants[3].stableVariantId, std::string("Video2"));
}

TEST(HlsMultivariantPlaylistParser_withInvalidHeader_throwsException) { CHECK(!ParseError(PLAYLIST_URI, PLAYLIST_WITH_INVALID_HEADER).empty()); }

TEST(HlsMultivariantPlaylistParser_withClosedCaption_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_CC);
    CHECK_EQ(playlist->muxedCaptionFormats->size(), 1u);
    const Format& closedCaptionFormat = playlist->muxedCaptionFormats->at(0);
    CHECK_EQ(closedCaptionFormat.sampleMimeType, std::string(stream::MimeTypes::APPLICATION_CEA708));
    CHECK_EQ(closedCaptionFormat.accessibilityChannel, 4);
    CHECK_EQ(closedCaptionFormat.language, std::string("es"));
}

TEST(HlsMultivariantPlaylistParser_withChannelsAttribute_success) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_CHANNELS_ATTRIBUTE);
    const auto& audios = parsed->audios;
    CHECK_EQ(audios.size(), 3u);
    CHECK_EQ(audios[0].format.channelCount, 6);
    CHECK_EQ(audios[1].format.channelCount, 2);
    CHECK_EQ(audios[2].format.channelCount, -1);
}

TEST(HlsMultivariantPlaylistParser_withoutClosedCaption_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITHOUT_CC);
    CHECK(playlist->muxedCaptionFormats.has_value() && playlist->muxedCaptionFormats->empty());
}

TEST(HlsMultivariantPlaylistParser_withAudio_codecPropagated) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_AUDIO_MEDIA_TAG);
    CHECK_EQ(playlist->audios[0].format.codecs, std::string("mp4a.40.2"));
    CHECK_EQ(playlist->audios[0].format.sampleMimeType, std::string(stream::MimeTypes::AUDIO_AAC));
    CHECK_EQ(playlist->audios[1].format.codecs, std::string("ac-3"));
    CHECK_EQ(playlist->audios[1].format.sampleMimeType, std::string(stream::MimeTypes::AUDIO_AC3));
}

TEST(HlsMultivariantPlaylistParser_withAudio_audioIdPropagated) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_AUDIO_MEDIA_TAG);
    CHECK_EQ(playlist->audios[0].format.id, std::string("aud1:English"));
    CHECK(playlist->audios[0].stableRenditionId.empty());
    CHECK_EQ(playlist->audios[1].format.id, std::string("aud2:English"));
    CHECK(playlist->audios[1].stableRenditionId.empty());
}

TEST(HlsMultivariantPlaylistParser_withAudio_stableRenditionIdPropagated) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_AUDIO_STABLE_RENDITION_ID);
    CHECK_EQ(playlist->audios[0].stableRenditionId, std::string("Audio1"));
    CHECK_EQ(playlist->audios[1].stableRenditionId, std::string("Audio2"));
}

TEST(HlsMultivariantPlaylistParser_withCc_cCIdPropagated) {
    CHECK_EQ(Parse(PLAYLIST_URI, PLAYLIST_WITH_CC)->muxedCaptionFormats->at(0).id, std::string("cc1:\fEng"));
}

TEST(HlsMultivariantPlaylistParser_withSubtitles_subtitlesIdPropagated) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_SUBTITLES);
    const auto& rendition = parsed->subtitles.at(0);
    CHECK_EQ(rendition.format.id, std::string("sub1:Eng"));
    CHECK_EQ(rendition.format.sampleMimeType, std::string(stream::MimeTypes::TEXT_VTT));
    CHECK(rendition.stableRenditionId.empty());
}

TEST(HlsMultivariantPlaylistParser_withSubtitles_stableRenditionIdPropagated) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_SUBTITLES_STABLE_RENDITION_ID);
    CHECK_EQ(playlist->subtitles[0].stableRenditionId, std::string("Subtitles-Eng"));
    CHECK_EQ(playlist->subtitles[1].stableRenditionId, std::string("Subtitles-Fra"));
}

TEST(HlsMultivariantPlaylistParser_subtitlesWithoutUri_skipsSubtitles) { CHECK(Parse(PLAYLIST_URI, PLAYLIST_WITH_SUBTITLES_NO_URI)->subtitles.empty()); }

TEST(HlsMultivariantPlaylistParser_withIndependentSegments_hasNoIndenpendentSegments) {
    CHECK(Parse(PLAYLIST_URI, PLAYLIST_WITH_INDEPENDENT_SEGMENTS)->hasIndependentSegments);
    CHECK(!Parse(PLAYLIST_URI, PLAYLIST_SIMPLE)->hasIndependentSegments);
}

TEST(HlsMultivariantPlaylistParser_withQueryParams_placeholderSubstituted) {
    auto parsed = Parse("http://example.com/?path=appended/path&codecs=mp4a.40.5", PLAYLIST_WITH_QUERY_PARAM_SUBSTITUTION);
    const auto& variant = parsed->variants.at(0);
    CHECK_EQ(variant.format.codecs, std::string("mp4a.40.5"));
    CHECK_EQ(variant.url, std::string("http://example.com/appended/path"));
}

TEST(HlsMultivariantPlaylistParser_withDuplicateVariableNameAndQueryParam_throws) {
    CHECK(Contains(ParseError("http://example.com/?path=appended/path&codecs=mp4a.40.5", PLAYLIST_WITH_DUPLICATE_VARIABLE_AND_QUERY_PARAM_NAMES),
                   "duplicate variable name \"path\""));
}

TEST(HlsMultivariantPlaylistParser_withDuplicateVariableName_throws) {
    CHECK(Contains(ParseError("http://example.com/?path=appended/path&codecs=mp4a.40.5", PLAYLIST_WITH_DUPLICATE_VARIABLE_NAMES),
                   "duplicate variable name \"var_name\""));
}

TEST(HlsMultivariantPlaylistParser_withDuplicateQueryParamName_throws) {
    CHECK(Contains(ParseError("http://example.com/?query_param=value_1", PLAYLIST_WITH_DUPLICATE_QUERY_PARAM_NAMES),
                   "duplicate variable name \"query_param\""));
}

TEST(HlsMultivariantPlaylistParser_missingQueryParam_throws) {
    CHECK(Contains(ParseError(PLAYLIST_URI, PLAYLIST_WITH_QUERY_PARAM_SUBSTITUTION), "QUERYPARAM \"path\" not found in playlist URI"));
}

namespace {
    std::shared_ptr<HlsMediaPlaylist> ParseDependent(const std::string& mediaPlaylistString) {
        auto multivariant = Parse("http://example.com/?path=appended/path&codecs=mp4a.40.5", PLAYLIST_WITH_QUERY_PARAM_SUBSTITUTION);
        return std::dynamic_pointer_cast<HlsMediaPlaylist>(HlsPlaylistParser(multivariant, nullptr).Parse("http://example.com/", mediaPlaylistString));
    }
}

TEST(HlsMultivariantPlaylistParser_dependentPlaylistWithImportedQueryParam_placeholderSubstituted) {
    auto mediaPlaylist = ParseDependent("#EXTM3U\n"
                                        "#EXT-X-VERSION:8\n"
                                        "#EXT-X-TARGETDURATION:5\n"
                                        "#EXT-X-MEDIA-SEQUENCE:10\n"
                                        "#EXT-X-DEFINE:IMPORT=\"path\"\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/1.ts\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/2.ts\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/3.ts\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/4.ts\n");
    CHECK_EQ(mediaPlaylist->segments.size(), 4u);
    for (std::size_t i = 0; i < mediaPlaylist->segments.size(); i++)
        CHECK_EQ(mediaPlaylist->segments[i].url, "relative/from/appended/path/" + std::to_string(i + 1) + ".ts");
}

TEST(HlsMultivariantPlaylistParser_dependentPlaylistWithoutImport_placeholderNotSubstituted) {
    auto mediaPlaylist = ParseDependent("#EXTM3U\n"
                                        "#EXT-X-VERSION:8\n"
                                        "#EXT-X-TARGETDURATION:5\n"
                                        "#EXT-X-MEDIA-SEQUENCE:10\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/1.ts\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/2.ts\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/3.ts\n"
                                        "#EXTINF:5.005,\n"
                                        "relative/from/{$path}/4.ts\n");
    CHECK_EQ(mediaPlaylist->segments.size(), 4u);
    for (std::size_t i = 0; i < mediaPlaylist->segments.size(); i++)
        CHECK_EQ(mediaPlaylist->segments[i].url, "relative/from/{$path}/" + std::to_string(i + 1) + ".ts");
}

TEST(HlsMultivariantPlaylistParser_withVariableSubstitution_success) {
    auto parsed = Parse(PLAYLIST_URI, PLAYLIST_WITH_VARIABLE_SUBSTITUTION);
    const auto& variant = parsed->variants.at(0);
    CHECK_EQ(variant.format.codecs, std::string("mp4a.40.5"));
    CHECK_EQ(variant.url, std::string("http://example.com/This/{$nested}/reference/shouldnt/work"));
}

TEST(HlsMultivariantPlaylistParser_withTtmlSubtitle) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_TTML_SUBTITLE);
    const Format& firstTextFormat = playlist->subtitles.at(0).format;
    CHECK_EQ(firstTextFormat.id, std::string("sub1:English"));
    CHECK_EQ(firstTextFormat.containerMimeType, std::string(stream::MimeTypes::APPLICATION_M3U8));
    CHECK_EQ(firstTextFormat.sampleMimeType, std::string(stream::MimeTypes::APPLICATION_TTML));
    CHECK_EQ(playlist->variants.at(0).format.codecs, std::string("stpp.ttml.im1t,mp4a.40.2,avc1.66.30"));
}

TEST(HlsMultivariantPlaylistParser_withMatchingStreamInfUrls_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_MATCHING_STREAM_INF_URLS);
    CHECK_EQ(playlist->variants.size(), 4u);
    auto streamInf = [](std::vector<HlsTrackMetadataEntry::VariantInfo> infos) { return HlsTrackMetadataEntry{ "", "", std::move(infos) }; };
    CHECK(playlist->variants[0].metadata == streamInf({ VariantInfo(2227464, "aud1"), VariantInfo(2448841, "aud2"), VariantInfo(2256841, "aud3") }));
    CHECK(playlist->variants[1].metadata == streamInf({ VariantInfo(6453202, "aud1"), VariantInfo(6482579, "aud3") }));
    CHECK(playlist->variants[2].metadata == streamInf({ VariantInfo(5054232, "aud1"), VariantInfo(5275609, "aud2") }));
    CHECK(playlist->variants[3].metadata == streamInf({ VariantInfo(8399417, "aud2"), VariantInfo(8207417, "aud3") }));
    CHECK_EQ(playlist->audios.size(), 3u);
    CHECK(playlist->audios[0].metadata == (HlsTrackMetadataEntry{ "aud1", "English", {} }));
    CHECK(playlist->audios[1].metadata == (HlsTrackMetadataEntry{ "aud2", "English", {} }));
    CHECK(playlist->audios[2].metadata == (HlsTrackMetadataEntry{ "aud3", "English", {} }));
}

TEST(HlsMultivariantPlaylistParser_testIFrameVariant) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_IFRAME_VARIANTS);
    CHECK_EQ(playlist->variants.size(), 5u);
    for (int i = 0; i < 4; i++) CHECK_EQ(playlist->variants[std::size_t(i)].format.roleFlags, 0);
    const auto& iFramesOnlyVariant = playlist->variants[4];
    CHECK_EQ(iFramesOnlyVariant.format.bitrate, 1313400);
    CHECK_EQ(iFramesOnlyVariant.format.roleFlags & kRoleFlagTrickPlay, kRoleFlagTrickPlay);
}

TEST(HlsMultivariantPlaylistParser_withIFrameStreamInfScore_success) {
    auto playlist = Parse(PLAYLIST_URI, PLAYLIST_WITH_IFRAME_VARIANT_SCORE);
    CHECK_EQ(playlist->variants.size(), 2u);
    CHECK_EQ(playlist->variants[0].format.selectionPriority, -1.0f);
    CHECK_EQ(playlist->variants[1].format.selectionPriority, 4.5f);
}
