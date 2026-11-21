#pragma once

// What media3's adaptive streaming (HLS, DASH) leans on from media3-common: UriUtil.resolve
// (RFC 3986 reference resolution), MimeTypes' codec-string mapping, Util's codec lists and
// xs:dateTime, DrmInitData and its PSSH boxes, and Base64 as android.util.Base64 decodes.

#include "kiln/media/TrackSelection.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::media::stream {

    // C.TIME_UNSET / C.LENGTH_UNSET / C.INDEX_UNSET.
    inline constexpr std::int64_t kTimeUnset = INT64_MIN + 1;
    inline constexpr std::int64_t kLengthUnset = -1;
    inline constexpr int kIndexUnset = -1;
    inline constexpr std::int64_t kMicrosPerSecond = 1'000'000;

    // ParserException: a manifest that cannot be read.
    class ParserException : public std::runtime_error {
    public:
        explicit ParserException(const std::string& message) : std::runtime_error(message) {}
    };

    namespace MimeTypes {
        inline constexpr const char* VIDEO_MP4 = "video/mp4";
        inline constexpr const char* VIDEO_WEBM = "video/webm";
        inline constexpr const char* VIDEO_H264 = "video/avc";
        inline constexpr const char* VIDEO_H265 = "video/hevc";
        inline constexpr const char* VIDEO_H266 = "video/vvc";
        inline constexpr const char* VIDEO_VP8 = "video/x-vnd.on2.vp8";
        inline constexpr const char* VIDEO_VP9 = "video/x-vnd.on2.vp9";
        inline constexpr const char* VIDEO_AV1 = "video/av01";
        inline constexpr const char* VIDEO_MP2T = "video/mp2t";
        inline constexpr const char* VIDEO_MP4V = "video/mp4v-es";
        inline constexpr const char* VIDEO_MPEG = "video/mpeg";
        inline constexpr const char* VIDEO_MPEG2 = "video/mpeg2";
        inline constexpr const char* VIDEO_VC1 = "video/wvc1";
        inline constexpr const char* VIDEO_DOLBY_VISION = "video/dolby-vision";
        inline constexpr const char* AUDIO_MP4 = "audio/mp4";
        inline constexpr const char* AUDIO_WEBM = "audio/webm";
        inline constexpr const char* AUDIO_AAC = "audio/mp4a-latm";
        inline constexpr const char* AUDIO_MPEG = "audio/mpeg";
        inline constexpr const char* AUDIO_MPEGH_MHA1 = "audio/mha1";
        inline constexpr const char* AUDIO_MPEGH_MHM1 = "audio/mhm1";
        inline constexpr const char* AUDIO_AC3 = "audio/ac3";
        inline constexpr const char* AUDIO_E_AC3 = "audio/eac3";
        inline constexpr const char* AUDIO_E_AC3_JOC = "audio/eac3-joc";
        inline constexpr const char* AUDIO_AC4 = "audio/ac4";
        inline constexpr const char* AUDIO_DTS = "audio/vnd.dts";
        inline constexpr const char* AUDIO_DTS_HD = "audio/vnd.dts.hd";
        inline constexpr const char* AUDIO_DTS_EXPRESS = "audio/vnd.dts.hd;profile=lbr";
        inline constexpr const char* AUDIO_DTS_UHD_P2 = "audio/vnd.dts.uhd;profile=p2";
        inline constexpr const char* AUDIO_VORBIS = "audio/vorbis";
        inline constexpr const char* AUDIO_OPUS = "audio/opus";
        inline constexpr const char* AUDIO_FLAC = "audio/flac";
        inline constexpr const char* TEXT_VTT = "text/vtt";
        inline constexpr const char* APPLICATION_MP4 = "application/mp4";
        inline constexpr const char* APPLICATION_WEBM = "application/webm";
        inline constexpr const char* APPLICATION_MPD = "application/dash+xml";
        inline constexpr const char* APPLICATION_M3U8 = "application/x-mpegURL";
        inline constexpr const char* APPLICATION_ID3 = "application/id3";
        inline constexpr const char* APPLICATION_CEA608 = "application/cea-608";
        inline constexpr const char* APPLICATION_CEA708 = "application/cea-708";
        inline constexpr const char* APPLICATION_SUBRIP = "application/x-subrip";
        inline constexpr const char* APPLICATION_TTML = "application/ttml+xml";
        inline constexpr const char* APPLICATION_TX3G = "application/x-quicktime-tx3g";
        inline constexpr const char* APPLICATION_MP4VTT = "application/x-mp4-vtt";
        inline constexpr const char* APPLICATION_MP4CEA608 = "application/x-mp4-cea-608";
        inline constexpr const char* APPLICATION_RAWCC = "application/x-rawcc";
        inline constexpr const char* APPLICATION_VOBSUB = "application/vobsub";
        inline constexpr const char* APPLICATION_PGS = "application/pgs";
        inline constexpr const char* APPLICATION_DVBSUBS = "application/dvbsubs";
        inline constexpr const char* APPLICATION_EMSG = "application/x-emsg";
        inline constexpr const char* IMAGE_JPEG = "image/jpeg";
        inline constexpr const char* CODEC_E_AC3_JOC = "ec+3";

        bool IsAudio(const std::string& mimeType);
        bool IsVideo(const std::string& mimeType);
        bool IsText(const std::string& mimeType);
        bool IsImage(const std::string& mimeType);
        // getMediaMimeType / getMimeTypeFromMp4ObjectType: "" when unknown.
        std::string GetMediaMimeType(const std::string& codec);
        std::string GetMimeTypeFromMp4ObjectType(int objectType);
        std::string GetVideoMediaMimeType(const std::string& codecs);
        std::string GetAudioMediaMimeType(const std::string& codecs);
        std::string GetTextMediaMimeType(const std::string& codecs);
        bool IsDolbyVisionCodec(const std::string& codecs, const std::string& supplementalCodecs);
        TrackType GetTrackType(const std::string& mimeType);
        TrackType GetTrackTypeOfCodec(const std::string& codec);
        std::string NormalizeMimeType(const std::string& mimeType);
        bool IsMatroska(const std::string& mimeType);
    }

    // Util.splitCodecs / getCodecsOfType / getCodecsWithoutType / getCodecCountOfType ("" for
    // Java's null).
    std::vector<std::string> SplitCodecs(const std::string& codecs);
    std::string GetCodecsOfType(const std::string& codecs, TrackType trackType);
    std::string GetCodecsWithoutType(const std::string& codecs, TrackType trackType);
    int GetCodecCountOfType(const std::string& codecs, TrackType trackType);
    // Util.getColorInfoForDolbyVision into the format's colour fields; false when none applies.
    bool ApplyColorInfoForDolbyVision(Format& format, const std::string& codecs, const std::string& supplementalCodecs,
                                      const std::string& supplementalProfiles);
    inline constexpr int kColorSpaceBt2020 = 6, kColorRangeFull = 1, kColorRangeLimited = 2, kColorTransferSt2084 = 6, kColorTransferHlg = 7;

    // UriUtil.resolve / isAbsolute.
    std::string ResolveUri(const std::string& baseUri, const std::string& referenceUri);
    bool IsAbsoluteUri(const std::string& uri);
    // Uri.getQueryParameter.
    std::optional<std::string> GetQueryParameter(const std::string& uri, const std::string& name);

    // Util.parseXsDateTime: milliseconds since the epoch.
    std::int64_t ParseXsDateTime(const std::string& value);

    // android.util.Base64.decode(DEFAULT).
    std::vector<std::uint8_t> Base64Decode(const std::string& text);

    // A 128-bit UUID, most significant byte first (java.util.UUID's order).
    using Uuid = std::array<std::uint8_t, 16>;
    Uuid ParseUuid(const std::string& text);
    inline constexpr Uuid kWidevineUuid = { 0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed };
    inline constexpr Uuid kPlayreadyUuid = { 0x9a, 0x04, 0xf0, 0x79, 0x98, 0x40, 0x42, 0x86, 0xab, 0x92, 0xe6, 0x5b, 0xe0, 0x88, 0x5f, 0x95 };
    inline constexpr Uuid kClearkeyUuid = { 0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9, 0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e };
    inline constexpr Uuid kCommonPsshUuid = { 0x10, 0x77, 0xef, 0xec, 0xc0, 0xb2, 0x4d, 0x02, 0xac, 0xe3, 0x3c, 0x1e, 0x52, 0xe2, 0xfb, 0x4b };
    inline constexpr Uuid kUuidNil = {};

    // PsshAtomUtil.buildPsshAtom (version 0).
    std::vector<std::uint8_t> BuildPsshAtom(const Uuid& systemId, const std::vector<std::uint8_t>& data);

    // DrmInitData: the protection schemes of a stream (C.CENC_TYPE_* scheme type), sorted as
    // media3 sorts them (by UUID, with C.UUID_NIL last).
    struct SchemeData {
        Uuid uuid{};
        std::string licenseServerUrl;
        std::string mimeType;
        std::optional<std::vector<std::uint8_t>> data;
        SchemeData copyWithData(std::optional<std::vector<std::uint8_t>> d) const {
            SchemeData s = *this;
            s.data = std::move(d);
            return s;
        }
        // Whether this applies to the scheme (a C.UUID_NIL scheme applies to all of them).
        bool Matches(const Uuid& schemeUuid) const { return uuid == kUuidNil || uuid == schemeUuid; }
        bool HasData() const { return data.has_value(); }
        bool operator==(const SchemeData&) const = default;
    };
    struct DrmInitData {
        std::string schemeType;   // "" for Java's null
        std::vector<SchemeData> schemeDatas;
        DrmInitData() = default;
        DrmInitData(std::string schemeType, std::vector<SchemeData> schemeDatas);
        bool operator==(const DrmInitData&) const = default;
    };
    inline constexpr const char* kCencTypeCenc = "cenc";
    inline constexpr const char* kCencTypeCbcs = "cbcs";

}
