#pragma once

// media3 exoplayer-rtsp's SDP: SessionDescription, MediaDescription, SessionDescriptionParser.

#include "kiln/media/rtsp/RtspMessage.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media::rtsp {

    // A media section (m= and its lines).
    struct MediaDescription {
        struct RtpMapAttribute {
            int payloadType = 0;
            std::string mediaEncoding;
            int clockRate = 0;
            int encodingParameters = -1;   // C.INDEX_UNSET
            static RtpMapAttribute Parse(const std::string& rtpmap);
            bool operator==(const RtpMapAttribute&) const = default;
        };

        static constexpr const char* MEDIA_TYPE_AUDIO = "audio";
        static constexpr const char* MEDIA_TYPE_VIDEO = "video";
        static constexpr const char* RTP_AVP_PROFILE = "RTP/AVP";

        class Builder {
        public:
            Builder(std::string mediaType, int port, std::string transportProtocol, int payloadType);
            Builder& SetMediaTitle(std::string v) { m_MediaTitle = std::move(v); return *this; }
            Builder& SetConnection(std::string v) { m_Connection = std::move(v); return *this; }
            Builder& SetBitrate(int v) { m_Bitrate = v; return *this; }
            Builder& SetKey(std::string v) { m_Key = std::move(v); return *this; }
            Builder& AddAttribute(const std::string& name, const std::string& value) { m_Attributes[name] = value; return *this; }
            // Throws std::logic_error (IllegalStateException) for an rtpmap it cannot read.
            MediaDescription Build() const;

        private:
            std::string m_MediaType, m_TransportProtocol;
            int m_Port, m_PayloadType;
            std::map<std::string, std::string> m_Attributes;
            int m_Bitrate = -1;
            std::optional<std::string> m_MediaTitle, m_Connection, m_Key;
        };

        std::string mediaType;
        int port = 0;
        std::string transportProtocol;
        int payloadType = 0;
        int bitrate = -1;
        std::optional<std::string> mediaTitle, connection, key;
        std::map<std::string, std::string> attributes;
        RtpMapAttribute rtpMapAttribute;

        // The fmtp attribute's parameters (after the payload type), name to value.
        std::map<std::string, std::string> GetFmtpParametersAsMap() const;
        bool operator==(const MediaDescription&) const = default;
    };

    struct SessionDescription {
        static constexpr const char* SUPPORTED_SDP_VERSION = "0";
        static constexpr const char* ATTR_CONTROL = "control";
        static constexpr const char* ATTR_FMTP = "fmtp";
        static constexpr const char* ATTR_LENGTH = "length";
        static constexpr const char* ATTR_RANGE = "range";
        static constexpr const char* ATTR_RTPMAP = "rtpmap";
        static constexpr const char* ATTR_TOOL = "tool";
        static constexpr const char* ATTR_TYPE = "type";

        std::map<std::string, std::string> attributes;
        std::vector<MediaDescription> mediaDescriptionList;
        std::optional<std::string> sessionName, origin, timing;
        int bitrate = -1;
        std::optional<std::string> uri, connection, key, emailAddress, phoneNumber, sessionInfo;
        bool operator==(const SessionDescription&) const = default;
    };

    namespace SessionDescriptionParser {
        // Throws stream::ParserException.
        SessionDescription Parse(const std::string& sdp);
    }

}
