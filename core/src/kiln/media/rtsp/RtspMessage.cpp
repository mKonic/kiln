// media3 exoplayer-rtsp: RtspHeaders, RtspMessageUtil, RtspAuthenticationInfo, RtspSessionTiming,
// RtspTrackTiming.
#include "kiln/media/rtsp/RtspMessage.h"

#include <array>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <regex>

namespace kiln::media::rtsp {

    // --- helpers --------------------------------------------------------------------------------
    namespace detail {
        std::string Trim(const std::string& s) {
            std::size_t b = 0, e = s.size();
            while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
            while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
            return s.substr(b, e - b);
        }
        // String.split(regex, -1): trailing empty strings kept; an empty input gives one empty
        // string.
        std::vector<std::string> Split(const std::string& s, const std::string& pattern) {
            const std::regex re(pattern);
            std::vector<std::string> out;
            std::sregex_token_iterator it(s.begin(), s.end(), re, -1), end;
            for (; it != end; ++it) out.push_back(*it);
            if (out.empty()) out.push_back("");
            // A match at the very end leaves a trailing empty string (Java keeps it with -1).
            std::smatch m;
            std::string::const_iterator from = s.begin();
            bool endsWithMatch = false;
            while (std::regex_search(from, s.cend(), m, re)) {
                if (m.length(0) == 0) break;
                endsWithMatch = m.suffix().length() == 0;
                from = m.suffix().first;
            }
            if (endsWithMatch && !s.empty()) out.push_back("");
            return out;
        }
        std::vector<std::string> SplitAtFirst(const std::string& s, const std::string& pattern) {
            std::smatch m;
            if (!std::regex_search(s, m, std::regex(pattern)) || m.length(0) == 0) return { s };
            return { m.prefix().str(), m.suffix().str() };
        }
        bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
            return true;
        }
        std::optional<std::string> UriScheme(const std::string& uri) {
            const std::size_t colon = uri.find(':');
            if (colon == std::string::npos || colon == 0) return std::nullopt;
            for (std::size_t i = 0; i < colon; ++i) {
                const char c = uri[i];
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.')) return std::nullopt;
            }
            return uri.substr(0, colon);
        }
        // The authority between // and the path.
        std::optional<std::string> Authority(const std::string& uri) {
            const std::size_t start = uri.find("//");
            if (start == std::string::npos) return std::nullopt;
            const std::size_t end = uri.find_first_of("/?#", start + 2);
            return uri.substr(start + 2, end == std::string::npos ? std::string::npos : end - start - 2);
        }
        std::optional<std::string> UriUserInfo(const std::string& uri) {
            auto a = Authority(uri);
            if (!a) return std::nullopt;
            const std::size_t at = a->rfind('@');
            if (at == std::string::npos) return std::nullopt;
            return a->substr(0, at);
        }
        std::optional<std::string> UriHost(const std::string& uri) {
            auto a = Authority(uri);
            if (!a) return std::nullopt;
            std::string hostPort = a->substr(a->rfind('@') == std::string::npos ? 0 : a->rfind('@') + 1);
            if (!hostPort.empty() && hostPort[0] == '[') {
                const std::size_t close = hostPort.find(']');
                return close == std::string::npos ? hostPort : hostPort.substr(1, close - 1);
            }
            const std::size_t colon = hostPort.rfind(':');
            std::string host = colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
            if (host.empty()) return std::nullopt;
            return host;
        }
        std::optional<int> UriPort(const std::string& uri) {
            auto a = Authority(uri);
            if (!a) return std::nullopt;
            std::string hostPort = a->substr(a->rfind('@') == std::string::npos ? 0 : a->rfind('@') + 1);
            const std::size_t close = hostPort.rfind(']');
            const std::size_t colon = hostPort.rfind(':');
            if (colon == std::string::npos || (close != std::string::npos && colon < close)) return std::nullopt;
            try {
                return std::stoi(hostPort.substr(colon + 1));
            } catch (...) {
                return std::nullopt;
            }
        }

        // RFC 1321 MD5.
        std::string Md5Hex(const std::string& data) {
            static constexpr std::uint32_t K[64] = {
                0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
                0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
                0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
                0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
                0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
                0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
            static constexpr int S[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                           4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
            std::vector<std::uint8_t> msg(data.begin(), data.end());
            const std::uint64_t bits = std::uint64_t(msg.size()) * 8;
            msg.push_back(0x80);
            while (msg.size() % 64 != 56) msg.push_back(0);
            for (int i = 0; i < 8; ++i) msg.push_back(std::uint8_t(bits >> (8 * i)));
            std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
            for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
                std::uint32_t M[16];
                for (int i = 0; i < 16; ++i)
                    M[i] = std::uint32_t(msg[chunk + 4 * i]) | std::uint32_t(msg[chunk + 4 * i + 1]) << 8 | std::uint32_t(msg[chunk + 4 * i + 2]) << 16 |
                           std::uint32_t(msg[chunk + 4 * i + 3]) << 24;
                std::uint32_t A = a0, B = b0, C = c0, D = d0;
                for (int i = 0; i < 64; ++i) {
                    std::uint32_t F;
                    int g;
                    if (i < 16) F = (B & C) | (~B & D), g = i;
                    else if (i < 32) F = (D & B) | (~D & C), g = (5 * i + 1) % 16;
                    else if (i < 48) F = B ^ C ^ D, g = (3 * i + 5) % 16;
                    else F = C ^ (B | ~D), g = (7 * i) % 16;
                    F = F + A + K[i] + M[g];
                    A = D;
                    D = C;
                    C = B;
                    B = B + ((F << S[i]) | (F >> (32 - S[i])));
                }
                a0 += A, b0 += B, c0 += C, d0 += D;
            }
            std::string hex;
            char buf[3];
            for (std::uint32_t v : { a0, b0, c0, d0 })
                for (int i = 0; i < 4; ++i) {
                    std::snprintf(buf, sizeof buf, "%02x", (v >> (8 * i)) & 0xff);
                    hex += buf;
                }
            return hex;
        }

        std::string Base64EncodeNoWrap(const std::vector<std::uint8_t>& data) {
            static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            std::size_t i = 0;
            for (; i + 2 < data.size(); i += 3) {
                const std::uint32_t v = std::uint32_t(data[i]) << 16 | std::uint32_t(data[i + 1]) << 8 | data[i + 2];
                for (int s : { 18, 12, 6, 0 }) out += kAlphabet[(v >> s) & 63];
            }
            if (i + 1 == data.size()) {
                const std::uint32_t v = std::uint32_t(data[i]) << 16;
                out += kAlphabet[(v >> 18) & 63];
                out += kAlphabet[(v >> 12) & 63];
                out += "==";
            } else if (i + 2 == data.size()) {
                const std::uint32_t v = std::uint32_t(data[i]) << 16 | std::uint32_t(data[i + 1]) << 8;
                out += kAlphabet[(v >> 18) & 63];
                out += kAlphabet[(v >> 12) & 63];
                out += kAlphabet[(v >> 6) & 63];
                out += '=';
            }
            return out;
        }
        // android.util.Base64.DEFAULT: a newline after every 76 characters and at the end.
        std::string Base64EncodeDefault(const std::vector<std::uint8_t>& data) {
            const std::string plain = Base64EncodeNoWrap(data);
            std::string out;
            for (std::size_t i = 0; i < plain.size(); i += 76) out += plain.substr(i, 76) + "\n";
            return out;
        }
    }

    using namespace detail;

    // --- RtspHeaders ----------------------------------------------------------------------------
    namespace {
        std::string ConvertToStandardHeaderName(const std::string& name) {
            static constexpr const char* kNames[] = {
                RtspHeaders::ACCEPT, RtspHeaders::ALLOW, RtspHeaders::AUTHORIZATION, RtspHeaders::BANDWIDTH, RtspHeaders::BLOCKSIZE,
                RtspHeaders::CACHE_CONTROL, RtspHeaders::CONNECTION, RtspHeaders::CONTENT_BASE, RtspHeaders::CONTENT_ENCODING,
                RtspHeaders::CONTENT_LANGUAGE, RtspHeaders::CONTENT_LENGTH, RtspHeaders::CONTENT_LOCATION, RtspHeaders::CONTENT_TYPE, RtspHeaders::CSEQ,
                RtspHeaders::DATE, RtspHeaders::EXPIRES, RtspHeaders::LOCATION, RtspHeaders::PROXY_AUTHENTICATE, RtspHeaders::PROXY_REQUIRE,
                RtspHeaders::PUBLIC, RtspHeaders::RANGE, RtspHeaders::RTP_INFO, RtspHeaders::RTCP_INTERVAL, RtspHeaders::SCALE, RtspHeaders::SESSION,
                RtspHeaders::SPEED, RtspHeaders::SUPPORTED, RtspHeaders::TIMESTAMP, RtspHeaders::TRANSPORT, RtspHeaders::USER_AGENT, RtspHeaders::VIA,
                RtspHeaders::WWW_AUTHENTICATE };
            for (const char* n : kNames)
                if (EqualsIgnoreCase(name, n)) return n;
            return name;
        }
    }

    RtspHeaders::Builder::Builder(const std::string& userAgent, const std::optional<std::string>& sessionId, int cSeq) {
        Add(USER_AGENT, userAgent);
        Add(CSEQ, std::to_string(cSeq));
        if (sessionId) Add(SESSION, *sessionId);
    }

    RtspHeaders::Builder& RtspHeaders::Builder::Add(const std::string& name, const std::string& value) {
        const std::string key = ConvertToStandardHeaderName(Trim(name));
        for (auto& [k, values] : m_Entries)
            if (k == key) {
                values.push_back(Trim(value));
                return *this;
            }
        m_Entries.push_back({ key, { Trim(value) } });
        return *this;
    }

    RtspHeaders::Builder& RtspHeaders::Builder::AddAll(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            const auto header = SplitAtFirst(line, ":\\s?");
            if (header.size() == 2) Add(header[0], header[1]);
        }
        return *this;
    }

    RtspHeaders::Builder& RtspHeaders::Builder::AddAll(const std::vector<std::pair<std::string, std::string>>& headers) {
        for (const auto& [k, v] : headers) Add(k, v);
        return *this;
    }

    RtspHeaders::Builder RtspHeaders::BuildUpon() const {
        Builder b;
        b.m_Entries = m_Entries;
        return b;
    }

    std::optional<std::string> RtspHeaders::Get(const std::string& name) const {
        const auto values = Values(name);
        if (values.empty()) return std::nullopt;
        return values.back();
    }

    std::vector<std::string> RtspHeaders::Values(const std::string& name) const {
        const std::string key = ConvertToStandardHeaderName(name);
        for (const auto& [k, values] : m_Entries)
            if (k == key) return values;
        return {};
    }

    // --- RtspMessageUtil ------------------------------------------------------------------------
    namespace RtspMessageUtil {
        namespace {
            const std::regex& RequestLine() {
                static const std::regex r("([A-Z_]+) (.*) RTSP/1\\.0");
                return r;
            }
            const std::regex& StatusLine() {
                static const std::regex r("RTSP/1\\.0 (\\d+) (.+)");
                return r;
            }
            void AddHeaderLines(std::vector<std::string>& out, const RtspHeaders& headers) {
                for (const auto& [name, values] : headers.AsMultiMap())
                    for (const auto& v : values) out.push_back(name + ": " + v);
            }
            std::string Join(const std::vector<std::string>& lines, std::size_t from, const char* separator) {
                std::string out;
                for (std::size_t i = from; i < lines.size(); ++i) {
                    if (i > from) out += separator;
                    out += lines[i];
                }
                return out;
            }
            std::ptrdiff_t IndexOfEmpty(const std::vector<std::string>& lines) {
                for (std::size_t i = 0; i < lines.size(); ++i)
                    if (lines[i].empty()) return std::ptrdiff_t(i);
                return -1;
            }
            [[noreturn]] void Malformed(const std::string& message) { throw ParserException("malformed RTSP message: " + message); }
        }

        std::vector<std::string> SerializeRequest(const RtspRequest& request) {
            if (!request.headers.Get(RtspHeaders::CSEQ)) throw std::invalid_argument("a request needs a CSeq");
            std::vector<std::string> out = { ToMethodString(request.method) + " " + request.uri + " RTSP/1.0" };
            AddHeaderLines(out, request.headers);
            out.push_back("");
            out.push_back(request.messageBody);
            return out;
        }

        std::vector<std::string> SerializeResponse(const RtspResponse& response) {
            if (!response.headers.Get(RtspHeaders::CSEQ)) throw std::invalid_argument("a response needs a CSeq");
            std::vector<std::string> out = { "RTSP/1.0 " + std::to_string(response.status) + " " + GetRtspStatusReasonPhrase(response.status) };
            AddHeaderLines(out, response.headers);
            out.push_back("");
            out.push_back(response.messageBody);
            return out;
        }

        std::string ConvertMessageToBytes(const std::vector<std::string>& message) { return Join(message, 0, "\r\n"); }

        std::string RemoveUserInfo(const std::string& uri) {
            const auto userInfo = UriUserInfo(uri);
            if (!userInfo) return uri;
            const std::size_t at = uri.find(*userInfo + "@");
            return uri.substr(0, at) + uri.substr(at + userInfo->size() + 1);
        }

        std::optional<RtspAuthUserInfo> ParseUserInfo(const std::string& uri) {
            const auto userInfo = UriUserInfo(uri);
            if (!userInfo || userInfo->find(':') == std::string::npos) return std::nullopt;
            const std::size_t colon = userInfo->find(':');
            return RtspAuthUserInfo{ userInfo->substr(0, colon), userInfo->substr(colon + 1) };
        }

        std::string ToMethodString(RtspMethod method) {
            switch (method) {
                case RtspMethod::Announce: return "ANNOUNCE";
                case RtspMethod::Describe: return "DESCRIBE";
                case RtspMethod::GetParameter: return "GET_PARAMETER";
                case RtspMethod::Options: return "OPTIONS";
                case RtspMethod::Pause: return "PAUSE";
                case RtspMethod::Play: return "PLAY";
                case RtspMethod::PlayNotify: return "PLAY_NOTIFY";
                case RtspMethod::Record: return "RECORD";
                case RtspMethod::Redirect: return "REDIRECT";
                case RtspMethod::Setup: return "SETUP";
                case RtspMethod::SetParameter: return "SET_PARAMETER";
                case RtspMethod::Teardown: return "TEARDOWN";
                case RtspMethod::Unset: break;
            }
            throw std::logic_error("no method");
        }

        RtspMethod ParseMethodString(const std::string& m) {
            static const std::pair<const char*, RtspMethod> kMethods[] = {
                { "ANNOUNCE", RtspMethod::Announce }, { "DESCRIBE", RtspMethod::Describe }, { "GET_PARAMETER", RtspMethod::GetParameter },
                { "OPTIONS", RtspMethod::Options },   { "PAUSE", RtspMethod::Pause },       { "PLAY", RtspMethod::Play },
                { "PLAY_NOTIFY", RtspMethod::PlayNotify }, { "RECORD", RtspMethod::Record }, { "REDIRECT", RtspMethod::Redirect },
                { "SETUP", RtspMethod::Setup },       { "SET_PARAMETER", RtspMethod::SetParameter }, { "TEARDOWN", RtspMethod::Teardown } };
            for (const auto& [name, method] : kMethods)
                if (m == name) return method;
            return RtspMethod::Unset;
        }

        RtspResponse ParseResponse(const std::vector<std::string>& lines) {
            std::smatch m;
            if (lines.empty() || !std::regex_match(lines[0], m, StatusLine())) throw std::invalid_argument("not an RTSP response");
            const int status = std::stoi(m[1].str());
            const std::ptrdiff_t body = IndexOfEmpty(lines);
            if (body <= 0) throw std::invalid_argument("no header end");
            const std::vector<std::string> headerLines(lines.begin() + 1, lines.begin() + body);
            return { status, RtspHeaders::Builder().AddAll(headerLines).Build(), Join(lines, std::size_t(body) + 1, "\r\n") };
        }

        RtspRequest ParseRequest(const std::vector<std::string>& lines) {
            std::smatch m;
            if (lines.empty() || !std::regex_match(lines[0], m, RequestLine())) throw std::invalid_argument("not an RTSP request");
            const RtspMethod method = ParseMethodString(m[1].str());
            const std::string uri = m[2].str();
            const std::ptrdiff_t body = IndexOfEmpty(lines);
            if (body <= 0) throw std::invalid_argument("no header end");
            const std::vector<std::string> headerLines(lines.begin() + 1, lines.begin() + body);
            return { uri, method, RtspHeaders::Builder().AddAll(headerLines).Build(), Join(lines, std::size_t(body) + 1, "\r\n") };
        }

        bool IsRtspStartLine(const std::string& line) { return std::regex_match(line, RequestLine()) || std::regex_match(line, StatusLine()); }
        bool IsRtspResponse(const std::vector<std::string>& lines) { return !lines.empty() && std::regex_match(lines[0], StatusLine()); }

        std::vector<std::string> SplitRtspMessageBody(const std::string& body) {
            return Split(body, body.find("\r\n") != std::string::npos ? "\r\n" : "\n");
        }

        std::int64_t ParseContentLengthHeader(const std::string& line) {
            static const std::regex r("Content-Length:\\s?(\\d+)", std::regex::icase);
            std::smatch m;
            if (!std::regex_search(line, m, r)) return stream::kLengthUnset;
            try {
                return std::stoll(m[1].str());
            } catch (const std::exception&) {
                Malformed(line);
            }
        }

        std::vector<RtspMethod> ParsePublicHeader(const std::optional<std::string>& value) {
            if (!value) return {};
            std::vector<RtspMethod> out;
            for (const auto& m : Split(*value, ",\\s?"))
                if (RtspMethod method = ParseMethodString(m); method != RtspMethod::Unset) out.push_back(method);
            return out;
        }

        RtspSessionHeader ParseSessionHeader(const std::string& value) {
            static const std::regex r("([\\w$\\-_.+]+)(?:;\\s?timeout=(\\d+))?");
            std::smatch m;
            if (!std::regex_match(value, m, r)) Malformed(value);
            RtspSessionHeader header{ m[1].str(), kDefaultRtspTimeoutMs };
            if (m[2].matched) {
                try {
                    header.timeoutMs = std::int64_t(std::stoi(m[2].str())) * 1000;
                } catch (const std::exception&) {
                    Malformed(value);
                }
            }
            return header;
        }

        RtspAuthenticationInfo ParseWwwAuthenticateHeader(const std::string& value) {
            static const std::regex digest(
                "Digest realm=\"([^\"\\x00-\\x08\\x0A-\\x1f\\x7f]+)\""
                ",\\s?(?:domain=\"(.+)\""
                ",\\s?)?nonce=\"([^\"\\x00-\\x08\\x0A-\\x1f\\x7f]+)\""
                "(?:,\\s?opaque=\"([^\"\\x00-\\x08\\x0A-\\x1f\\x7f]+)\")?");
            static const std::regex basic("Basic realm=\"([^\"\\x00-\\x08\\x0A-\\x1f\\x7f]+)\"");
            std::smatch m;
            if (std::regex_search(value, m, digest)) return { RtspAuthenticationInfo::DIGEST, m[1].str(), m[3].str(), m[4].matched ? m[4].str() : "" };
            if (std::regex_match(value, m, basic)) return { RtspAuthenticationInfo::BASIC, m[1].str(), "", "" };
            throw ParserException("Invalid WWW-Authenticate header " + value);
        }

        void CheckManifestExpression(bool expression, const std::string& message) {
            if (!expression) Malformed(message);
        }

        std::string GetRtspStatusReasonPhrase(int statusCode) {
            switch (statusCode) {
                case 200: return "OK";
                case 301: return "Move Permanently";
                case 302: return "Move Temporarily";
                case 400: return "Bad Request";
                case 401: return "Unauthorized";
                case 404: return "Not Found";
                case 405: return "Method Not Allowed";
                case 454: return "Session Not Found";
                case 455: return "Method Not Valid In This State";
                case 456: return "Header Field Not Valid";
                case 457: return "Invalid Range";
                case 461: return "Unsupported Transport";
                case 500: return "Internal Server Error";
                case 505: return "RTSP Version Not Supported";
                default: throw std::invalid_argument("no reason phrase for " + std::to_string(statusCode));
            }
        }

        int ParseInt(const std::string& value) {
            try {
                std::size_t used = 0;
                const long long n = std::stoll(value, &used);
                if (used != value.size() || n < INT32_MIN || n > INT32_MAX) throw std::invalid_argument(value);
                return int(n);
            } catch (const std::exception&) {
                Malformed(value);
            }
        }
    }

    // --- RtspAuthenticationInfo -----------------------------------------------------------------
    std::string RtspAuthenticationInfo::GetAuthorizationHeaderValue(const RtspAuthUserInfo& user, const std::string& uri, RtspMethod method) const {
        if (authenticationMechanism == BASIC) {
            const std::string credentials = user.username + ":" + user.password;
            return "Basic " + Base64EncodeDefault(std::vector<std::uint8_t>(credentials.begin(), credentials.end()));
        }
        if (authenticationMechanism != DIGEST) throw RtspException("unsupported authentication");
        const std::string hashA1 = Md5Hex(user.username + ":" + realm + ":" + user.password);
        const std::string hashA2 = Md5Hex(RtspMessageUtil::ToMethodString(method) + ":" + uri);
        const std::string response = Md5Hex(hashA1 + ":" + nonce + ":" + hashA2);
        std::string value = "Digest username=\"" + user.username + "\", realm=\"" + realm + "\", nonce=\"" + nonce + "\", uri=\"" + uri + "\", response=\"" + response + "\"";
        if (!opaque.empty()) value += ", opaque=\"" + opaque + "\"";
        return value;
    }

    // --- timing ---------------------------------------------------------------------------------
    RtspSessionTiming RtspSessionTiming::ParseTiming(const std::string& range) {
        static const std::regex r("npt[:=]([.\\d]+|now)\\s?-\\s?([.\\d]+)?");
        std::smatch m;
        RtspMessageUtil::CheckManifestExpression(std::regex_match(range, m, r), range);
        RtspSessionTiming t;
        auto seconds = [&](const std::string& s) {
            try {
                return std::int64_t(std::stof(s) * 1000);   // (long) (Float.parseFloat(s) * 1000)
            } catch (const std::exception&) {
                throw ParserException("malformed RTSP timing: " + s);
            }
        };
        t.startTimeMs = m[1].str() == "now" ? 0 : seconds(m[1].str());
        if (m[2].matched) {
            t.stopTimeMs = seconds(m[2].str());
            RtspMessageUtil::CheckManifestExpression(t.stopTimeMs >= t.startTimeMs, range);
        }
        return t;
    }

    std::string RtspSessionTiming::GetOffsetStartTimeTiming(std::int64_t offsetStartTimeMs) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "npt=%.3f-", double(offsetStartTimeMs) / 1000);
        return buf;
    }

    std::vector<RtspTrackTiming> RtspTrackTiming::ParseTrackTiming(const std::string& rtpInfo, const std::string& sessionUri) {
        std::vector<RtspTrackTiming> out;
        for (const auto& perTrack : Split(rtpInfo, ",")) {
            RtspTrackTiming t;
            std::optional<std::string> uri;
            for (const auto& pair : Split(perTrack, ";")) {
                try {
                    const auto attributes = SplitAtFirst(pair, "=");
                    const std::string& name = attributes.at(0);
                    const std::string& value = attributes.at(1);
                    if (name == "url") uri = ResolveUri(value, sessionUri);
                    else if (name == "seq") t.sequenceNumber = RtspMessageUtil::ParseInt(value);
                    else if (name == "rtptime") t.rtpTimestamp = std::stoll(value);
                    else throw ParserException(name);
                } catch (const std::exception&) {
                    throw ParserException("malformed RTP-Info: " + pair);
                }
            }
            if (!uri || !UriScheme(*uri) || (t.sequenceNumber == -1 && t.rtpTimestamp == kTimeUnset)) throw ParserException("malformed RTP-Info: " + perTrack);
            t.uri = *uri;
            out.push_back(std::move(t));
        }
        return out;
    }

    std::string RtspTrackTiming::ResolveUri(const std::string& url, const std::string& sessionUri) {
        if (UriScheme(sessionUri).value_or("") != "rtsp") throw std::invalid_argument("not an rtsp URI: " + sessionUri);
        if (UriScheme(url)) return url;   // absolute
        const std::string asRtsp = "rtsp://" + url;
        if (UriHost(asRtsp) && UriHost(asRtsp) == UriHost(sessionUri)) return asRtsp;
        return stream::ResolveUri(sessionUri.ends_with('/') ? sessionUri : sessionUri + "/", url);
    }

}
