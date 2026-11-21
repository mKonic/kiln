#pragma once

// media3 exoplayer-rtsp's RTP side: RtpPacket, RtpPacketReorderingQueue, RtpPayloadFormat and
// RtspMediaTrack (a track's format and URI from its SDP media section).

#include "kiln/media/TrackSelection.h"
#include "kiln/media/rtsp/SessionDescription.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kiln::media::rtsp {

    // C.ENCODING_PCM_8BIT / C.ENCODING_PCM_16BIT_BIG_ENDIAN.
    inline constexpr int kEncodingPcm8Bit = 3, kEncodingPcm16BitBigEndian = 0x10000000;
    // MimeTypes the RTP formats use that the stream helpers do not name.
    inline constexpr const char* kMimeAudioAmrNb = "audio/3gpp";
    inline constexpr const char* kMimeAudioAmrWb = "audio/amr-wb";
    inline constexpr const char* kMimeAudioRaw = "audio/raw";
    inline constexpr const char* kMimeAudioAlaw = "audio/g711-alaw";
    inline constexpr const char* kMimeAudioMlaw = "audio/g711-mlaw";
    inline constexpr const char* kMimeVideoH263 = "video/3gpp";

    struct RtpPacket {
        static constexpr int RTP_VERSION = 2;
        static constexpr int MAX_SIZE = 65507;
        static constexpr int MIN_HEADER_SIZE = 12;
        static constexpr int MIN_SEQUENCE_NUMBER = 0;
        static constexpr int MAX_SEQUENCE_NUMBER = 0xFFFF;
        static constexpr int CSRC_SIZE = 4;

        static int GetNextSequenceNumber(int sequenceNumber) { return ((sequenceNumber + 1) % (MAX_SEQUENCE_NUMBER + 1) + (MAX_SEQUENCE_NUMBER + 1)) % (MAX_SEQUENCE_NUMBER + 1); }
        static int GetPreviousSequenceNumber(int sequenceNumber) { return ((sequenceNumber - 1) % (MAX_SEQUENCE_NUMBER + 1) + (MAX_SEQUENCE_NUMBER + 1)) % (MAX_SEQUENCE_NUMBER + 1); }

        int version = RTP_VERSION;
        bool padding = false, extension = false;
        int csrcCount = 0;
        bool marker = false;
        int payloadType = 0;
        int sequenceNumber = 0;
        std::int64_t timestamp = 0;
        int ssrc = 0;
        std::vector<std::uint8_t> csrc, payloadData;

        // Throws std::invalid_argument for a sequence number out of range (Builder.setSequenceNumber).
        void SetSequenceNumber(int n);
        void SetCsrc(std::vector<std::uint8_t> c) {
            csrc = std::move(c);
            csrcCount = int(csrc.size() / CSRC_SIZE);
        }
        // nullopt for fewer than 12 bytes or another version.
        static std::optional<RtpPacket> Parse(const std::uint8_t* data, int length);
        // The packet into target (C.LENGTH_UNSET when it does not fit).
        int WriteToBuffer(std::uint8_t* target, int targetSize, int offset, int length) const;
        // equals: the header fields that identify a packet.
        bool operator==(const RtpPacket& o) const {
            return payloadType == o.payloadType && sequenceNumber == o.sequenceNumber && marker == o.marker && timestamp == o.timestamp && ssrc == o.ssrc;
        }
    };

    // Orders packets by sequence number (wrapping), passing them on in order or after a cutoff.
    class RtpPacketReorderingQueue {
    public:
        static constexpr int MAX_SEQUENCE_LEAP_ALLOWED = 1000;
        RtpPacketReorderingQueue() { Reset(); }
        void Reset();
        // Whether it was queued (a packet already dequeued, or a duplicate, is not). Throws
        // std::logic_error past 5000 queued packets.
        bool Offer(const RtpPacket& packet, std::int64_t receivedTimestampMs);
        // The next packet, if it follows the last one or was received at or before the cutoff.
        std::optional<RtpPacket> Poll(std::int64_t cutoffTimestampMs);

    private:
        struct Container {
            RtpPacket packet;
            std::int64_t receivedTimestampMs;
        };
        struct Compare {
            bool operator()(const Container& a, const Container& b) const;
        };
        void AddToQueue(Container c);
        std::mutex m_Mutex;
        std::set<Container, Compare> m_Queue;
        int m_LastReceivedSequenceNumber = -1, m_LastDequeuedSequenceNumber = -1;
        bool m_Started = false;
    };

    struct RtpPayloadFormat {
        static constexpr const char* RTP_MEDIA_AC3 = "AC3";
        static constexpr const char* RTP_MEDIA_AMR = "AMR";
        static constexpr const char* RTP_MEDIA_AMR_WB = "AMR-WB";
        static constexpr const char* RTP_MEDIA_MPEG4_GENERIC = "MPEG4-GENERIC";
        static constexpr const char* RTP_MEDIA_MPEG4_LATM_AUDIO = "MP4A-LATM";
        static constexpr const char* RTP_MEDIA_MPEG4_VIDEO = "MP4V-ES";
        static constexpr const char* RTP_MEDIA_H263_1998 = "H263-1998";
        static constexpr const char* RTP_MEDIA_H263_2000 = "H263-2000";
        static constexpr const char* RTP_MEDIA_H264 = "H264";
        static constexpr const char* RTP_MEDIA_H265 = "H265";
        static constexpr const char* RTP_MEDIA_OPUS = "OPUS";
        static constexpr const char* RTP_MEDIA_PCM_L8 = "L8";
        static constexpr const char* RTP_MEDIA_PCM_L16 = "L16";
        static constexpr const char* RTP_MEDIA_PCMA = "PCMA";
        static constexpr const char* RTP_MEDIA_PCMU = "PCMU";
        static constexpr const char* RTP_MEDIA_VP8 = "VP8";
        static constexpr const char* RTP_MEDIA_VP9 = "VP9";

        static bool IsFormatSupported(const MediaDescription& mediaDescription);
        // Throws std::invalid_argument for an encoding without a MIME type.
        static std::string GetMimeTypeFromRtpMediaType(const std::string& mediaType);
        static int GetRawPcmEncodingType(const std::string& mediaEncoding);

        Format format;
        int rtpPayloadType = 0;
        int clockRate = 0;
        std::map<std::string, std::string> fmtpParameters;
        std::string mediaEncoding;
        bool operator==(const RtpPayloadFormat&) const = default;
    };

    struct RtspMediaTrack {
        RtpPayloadFormat payloadFormat;
        std::string uri;
        // Throws std::invalid_argument (checkArgument) for a section it cannot play.
        RtspMediaTrack(const RtspHeaders& headers, const MediaDescription& mediaDescription, const std::string& sessionUri);
        static RtpPayloadFormat GeneratePayloadFormat(const MediaDescription& mediaDescription);
        static std::string ExtractTrackUri(const RtspHeaders& headers, const std::string& sessionUri, const std::string& control);
        bool operator==(const RtspMediaTrack&) const = default;
    };

}
