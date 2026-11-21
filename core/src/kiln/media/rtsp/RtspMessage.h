#pragma once

// media3 exoplayer-rtsp's messages: RtspHeaders, RtspRequest / RtspResponse, RtspMessageUtil,
// RtspAuthenticationInfo, RtspSessionTiming and RtspTrackTiming.

#include "kiln/media/stream/StreamUtil.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kiln::media::rtsp {

    using stream::kTimeUnset;
    using stream::ParserException;

    // ParserException.createForManifestWithUnsupportedFeature and the like: what RTSP throws.
    class RtspException : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // RtspHeaders: names in their standard spelling, values in the order added, grouped by name in
    // the order each name was first added (ImmutableListMultimap).
    class RtspHeaders {
    public:
        static constexpr const char* ACCEPT = "Accept";
        static constexpr const char* ALLOW = "Allow";
        static constexpr const char* AUTHORIZATION = "Authorization";
        static constexpr const char* BANDWIDTH = "Bandwidth";
        static constexpr const char* BLOCKSIZE = "Blocksize";
        static constexpr const char* CACHE_CONTROL = "Cache-Control";
        static constexpr const char* CONNECTION = "Connection";
        static constexpr const char* CONTENT_BASE = "Content-Base";
        static constexpr const char* CONTENT_ENCODING = "Content-Encoding";
        static constexpr const char* CONTENT_LANGUAGE = "Content-Language";
        static constexpr const char* CONTENT_LENGTH = "Content-Length";
        static constexpr const char* CONTENT_LOCATION = "Content-Location";
        static constexpr const char* CONTENT_TYPE = "Content-Type";
        static constexpr const char* CSEQ = "CSeq";
        static constexpr const char* DATE = "Date";
        static constexpr const char* EXPIRES = "Expires";
        static constexpr const char* LOCATION = "Location";
        static constexpr const char* PROXY_AUTHENTICATE = "Proxy-Authenticate";
        static constexpr const char* PROXY_REQUIRE = "Proxy-Require";
        static constexpr const char* PUBLIC = "Public";
        static constexpr const char* RANGE = "Range";
        static constexpr const char* RTP_INFO = "RTP-Info";
        static constexpr const char* RTCP_INTERVAL = "RTCP-Interval";
        static constexpr const char* SCALE = "Scale";
        static constexpr const char* SESSION = "Session";
        static constexpr const char* SPEED = "Speed";
        static constexpr const char* SUPPORTED = "Supported";
        static constexpr const char* TIMESTAMP = "Timestamp";
        static constexpr const char* TRANSPORT = "Transport";
        static constexpr const char* USER_AGENT = "User-Agent";
        static constexpr const char* VIA = "Via";
        static constexpr const char* WWW_AUTHENTICATE = "WWW-Authenticate";

        class Builder {
        public:
            Builder() = default;
            Builder(const std::string& userAgent, const std::optional<std::string>& sessionId, int cSeq);
            // Trimmed, the name in its standard spelling.
            Builder& Add(const std::string& name, const std::string& value);
            // "Name: value" lines (those without a colon are skipped).
            Builder& AddAll(const std::vector<std::string>& lines);
            Builder& AddAll(const std::vector<std::pair<std::string, std::string>>& headers);
            RtspHeaders Build() const { return RtspHeaders(m_Entries); }

        private:
            friend class RtspHeaders;
            std::vector<std::pair<std::string, std::vector<std::string>>> m_Entries;
        };

        RtspHeaders() = default;
        Builder BuildUpon() const;
        // The last value for the name.
        std::optional<std::string> Get(const std::string& name) const;
        std::vector<std::string> Values(const std::string& name) const;
        const std::vector<std::pair<std::string, std::vector<std::string>>>& AsMultiMap() const { return m_Entries; }
        bool operator==(const RtspHeaders&) const = default;

    private:
        explicit RtspHeaders(std::vector<std::pair<std::string, std::vector<std::string>>> entries) : m_Entries(std::move(entries)) {}
        std::vector<std::pair<std::string, std::vector<std::string>>> m_Entries;
    };

    enum class RtspMethod {
        Unset = 0, Announce, Describe, GetParameter, Options, Pause, Play, PlayNotify, Record, Redirect, Setup, SetParameter, Teardown
    };

    struct RtspRequest {
        std::string uri;
        RtspMethod method = RtspMethod::Unset;
        RtspHeaders headers;
        std::string messageBody;
    };

    struct RtspResponse {
        int status = 0;
        RtspHeaders headers;
        std::string messageBody;
    };

    struct RtspSessionHeader {
        std::string sessionId;
        std::int64_t timeoutMs = 0;
    };

    struct RtspAuthUserInfo {
        std::string username, password;
    };

    // RtspAuthenticationInfo: Basic or Digest (MD5), from a WWW-Authenticate header.
    struct RtspAuthenticationInfo {
        enum Mechanism { BASIC = 1, DIGEST = 2 };
        int authenticationMechanism = BASIC;
        std::string realm, nonce, opaque;
        // The Authorization header's value for the request.
        std::string GetAuthorizationHeaderValue(const RtspAuthUserInfo& user, const std::string& uri, RtspMethod method) const;
    };

    namespace RtspMessageUtil {
        inline constexpr std::int64_t kDefaultRtspTimeoutMs = 60'000;
        // The message's lines: start line, headers, an empty line, the body.
        std::vector<std::string> SerializeRequest(const RtspRequest& request);
        std::vector<std::string> SerializeResponse(const RtspResponse& response);
        // The lines joined with CRLF.
        std::string ConvertMessageToBytes(const std::vector<std::string>& message);
        std::string RemoveUserInfo(const std::string& uri);
        std::optional<RtspAuthUserInfo> ParseUserInfo(const std::string& uri);
        std::string ToMethodString(RtspMethod method);
        RtspMethod ParseMethodString(const std::string& method);
        RtspResponse ParseResponse(const std::vector<std::string>& lines);
        RtspRequest ParseRequest(const std::vector<std::string>& lines);
        bool IsRtspStartLine(const std::string& line);
        bool IsRtspResponse(const std::vector<std::string>& lines);
        // The body's lines (CRLF or LF separated).
        std::vector<std::string> SplitRtspMessageBody(const std::string& body);
        // stream::kLengthUnset when the line is not a Content-Length header.
        std::int64_t ParseContentLengthHeader(const std::string& line);
        std::vector<RtspMethod> ParsePublicHeader(const std::optional<std::string>& value);
        RtspSessionHeader ParseSessionHeader(const std::string& value);
        RtspAuthenticationInfo ParseWwwAuthenticateHeader(const std::string& value);
        void CheckManifestExpression(bool expression, const std::string& message);
        std::string GetRtspStatusReasonPhrase(int statusCode);
        int ParseInt(const std::string& value);
    }

    // RtspSessionTiming: the SDP / PLAY response's npt range.
    struct RtspSessionTiming {
        std::int64_t startTimeMs = 0, stopTimeMs = kTimeUnset;
        static RtspSessionTiming Default() { return {}; }
        static RtspSessionTiming ParseTiming(const std::string& sdpRangeAttribute);
        // "npt=<seconds, 3 decimals>-".
        static std::string GetOffsetStartTimeTiming(std::int64_t offsetStartTimeMs);
        bool IsLive() const { return stopTimeMs == kTimeUnset; }
        std::int64_t GetDurationMs() const { return stopTimeMs - startTimeMs; }
    };

    // RtspTrackTiming: an RTP-Info entry.
    struct RtspTrackTiming {
        std::int64_t rtpTimestamp = kTimeUnset;
        int sequenceNumber = -1;   // C.INDEX_UNSET
        std::string uri;
        static std::vector<RtspTrackTiming> ParseTrackTiming(const std::string& rtpInfo, const std::string& sessionUri);
        static std::string ResolveUri(const std::string& url, const std::string& sessionUri);
    };

    // Small helpers the module shares: java.lang.String.trim, Util.split / splitAtFirst (regex
    // separators), a URI's scheme / host / user info, MD5 and Base64 (DEFAULT flags).
    namespace detail {
        std::string Trim(const std::string& s);
        std::vector<std::string> Split(const std::string& s, const std::string& regex);
        std::vector<std::string> SplitAtFirst(const std::string& s, const std::string& regex);
        bool EqualsIgnoreCase(const std::string& a, const std::string& b);
        std::optional<std::string> UriScheme(const std::string& uri);
        std::optional<std::string> UriHost(const std::string& uri);
        std::optional<int> UriPort(const std::string& uri);
        std::optional<std::string> UriUserInfo(const std::string& uri);
        std::string Md5Hex(const std::string& data);
        std::string Base64EncodeDefault(const std::vector<std::uint8_t>& data);
        std::string Base64EncodeNoWrap(const std::vector<std::uint8_t>& data);
    }

}
