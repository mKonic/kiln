// Ported from media3-exoplayer-hls HlsMediaPlaylistParserTest.java. Expected values are
// upstream's; a null string is "" here and a null object an empty optional or pointer.
#include "Check.h"

#include "kiln/media/hls/HlsPlaylist.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

using namespace kiln::media;
using namespace kiln::media::hls;
using Interstitial = HlsMediaPlaylist::Interstitial;
using ClientDefinedAttribute = HlsMediaPlaylist::ClientDefinedAttribute;
using Segment = HlsMediaPlaylist::Segment;

namespace {

    std::shared_ptr<HlsMediaPlaylist> MediaOf(const std::shared_ptr<HlsPlaylist>& playlist) { return std::dynamic_pointer_cast<HlsMediaPlaylist>(playlist); }

    std::shared_ptr<const HlsMultivariantPlaylist> EmptyMultivariant() {
        return std::shared_ptr<const HlsMultivariantPlaylist>(&HlsMultivariantPlaylist::Empty(), [](const HlsMultivariantPlaylist*) {});
    }

    bool IsNull(const std::string& s) { return s.empty(); }
    template<typename T>
    bool IsNull(const std::optional<T>& o) { return !o.has_value(); }
    template<typename T>
    bool IsNull(const std::shared_ptr<T>& p) { return p == nullptr; }
    template<typename T>
    bool IsNull(const std::vector<T>&) { return false; }

    bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
    }
    bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
    bool SameList(const std::vector<std::string>& a, const std::vector<std::string>& b) { return a == b; }
    bool SameList(const std::vector<Interstitial>& a, const std::vector<Interstitial>& b) { return a == b; }

    template<typename E, typename F>
    bool Throws(F&& f) {
        try {
            f();
        } catch (const E&) {
            return true;
        }
        return false;
    }
    template<typename E, typename F>
    std::string ThrowMessage(F&& f) {
        try {
            f();
        } catch (const E& e) {
            return e.what();
        }
        return "";
    }

    // new Interstitial(...), in upstream's argument order.
    Interstitial MakeInterstitial(std::string id, std::string assetUri, std::string assetListUri, std::int64_t startDateUnixUs, std::int64_t endDateUnixUs,
                                  std::int64_t durationUs, std::int64_t plannedDurationUs, std::vector<std::string> cue, bool endOnNext, std::int64_t resumeOffsetUs,
                                  std::int64_t playoutLimitUs, std::vector<std::string> snapTypes, std::vector<std::string> restrictions,
                                  std::vector<ClientDefinedAttribute> clientDefinedAttributes, bool contentMayVary, std::string timelineOccupies,
                                  std::string timelineStyle, std::int64_t skipControlOffsetUs, std::int64_t skipControlDurationUs, std::string skipControlLabelId) {
        std::sort(clientDefinedAttributes.begin(), clientDefinedAttributes.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        Interstitial i;
        i.id = std::move(id);
        i.assetUri = std::move(assetUri);
        i.assetListUri = std::move(assetListUri);
        i.startDateUnixUs = startDateUnixUs;
        i.endDateUnixUs = endDateUnixUs;
        i.durationUs = durationUs;
        i.plannedDurationUs = plannedDurationUs;
        i.cue = std::move(cue);
        i.endOnNext = endOnNext;
        i.resumeOffsetUs = resumeOffsetUs;
        i.playoutLimitUs = playoutLimitUs;
        i.snapTypes = std::move(snapTypes);
        i.restrictions = std::move(restrictions);
        i.clientDefinedAttributes = std::move(clientDefinedAttributes);
        i.contentMayVary = contentMayVary;
        i.timelineOccupies = std::move(timelineOccupies);
        i.timelineStyle = std::move(timelineStyle);
        i.skipControlOffsetUs = skipControlOffsetUs;
        i.skipControlDurationUs = skipControlDurationUs;
        i.skipControlLabelId = std::move(skipControlLabelId);
        return i;
    }

    // test_data media/hls/interstitials/interstitials_vod_asset_uri.m3u8.
    const std::string INTERSTITIALS_VOD_ASSET_URI = R"(#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:0
#EXT-X-INDEPENDENT-SEGMENTS
#EXT-X-PLAYLIST-TYPE:VOD
#EXT-X-BITRATE:4612
#EXT-X-PROGRAM-DATE-TIME:2024-11-11T04:35:19.353-08:00
#EXTINF:6,
https://example.com/fileSequence0.ts
#EXT-X-BITRATE:4609
#EXTINF:6,
https://example.com/fileSequence1.ts
#EXT-X-BITRATE:4609
#EXTINF:6,
https://example.com/fileSequence2.ts
#EXT-X-BITRATE:4611
#EXTINF:6,
https://example.com/fileSequence3.ts
#EXT-X-BITRATE:4611
#EXTINF:6,
https://example.com/fileSequence4.ts
#EXT-X-BITRATE:4609
#EXTINF:6,
https://example.com/fileSequence5.ts
#EXT-X-BITRATE:4610
#EXTINF:6,
https://example.com/fileSequence6.ts
#EXT-X-BITRATE:4610
#EXTINF:6,
https://example.com/fileSequence7.ts
#EXT-X-BITRATE:4611
#EXTINF:6,
https://example.com/fileSequence8.ts
#EXT-X-BITRATE:4609
#EXTINF:6,
https://example.com/fileSequence9.ts
#EXT-X-BITRATE:4609
#EXTINF:6,
https://example.com/fileSequence10.ts
#EXT-X-BITRATE:4611
#EXTINF:6,
https://example.com/fileSequence11.ts
#EXT-X-DATERANGE:ID="preroll",START-DATE="2024-11-11T04:35:19.353-08:00",CLASS="com.apple.hls.interstitial",CUE="PRE",X-ASSET-URI="https://example.com/0.m3u8?parse=1",X-RESUME-OFFSET=0,X-RESTRICT="JUMP,SKIP"
#EXT-X-DATERANGE:ID="midroll-1",START-DATE="2024-11-11T04:35:34.353-08:00",CLASS="com.apple.hls.interstitial",X-ASSET-URI="https://example.com/1.m3u8?parse=1",X-RESUME-OFFSET=0,X-RESTRICT="JUMP,SKIP"
#EXT-X-DATERANGE:ID="postroll",START-DATE="2024-11-11T04:36:31.353-08:00",CLASS="com.apple.hls.interstitial",CUE="POST",X-ASSET-URI="https://example.com/2.m3u8?parse=1",X-RESUME-OFFSET=0,X-RESTRICT="JUMP,SKIP"
#EXT-X-ENDLIST
)";

}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-START:TIME-OFFSET=-25\n#EXT-X-TARGETDURATION:8\n#EXT-X-MEDIA-SEQUENCE:2679\n#EXT-X-DISCONTINUITY-SEQUENCE:4\n#EXT-X-ALLOW-CACHE:YES\n\n#EXTINF:7.975,\n#EXT-X-BYTERANGE:51370@0\nhttps://priv.example.com/fileSequence2679.ts\n\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=2680\",IV=0x1566B\n#EXTINF:7.975,segment title\n#EXT-X-BYTERANGE:51501@2147483648\nhttps://priv.example.com/fileSequence2680.ts\n\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:7.941,segment title .,:/# with interesting chars\n#EXT-X-BYTERANGE:51501\nhttps://priv.example.com/fileSequence2681.ts\n\n#EXT-X-DISCONTINUITY\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=2682\"\n#EXTINF:7.975\n#EXT-X-BYTERANGE:51740\nhttps://priv.example.com/fileSequence2682.ts\n\n#EXTINF:7.975,\nhttps://priv.example.com/fileSequence2683.ts\n\n#EXTINF:2.002,\nhttps://priv.example.com/fileSequence2684.ts\n#EXT-X-ENDLIST";
    const std::string inputStream = playlistString;
    auto playlist = HlsPlaylistParser().Parse(playlistUri, inputStream);
    auto mediaPlaylist = MediaOf(playlist);
    CHECK_EQ(mediaPlaylist->playlistType, HlsMediaPlaylist::PLAYLIST_TYPE_VOD);
    CHECK_EQ(mediaPlaylist->startOffsetUs, mediaPlaylist->durationUs - 25000000);
    CHECK_EQ(mediaPlaylist->mediaSequence, 2679);
    CHECK_EQ(mediaPlaylist->version, 3);
    CHECK(mediaPlaylist->hasEndTag);
    CHECK(IsNull(mediaPlaylist->protectionSchemes));
    CHECK_EQ(mediaPlaylist->targetDurationUs, 8000000);
    CHECK_EQ(mediaPlaylist->partTargetDurationUs, stream::kTimeUnset);
    auto segments = mediaPlaylist->segments;
    CHECK(!IsNull(segments));
    CHECK_EQ(segments.size(), std::size_t(6));
    auto segment = segments[0];
    CHECK_EQ(mediaPlaylist->discontinuitySequence + segment.relativeDiscontinuitySequence, 4);
    CHECK_EQ(segment.durationUs, 7975000);
    CHECK_EQ(segment.title, "");
    CHECK(IsNull(segment.fullSegmentEncryptionKeyUri));
    CHECK(IsNull(segment.encryptionIV));
    CHECK_EQ(segment.byteRangeLength, 51370);
    CHECK_EQ(segment.byteRangeOffset, 0);
    CHECK_EQ(segment.url, "https://priv.example.com/fileSequence2679.ts");
    segment = segments[1];
    CHECK_EQ(segment.relativeDiscontinuitySequence, 0);
    CHECK_EQ(segment.durationUs, 7975000);
    CHECK_EQ(segment.title, "segment title");
    CHECK_EQ(segment.fullSegmentEncryptionKeyUri, "https://priv.example.com/key.php?r=2680");
    CHECK_EQ(segment.encryptionIV, "0x1566B");
    CHECK_EQ(segment.byteRangeLength, 51501);
    CHECK_EQ(segment.byteRangeOffset, 2147483648LL);
    CHECK_EQ(segment.url, "https://priv.example.com/fileSequence2680.ts");
    segment = segments[2];
    CHECK_EQ(segment.relativeDiscontinuitySequence, 0);
    CHECK_EQ(segment.durationUs, 7941000);
    CHECK_EQ(segment.title, "segment title .,:/# with interesting chars");
    CHECK(IsNull(segment.fullSegmentEncryptionKeyUri));
    CHECK(IsNull(segment.encryptionIV));
    CHECK_EQ(segment.byteRangeLength, 51501);
    CHECK_EQ(segment.byteRangeOffset, 2147535149LL);
    CHECK_EQ(segment.url, "https://priv.example.com/fileSequence2681.ts");
    segment = segments[3];
    CHECK_EQ(segment.relativeDiscontinuitySequence, 1);
    CHECK_EQ(segment.durationUs, 7975000);
    CHECK_EQ(segment.title, "");
    CHECK_EQ(segment.fullSegmentEncryptionKeyUri, "https://priv.example.com/key.php?r=2682");
    CHECK(!IsNull(segment.encryptionIV));
    CHECK(EqualsIgnoreCase(segment.encryptionIV, "A7A"));
    CHECK_EQ(segment.byteRangeLength, 51740);
    CHECK_EQ(segment.byteRangeOffset, 2147586650LL);
    CHECK_EQ(segment.url, "https://priv.example.com/fileSequence2682.ts");
    segment = segments[4];
    CHECK_EQ(segment.relativeDiscontinuitySequence, 1);
    CHECK_EQ(segment.durationUs, 7975000);
    CHECK_EQ(segment.title, "");
    CHECK_EQ(segment.fullSegmentEncryptionKeyUri, "https://priv.example.com/key.php?r=2682");
    CHECK(!IsNull(segment.encryptionIV));
    CHECK(EqualsIgnoreCase(segment.encryptionIV, "A7B"));
    CHECK_EQ(segment.byteRangeLength, stream::kLengthUnset);
    CHECK_EQ(segment.byteRangeOffset, 0);
    CHECK_EQ(segment.url, "https://priv.example.com/fileSequence2683.ts");
    segment = segments[5];
    CHECK_EQ(segment.durationUs, 2002000);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withByteRanges) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n\n#EXT-X-BYTERANGE:200@100\n#EXT-X-MAP:URI=\"stream.mp4\"\n#EXTINF:5,\n#EXT-X-BYTERANGE:400\nstream.mp4\n#EXTINF:5,\n#EXT-X-BYTERANGE:500\nstream.mp4\n#EXT-X-DISCONTINUITY\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:5,\nsegment.mp4\n";
    const std::string inputStream = playlistString;
    auto playlist = HlsPlaylistParser().Parse(playlistUri, inputStream);
    auto mediaPlaylist = MediaOf(playlist);
    auto segments = mediaPlaylist->segments;
    CHECK(!IsNull(segments));
    CHECK_EQ(segments.size(), std::size_t(3));
    auto segment = segments[0];
    CHECK_EQ(segment.initializationSegment->byteRangeOffset, 100);
    CHECK_EQ(segment.initializationSegment->byteRangeLength, 200);
    CHECK_EQ(segment.byteRangeOffset, 300);
    CHECK_EQ(segment.byteRangeLength, 400);
    segment = segments[1];
    CHECK_EQ(segment.byteRangeOffset, 700);
    CHECK_EQ(segment.byteRangeLength, 500);
    segment = segments[2];
    CHECK_EQ(segment.initializationSegment->byteRangeOffset, 0);
    CHECK_EQ(segment.initializationSegment->byteRangeLength, stream::kLengthUnset);
    CHECK_EQ(segment.byteRangeOffset, 0);
    CHECK_EQ(segment.byteRangeLength, stream::kLengthUnset);
}

TEST(HlsMediaPlaylistParser_parseSampleAesMethod) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:8,\nhttps://priv.example.com/1.ts\n\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"data:text/plain;base64,VGhpcyBpcyBhbiBlYXN0ZXIgZWdn\",IV=0x9358382AEB449EE23C3D809DA0B9CCD3,KEYFORMATVERSIONS=\"1\",KEYFORMAT=\"com.widevine\",IV=0x1566B\n#EXTINF:8,\nhttps://priv.example.com/2.ts\n#EXT-X-ENDLIST\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->protectionSchemes->schemeType, std::string(stream::kCencTypeCbcs));
    CHECK(playlist->protectionSchemes->schemeDatas[0].Matches(stream::kWidevineUuid));
    CHECK(!(playlist->protectionSchemes->schemeDatas[0].HasData()));
    CHECK(IsNull(playlist->segments[0].drmInitData));
    CHECK(playlist->segments[1].drmInitData->schemeDatas[0].Matches(stream::kWidevineUuid));
    CHECK(playlist->segments[1].drmInitData->schemeDatas[0].HasData());
}

TEST(HlsMediaPlaylistParser_parseSampleAesCencMethod) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:8,\nhttps://priv.example.com/1.ts\n\n#EXT-X-KEY:URI=\"data:text/plain;base64,VGhpcyBpcyBhbiBlYXN0ZXIgZWdn\",IV=0x9358382AEB449EE23C3D809DA0B9CCD3,KEYFORMATVERSIONS=\"1\",KEYFORMAT=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\",IV=0x1566B,METHOD=SAMPLE-AES-CENC \n#EXTINF:8,\nhttps://priv.example.com/2.ts\n#EXT-X-ENDLIST\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->protectionSchemes->schemeType, std::string(stream::kCencTypeCenc));
    CHECK(playlist->protectionSchemes->schemeDatas[0].Matches(stream::kWidevineUuid));
    CHECK(!(playlist->protectionSchemes->schemeDatas[0].HasData()));
}

TEST(HlsMediaPlaylistParser_parseSampleAesCtrMethod) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:8,\nhttps://priv.example.com/1.ts\n\n#EXT-X-KEY:METHOD=SAMPLE-AES-CTR,URI=\"data:text/plain;base64,VGhpcyBpcyBhbiBlYXN0ZXIgZWdn\",IV=0x9358382AEB449EE23C3D809DA0B9CCD3,KEYFORMATVERSIONS=\"1\",KEYFORMAT=\"com.widevine\",IV=0x1566B\n#EXTINF:8,\nhttps://priv.example.com/2.ts\n#EXT-X-ENDLIST\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->protectionSchemes->schemeType, std::string(stream::kCencTypeCenc));
    CHECK(playlist->protectionSchemes->schemeDatas[0].Matches(stream::kWidevineUuid));
    CHECK(!(playlist->protectionSchemes->schemeDatas[0].HasData()));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPartMediaInformation_succeeds) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-SERVER-CONTROL:PART-HOLD-BACK=1.234\n#EXT-X-PART-INF:PART-TARGET=0.5\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-PROGRAM-DATE-TIME:2019-02-14T02:13:36.106Z\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.00008,\nfileSequence266.mp4";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->partTargetDurationUs, 500000);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withoutServerControl_serverControlDefaultValues) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:8,\nhttps://priv.example.com/1.ts\n#EXT-X-ENDLIST\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(!(playlist->serverControl.canBlockReload));
    CHECK_EQ(playlist->serverControl.partHoldBackUs, stream::kTimeUnset);
    CHECK_EQ(playlist->serverControl.holdBackUs, stream::kTimeUnset);
    CHECK_EQ(playlist->serverControl.skipUntilUs, stream::kTimeUnset);
    CHECK(!(playlist->serverControl.canSkipDateRanges));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withServerControl_succeeds) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,HOLD-BACK=18.5,PART-HOLD-BACK=1.234,CAN-SKIP-UNTIL=24.0,CAN-SKIP-DATERANGES=YES\n#EXT-X-PART-INF:PART-TARGET=0.5\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-PROGRAM-DATE-TIME:2019-02-14T02:13:36.106Z\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.00008,\nfileSequence266.mp4";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->serverControl.canBlockReload);
    CHECK_EQ(playlist->serverControl.partHoldBackUs, 1234000);
    CHECK_EQ(playlist->serverControl.holdBackUs, 18500000);
    CHECK_EQ(playlist->serverControl.skipUntilUs, 24000000);
    CHECK(playlist->serverControl.canSkipDateRanges);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withSkippedSegments_correctlyMergedSegments) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string previousPlaylistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:263\n#EXTINF:4.00008,\nfileSequence264.mp4\n#EXT-X-DISCONTINUITY\n#EXTINF:4.00008,\nfileSequence265.mp4\n#EXTINF:4.00008,\nfileSequence266.mp4";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:265\n#EXT-X-SKIP:SKIPPED-SEGMENTS=1\n#EXTINF:4.00008,\nfileSequence266.mp4#EXTINF:4.00008,\nfileSequence267.mp4\n";
    const std::string previousInputStream = previousPlaylistString;
    auto previousPlaylist = MediaOf(HlsPlaylistParser().Parse(playlistUri, previousInputStream));
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser(EmptyMultivariant(), previousPlaylist).Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->segments.size(), std::size_t(3));
    CHECK_EQ(playlist->segments[1].relativeStartTimeUs, 4000080);
    CHECK_EQ(previousPlaylist->segments[0].relativeDiscontinuitySequence, 0);
    CHECK_EQ(previousPlaylist->segments[1].relativeDiscontinuitySequence, 1);
    CHECK_EQ(previousPlaylist->segments[2].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->segments[0].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->segments[1].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->segments[2].relativeDiscontinuitySequence, 1);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withSkippedSegments_correctlyMergedParts) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string previousPlaylistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:264\n#EXT-X-PART:DURATION=2.00000,URI=\"part264.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part264.2.ts\"\n#EXTINF:4.00008,\nfileSequence264.mp4\n#EXT-X-DISCONTINUITY\n#EXT-X-PART:DURATION=2.00000,URI=\"part265.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part265.2.ts\"\n#EXTINF:4.00008,\nfileSequence265.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.2.ts\"\n#EXTINF:4.00008,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:265\n#EXT-X-SKIP:SKIPPED-SEGMENTS=2\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"";
    const std::string previousInputStream = previousPlaylistString;
    auto previousPlaylist = MediaOf(HlsPlaylistParser().Parse(playlistUri, previousInputStream));
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser(EmptyMultivariant(), previousPlaylist).Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->segments.size(), std::size_t(2));
    CHECK_EQ(playlist->segments[0].relativeStartTimeUs, 0);
    CHECK_EQ(playlist->segments[0].parts[0].relativeStartTimeUs, 0);
    CHECK_EQ(playlist->segments[0].parts[0].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->segments[0].parts[1].relativeStartTimeUs, 2000000);
    CHECK_EQ(playlist->segments[0].parts[1].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->segments[1].relativeStartTimeUs, 4000080);
    CHECK_EQ(playlist->segments[1].parts[0].relativeStartTimeUs, 4000080);
    CHECK_EQ(playlist->segments[1].parts[1].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->segments[1].parts[1].relativeStartTimeUs, 6000080);
    CHECK_EQ(playlist->segments[1].parts[1].relativeDiscontinuitySequence, 1);
    CHECK_EQ(playlist->trailingParts[0].relativeStartTimeUs, 8000160);
    CHECK_EQ(playlist->trailingParts[0].relativeDiscontinuitySequence, 1);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withSkippedSegments_startTimeUsPreservedWhenProgramStartTimeRemoved) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string previousPlaylistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:264\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T20:00:00.000Z\n#EXT-X-PART:DURATION=2.00000,URI=\"part264.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part264.2.ts\"\n#EXTINF:4.00008,\nfileSequence264.mp4\n#EXT-X-DISCONTINUITY\n#EXT-X-PART:DURATION=2.00000,URI=\"part265.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part265.2.ts\"\n#EXTINF:4.00008,\nfileSequence265.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.2.ts\"\n#EXTINF:4.00008,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:265\n#EXT-X-SKIP:SKIPPED-SEGMENTS=2\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"";
    const std::string previousInputStream = previousPlaylistString;
    auto previousPlaylist = MediaOf(HlsPlaylistParser().Parse(playlistUri, previousInputStream));
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser(EmptyMultivariant(), previousPlaylist).Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->startTimeUs, 1577995200000000LL);
    CHECK_EQ(previousPlaylist->startTimeUs, 1577995200000000LL);
    CHECK_EQ(playlist->segments.size(), std::size_t(2));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withSkippedSegmentsWithoutProgramStartDateTime_startTimeUsNotPreserved) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string previousPlaylistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:264\n#EXT-X-PART:DURATION=2.00000,URI=\"part264.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part264.2.ts\"\n#EXTINF:4.00008,\nfileSequence264.mp4\n#EXT-X-DISCONTINUITY\n#EXT-X-PART:DURATION=2.00000,URI=\"part265.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part265.2.ts\"\n#EXTINF:4.00008,\nfileSequence265.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.2.ts\"\n#EXTINF:4.00008,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-DISCONTINUITY-SEQUENCE:1234\n#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=24.0\n#EXT-X-MEDIA-SEQUENCE:265\n#EXT-X-SKIP:SKIPPED-SEGMENTS=2\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"";
    const std::string previousInputStream = previousPlaylistString;
    auto previousPlaylist = MediaOf(HlsPlaylistParser().Parse(playlistUri, previousInputStream));
    previousPlaylist = std::make_shared<HlsMediaPlaylist>(previousPlaylist->CopyWith(1000000LL, 0));
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser(EmptyMultivariant(), previousPlaylist).Parse(playlistUri, inputStream));
    CHECK_EQ(previousPlaylist->startTimeUs, 1000000LL);
    CHECK_EQ(playlist->startTimeUs, 0LL);
    CHECK_EQ(playlist->segments.size(), std::size_t(2));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withParts_parsesPartWithAllAttributes) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,GAP=YES,INDEPENDENT=YES,URI=\"part267.1.ts\"\n#EXT-X-PART:DURATION=2.00000,BYTERANGE=\"1000@1234\",URI=\"part267.2.ts\"\n#EXTINF:4.00000,\nfileSequence267.ts\n#EXT-X-PART:DURATION=2.00000, BYTERANGE=\"1000@1234\",URI=\"part268.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part268.2.ts\", BYTERANGE=\"1000\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    CHECK_EQ(playlist->segments[1].parts.size(), std::size_t(2));
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(2));
    auto firstPart = playlist->segments[1].parts[0];
    CHECK_EQ(firstPart.byteRangeLength, stream::kLengthUnset);
    CHECK_EQ(firstPart.byteRangeOffset, 0);
    CHECK_EQ(firstPart.durationUs, 2000000);
    CHECK_EQ(firstPart.relativeStartTimeUs, playlist->segments[0].durationUs);
    CHECK(firstPart.isIndependent);
    CHECK(!(firstPart.isPreload));
    CHECK(firstPart.hasGapTag);
    CHECK_EQ(firstPart.url, "part267.1.ts");
    auto secondPart = playlist->segments[1].parts[1];
    CHECK_EQ(secondPart.byteRangeLength, 1000);
    CHECK_EQ(secondPart.byteRangeOffset, 1234);
    auto thirdPart = playlist->trailingParts[0];
    CHECK_EQ(thirdPart.byteRangeLength, 1000);
    CHECK_EQ(thirdPart.byteRangeOffset, 1234);
    CHECK_EQ(thirdPart.relativeStartTimeUs, 8000000);
    auto lastPart = playlist->trailingParts[1];
    CHECK_EQ(lastPart.relativeStartTimeUs, 10000000);
    CHECK(!(lastPart.hasGapTag));
    CHECK(!(lastPart.isIndependent));
    CHECK_EQ(lastPart.byteRangeLength, 1000);
    CHECK_EQ(lastPart.byteRangeOffset, 2234);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPartAndAesPlayReadyKey_correctDrmInitData) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.microsoft.playready\",URI=\"data:text/plain;base64,RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==\"\n#EXTINF:4.00000,\nfileSequence266.ts\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    CHECK_EQ(playlist->protectionSchemes->schemeType, "cbcs");
    auto part = playlist->trailingParts[0];
    CHECK_EQ(part.drmInitData->schemeType, "cbcs");
    CHECK_EQ(part.drmInitData->schemeDatas.size(), 1);
    CHECK_EQ(part.drmInitData->schemeDatas[0].uuid, stream::kPlayreadyUuid);
    CHECK_EQ(part.drmInitData->schemeDatas[0].data, stream::BuildPsshAtom(stream::kPlayreadyUuid,stream::Base64Decode("RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==")));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPartAndAesPlayReadyWithOutPrecedingSegment_correctDrmInitData) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.microsoft.playready\",URI=\"data:text/plain;base64,RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments.empty());
    CHECK_EQ(playlist->protectionSchemes->schemeType, "cbcs");
    auto part = playlist->trailingParts[0];
    CHECK_EQ(part.drmInitData->schemeType, "cbcs");
    CHECK_EQ(part.drmInitData->schemeDatas.size(), 1);
    CHECK_EQ(part.drmInitData->schemeDatas[0].uuid, stream::kPlayreadyUuid);
    CHECK_EQ(part.drmInitData->schemeDatas[0].data, stream::BuildPsshAtom(stream::kPlayreadyUuid,stream::Base64Decode("RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==")));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPartAndAes128_partHasDrmKeyUriAndIV) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-KEY:METHOD=AES-128,KEYFORMAT=\"identity\", IV=0x410C8AC18AA42EFA18B5155484F5FC34,URI=\"fake://foo.bar/uri\"\n#EXTINF:4.00000,\nfileSequence266.ts\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    auto part = playlist->trailingParts[0];
    CHECK(IsNull(playlist->protectionSchemes));
    CHECK(IsNull(part.drmInitData));
    CHECK_EQ(part.fullSegmentEncryptionKeyUri, "fake://foo.bar/uri");
    CHECK_EQ(part.encryptionIV, "0x410C8AC18AA42EFA18B5155484F5FC34");
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPartAndAes128WithoutPrecedingSegment_partHasDrmKeyUriAndIV) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-KEY:METHOD=AES-128,KEYFORMAT=\"identity\", IV=0x410C8AC18AA42EFA18B5155484F5FC34,URI=\"fake://foo.bar/uri\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments.empty());
    auto part = playlist->trailingParts[0];
    CHECK(IsNull(playlist->protectionSchemes));
    CHECK(IsNull(part.drmInitData));
    CHECK_EQ(part.fullSegmentEncryptionKeyUri, "fake://foo.bar/uri");
    CHECK_EQ(part.encryptionIV, "0x410C8AC18AA42EFA18B5155484F5FC34");
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPreloadHintTypePart_hasPreloadPartWithAllAttributes) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-MAP:URI=\"map.mp4\"\n#EXT-X-KEY:METHOD=AES-128,KEYFORMAT=\"identity\", IV=0x410C8AC18AA42EFA18B5155484F5FC34,URI=\"fake://foo.bar/uri\"\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,BYTERANGE-START=1234,BYTERANGE-LENGTH=1000,URI=\"filePart267.2.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(2));
    auto preloadPart = playlist->trailingParts[1];
    CHECK_EQ(preloadPart.durationUs, 0LL);
    CHECK_EQ(preloadPart.url, "filePart267.2.ts");
    CHECK_EQ(preloadPart.byteRangeLength, 1000);
    CHECK_EQ(preloadPart.byteRangeOffset, 1234);
    CHECK_EQ(preloadPart.initializationSegment->url, "map.mp4");
    CHECK_EQ(preloadPart.encryptionIV, "0x410C8AC18AA42EFA18B5155484F5FC34");
    CHECK(preloadPart.isPreload);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withMultiplePreloadHintTypeParts_picksOnlyFirstPreloadPart) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.2.ts\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.3.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(2));
    CHECK_EQ(playlist->trailingParts[1].url, "filePart267.2.ts");
    CHECK(playlist->trailingParts[1].isPreload);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withUnboundedPreloadHintTypePart_ignoresPreloadPart) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.2.ts,BYTERANGE-START=0\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(1));
    CHECK_EQ(playlist->trailingParts.back().url, "part267.1.ts");
    CHECK(!(playlist->trailingParts.back().isPreload));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPreloadHintTypePartAndAesPlayReadyKey_inheritsDrmInitData) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.microsoft.playready\",URI=\"data:text/plain;base64,RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==\"\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.2.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(1));
    CHECK_EQ(playlist->protectionSchemes->schemeDatas.size(), 1);
    auto preloadPart = playlist->trailingParts[0];
    CHECK_EQ(preloadPart.drmInitData->schemeType, "cbcs");
    CHECK_EQ(preloadPart.drmInitData->schemeDatas.size(), 1);
    CHECK_EQ(preloadPart.drmInitData->schemeDatas[0].uuid, stream::kPlayreadyUuid);
    CHECK_EQ(preloadPart.drmInitData->schemeDatas[0].data, stream::BuildPsshAtom(stream::kPlayreadyUuid,stream::Base64Decode("RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==")));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPreloadHintTypePartAndNewAesPlayReadyKey_correctDrmInitData) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.microsoft.playready\",URI=\"data:text/plain;base64,RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.2.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(1));
    CHECK_EQ(playlist->protectionSchemes->schemeDatas.size(), 1);
    auto preloadPart = playlist->trailingParts[0];
    CHECK_EQ(preloadPart.drmInitData->schemeType, "cbcs");
    CHECK_EQ(preloadPart.drmInitData->schemeDatas.size(), 1);
    CHECK_EQ(preloadPart.drmInitData->schemeDatas[0].uuid, stream::kPlayreadyUuid);
    CHECK_EQ(preloadPart.drmInitData->schemeDatas[0].data, stream::BuildPsshAtom(stream::kPlayreadyUuid,stream::Base64Decode("RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==")));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withPreloadHintTypePartAndAes128_partHasDrmKeyUriAndIV) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-KEY:METHOD=AES-128,KEYFORMAT=\"identity\", IV=0x410C8AC18AA42EFA18B5155484F5FC34,URI=\"fake://foo.bar/uri\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.2.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(playlist->segments[0].parts.empty());
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(1));
    auto preloadPart = playlist->trailingParts[0];
    CHECK(IsNull(preloadPart.drmInitData));
    CHECK_EQ(preloadPart.fullSegmentEncryptionKeyUri, "fake://foo.bar/uri");
    CHECK_EQ(preloadPart.encryptionIV, "0x410C8AC18AA42EFA18B5155484F5FC34");
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportWithoutPartTargetDuration_lastPartIndexUnset) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-MSN=100\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 100);
    CHECK_EQ(report0.lastPartIndex, stream::kIndexUnset);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportWithoutPartTargetDurationWithoutLastMsn_sameLastMsnAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 266);
    CHECK_EQ(report0.lastPartIndex, stream::kIndexUnset);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportLowLatency_parseAllAttributes) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-MSN=100,LAST-PART=2\n#EXT-X-RENDITION-REPORT:URI=\"http://foo.bar/rendition2.m3u8\",LAST-MSN=1000,LAST-PART=3\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(2));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 100);
    CHECK_EQ(report0.lastPartIndex, 2);
    auto report2 = playlist->renditionReports.at("http://foo.bar/rendition2.m3u8");
    CHECK_EQ(report2.lastMediaSequence, 1000);
    CHECK_EQ(report2.lastPartIndex, 3);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportLowLatencyWithoutLastMsn_sameMsnAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.0.ts\"\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-PART=2\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 267);
    CHECK_EQ(report0.lastPartIndex, 2);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportBeforeSegmentsWithoutPartTargetDurationWithoutLastMsn_sameLastMsnAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\"\n#EXTINF:4.00000,\nfileSequence266.mp4\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report.lastMediaSequence, 266);
    CHECK_EQ(report.lastPartIndex, stream::kIndexUnset);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportBeforeSegementsDefaultMsn_sameMsnAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-PART=2\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.0.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report.lastMediaSequence, 267);
    CHECK_EQ(report.lastPartIndex, 2);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportBeforeSegementsDefaultLastPart_sameLastPartIndexAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-MSN=267\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.0.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report.lastMediaSequence, 267);
    CHECK_EQ(report.lastPartIndex, 1);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportBeforeSegements_sameMsnAndLastPartIndexAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\"\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.0.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.1.ts\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report.lastMediaSequence, 267);
    CHECK_EQ(report.lastPartIndex, 1);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportLowLatencyWithoutLastPartIndex_sameLastPartIndexAsCurrentPlaylist) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.0.ts\"\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-MSN=267\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 267);
    CHECK_EQ(report0.lastPartIndex, 0);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportLowLatencyWithoutLastPartIndex_ignoredPreloadPart) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PART:DURATION=2.00000,URI=\"part267.0.ts\"\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.1.ts\"\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\",LAST-MSN=267\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->trailingParts.size(), std::size_t(2));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 267);
    CHECK_EQ(report0.lastPartIndex, 0);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withRenditionReportLowLatencyFullSegment_rollingPartIndexUriParam) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART-INF:PART-TARGET=1\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:266\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.0.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.1.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.2.ts\"\n#EXT-X-PART:DURATION=2.00000,URI=\"part266.3.ts\"\n#EXTINF:4.00000,\nfileSequence266.mp4\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"filePart267.0.ts\"\n#EXT-X-RENDITION-REPORT:URI=\"/rendition0.m3u8\"\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->renditionReports.size(), std::size_t(1));
    auto report0 = playlist->renditionReports.at("https://example.com/rendition0.m3u8");
    CHECK_EQ(report0.lastMediaSequence, 266);
    CHECK_EQ(report0.lastPartIndex, 3);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialDateRanges) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad1\",START-DATE=\"2020-01-02T21:55:44.000Z\",END-DATE=\"2020-01-02T21:55:59.000Z\",DURATION=15.0,PLANNED-DURATION=15.13,CUE=\" PRE \",END-ON-NEXT=YES, X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-RESUME-OFFSET=-1.1, X-PLAYOUT-LIMIT=15.112, X-SNAP=\"    IN  , OUT  \", X-RESTRICT=\"SKIP ,  JUMP\",X-CONTENT-MAY-VARY=\"NO\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"PRIMARY\",X-GOOGLE-TEST-TEXT=\"Value at the end\"\n#EXT-X-DATERANGE:X-GOOGLE-TEST-HEX=0XAB10A,ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T22:55:44.000Z\",END-DATE=\"2020-01-02T22:55:49.120Z\",DURATION=5.12,PLANNED-DURATION=5.11,CUE=\"  POST,    ONCE\",X-GOOGLE-TEST-DOUBLE1=12.123,X-CONTENT-MAY-VARY=\"YES\",X-TIMELINE-OCCUPIES=\"POINT\",X-TIMELINE-STYLE=\"HIGHLIGHT\",X-ASSET-LIST=\"http://example.com/ad2-assets.json\"X-GOOGLE-TEST-DOUBLE2=1\n";
    std::int64_t playlistStartTimeUs = (stream::ParseXsDateTime("2020-01-02T21:55:40.000Z") * 1000);
    auto interstitial1 = MakeInterstitial( "ad1", "http://example.com/ad1.m3u8", "", playlistStartTimeUs + 4000000LL, playlistStartTimeUs + 4000000LL + 15000000LL, 15000000LL, 15130000LL, {Interstitial::CUE_TRIGGER_PRE}, true, -1100000LL, 15112000LL, {Interstitial::SNAP_TYPE_IN, Interstitial::SNAP_TYPE_OUT}, {Interstitial::NAVIGATION_RESTRICTION_SKIP, Interstitial::NAVIGATION_RESTRICTION_JUMP}, { ClientDefinedAttribute( "X-GOOGLE-TEST-TEXT", "Value at the end", ClientDefinedAttribute::TYPE_TEXT)}, false, Interstitial::TIMELINE_OCCUPIES_RANGE, Interstitial::TIMELINE_STYLE_PRIMARY, stream::kTimeUnset, stream::kTimeUnset, "");
    auto interstitial2 = MakeInterstitial( "ad2", "", "http://example.com/ad2-assets.json", playlistStartTimeUs + 4000000LL + (60LL * 60LL * 1000000LL), playlistStartTimeUs + 4000000LL + 5120000LL + (60LL * 60LL * 1000000LL), 5120000LL, 5110000LL, {Interstitial::CUE_TRIGGER_POST, Interstitial::CUE_TRIGGER_ONCE}, false, stream::kTimeUnset, stream::kTimeUnset, {}, {}, { ClientDefinedAttribute("X-GOOGLE-TEST-DOUBLE1", 12.123), ClientDefinedAttribute("X-GOOGLE-TEST-DOUBLE2", 1), ClientDefinedAttribute( "X-GOOGLE-TEST-HEX", "0XAB10A", ClientDefinedAttribute::TYPE_HEX_TEXT)}, true, Interstitial::TIMELINE_OCCUPIES_POINT, Interstitial::TIMELINE_STYLE_HIGHLIGHT, stream::kTimeUnset, stream::kTimeUnset, "");
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(SameList(playlist->interstitials, { interstitial1, interstitial2 }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialsVodWithAssetUriOnly_correctlyParseAllAttributes) {
    std::string playlistUri = "http://example.com/interstitials_vod_asset_uri.m3u8";
    const std::string inputStream = INTERSTITIALS_VOD_ASSET_URI;
    auto interstitial1 = MakeInterstitial( "preroll", "https://example.com/0.m3u8?parse=1", "", (stream::ParseXsDateTime("2024-11-11T04:35:19.353-08:00") * 1000), stream::kTimeUnset, stream::kTimeUnset, stream::kTimeUnset, {"PRE"}, false, 0LL, stream::kTimeUnset, {}, {"JUMP", "SKIP"}, {}, true, Interstitial::TIMELINE_OCCUPIES_POINT, Interstitial::TIMELINE_STYLE_HIGHLIGHT, stream::kTimeUnset, stream::kTimeUnset, "");
    auto interstitial2 = MakeInterstitial( "midroll-1", "https://example.com/1.m3u8?parse=1", "", (stream::ParseXsDateTime("2024-11-11T04:35:34.353-08:00") * 1000), stream::kTimeUnset, stream::kTimeUnset, stream::kTimeUnset, {}, false, 0LL, stream::kTimeUnset, {}, {"JUMP", "SKIP"}, {}, true, Interstitial::TIMELINE_OCCUPIES_POINT, Interstitial::TIMELINE_STYLE_HIGHLIGHT, stream::kTimeUnset, stream::kTimeUnset, "");
    auto interstitial3 = MakeInterstitial( "postroll", "https://example.com/2.m3u8?parse=1", "", (stream::ParseXsDateTime("2024-11-11T04:36:31.353-08:00") * 1000), stream::kTimeUnset, stream::kTimeUnset, stream::kTimeUnset, {"POST"}, false, 0LL, stream::kTimeUnset, {}, { Interstitial::NAVIGATION_RESTRICTION_JUMP, Interstitial::NAVIGATION_RESTRICTION_SKIP}, {}, true, Interstitial::TIMELINE_OCCUPIES_POINT, Interstitial::TIMELINE_STYLE_HIGHLIGHT, stream::kTimeUnset, stream::kTimeUnset, "");
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(SameList(playlist->interstitials, { interstitial1, interstitial2, interstitial3 }));
    CHECK_EQ(playlist->startTimeUs, (stream::ParseXsDateTime("2024-11-11T04:35:19.353-08:00") * 1000));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withDuration_durationSetCorrectly) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad0-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:41.123Z\",DURATION=2.222,X-ASSET-URI=\"http://example.com/media-0-0.m3u8\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto mediaPlaylist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK_EQ(mediaPlaylist->interstitials[0].durationUs, 2222000LL);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInvalidDuration_durationUnset) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad0-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:41.123Z\",DURATION=-2.222,PLANNED-DURATION=- 2.222,X-PLAYOUT-LIMIT=-2.222,X-RESUME-OFFSET=24.953741497,X-ASSET-URI=\"http://example.com/media-0-0.m3u8\"\n#EXT-X-DATERANGE:ID=\"ad0-1\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:41.123Z\",DURATION=\"2.222\",PLANNED-DURATION=\"2.222\",X-PLAYOUT-LIMIT=\"2.222\",X-RESUME-OFFSET=-24.953741497,X-ASSET-URI=\"http://example.com/media-0-0.m3u8\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto mediaPlaylist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK_EQ(mediaPlaylist->interstitials[0].id, "ad0-0");
    CHECK_EQ(mediaPlaylist->interstitials[0].durationUs, stream::kTimeUnset);
    CHECK_EQ(mediaPlaylist->interstitials[0].plannedDurationUs, stream::kTimeUnset);
    CHECK_EQ(mediaPlaylist->interstitials[0].playoutLimitUs, stream::kTimeUnset);
    CHECK_EQ(mediaPlaylist->interstitials[0].resumeOffsetUs, 24953741LL);
    CHECK_EQ(mediaPlaylist->interstitials[1].id, "ad0-1");
    CHECK_EQ(mediaPlaylist->interstitials[1].durationUs, stream::kTimeUnset);
    CHECK_EQ(mediaPlaylist->interstitials[1].plannedDurationUs, stream::kTimeUnset);
    CHECK_EQ(mediaPlaylist->interstitials[1].playoutLimitUs, stream::kTimeUnset);
    CHECK_EQ(mediaPlaylist->interstitials[1].resumeOffsetUs, -24953741LL);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withDurationAfterPlannedDuration_durationSetCorrectly) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad0-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:41.123Z\",PLANNED-DURATION=1.111,DURATION=2.222,X-ASSET-URI=\"http://example.com/media-0-0.m3u8\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto mediaPlaylist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK_EQ(mediaPlaylist->interstitials[0].durationUs, 2222000LL);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithSkipControlAttributes_correctlyParsed) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad1\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-SKIP-CONTROL-OFFSET=5.5,X-SKIP-CONTROL-DURATION=10.0,X-SKIP-CONTROL-LABEL-ID=\"skip-label\"\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad2\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-SKIP-CONTROL-OFFSET=15.5,X-SKIP-CONTROL-DURATION=110.0,\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad3\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->interstitials.size(), std::size_t(3));
    auto interstitial = playlist->interstitials[0];
    CHECK_EQ(interstitial.skipControlOffsetUs, 5500000LL);
    CHECK_EQ(interstitial.skipControlDurationUs, 10000000LL);
    CHECK_EQ(interstitial.skipControlLabelId, "skip-label");
    auto interstitial2 = playlist->interstitials[1];
    CHECK_EQ(interstitial2.skipControlOffsetUs, 15500000LL);
    CHECK_EQ(interstitial2.skipControlDurationUs, 110000000LL);
    CHECK(IsNull(interstitial2.skipControlLabelId));
    auto interstitial3 = playlist->interstitials[2];
    CHECK_EQ(interstitial3.skipControlOffsetUs, stream::kTimeUnset);
    CHECK_EQ(interstitial3.skipControlDurationUs, stream::kTimeUnset);
    CHECK(IsNull(interstitial3.skipControlLabelId));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithInvalidSkipControlAttributes_isRobust) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad1\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-SKIP-CONTROL-OFFSET=invalid,X-SKIP-CONTROL-DURATION=10.0\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad2\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-SKIP-CONTROL-OFFSET=5.5,X-SKIP-CONTROL-DURATION=invalid\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad3\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-SKIP-CONTROL-OFFSET=-5.5,X-SKIP-CONTROL-DURATION=10.0\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",ID=\"ad4\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",X-SKIP-CONTROL-OFFSET=5.5,X-SKIP-CONTROL-DURATION=-10.0\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->interstitials.size(), std::size_t(4));
    auto interstitial1 = playlist->interstitials[0];
    CHECK_EQ(interstitial1.skipControlOffsetUs, stream::kTimeUnset);
    CHECK_EQ(interstitial1.skipControlDurationUs, 10000000LL);
    auto interstitial2 = playlist->interstitials[1];
    CHECK_EQ(interstitial2.skipControlOffsetUs, 5500000LL);
    CHECK_EQ(interstitial2.skipControlDurationUs, stream::kTimeUnset);
    auto interstitial3 = playlist->interstitials[2];
    CHECK_EQ(interstitial3.skipControlOffsetUs, stream::kTimeUnset);
    CHECK_EQ(interstitial3.skipControlDurationUs, 10000000LL);
    auto interstitial4 = playlist->interstitials[3];
    CHECK_EQ(interstitial4.skipControlOffsetUs, 5500000LL);
    CHECK_EQ(interstitial4.skipControlDurationUs, stream::kTimeUnset);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withDurationBeforePlannedDuration_durationSetCorrectly) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:DURATION=2.234,ID=\"ad0-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:41.123Z\",PLANNED-DURATION=3.456,X-ASSET-URI=\"http://example.com/media-0-0.m3u8\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto mediaPlaylist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK_EQ(mediaPlaylist->interstitials[0].durationUs, 2234000LL);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithoutId_throwsParserException) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:CLASS=\"com.apple.hls.interstitial\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    CHECK(Throws<stream::ParserException>([&] { hlsPlaylistParser.Parse( playlistUri, playlistString); }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withDateRangeWithUnknownClass_dateRangeIgnored) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"some.other.class\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto playlist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK(playlist->interstitials.empty());
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withDateRangeWithMissingClass_dateRangeIgnored) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",X-ASSET-URI=\"http://example.com/ad1.m3u8\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto playlist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK(playlist->interstitials.empty());
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_interstitialWithoutStartDate_ignored) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",X-ASSET-LIST=\"http://example.com/ad2-assets.json\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto playlist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK(playlist->interstitials.empty());
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithAssetUriAndList_interstitialIgnored) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:44.000Z\",X-ASSET-URI=\"http://example.com/media.m3u8\",X-ASSET-LIST=\"http://example.com/ad2-assets.json\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto hlsMediaPlaylist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK(hlsMediaPlaylist->interstitials.empty());
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithNeitherAssetUriNorAssetList_interstitialIgnored) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    auto hlsMediaPlaylist = MediaOf(hlsPlaylistParser.Parse( playlistUri, playlistString));
    CHECK(hlsMediaPlaylist->interstitials.empty());
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithUnknownRestrictions_ignoredForForwardCompatibility) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",X-RESTRICT=\"SKIP , FLY , ROW , JUMP\",X-ASSET-URI=\"http://example.com/media.m3u8\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, playlistString));
    CHECK(SameList(playlist->interstitials[0].restrictions, { Interstitial::NAVIGATION_RESTRICTION_SKIP, Interstitial::NAVIGATION_RESTRICTION_JUMP }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithUnknownCueIdentifier_ignoredForForwardCompatibility) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",X-CUE=\"ALONE , PRE , INBETWEEN , BEYOND\",X-ASSET-URI=\"http://example.com/media.m3u8\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, playlistString));
    CHECK(SameList(playlist->interstitials[0].cue, { Interstitial::CUE_TRIGGER_PRE }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithUnknownSnapTypes_ignoredForForwardCompatibility) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2020-01-02T21:55:40.000Z\n#EXTINF:6,\nmain1.0.ts\n#EXT-X-ENDLIST\n#EXT-X-DATERANGE:ID=\"ad2\",CLASS=\"com.apple.hls.interstitial\",X-SNAP=\"    FOR  , OUT  , AT\", X-ASSET-URI=\"http://example.com/media.m3u8\",START-DATE=\"2020-01-02T21:55:44.000Z\"\n";
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, playlistString));
    CHECK(SameList(playlist->interstitials[0].snapTypes, { Interstitial::SNAP_TYPE_OUT }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialWithUpdatingDateRange) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXTINF:10.007800,\naudio0000.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",PLANNED-DURATION=25,X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",X-SNAP=\"OUT,IN\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"HIGHLIGHT\",X-CONTENT-MAY-VARY=\"YES\"\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",END-DATE=\"2024-09-20T15:29:49.006Z\",X-PLAYOUT-LIMIT=24.953741497,X-RESUME-OFFSET=24.953741497\n";
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, playlistString));
    CHECK_EQ(playlist->interstitials.size(), std::size_t(1));
    CHECK_EQ(playlist->interstitials[0].resumeOffsetUs, 24953741LL);
    CHECK_EQ(playlist->interstitials[0].endDateUnixUs, 1726846189006000LL);
    CHECK_EQ(playlist->interstitials[0].playoutLimitUs, 24953741LL);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialStartDateInvalidUpdate_throwsIllegalArgumentException) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXTINF:10.007800,\naudio0000.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",PLANNED-DURATION=25,X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",X-SNAP=\"OUT,IN\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"HIGHLIGHT\",X-CONTENT-MAY-VARY=\"YES\"\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:25.006Z\",END-DATE=\"2024-09-20T15:29:49.006Z\",X-PLAYOUT-LIMIT=24.953741497,X-RESUME-OFFSET=24.953741497\n";
    HlsPlaylistParser hlsPlaylistParser;
    const std::string inputStream = playlistString;
    CHECK(Throws<std::invalid_argument>([&] { hlsPlaylistParser.Parse(playlistUri, inputStream); }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialTimelineAttributeChanged_throwsIllegalArgumentException) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXT-X-DATERANGE:ID=\"ID-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"HIGHLIGHT\",\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"ID-0\",X-TIMELINE-STYLE=\"PRIMARY\",CLASS=\"com.apple.hls.interstitial\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    const std::string inputStream = playlistString;
    CHECK(Throws<std::invalid_argument>([&] { hlsPlaylistParser.Parse(playlistUri, inputStream); }));
    playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXT-X-DATERANGE:ID=\"ID-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"HIGHLIGHT\",\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"ID-0\",X-TIMELINE-OCCUPIES=\"POINT\",CLASS=\"com.apple.hls.interstitial\"\n";
    const std::string inputStream2 = playlistString;
    CHECK(Throws<std::invalid_argument>([&] { hlsPlaylistParser.Parse(playlistUri, inputStream2); }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialTimelineAttributeUnsetThenOverridden_updateAccepted) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXT-X-DATERANGE:ID=\"ID-0\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"ID-0\",X-CONTENT-MAY-VARY=\"NO\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"PRIMARY\",CLASS=\"com.apple.hls.interstitial\"\n";
    HlsPlaylistParser hlsPlaylistParser;
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(hlsPlaylistParser.Parse(playlistUri, inputStream));
    CHECK(!(playlist->interstitials[0].contentMayVary));
    CHECK_EQ(playlist->interstitials[0].timelineOccupies, Interstitial::TIMELINE_OCCUPIES_RANGE);
    CHECK_EQ(playlist->interstitials[0].timelineStyle, Interstitial::TIMELINE_STYLE_PRIMARY);
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialClientDefinedAttributeInvalidUpdate_throwsIllegalArgumentException) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXTINF:10.007800,\naudio0000.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",PLANNED-DURATION=25,X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",X-SNAP=\"OUT,IN\",X-TIMELINE-OCCUPIES=\"RANGE\",X-TIMELINE-STYLE=\"HIGHLIGHT\",X-CONTENT-MAY-VARY=\"YES\"\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",END-DATE=\"2024-09-20T15:29:49.006Z\",X-PLAYOUT-LIMIT=24.953741497,X-CONTENT-MAY-VARY=\"NO\",X-RESUME-OFFSET=24.953741497\n";
    HlsPlaylistParser hlsPlaylistParser;
    const std::string inputStream = playlistString;
    CHECK(Throws<std::invalid_argument>([&] { hlsPlaylistParser.Parse(playlistUri, inputStream); }));
}

TEST(HlsMediaPlaylistParser_parseMediaPlaylist_withInterstitialClientDefinedAttribute) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2024-09-20T15:29:20.000Z\n#EXTINF:10.007800,\naudio0000.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"2024-09-20T15:29:24.006Z\",PLANNED-DURATION=25,X-ASSET-LIST=\"myapp://interstitial/req?_HLS_interstitial_id=15943\",X-SNAP=\"OUT,IN\",X-CONTENT-MAY-VARY=\"YES\"\n#EXTINF:10.007800,\naudio0001.ts\n#EXT-X-DATERANGE:ID=\"15943\",CLASS=\"com.apple.hls.interstitial\",END-DATE=\"2024-09-20T15:29:49.006Z\",X-PLAYOUT-LIMIT=24.953741497,X-CUSTOM-ATTRIBUTE=\"YES\",X-RESUME-OFFSET=24.953741497\n";
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, playlistString));
    CHECK_EQ(playlist->interstitials.size(), std::size_t(1));
    auto clientDefinedAttributes = playlist->interstitials[0].clientDefinedAttributes;
    CHECK_EQ(clientDefinedAttributes.size(), std::size_t(1));
    auto clientDefinedAttribute = clientDefinedAttributes[0];
    CHECK_EQ(clientDefinedAttribute.name, "X-CUSTOM-ATTRIBUTE");
    CHECK_EQ(clientDefinedAttribute.GetTextValue(), "YES");
}

TEST(HlsMediaPlaylistParser_multipleExtXKeysForSingleSegment) {
    std::string playlistUri = "https://example.com/test.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-TARGETDURATION:6\n#EXT-X-MAP:URI=\"map.mp4\"\n#EXTINF:5.005,\ns000000.mp4\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\",KEYFORMATVERSIONS=\"1\",URI=\"data:text/plain;base64,Tm90aGluZyB0byBzZWUgaGVyZQ==\"\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.microsoft.playready\",KEYFORMATVERSIONS=\"1\",URI=\"data:text/plain;charset=UTF-16;base64,VGhpcyBpcyBhbiBlYXN0ZXIgZWdn\"\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.apple.streamingkeydelivery\",KEYFORMATVERSIONS=\"1\",URI=\"skd://QW5vdGhlciBlYXN0ZXIgZWdn\"\n#EXT-X-MAP:URI=\"map.mp4\"\n#EXTINF:5.005,\ns000000.mp4\n#EXTINF:5.005,\ns000001.mp4\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\",KEYFORMATVERSIONS=\"1\",URI=\"data:text/plain;base64,RG9uJ3QgeW91IGdldCB0aXJlZCBvZiBkb2luZyB0aGlzPw==\"\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.microsoft.playready\",KEYFORMATVERSIONS=\"1\",URI=\"data:text/plain;charset=UTF-16;base64,T2ssIGl0J3Mgbm90IGZ1biBhbnltb3Jl\"\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.apple.streamingkeydelivery\",KEYFORMATVERSIONS=\"1\",URI=\"skd://V2FpdCB1bnRpbCB5b3Ugc2VlIHRoZSBuZXh0IG9uZSE=\"\n#EXTINF:5.005,\ns000024.mp4\n#EXTINF:5.005,\ns000025.mp4\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:5.005,\ns000026.mp4\n#EXTINF:5.005,\ns000026.mp4\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK_EQ(playlist->protectionSchemes->schemeType, std::string(stream::kCencTypeCbcs));
    CHECK_EQ(playlist->protectionSchemes->schemeDatas.size(), 2);
    CHECK(playlist->protectionSchemes->schemeDatas[0].Matches(stream::kPlayreadyUuid));
    CHECK(!(playlist->protectionSchemes->schemeDatas[0].HasData()));
    CHECK(playlist->protectionSchemes->schemeDatas[1].Matches(stream::kWidevineUuid));
    CHECK(!(playlist->protectionSchemes->schemeDatas[1].HasData()));
    CHECK(IsNull(playlist->segments[0].drmInitData));
    CHECK(playlist->segments[1].drmInitData->schemeDatas[0].Matches(stream::kPlayreadyUuid));
    CHECK(playlist->segments[1].drmInitData->schemeDatas[0].HasData());
    CHECK(playlist->segments[1].drmInitData->schemeDatas[1].Matches(stream::kWidevineUuid));
    CHECK(playlist->segments[1].drmInitData->schemeDatas[1].HasData());
    CHECK_EQ(playlist->segments[1].drmInitData, playlist->segments[2].drmInitData);
    CHECK(!(playlist->segments[2].drmInitData == playlist->segments[3].drmInitData));
    CHECK(playlist->segments[3].drmInitData->schemeDatas[0].Matches(stream::kPlayreadyUuid));
    CHECK(playlist->segments[3].drmInitData->schemeDatas[0].HasData());
    CHECK(playlist->segments[3].drmInitData->schemeDatas[1].Matches(stream::kWidevineUuid));
    CHECK(playlist->segments[3].drmInitData->schemeDatas[1].HasData());
    CHECK_EQ(playlist->segments[3].drmInitData, playlist->segments[4].drmInitData);
    CHECK(IsNull(playlist->segments[5].drmInitData));
    CHECK(IsNull(playlist->segments[6].drmInitData));
}

TEST(HlsMediaPlaylistParser_gapTag) {
    std::string playlistUri = "https://example.com/test2.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PROGRAM-DATE-TIME:2016-09-22T02:00:01+00:00\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://example.com/key?value=something\"\n#EXTINF:5.005,\n02/00/27.ts\n#EXTINF:5.005,\n02/00/32.ts\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:5.005,\n#EXT-X-GAP \n../test.ts\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://key-service.bamgrid.com/1.0/key?hex-value=9FB8989D15EEAAF8B21B860D7ED3072A\",IV=0x410C8AC18AA42EFA18B5155484F5FC34\n#EXTINF:5.005,\n02/00/42.ts\n#EXTINF:5.005,\n02/00/47.ts\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(!(playlist->hasEndTag));
    CHECK(!(playlist->segments[1].hasGapTag));
    CHECK(playlist->segments[2].hasGapTag);
    CHECK(!(playlist->segments[3].hasGapTag));
}

TEST(HlsMediaPlaylistParser_mapTag) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:5.005,\n02/00/27.ts\n#EXT-X-MAP:URI=\"init1.ts\"#EXTINF:5.005,\n02/00/32.ts\n#EXTINF:5.005,\n02/00/42.ts\n#EXT-X-MAP:URI=\"init2.ts\"#EXTINF:5.005,\n02/00/47.ts\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    auto segments = playlist->segments;
    CHECK(IsNull(segments[0].initializationSegment));
    CHECK(segments[1].initializationSegment == segments[2].initializationSegment);
    CHECK_EQ(segments[1].initializationSegment->url, "init1.ts");
    CHECK_EQ(segments[3].initializationSegment->url, "init2.ts");
}

TEST(HlsMediaPlaylistParser_noExplicitInitSegmentInIFrameOnly_infersInitSegment) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-TARGETDURATION:5\n#EXT-X-I-FRAMES-ONLY\n#EXTINF:5.005,\n#EXT-X-BYTERANGE:100@300\nsegment1.ts\n#EXTINF:5.005,\n#EXT-X-BYTERANGE:100@400\nsegment2.ts\n#EXTINF:5.005,\n#EXT-X-BYTERANGE:100@400\nsegment1.ts\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    auto segments = playlist->segments;
    auto initializationSegment = segments[0].initializationSegment;
    CHECK_EQ(initializationSegment->url, "segment1.ts");
    CHECK_EQ(initializationSegment->byteRangeOffset, 0);
    CHECK_EQ(initializationSegment->byteRangeLength, 300);
    initializationSegment = segments[1].initializationSegment;
    CHECK_EQ(initializationSegment->url, "segment2.ts");
    CHECK_EQ(initializationSegment->byteRangeOffset, 0);
    CHECK_EQ(initializationSegment->byteRangeLength, 400);
    initializationSegment = segments[2].initializationSegment;
    CHECK_EQ(initializationSegment->url, "segment1.ts");
    CHECK_EQ(initializationSegment->byteRangeOffset, 0);
    CHECK_EQ(initializationSegment->byteRangeLength, 300);
}

TEST(HlsMediaPlaylistParser_encryptedMapTag) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=2680\",IV=0x1566B\n#EXT-X-MAP:URI=\"init1.ts\"#EXTINF:5.005,\n02/00/32.ts\n#EXT-X-KEY:METHOD=NONE\n#EXT-X-MAP:URI=\"init2.ts\"#EXTINF:5.005,\n02/00/47.ts\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    auto segments = playlist->segments;
    auto initSegment1 = segments[0].initializationSegment;
    CHECK_EQ(initSegment1->fullSegmentEncryptionKeyUri, "https://priv.example.com/key.php?r=2680");
    CHECK_EQ(initSegment1->encryptionIV, "0x1566B");
    auto initSegment2 = segments[1].initializationSegment;
    CHECK(IsNull(initSegment2->fullSegmentEncryptionKeyUri));
    CHECK(IsNull(initSegment2->encryptionIV));
}

TEST(HlsMediaPlaylistParser_encryptedMapTagWithNoIvFails) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=2680\"\n#EXT-X-MAP:URI=\"init1.ts\"#EXTINF:5.005,\n02/00/32.ts\n";
    const std::string inputStream = playlistString;
    CHECK(Throws<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); }));
}

TEST(HlsMediaPlaylistParser_iframeOnly_withExplicitInitSegment_hasCorrectByteRange) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:1616630672\n#EXT-X-TARGETDURATION:7\n#EXT-X-DISCONTINUITY-SEQUENCE:491 \n#EXT-X-MAP:URI=\"iframe0.tsv\",BYTERANGE=\"564@0\"\n\n#EXT-X-I-FRAMES-ONLY\n#EXT-X-PROGRAM-DATE-TIME:2021-04-12T17:08:22.000Z\n#EXTINF:1.001000,\n#EXT-X-BYTERANGE:121260@1128\niframe0.tsv";
    const std::string inputStream = playlistString;
    auto standalonePlaylist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    auto initSegment = standalonePlaylist->segments[0].initializationSegment;
    CHECK_EQ(standalonePlaylist->segments.size(), std::size_t(1));
    CHECK_EQ(initSegment->byteRangeLength, 564);
    CHECK_EQ(initSegment->byteRangeOffset, 0);
}

TEST(HlsMediaPlaylistParser_multivariantPlaylistAttributeInheritance) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:5.005,\n02/00/27.ts\n#EXT-X-MAP:URI=\"init1.ts\"#EXTINF:5.005,\n02/00/32.ts\n#EXTINF:5.005,\n02/00/42.ts\n#EXT-X-MAP:URI=\"init2.ts\"#EXTINF:5.005,\n02/00/47.ts\n";
    const std::string inputStream = playlistString;
    auto standalonePlaylist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    CHECK(!(standalonePlaylist->hasIndependentSegments));
    auto multivariantPlaylist = std::make_shared<HlsMultivariantPlaylist>("https://example.com/", std::vector<std::string>{}, std::vector<HlsMultivariantPlaylist::Variant>{},
        std::vector<HlsMultivariantPlaylist::Rendition>{}, std::vector<HlsMultivariantPlaylist::Rendition>{}, std::vector<HlsMultivariantPlaylist::Rendition>{},
        std::vector<HlsMultivariantPlaylist::Rendition>{}, std::nullopt, std::nullopt, true, std::map<std::string, std::string>{}, std::vector<stream::DrmInitData>{}, std::nullopt);
    auto playlistWithInheritance = MediaOf(HlsPlaylistParser(multivariantPlaylist, nullptr).Parse(playlistUri, inputStream));
    CHECK(playlistWithInheritance->hasIndependentSegments);
}

TEST(HlsMediaPlaylistParser_variableSubstitution) {
    std::string playlistUri = "https://example.com/substitution.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:NAME=\"underscore_1\",VALUE=\"{\"\n#EXT-X-DEFINE:NAME=\"dash-1\",VALUE=\"replaced_value.ts\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:5.005,\nsegment1.ts\n#EXT-X-MAP:URI=\"{$dash-1}\"#EXTINF:5.005,\nsegment{$underscore_1}$name_1}\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    auto segment = playlist->segments[1];
    CHECK_EQ(segment.initializationSegment->url, "replaced_value.ts");
    CHECK_EQ(segment.url, "segment{$name_1}");
}

TEST(HlsMediaPlaylistParser_inheritedVariableSubstitution) {
    std::string playlistUri = "https://example.com/test3.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-DEFINE:IMPORT=\"imported_base\"\n#EXTINF:5.005,\n{$imported_base}1.ts\n#EXTINF:5.005,\n{$imported_base}2.ts\n#EXTINF:5.005,\n{$imported_base}3.ts\n#EXTINF:5.005,\n{$imported_base}4.ts\n";
    const std::string inputStream = playlistString;
    std::map<std::string, std::string> variableDefinitions;
    variableDefinitions["imported_base"] = "long_path";
    auto multivariantPlaylist = std::make_shared<HlsMultivariantPlaylist>("", std::vector<std::string>{}, std::vector<HlsMultivariantPlaylist::Variant>{},
        std::vector<HlsMultivariantPlaylist::Rendition>{}, std::vector<HlsMultivariantPlaylist::Rendition>{}, std::vector<HlsMultivariantPlaylist::Rendition>{},
        std::vector<HlsMultivariantPlaylist::Rendition>{}, std::nullopt, std::vector<Format>{}, false, variableDefinitions, std::vector<stream::DrmInitData>{}, std::nullopt);
    auto playlist = MediaOf(HlsPlaylistParser(multivariantPlaylist, nullptr).Parse(playlistUri, inputStream));
    for (int i = 1; i <= 4; i++) CHECK_EQ(playlist->segments[std::size_t(i - 1)].url, "long_path" + std::to_string(i) + ".ts");
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution) {
    std::string playlistUri = "https://example.com/substitution.m3u8?queryparam_1=replaced_value.ts&param-2=22";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:QUERYPARAM=\"queryparam_1\",VALUE=\"\"\n#EXT-X-DEFINE:QUERYPARAM=\"param-2\",VALUE=\"\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:5.005,\nsegment1.ts\n#EXT-X-MAP:URI=\"{$queryparam_1}\"#EXTINF:5.005,\nsegment{$param-2}$name_1}\n";
    const std::string inputStream = playlistString;
    auto playlist = MediaOf(HlsPlaylistParser().Parse(playlistUri, inputStream));
    auto segment = playlist->segments[1];
    CHECK_EQ(segment.initializationSegment->url, "replaced_value.ts");
    CHECK_EQ(segment.url, "segment22$name_1}");
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution_queryParam_isCaseSensitiveOrThrows) {
    std::string playlistUri = "https://example.com/substitution.m3u8?queryparam_1=replaced_value.ts";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:QUERYPARAM=\"Queryparam_1\",VALUE=\"\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:5.005,\nsegment1.ts\n#EXT-X-MAP:URI=\"{$queryparam_1}\"#EXTINF:5.005,\nsegment2.ts\n";
    const std::string inputStream = playlistString;
    const std::string parserException = ThrowMessage<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); });
    CHECK(Contains(parserException, "QUERYPARAM \"Queryparam_1\" not found in playlist URI"));
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution_missingQueryParam_throws) {
    std::string playlistUri = "https://example.com/substitution.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:QUERYPARAM=\"queryparam_1\",VALUE=\"\"\n#EXT-X-DEFINE:QUERYPARAM=\"param-2\",VALUE=\"\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:5.005,\nsegment1.ts\n#EXT-X-MAP:URI=\"{$queryparam_1}\"#EXTINF:5.005,\nsegment{$param-2}$name_1}\n";
    const std::string inputStream = playlistString;
    const std::string parserException = ThrowMessage<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); });
    CHECK(Contains(parserException, "QUERYPARAM \"queryparam_1\" not found in playlist URI"));
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution_duplicateVariableName_throws) {
    std::string playlistUri = "https://example.com/substitution.m3u8?queryparam_1=foo";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:NAME=\"name_1\",VALUE=\"value_1\"\n#EXT-X-DEFINE:NAME=\"name_1\",VALUE=\"value_1\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-MAP:URI=\"{name_1}\"#EXTINF:5.005,\nsegment1.ts\n";
    const std::string inputStream = playlistString;
    const std::string parserException = ThrowMessage<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); });
    CHECK(Contains(parserException, "duplicate variable name \"name_1\""));
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution_duplicateVariableAndQueryParamName_throws) {
    std::string playlistUri = "https://example.com/substitution.m3u8?queryparam_1=foo";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:NAME=\"queryparam_1\",VALUE=\"value_1\"\n#EXT-X-DEFINE:QUERYPARAM=\"queryparam_1\",VALUE=\"\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-MAP:URI=\"{$queryparam_1}\"#EXTINF:5.005,\nsegment1.ts\n";
    const std::string inputStream = playlistString;
    const std::string parserException = ThrowMessage<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); });
    CHECK(Contains(parserException, "duplicate variable name \"queryparam_1\""));
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution_duplicateVariableAndImportName_throws) {
    std::string playlistUri = "https://example.com/substitution.m3u8";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:NAME=\"import_1\",VALUE=\"value_1\"\n#EXT-X-DEFINE:IMPORT=\"import_1\",VALUE=\"\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-MAP:URI=\"{$import_1}\"#EXTINF:5.005,\nsegment1.ts\n";
    const std::string inputStream = playlistString;
    const std::string parserException = ThrowMessage<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); });
    CHECK(Contains(parserException, "duplicate variable name \"import_1\""));
}

TEST(HlsMediaPlaylistParser_queryParamSubstitution_duplicateQueryParamAndImportName_throws) {
    std::string playlistUri = "https://example.com/substitution.m3u8?import_1=value_1";
    std::string playlistString = "#EXTM3U\n#EXT-X-VERSION:8\n#EXT-X-DEFINE:QUERYPARAM=\"import_1\",VALUE=\"\"\n#EXT-X-DEFINE:IMPORT=\"import_1\",VALUE=\"\"\n#EXT-X-TARGETDURATION:5\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-MAP:URI=\"{$import_1}\"#EXTINF:5.005,\nsegment1.ts\n";
    const std::string inputStream = playlistString;
    const std::string parserException = ThrowMessage<stream::ParserException>([&] { HlsPlaylistParser().Parse(playlistUri, inputStream); });
    CHECK(Contains(parserException, "duplicate variable name \"import_1\""));
}

