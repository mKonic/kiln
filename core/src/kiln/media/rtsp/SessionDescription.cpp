// media3 exoplayer-rtsp: SessionDescriptionParser, MediaDescription.
#include "kiln/media/rtsp/SessionDescription.h"

#include <regex>

namespace kiln::media::rtsp {

    using namespace detail;

    MediaDescription::RtpMapAttribute MediaDescription::RtpMapAttribute::Parse(const std::string& rtpmap) {
        const auto info = SplitAtFirst(rtpmap, " ");
        if (info.size() != 2) throw std::invalid_argument("rtpmap: " + rtpmap);
        RtpMapAttribute a;
        a.payloadType = RtspMessageUtil::ParseInt(info[0]);
        const auto media = Split(Trim(info[1]), "/");
        if (media.size() < 2) throw std::invalid_argument("rtpmap: " + rtpmap);
        a.mediaEncoding = media[0];
        a.clockRate = RtspMessageUtil::ParseInt(media[1]);
        if (media.size() == 3) a.encodingParameters = RtspMessageUtil::ParseInt(media[2]);
        return a;
    }

    MediaDescription::Builder::Builder(std::string mediaType, int port, std::string transportProtocol, int payloadType)
        : m_MediaType(std::move(mediaType)), m_TransportProtocol(std::move(transportProtocol)), m_Port(port), m_PayloadType(payloadType) {}

    MediaDescription MediaDescription::Builder::Build() const {
        auto staticRtpMap = [](int payloadType) -> std::string {
            if (payloadType >= 96) throw std::invalid_argument("dynamic payload type without rtpmap");
            switch (payloadType) {
                case 0: return "0 PCMU/8000/1";
                case 8: return "8 PCMA/8000/1";
                case 10: return "10 L16/44100/2";
                case 11: return "11 L16/44100/1";
                default: throw std::logic_error("Unsupported static paylod type " + std::to_string(payloadType));
            }
        };
        MediaDescription d;
        try {
            auto it = m_Attributes.find(SessionDescription::ATTR_RTPMAP);
            d.rtpMapAttribute = RtpMapAttribute::Parse(it != m_Attributes.end() ? it->second : staticRtpMap(m_PayloadType));
        } catch (const ParserException& e) {
            throw std::logic_error(e.what());
        }
        d.mediaType = m_MediaType;
        d.port = m_Port;
        d.transportProtocol = m_TransportProtocol;
        d.payloadType = m_PayloadType;
        d.mediaTitle = m_MediaTitle;
        d.connection = m_Connection;
        d.bitrate = m_Bitrate;
        d.key = m_Key;
        d.attributes = m_Attributes;
        return d;
    }

    std::map<std::string, std::string> MediaDescription::GetFmtpParametersAsMap() const {
        auto it = attributes.find(SessionDescription::ATTR_FMTP);
        if (it == attributes.end()) return {};
        const auto components = SplitAtFirst(it->second, " ");
        if (components.size() != 2) throw std::invalid_argument(it->second);
        // String.split(";\\s?", 0): trailing empty strings dropped.
        std::vector<std::string> parameters = Split(components[1], ";\\s?");
        while (!parameters.empty() && parameters.back().empty()) parameters.pop_back();
        std::map<std::string, std::string> out;
        for (const auto& parameter : parameters) {
            const auto pair = SplitAtFirst(parameter, "=");
            if (pair.size() != 2) throw std::out_of_range("fmtp parameter without a value: " + parameter);
            if (!out.emplace(pair[0], pair[1]).second) throw std::invalid_argument("duplicate fmtp parameter " + pair[0]);   // buildOrThrow
        }
        return out;
    }

    namespace SessionDescriptionParser {
        SessionDescription Parse(const std::string& sdp) {
            static const std::regex line("([a-z])=\\s?([^\\n\\r]+)");
            static const std::regex emptyValue("^([a-z])=$");
            static const std::regex attribute("([\\x21\\x23-\\x27\\x2a\\x2b\\x2d\\x2e\\x30-\\x39\\x41-\\x5a\\x5e-\\x7e]+)(?::([^\\n\\r]*))?");
            static const std::regex mediaLine("(\\S+)\\s(\\S+)\\s(\\S+)\\s(\\S+)");
            SessionDescription session;
            std::optional<MediaDescription::Builder> media;
            bool skippingMedia = false;
            auto addMedia = [&] {
                try {
                    session.mediaDescriptionList.push_back(media->Build());
                } catch (const std::logic_error& e) {
                    throw ParserException(std::string("malformed SDP: ") + e.what());
                }
            };
            for (std::string l : RtspMessageUtil::SplitRtspMessageBody(sdp)) {
                l = Trim(l);
                if (l.empty()) continue;
                std::smatch m;
                if (!std::regex_match(l, m, line)) {
                    std::smatch e;
                    if (std::regex_match(l, e, emptyValue) && e[1].str() == "i") continue;
                    throw ParserException("Malformed SDP line: " + l);
                }
                const std::string type = m[1].str(), value = m[2].str();
                const char t = type[0];
                if (t == 'v') {
                    if (value != SessionDescription::SUPPORTED_SDP_VERSION) throw ParserException("SDP version " + value + " is not supported.");
                } else if (t == 'o') {
                    session.origin = value;
                } else if (t == 's') {
                    session.sessionName = value;
                } else if (t == 'i') {
                    if (skippingMedia) continue;
                    if (!media) session.sessionInfo = value;
                    else media->SetMediaTitle(value);
                } else if (t == 'u') {
                    session.uri = value;
                } else if (t == 'e') {
                    session.emailAddress = value;
                } else if (t == 'p') {
                    session.phoneNumber = value;
                } else if (t == 'c') {
                    if (skippingMedia) continue;
                    if (!media) session.connection = value;
                    else media->SetConnection(value);
                } else if (t == 'b') {
                    if (skippingMedia) continue;
                    const auto parts = Split(value, ":\\s?");
                    if (parts.size() != 2) throw std::invalid_argument("bandwidth: " + value);
                    const int kbps = std::stoi(parts[1]);
                    if (!media) session.bitrate = kbps * 1000;
                    else media->SetBitrate(kbps * 1000);
                } else if (t == 't') {
                    session.timing = value;
                } else if (t == 'k') {
                    if (skippingMedia) continue;
                    if (!media) session.key = value;
                    else media->SetKey(value);
                } else if (t == 'a') {
                    if (skippingMedia) continue;
                    std::smatch a;
                    if (!std::regex_match(value, a, attribute)) throw ParserException("Malformed Attribute line: " + l);
                    const std::string name = a[1].str(), attributeValue = a[2].matched ? a[2].str() : "";
                    if (!media) session.attributes[name] = attributeValue;
                    else media->AddAttribute(name, attributeValue);
                } else if (t == 'm') {
                    if (media) addMedia();
                    std::smatch d;
                    if (!std::regex_match(value, d, mediaLine)) throw ParserException("Malformed SDP media description line: " + value);
                    media.reset();
                    try {
                        std::size_t used = 0;
                        const int port = std::stoi(d[2].str(), &used);
                        if (used != d[2].str().size()) throw std::invalid_argument("port");
                        const int payloadType = std::stoi(d[4].str(), &used);
                        if (used != d[4].str().size()) throw std::invalid_argument("payload type");
                        media.emplace(d[1].str(), port, d[3].str(), payloadType);
                    } catch (const std::exception&) {
                        // Log.w: the media description is skipped.
                    }
                    skippingMedia = !media;
                }
            }
            if (media) addMedia();
            return session;
        }
    }

}
