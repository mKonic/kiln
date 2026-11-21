#pragma once

// What media3's adaptive streaming (HLS, DASH) leans on from media3-common: UriUtil.resolve
// (RFC 3986 reference resolution), MimeTypes' codec-string mapping, Util's codec lists and
// xs:dateTime, DrmInitData and its PSSH boxes, and Base64 as android.util.Base64 decodes.

#include "kiln/media/TrackSelection.h"
#include "kiln/media/stream/DrmInitData.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::media::stream {

    // C.TIME_UNSET / C.LENGTH_UNSET / C.INDEX_UNSET.
    using media::kTimeUnset;   // C.TIME_UNSET
    inline constexpr std::int64_t kLengthUnset = -1;
    using media::kIndexUnset;   // C.INDEX_UNSET
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
    // What Format's constructor does with Builder.setLanguage/setLabel/setLabels: normalizes the
    // language, then fills labels from label (or label from labels, preferring the language's).
    void FinishFormat(Format& format);

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
    // Util.parseXsDuration: an xs:duration in milliseconds; anything else is read as a number of
    // hours (std::invalid_argument if it is not one).
    std::int64_t ParseXsDuration(const std::string& value);
    // Util.scaleLargeTimestamp: timestamp * multiplier / divisor without overflow, rounded down.
    std::int64_t ScaleLargeTimestamp(std::int64_t timestamp, std::int64_t multiplier, std::int64_t divisor);
    // Util.ceilDivide.
    inline std::int64_t CeilDivide(std::int64_t numerator, std::int64_t denominator) { return (numerator + denominator - 1) / denominator; }
    // Util.msToUs (kTimeUnset and C.TIME_END_OF_SOURCE kept).
    inline std::int64_t MsToUs(std::int64_t timeMs) { return timeMs == kTimeUnset || timeMs == INT64_MIN ? timeMs : timeMs * 1000; }

    // StreamKey: a stream of a manifest (period, group, then index in it), ordered by period,
    // group and stream index.
    struct StreamKey {
        int periodIndex = 0;
        int groupIndex = 0;
        int streamIndex = 0;
        auto operator<=>(const StreamKey&) const = default;
    };

    // android.util.Base64.decode(DEFAULT).
    std::vector<std::uint8_t> Base64Decode(const std::string& text);

    // PsshAtomUtil.buildPsshAtom (version 0).
    std::vector<std::uint8_t> BuildPsshAtom(const Uuid& systemId, const std::vector<std::uint8_t>& data);
    // PsshAtomUtil.buildPsshAtom(systemId, keyIds, data): a version 1 box with key ids (version 0
    // without them).
    std::vector<std::uint8_t> BuildPsshAtom(const Uuid& systemId, const std::vector<Uuid>* keyIds, const std::vector<std::uint8_t>* data);
    // PsshAtomUtil.parseUuid: the system id of a well-formed pssh box.
    std::optional<Uuid> ParsePsshUuid(const std::vector<std::uint8_t>& atom);


}
