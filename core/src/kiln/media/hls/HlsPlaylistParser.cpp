// media3-exoplayer-hls HlsPlaylistParser: the tags of RFC 8216 (and its low-latency, content
// steering and interstitial extensions), read with upstream's regular expressions.

#include "kiln/media/hls/HlsPlaylist.h"

#include "kiln/Log.h"

#include <cctype>
#include <cstdio>
#include <deque>
#include <regex>
#include <set>

namespace kiln::media::hls {

    using namespace stream;
    using Variant = HlsMultivariantPlaylist::Variant;
    using Rendition = HlsMultivariantPlaylist::Rendition;
    using Segment = HlsMediaPlaylist::Segment;
    using Part = HlsMediaPlaylist::Part;
    using Interstitial = HlsMediaPlaylist::Interstitial;
    using ClientDefinedAttribute = HlsMediaPlaylist::ClientDefinedAttribute;

    namespace {

        const std::string PLAYLIST_HEADER = "#EXTM3U";
        const std::string TAG_PREFIX = "#EXT";
        const std::string TAG_VERSION = "#EXT-X-VERSION";
        const std::string TAG_PLAYLIST_TYPE = "#EXT-X-PLAYLIST-TYPE";
        const std::string TAG_DEFINE = "#EXT-X-DEFINE";
        const std::string TAG_SERVER_CONTROL = "#EXT-X-SERVER-CONTROL";
        const std::string TAG_STREAM_INF = "#EXT-X-STREAM-INF";
        const std::string TAG_PART_INF = "#EXT-X-PART-INF";
        const std::string TAG_PART = "#EXT-X-PART";
        const std::string TAG_I_FRAME_STREAM_INF = "#EXT-X-I-FRAME-STREAM-INF";
        const std::string TAG_IFRAME = "#EXT-X-I-FRAMES-ONLY";
        const std::string TAG_MEDIA = "#EXT-X-MEDIA";
        const std::string TAG_TARGET_DURATION = "#EXT-X-TARGETDURATION";
        const std::string TAG_DISCONTINUITY = "#EXT-X-DISCONTINUITY";
        const std::string TAG_DISCONTINUITY_SEQUENCE = "#EXT-X-DISCONTINUITY-SEQUENCE";
        const std::string TAG_PROGRAM_DATE_TIME = "#EXT-X-PROGRAM-DATE-TIME";
        const std::string TAG_INIT_SEGMENT = "#EXT-X-MAP";
        const std::string TAG_INDEPENDENT_SEGMENTS = "#EXT-X-INDEPENDENT-SEGMENTS";
        const std::string TAG_MEDIA_DURATION = "#EXTINF";
        const std::string TAG_MEDIA_SEQUENCE = "#EXT-X-MEDIA-SEQUENCE";
        const std::string TAG_START = "#EXT-X-START";
        const std::string TAG_ENDLIST = "#EXT-X-ENDLIST";
        const std::string TAG_KEY = "#EXT-X-KEY";
        const std::string TAG_SESSION_KEY = "#EXT-X-SESSION-KEY";
        const std::string TAG_BYTERANGE = "#EXT-X-BYTERANGE";
        const std::string TAG_GAP = "#EXT-X-GAP";
        const std::string TAG_SKIP = "#EXT-X-SKIP";
        const std::string TAG_PRELOAD_HINT = "#EXT-X-PRELOAD-HINT";
        const std::string TAG_RENDITION_REPORT = "#EXT-X-RENDITION-REPORT";
        const std::string TAG_DATERANGE = "#EXT-X-DATERANGE";
        const std::string TAG_CONTENT_STEERING = "#EXT-X-CONTENT-STEERING";
        const std::string TYPE_AUDIO = "AUDIO";
        const std::string TYPE_VIDEO = "VIDEO";
        const std::string TYPE_SUBTITLES = "SUBTITLES";
        const std::string TYPE_CLOSED_CAPTIONS = "CLOSED-CAPTIONS";
        const std::string TYPE_PART = "PART";
        const std::string METHOD_NONE = "NONE";
        const std::string METHOD_AES_128 = "AES-128";
        const std::string METHOD_SAMPLE_AES_CENC = "SAMPLE-AES-CENC";
        const std::string METHOD_SAMPLE_AES_CTR = "SAMPLE-AES-CTR";
        const std::string KEYFORMAT_PLAYREADY = "com.microsoft.playready";
        const std::string KEYFORMAT_IDENTITY = "identity";
        const std::string KEYFORMAT_WIDEVINE_PSSH_BINARY = "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed";
        const std::string KEYFORMAT_WIDEVINE_PSSH_JSON = "com.widevine";
        const std::string BOOLEAN_TRUE = "YES";
        const std::string BOOLEAN_FALSE = "NO";
        const std::string ATTR_CLOSED_CAPTIONS_NONE = "CLOSED-CAPTIONS=NONE";
        const std::string Q = "\"((?:.|\\f)+?)\"";   // ATTR_QUOTED_STRING_VALUE_PATTERN
        const std::string DATERANGE_CLASS_INTERSTITIALS = "com.apple.hls.interstitial";

        struct Pattern {
            std::string source;
            std::regex re;
            explicit Pattern(std::string s) : source(std::move(s)), re(source) {}
        };
        Pattern Bool(const std::string& attribute) { return Pattern(attribute + "=(" + BOOLEAN_FALSE + "|" + BOOLEAN_TRUE + ")"); }

        const Pattern REGEX_AVERAGE_BANDWIDTH("AVERAGE-BANDWIDTH=(\\d+)\\b");
        const Pattern REGEX_VIDEO("VIDEO=" + Q);
        const Pattern REGEX_AUDIO("AUDIO=" + Q);
        const Pattern REGEX_SUBTITLES("SUBTITLES=" + Q);
        const Pattern REGEX_CLOSED_CAPTIONS("CLOSED-CAPTIONS=" + Q);
        const Pattern REGEX_BANDWIDTH("[^-]BANDWIDTH=(\\d+)\\b");
        const Pattern REGEX_CHANNELS("CHANNELS=" + Q);
        const Pattern REGEX_SAMPLE_RATE("SAMPLE-RATE=(\\d+)\\b");
        const Pattern REGEX_VIDEO_RANGE("VIDEO-RANGE=(SDR|PQ|HLG)");
        const Pattern REGEX_CODECS("CODECS=" + Q);
        const Pattern REGEX_SUPPLEMENTAL_CODECS("SUPPLEMENTAL-CODECS=" + Q);
        const Pattern REGEX_RESOLUTION("RESOLUTION=(\\d+x\\d+)");
        const Pattern REGEX_FRAME_RATE("FRAME-RATE=([\\d\\.]+)\\b");
        const Pattern REGEX_SCORE("SCORE=([\\d\\.]+)\\b");
        const Pattern REGEX_SERVER_URI("SERVER-URI=" + Q);
        const Pattern REGEX_PATHWAY_ID("PATHWAY-ID=" + Q);
        const Pattern REGEX_STABLE_VARIANT_ID("STABLE-VARIANT-ID=" + Q);
        const Pattern REGEX_STABLE_RENDITION_ID("STABLE-RENDITION-ID=" + Q);
        const Pattern REGEX_TARGET_DURATION(TAG_TARGET_DURATION + ":(\\d+)\\b");
        const Pattern REGEX_ATTR_DURATION("DURATION=([\\d\\.]+)\\b");
        const Pattern REGEX_ATTR_DURATION_PREFIXED("[:,]DURATION=([\\d\\.]+)\\b");
        const Pattern REGEX_PART_TARGET_DURATION("PART-TARGET=([\\d\\.]+)\\b");
        const Pattern REGEX_VERSION(TAG_VERSION + ":(\\d+)\\b");
        const Pattern REGEX_PLAYLIST_TYPE(TAG_PLAYLIST_TYPE + ":(.+)\\b");
        const Pattern REGEX_CAN_SKIP_UNTIL("CAN-SKIP-UNTIL=([\\d\\.]+)\\b");
        const Pattern REGEX_CAN_SKIP_DATE_RANGES = Bool("CAN-SKIP-DATERANGES");
        const Pattern REGEX_SKIPPED_SEGMENTS("SKIPPED-SEGMENTS=(\\d+)\\b");
        const Pattern REGEX_HOLD_BACK("[:|,]HOLD-BACK=([\\d\\.]+)\\b");
        const Pattern REGEX_PART_HOLD_BACK("PART-HOLD-BACK=([\\d\\.]+)\\b");
        const Pattern REGEX_CAN_BLOCK_RELOAD = Bool("CAN-BLOCK-RELOAD");
        const Pattern REGEX_MEDIA_SEQUENCE(TAG_MEDIA_SEQUENCE + ":(\\d+)\\b");
        const Pattern REGEX_MEDIA_DURATION(TAG_MEDIA_DURATION + ":([\\d\\.]+)\\b");
        const Pattern REGEX_MEDIA_TITLE(TAG_MEDIA_DURATION + ":[\\d\\.]+\\b,(.+)");
        const Pattern REGEX_LAST_MSN("LAST-MSN=(\\d+)\\b");
        const Pattern REGEX_LAST_PART("LAST-PART=(\\d+)\\b");
        const Pattern REGEX_TIME_OFFSET("TIME-OFFSET=(-?[\\d\\.]+)\\b");
        const Pattern REGEX_BYTERANGE(TAG_BYTERANGE + ":(\\d+(?:@\\d+)?)\\b");
        const Pattern REGEX_ATTR_BYTERANGE("BYTERANGE=\"(\\d+(?:@\\d+)?)\\b\"");
        const Pattern REGEX_BYTERANGE_START("BYTERANGE-START=(\\d+)\\b");
        const Pattern REGEX_BYTERANGE_LENGTH("BYTERANGE-LENGTH=(\\d+)\\b");
        const Pattern REGEX_METHOD("METHOD=(NONE|AES-128|SAMPLE-AES|SAMPLE-AES-CENC|SAMPLE-AES-CTR)\\s*(?:,|$)");
        const Pattern REGEX_KEYFORMAT("KEYFORMAT=" + Q);
        const Pattern REGEX_KEYFORMATVERSIONS("KEYFORMATVERSIONS=" + Q);
        const Pattern REGEX_URI("URI=" + Q);
        const Pattern REGEX_IV("IV=([^,.*]+)");
        const Pattern REGEX_TYPE("TYPE=(AUDIO|VIDEO|SUBTITLES|CLOSED-CAPTIONS)");
        const Pattern REGEX_PRELOAD_HINT_TYPE("TYPE=(PART|MAP)");
        const Pattern REGEX_LANGUAGE("LANGUAGE=" + Q);
        const Pattern REGEX_NAME("NAME=" + Q);
        const Pattern REGEX_QUERY_PARAM("QUERYPARAM=" + Q);
        const Pattern REGEX_GROUP_ID("GROUP-ID=" + Q);
        const Pattern REGEX_CHARACTERISTICS("CHARACTERISTICS=" + Q);
        const Pattern REGEX_INSTREAM_ID("INSTREAM-ID=\"((?:CC|SERVICE)\\d+)\"");
        const Pattern REGEX_AUTOSELECT = Bool("AUTOSELECT");
        const Pattern REGEX_DEFAULT = Bool("DEFAULT");
        const Pattern REGEX_FORCED = Bool("FORCED");
        const Pattern REGEX_INDEPENDENT = Bool("INDEPENDENT");
        const Pattern REGEX_GAP = Bool("GAP");
        const Pattern REGEX_PRECISE = Bool("PRECISE");
        const Pattern REGEX_VALUE("VALUE=" + Q);
        const Pattern REGEX_IMPORT("IMPORT=" + Q);
        const Pattern REGEX_ID("[:,]ID=" + Q);
        const Pattern REGEX_CLASS("CLASS=" + Q);
        const Pattern REGEX_START_DATE("START-DATE=" + Q);
        const Pattern REGEX_CUE("CUE=" + Q);
        const Pattern REGEX_END_DATE("END-DATE=" + Q);
        const Pattern REGEX_PLANNED_DURATION("PLANNED-DURATION=([\\d\\.]+)\\b");
        const Pattern REGEX_END_ON_NEXT = Bool("END-ON-NEXT");
        const Pattern REGEX_ASSET_URI("X-ASSET-URI=" + Q);
        const Pattern REGEX_ASSET_LIST_URI("X-ASSET-LIST=" + Q);
        const Pattern REGEX_RESUME_OFFSET("X-RESUME-OFFSET=(-?[\\d\\.]+)\\b");
        const Pattern REGEX_PLAYOUT_LIMIT("X-PLAYOUT-LIMIT=([\\d\\.]+)\\b");
        const Pattern REGEX_SNAP("X-SNAP=" + Q);
        const Pattern REGEX_RESTRICT("X-RESTRICT=" + Q);
        const Pattern REGEX_CONTENT_MAY_VARY("X-CONTENT-MAY-VARY=" + Q);
        const Pattern REGEX_TIMELINE_OCCUPIES("X-TIMELINE-OCCUPIES=" + Q);
        const Pattern REGEX_TIMELINE_STYLE("X-TIMELINE-STYLE=" + Q);
        const Pattern REGEX_SKIP_CONTROL_OFFSET("X-SKIP-CONTROL-OFFSET=([\\d\\.]+)\\b");
        const Pattern REGEX_SKIP_CONTROL_DURATION("X-SKIP-CONTROL-DURATION=([\\d\\.]+)\\b");
        const Pattern REGEX_SKIP_CONTROL_LABEL_ID("X-SKIP-CONTROL-LABEL-ID=" + Q);
        const Pattern REGEX_VARIABLE_REFERENCE("\\{\\$([a-zA-Z0-9\\-_]+)\\}");
        const Pattern REGEX_CLIENT_DEFINED_ATTRIBUTE_PREFIX("\\b(X-[A-Z0-9-]+)=");

        using Variables = std::map<std::string, std::string>;

        bool StartsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

        // String.trim: characters up to ' '.
        std::string JavaTrim(const std::string& s) {
            std::size_t b = 0, e = s.size();
            while (b < e && static_cast<unsigned char>(s[b]) <= ' ') b++;
            while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') e--;
            return s.substr(b, e - b);
        }

        std::vector<std::string> Split(const std::string& value, const std::string& regex) {
            const std::regex re(regex);
            return { std::sregex_token_iterator(value.begin(), value.end(), re, -1), std::sregex_token_iterator() };
        }
        // Util.splitAtFirst.
        std::vector<std::string> SplitAtFirst(const std::string& value, const std::string& delimiter) {
            const std::size_t i = value.find(delimiter);
            if (i == std::string::npos) return { value };
            return { value.substr(0, i), value.substr(i + delimiter.size()) };
        }

        std::string ReplaceVariableReferences(const std::string& string, const Variables& variables) {
            std::string out;
            auto begin = string.cbegin();
            std::smatch m;
            auto it = string.cbegin();
            while (std::regex_search(it, string.cend(), m, REGEX_VARIABLE_REFERENCE.re)) {
                const auto found = variables.find(m[1].str());
                out.append(begin, m[0].first);
                if (found != variables.end()) out += found->second;
                else out.append(m[0].first, m[0].second);
                begin = m[0].second;
                it = m[0].second;
            }
            out.append(begin, string.cend());
            return out;
        }

        std::optional<std::string> ParseOptionalStringAttr(const std::string& line, const Pattern& pattern, const Variables& variables) {
            std::smatch m;
            if (!std::regex_search(line, m, pattern.re)) return std::nullopt;
            std::string value = m[1].str();
            return variables.empty() ? value : ReplaceVariableReferences(value, variables);
        }
        std::string ParseOptionalStringAttr(const std::string& line, const Pattern& pattern, const std::string& defaultValue, const Variables& variables) {
            std::smatch m;
            const std::string value = std::regex_search(line, m, pattern.re) ? m[1].str() : defaultValue;
            return variables.empty() ? value : ReplaceVariableReferences(value, variables);
        }
        std::string ParseStringAttr(const std::string& line, const Pattern& pattern, const Variables& variables) {
            if (std::optional<std::string> v = ParseOptionalStringAttr(line, pattern, variables)) return *v;
            throw ParserException("Couldn't match " + pattern.source + " in " + line);
        }
        int ParseIntAttr(const std::string& line, const Pattern& pattern) { return std::stoi(ParseStringAttr(line, pattern, {})); }
        std::int64_t ParseLongAttr(const std::string& line, const Pattern& pattern) { return std::stoll(ParseStringAttr(line, pattern, {})); }
        double ParseDoubleAttr(const std::string& line, const Pattern& pattern) { return std::stod(ParseStringAttr(line, pattern, {})); }
        int ParseOptionalIntAttr(const std::string& line, const Pattern& pattern, int defaultValue) {
            std::smatch m;
            return std::regex_search(line, m, pattern.re) ? std::stoi(m[1].str()) : defaultValue;
        }
        std::int64_t ParseOptionalLongAttr(const std::string& line, const Pattern& pattern, std::int64_t defaultValue) {
            std::smatch m;
            return std::regex_search(line, m, pattern.re) ? std::stoll(m[1].str()) : defaultValue;
        }
        double ParseOptionalDoubleAttr(const std::string& line, const Pattern& pattern, double defaultValue) {
            std::smatch m;
            return std::regex_search(line, m, pattern.re) ? std::stod(m[1].str()) : defaultValue;
        }
        bool ParseOptionalBooleanAttribute(const std::string& line, const Pattern& pattern, bool defaultValue) {
            std::smatch m;
            return std::regex_search(line, m, pattern.re) ? m[1].str() == BOOLEAN_TRUE : defaultValue;
        }
        // new BigDecimal(seconds).multiply(1000000).longValue(): exact, truncated.
        std::int64_t ParseTimeSecondsToUs(const std::string& line, const Pattern& pattern) {
            const std::string value = ParseStringAttr(line, pattern, {});
            const std::size_t dot = value.find('.');
            const std::string whole = value.substr(0, dot);
            std::string fraction = dot == std::string::npos ? std::string() : value.substr(dot + 1);
            fraction = (fraction + "000000").substr(0, 6);
            return (whole.empty() ? 0 : std::stoll(whole)) * kMicrosPerSecond + std::stoll(fraction);
        }
        std::int64_t SecondsToUs(double seconds) { return std::int64_t(seconds * double(kMicrosPerSecond)); }

        void VerifyVariableNameNotContainedOrThrow(const std::string& name, const Variables& variables) {
            if (variables.count(name)) throw ParserException("duplicate variable name \"" + name + "\"");
        }

        std::string ToHex(std::int64_t v) {   // Long.toHexString
            char buf[24];
            std::snprintf(buf, sizeof buf, "%llx", static_cast<unsigned long long>(v));
            return buf;
        }

        std::string ParseEncryptionScheme(const std::string& method) {
            return method == METHOD_SAMPLE_AES_CENC || method == METHOD_SAMPLE_AES_CTR ? kCencTypeCenc : kCencTypeCbcs;
        }

        std::optional<SchemeData> ParseDrmSchemeData(const std::string& line, const std::string& keyFormat, const Variables& variables) {
            const std::string keyFormatVersions = ParseOptionalStringAttr(line, REGEX_KEYFORMATVERSIONS, "1", variables);
            auto base64Payload = [&](const std::string& uriString) {
                const std::size_t comma = uriString.find(',');
                return Base64Decode(comma == std::string::npos ? uriString : uriString.substr(comma));
            };
            if (keyFormat == KEYFORMAT_WIDEVINE_PSSH_BINARY) {
                const std::string uriString = ParseStringAttr(line, REGEX_URI, variables);
                return SchemeData{ kWidevineUuid, "", MimeTypes::VIDEO_MP4, base64Payload(uriString) };
            }
            if (keyFormat == KEYFORMAT_WIDEVINE_PSSH_JSON) return SchemeData{ kWidevineUuid, "", "hls", std::vector<std::uint8_t>(line.begin(), line.end()) };
            if (keyFormat == KEYFORMAT_PLAYREADY && keyFormatVersions == "1") {
                const std::string uriString = ParseStringAttr(line, REGEX_URI, variables);
                return SchemeData{ kPlayreadyUuid, "", MimeTypes::VIDEO_MP4, BuildPsshAtom(kPlayreadyUuid, base64Payload(uriString)) };
            }
            return std::nullopt;
        }

        int ParseSelectionFlags(const std::string& line) {
            int flags = 0;
            if (ParseOptionalBooleanAttribute(line, REGEX_DEFAULT, false)) flags |= kSelectionFlagDefault;
            if (ParseOptionalBooleanAttribute(line, REGEX_FORCED, false)) flags |= kSelectionFlagForced;
            if (ParseOptionalBooleanAttribute(line, REGEX_AUTOSELECT, false)) flags |= kSelectionFlagAutoselect;
            return flags;
        }

        int ParseRoleFlags(const std::string& line, const Variables& variables) {
            const std::optional<std::string> characteristicsString = ParseOptionalStringAttr(line, REGEX_CHARACTERISTICS, variables);
            if (!characteristicsString || characteristicsString->empty()) return 0;
            const std::vector<std::string> characteristics = Split(*characteristicsString, ",");
            auto has = [&](const char* c) { return std::find(characteristics.begin(), characteristics.end(), c) != characteristics.end(); };
            int roleFlags = 0;
            if (has("public.accessibility.describes-video")) roleFlags |= kRoleFlagDescribesVideo;
            if (has("public.accessibility.transcribes-spoken-dialog")) roleFlags |= kRoleFlagTranscribesDialog;
            if (has("public.accessibility.describes-music-and-sound")) roleFlags |= kRoleFlagDescribesMusicAndSound;
            if (has("public.easy-to-read")) roleFlags |= kRoleFlagEasyToRead;
            return roleFlags;
        }

        HlsMediaPlaylist::ServerControl ParseServerControl(const std::string& line) {
            const double unset = double(kTimeUnset);
            const double skipUntilSeconds = ParseOptionalDoubleAttr(line, REGEX_CAN_SKIP_UNTIL, unset);
            const double holdBackSeconds = ParseOptionalDoubleAttr(line, REGEX_HOLD_BACK, unset);
            const double partHoldBackSeconds = ParseOptionalDoubleAttr(line, REGEX_PART_HOLD_BACK, unset);
            HlsMediaPlaylist::ServerControl control;
            control.skipUntilUs = skipUntilSeconds == unset ? kTimeUnset : SecondsToUs(skipUntilSeconds);
            control.canSkipDateRanges = ParseOptionalBooleanAttribute(line, REGEX_CAN_SKIP_DATE_RANGES, false);
            control.holdBackUs = holdBackSeconds == unset ? kTimeUnset : SecondsToUs(holdBackSeconds);
            control.partHoldBackUs = partHoldBackSeconds == unset ? kTimeUnset : SecondsToUs(partHoldBackSeconds);
            control.canBlockReload = ParseOptionalBooleanAttribute(line, REGEX_CAN_BLOCK_RELOAD, false);
            return control;
        }

        ClientDefinedAttribute ParseClientDefinedAttribute(const std::string& attributes, const std::string& clientAttribute, const Variables& variables) {
            const std::string prefix = clientAttribute + "=";
            const std::size_t index = attributes.find(prefix);
            const std::size_t valueBeginIndex = index + prefix.size();
            const std::size_t valueBeginMaxLength = attributes.size() == valueBeginIndex + 1 ? 1 : 2;
            const std::string valueBegin = attributes.substr(valueBeginIndex, valueBeginMaxLength);
            if (StartsWith(valueBegin, "\"")) {
                const Pattern pattern(clientAttribute + "=" + Q);
                return ClientDefinedAttribute(clientAttribute, ParseStringAttr(attributes, pattern, variables), ClientDefinedAttribute::TYPE_TEXT);
            }
            if (valueBegin == "0x" || valueBegin == "0X") {
                const Pattern pattern(clientAttribute + "=(0[xX][A-F0-9]+)");
                return ClientDefinedAttribute(clientAttribute, ParseStringAttr(attributes, pattern, variables), ClientDefinedAttribute::TYPE_HEX_TEXT);
            }
            const Pattern pattern(clientAttribute + "=([\\d\\.]+)\\b");
            return ClientDefinedAttribute(clientAttribute, ParseDoubleAttr(attributes, pattern));
        }

        std::string SegmentEncryptionIV(std::int64_t segmentMediaSequence, const std::string& keyUri, const std::string& iv) {
            if (keyUri.empty()) return {};
            if (!iv.empty()) return iv;
            return ToHex(segmentMediaSequence);
        }

        DrmInitData PlaylistProtectionSchemes(const std::string& encryptionScheme, const std::vector<SchemeData>& schemeDatas) {
            std::vector<SchemeData> playlistSchemeDatas;
            for (const SchemeData& s : schemeDatas) playlistSchemeDatas.push_back(s.copyWithData(std::nullopt));
            return DrmInitData(encryptionScheme, std::move(playlistSchemeDatas));
        }

        // The lines of the playlist (BufferedReader.readLine: \n, \r or \r\n), trimmed, empty
        // ones skipped, after the lines read while identifying the playlist.
        class LineIterator {
        public:
            LineIterator(std::deque<std::string> extraLines, const std::string& content, std::size_t pos)
                : m_Extra(std::move(extraLines)), m_Content(content), m_Pos(pos) {}
            bool HasNext() {
                if (m_Next) return true;
                if (!m_Extra.empty()) {
                    m_Next = m_Extra.front();
                    m_Extra.pop_front();
                    return true;
                }
                while (std::optional<std::string> line = ReadLine()) {
                    std::string trimmed = JavaTrim(*line);
                    if (!trimmed.empty()) {
                        m_Next = trimmed;
                        return true;
                    }
                }
                return false;
            }
            std::string Next() {
                if (!HasNext()) throw std::out_of_range("no more lines");
                std::string result = *m_Next;
                m_Next.reset();
                return result;
            }
            std::size_t Position() const { return m_Pos; }
            std::optional<std::string> ReadLine() {
                if (m_Pos >= m_Content.size()) return std::nullopt;
                std::size_t end = m_Pos;
                while (end < m_Content.size() && m_Content[end] != '\n' && m_Content[end] != '\r') end++;
                std::string line = m_Content.substr(m_Pos, end - m_Pos);
                if (end < m_Content.size() && m_Content[end] == '\r' && end + 1 < m_Content.size() && m_Content[end + 1] == '\n') end++;
                m_Pos = end + 1;
                return line;
            }

        private:
            std::deque<std::string> m_Extra;
            const std::string& m_Content;
            std::size_t m_Pos;
            std::optional<std::string> m_Next;
        };

        bool IsLinebreak(int c) { return c == '\n' || c == '\r'; }
        bool IsJavaWhitespace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0B || (c >= 0x1C && c <= 0x1F); }

        // checkPlaylistHeader: an optional BOM, whitespace, #EXTM3U, then a line break; the
        // position after it.
        std::optional<std::size_t> CheckPlaylistHeader(const std::string& content) {
            std::size_t i = 0;
            auto read = [&]() -> int { return i < content.size() ? static_cast<unsigned char>(content[i++]) : -1; };
            int last = read();
            if (last == 0xEF) {
                if (read() != 0xBB || read() != 0xBF) return std::nullopt;
                last = read();
            }
            while (last != -1 && IsJavaWhitespace(last)) last = read();
            for (char c : PLAYLIST_HEADER) {
                if (last != static_cast<unsigned char>(c)) return std::nullopt;
                last = read();
            }
            while (last != -1 && IsJavaWhitespace(last) && !IsLinebreak(last)) last = read();
            if (!IsLinebreak(last)) return std::nullopt;
            return i;   // the rest of the line break's line is read as an (empty) line
        }

        bool IsDolbyVisionFormat(const std::optional<std::string>& videoRange, const std::string& codecs, const std::string& supplementalCodecs,
                                 const std::string& supplementalProfiles) {
            if (!MimeTypes::IsDolbyVisionCodec(codecs, supplementalCodecs)) return false;
            if (supplementalCodecs.empty()) return true;
            if (!videoRange || supplementalProfiles.empty()) return false;
            if ((*videoRange == "PQ" && supplementalProfiles != "db1p") || (*videoRange == "SDR" && supplementalProfiles != "db2g") ||
                (*videoRange == "HLG" && !StartsWith(supplementalProfiles, "db4")))
                return false;
            return true;
        }

        const Variant* VariantWithGroup(const std::vector<Variant>& variants, const std::string& groupId, std::string Variant::*group) {
            for (const Variant& v : variants)
                if (v.*group == groupId) return &v;
            return nullptr;
        }

        std::shared_ptr<HlsMultivariantPlaylist> ParseMultivariantPlaylist(LineIterator& iterator, const std::string& playlistUri) {
            const std::string& baseUri = playlistUri;
            std::map<std::string, std::vector<HlsTrackMetadataEntry::VariantInfo>> urlToVariantInfos;
            Variables variableDefinitions;
            std::vector<Variant> variants;
            std::vector<Rendition> videos, audios, subtitles, closedCaptions;
            std::vector<std::string> mediaTags;
            std::vector<DrmInitData> sessionKeyDrmInitData;
            std::vector<std::string> tags;
            std::optional<Format> muxedAudioFormat;
            std::optional<std::vector<Format>> muxedCaptionFormats;
            bool noClosedCaptions = false;
            bool hasIndependentSegmentsTag = false;
            std::optional<HlsMultivariantPlaylist::ContentSteeringInfo> contentSteeringInfo;
            while (iterator.HasNext()) {
                std::string line = iterator.Next();
                if (StartsWith(line, TAG_PREFIX)) tags.push_back(line);
                const bool isIFrameOnlyVariant = StartsWith(line, TAG_I_FRAME_STREAM_INF);
                if (StartsWith(line, TAG_DEFINE)) {
                    std::optional<std::string> variableName = ParseOptionalStringAttr(line, REGEX_NAME, variableDefinitions);
                    if (variableName) {
                        VerifyVariableNameNotContainedOrThrow(*variableName, variableDefinitions);
                        variableDefinitions[*variableName] = ParseStringAttr(line, REGEX_VALUE, variableDefinitions);
                    } else {
                        const std::string name = ParseStringAttr(line, REGEX_QUERY_PARAM, variableDefinitions);
                        VerifyVariableNameNotContainedOrThrow(name, variableDefinitions);
                        const std::optional<std::string> value = GetQueryParameter(playlistUri, name);
                        if (!value) throw ParserException("QUERYPARAM \"" + name + "\" not found in playlist URI");
                        variableDefinitions[name] = *value;
                    }
                } else if (line == TAG_INDEPENDENT_SEGMENTS) {
                    hasIndependentSegmentsTag = true;
                } else if (StartsWith(line, TAG_MEDIA)) {
                    mediaTags.push_back(line);
                } else if (StartsWith(line, TAG_SESSION_KEY)) {
                    const std::string keyFormat = ParseOptionalStringAttr(line, REGEX_KEYFORMAT, KEYFORMAT_IDENTITY, variableDefinitions);
                    if (std::optional<SchemeData> schemeData = ParseDrmSchemeData(line, keyFormat, variableDefinitions)) {
                        const std::string method = ParseStringAttr(line, REGEX_METHOD, variableDefinitions);
                        sessionKeyDrmInitData.emplace_back(ParseEncryptionScheme(method), std::vector<SchemeData>{ *schemeData });
                    }
                } else if (StartsWith(line, TAG_CONTENT_STEERING)) {
                    if (contentSteeringInfo)
                        throw ParserException("The #EXT-X-CONTENT-STEERING tag must not appear more than once in a multivariant playlist");
                    const std::string serverUriString = ParseStringAttr(line, REGEX_SERVER_URI, variableDefinitions);
                    contentSteeringInfo = HlsMultivariantPlaylist::ContentSteeringInfo{
                        ResolveUri(baseUri, serverUriString), ParseOptionalStringAttr(line, REGEX_PATHWAY_ID, variableDefinitions).value_or("") };
                } else if (StartsWith(line, TAG_STREAM_INF) || isIFrameOnlyVariant) {
                    noClosedCaptions |= line.find(ATTR_CLOSED_CAPTIONS_NONE) != std::string::npos;
                    const int roleFlags = isIFrameOnlyVariant ? kRoleFlagTrickPlay : 0;
                    const int peakBitrate = ParseIntAttr(line, REGEX_BANDWIDTH);
                    const int averageBitrate = ParseOptionalIntAttr(line, REGEX_AVERAGE_BANDWIDTH, -1);
                    const std::optional<std::string> videoRange = ParseOptionalStringAttr(line, REGEX_VIDEO_RANGE, variableDefinitions);
                    std::string codecs = ParseOptionalStringAttr(line, REGEX_CODECS, variableDefinitions).value_or("");
                    const std::optional<std::string> supplementalCodecsStrings = ParseOptionalStringAttr(line, REGEX_SUPPLEMENTAL_CODECS, variableDefinitions);
                    std::string supplementalCodecs, supplementalProfiles;
                    if (supplementalCodecsStrings) {
                        const std::vector<std::string> first = SplitAtFirst(*supplementalCodecsStrings, ",");
                        const std::vector<std::string> codecsAndProfiles = Split(first[0], "/");
                        supplementalCodecs = codecsAndProfiles[0];
                        if (codecsAndProfiles.size() > 1) supplementalProfiles = codecsAndProfiles[1];
                    }
                    Format format;
                    std::string videoCodecs = GetCodecsOfType(codecs, TrackType::Video);
                    if (IsDolbyVisionFormat(videoRange, videoCodecs, supplementalCodecs, supplementalProfiles)) {
                        ApplyColorInfoForDolbyVision(format, codecs, supplementalCodecs, supplementalProfiles);
                        videoCodecs = !supplementalCodecs.empty() ? supplementalCodecs : videoCodecs;
                        const std::string nonVideoCodecs = GetCodecsWithoutType(codecs, TrackType::Video);
                        codecs = !nonVideoCodecs.empty() ? videoCodecs + "," + nonVideoCodecs : videoCodecs;
                    }
                    int width = -1, height = -1;
                    if (const std::optional<std::string> resolutionString = ParseOptionalStringAttr(line, REGEX_RESOLUTION, variableDefinitions)) {
                        const std::vector<std::string> widthAndHeight = Split(*resolutionString, "x");
                        width = std::stoi(widthAndHeight[0]);
                        height = std::stoi(widthAndHeight[1]);
                        if (width <= 0 || height <= 0) width = height = -1;
                    }
                    float frameRate = -1;
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_FRAME_RATE, variableDefinitions)) frameRate = std::stof(*s);
                    float selectionPriority = -1;
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_SCORE, variableDefinitions)) selectionPriority = std::stof(*s);
                    Variant variant;
                    variant.pathwayId = ParseOptionalStringAttr(line, REGEX_PATHWAY_ID, variableDefinitions).value_or("");
                    variant.videoGroupId = ParseOptionalStringAttr(line, REGEX_VIDEO, variableDefinitions).value_or("");
                    variant.audioGroupId = ParseOptionalStringAttr(line, REGEX_AUDIO, variableDefinitions).value_or("");
                    variant.subtitleGroupId = ParseOptionalStringAttr(line, REGEX_SUBTITLES, variableDefinitions).value_or("");
                    variant.captionGroupId = ParseOptionalStringAttr(line, REGEX_CLOSED_CAPTIONS, variableDefinitions).value_or("");
                    variant.stableVariantId = ParseOptionalStringAttr(line, REGEX_STABLE_VARIANT_ID, variableDefinitions).value_or("");
                    if (isIFrameOnlyVariant) {
                        variant.url = ResolveUri(baseUri, ParseStringAttr(line, REGEX_URI, variableDefinitions));
                    } else if (!iterator.HasNext()) {
                        throw ParserException("#EXT-X-STREAM-INF must be followed by another line");
                    } else {
                        line = ReplaceVariableReferences(iterator.Next(), variableDefinitions);
                        variant.url = ResolveUri(baseUri, line);
                    }
                    format.id = std::to_string(variants.size());
                    format.containerMimeType = MimeTypes::APPLICATION_M3U8;
                    format.codecs = codecs;
                    format.SetBitrates(averageBitrate, peakBitrate);
                    format.width = width;
                    format.height = height;
                    format.frameRate = frameRate;
                    format.selectionPriority = selectionPriority;
                    format.roleFlags = roleFlags;
                    variant.format = format;
                    urlToVariantInfos[variant.url].push_back(
                        { averageBitrate, peakBitrate, variant.videoGroupId, variant.audioGroupId, variant.subtitleGroupId, variant.captionGroupId });
                    variants.push_back(std::move(variant));
                }
            }
            std::vector<Variant> deduplicatedVariants;
            std::set<std::string> urlsInDeduplicatedVariants;
            for (const Variant& variant : variants) {
                if (urlsInDeduplicatedVariants.insert(variant.url).second) {
                    Variant v = variant;
                    v.metadata = HlsTrackMetadataEntry{ "", "", urlToVariantInfos[variant.url] };
                    deduplicatedVariants.push_back(std::move(v));
                }
            }
            for (const std::string& mediaLine : mediaTags) {
                const std::string groupId = ParseStringAttr(mediaLine, REGEX_GROUP_ID, variableDefinitions);
                const std::string name = ParseStringAttr(mediaLine, REGEX_NAME, variableDefinitions);
                const std::string stableRenditionId = ParseOptionalStringAttr(mediaLine, REGEX_STABLE_RENDITION_ID, variableDefinitions).value_or("");
                Format format;
                format.id = groupId + ":" + name;
                format.label = name;
                format.containerMimeType = MimeTypes::APPLICATION_M3U8;
                format.selectionFlags = ParseSelectionFlags(mediaLine);
                format.roleFlags = ParseRoleFlags(mediaLine, variableDefinitions);
                format.language = ParseOptionalStringAttr(mediaLine, REGEX_LANGUAGE, variableDefinitions).value_or("");
                FinishFormat(format);
                const std::optional<std::string> referenceUri = ParseOptionalStringAttr(mediaLine, REGEX_URI, variableDefinitions);
                const std::string uri = referenceUri ? ResolveUri(baseUri, *referenceUri) : std::string();
                const HlsTrackMetadataEntry metadata{ groupId, name, {} };
                const std::string type = ParseStringAttr(mediaLine, REGEX_TYPE, variableDefinitions);
                if (type == TYPE_VIDEO) {
                    if (const Variant* variant = VariantWithGroup(variants, groupId, &Variant::videoGroupId)) {
                        const std::string codecs = GetCodecsOfType(variant->format.codecs, TrackType::Video);
                        format.codecs = codecs;
                        format.sampleMimeType = MimeTypes::GetMediaMimeType(codecs);
                        format.width = variant->format.width;
                        format.height = variant->format.height;
                        format.frameRate = variant->format.frameRate;
                    }
                    if (!uri.empty()) videos.push_back({ uri, format, groupId, name, stableRenditionId, metadata });
                } else if (type == TYPE_AUDIO) {
                    std::string sampleMimeType;
                    const Variant* variant = VariantWithGroup(variants, groupId, &Variant::audioGroupId);
                    if (variant) {
                        const std::string codecs = GetCodecsOfType(variant->format.codecs, TrackType::Audio);
                        format.codecs = codecs;
                        sampleMimeType = MimeTypes::GetMediaMimeType(codecs);
                    }
                    if (const std::optional<std::string> channelsString = ParseOptionalStringAttr(mediaLine, REGEX_CHANNELS, variableDefinitions)) {
                        format.channelCount = std::stoi(SplitAtFirst(*channelsString, "/")[0]);
                        if (sampleMimeType == MimeTypes::AUDIO_E_AC3 && channelsString->size() >= 4 &&
                            channelsString->compare(channelsString->size() - 4, 4, "/JOC") == 0) {
                            sampleMimeType = MimeTypes::AUDIO_E_AC3_JOC;
                            format.codecs = MimeTypes::CODEC_E_AC3_JOC;
                        }
                    }
                    const int sampleRate = ParseOptionalIntAttr(mediaLine, REGEX_SAMPLE_RATE, -1);
                    if (sampleRate != -1) format.sampleRate = sampleRate;
                    format.sampleMimeType = sampleMimeType;
                    if (!uri.empty()) audios.push_back({ uri, format, groupId, name, stableRenditionId, metadata });
                    else if (variant) muxedAudioFormat = format;
                } else if (type == TYPE_SUBTITLES) {
                    std::string sampleMimeType;
                    if (const Variant* variant = VariantWithGroup(variants, groupId, &Variant::subtitleGroupId)) {
                        const std::string codecs = GetCodecsOfType(variant->format.codecs, TrackType::Text);
                        format.codecs = codecs;
                        sampleMimeType = MimeTypes::GetMediaMimeType(codecs);
                    }
                    if (sampleMimeType.empty()) sampleMimeType = MimeTypes::TEXT_VTT;
                    format.sampleMimeType = sampleMimeType;
                    if (!uri.empty()) subtitles.push_back({ uri, format, groupId, name, stableRenditionId, metadata });
                    else log::Warn("HlsPlaylistParser", "EXT-X-MEDIA tag with missing mandatory URI attribute: skipping");
                } else if (type == TYPE_CLOSED_CAPTIONS) {
                    const std::string instreamId = ParseStringAttr(mediaLine, REGEX_INSTREAM_ID, variableDefinitions);
                    if (StartsWith(instreamId, "CC")) {
                        format.sampleMimeType = MimeTypes::APPLICATION_CEA608;
                        format.accessibilityChannel = std::stoi(instreamId.substr(2));
                    } else {
                        format.sampleMimeType = MimeTypes::APPLICATION_CEA708;
                        format.accessibilityChannel = std::stoi(instreamId.substr(7));
                    }
                    if (!muxedCaptionFormats) muxedCaptionFormats.emplace();
                    muxedCaptionFormats->push_back(format);
                }
            }
            if (noClosedCaptions) muxedCaptionFormats = std::vector<Format>{};
            return std::make_shared<HlsMultivariantPlaylist>(playlistUri, std::move(tags), std::move(deduplicatedVariants), std::move(videos), std::move(audios),
                                                             std::move(subtitles), std::move(closedCaptions), std::move(muxedAudioFormat),
                                                             std::move(muxedCaptionFormats), hasIndependentSegmentsTag, std::move(variableDefinitions),
                                                             std::move(sessionKeyDrmInitData), std::move(contentSteeringInfo));
        }

        std::vector<std::string> ParseListAttr(const std::string& line, const Pattern& pattern, const Variables& variables,
                                               std::initializer_list<const char*> allowed) {
            std::vector<std::string> out;
            if (const std::optional<std::string> s = ParseOptionalStringAttr(line, pattern, variables)) {
                for (std::string item : Split(*s, ",")) {
                    item = JavaTrim(item);
                    for (const char* a : allowed)
                        if (item == a) out.push_back(item);
                }
            }
            return out;
        }

        std::shared_ptr<HlsMediaPlaylist> ParseMediaPlaylist(const HlsMultivariantPlaylist& multivariantPlaylist, const HlsMediaPlaylist* previousMediaPlaylist,
                                                             LineIterator& iterator, const std::string& playlistUri) {
            const std::string& baseUri = playlistUri;
            int playlistType = HlsMediaPlaylist::PLAYLIST_TYPE_UNKNOWN;
            std::int64_t startOffsetUs = kTimeUnset;
            std::int64_t mediaSequence = 0;
            int version = 1;
            std::int64_t targetDurationUs = kTimeUnset;
            std::int64_t partTargetDurationUs = kTimeUnset;
            bool hasIndependentSegmentsTag = multivariantPlaylist.hasIndependentSegments;
            bool hasEndTag = false;
            std::shared_ptr<const Segment> initializationSegment = previousMediaPlaylist ? previousMediaPlaylist->lastSeenInitSegment : nullptr;
            Variables variableDefinitions;
            std::map<std::string, std::shared_ptr<const Segment>> urlToInferredInitSegment;
            std::vector<Segment> segments;
            std::vector<Part> trailingParts;
            std::optional<Part> preloadPart;
            std::vector<HlsMediaPlaylist::RenditionReport> renditionReports;
            std::vector<std::string> tags;
            std::vector<std::pair<std::string, Interstitial::Builder>> interstitialBuilderMap;   // LinkedHashMap
            std::int64_t segmentDurationUs = 0;
            std::string segmentTitle;
            bool hasDiscontinuitySequence = false;
            int playlistDiscontinuitySequence = 0;
            int relativeDiscontinuitySequence = 0;
            std::int64_t playlistStartTimeUs = 0;
            std::int64_t segmentStartTimeUs = 0;
            bool preciseStart = false;
            std::int64_t segmentByteRangeOffset = 0;
            std::int64_t segmentByteRangeLength = kLengthUnset;
            std::int64_t partStartTimeUs = 0;
            std::int64_t partByteRangeOffset = 0;
            bool isIFrameOnly = false;
            std::int64_t segmentMediaSequence = 0;
            bool hasGapTag = false;
            HlsMediaPlaylist::ServerControl serverControl;
            std::optional<DrmInitData> playlistProtectionSchemes;
            std::string fullSegmentEncryptionKeyUri;
            std::string fullSegmentEncryptionIV;
            std::map<std::string, SchemeData> currentSchemeDatas;   // TreeMap
            std::string encryptionScheme;
            std::optional<DrmInitData> cachedDrmInitData;

            auto ensureDrmInitData = [&] {
                if (!cachedDrmInitData && !currentSchemeDatas.empty()) {
                    std::vector<SchemeData> schemeDatas;
                    for (const auto& [k, v] : currentSchemeDatas) schemeDatas.push_back(v);
                    cachedDrmInitData = DrmInitData(encryptionScheme, schemeDatas);
                    if (!playlistProtectionSchemes) playlistProtectionSchemes = PlaylistProtectionSchemes(encryptionScheme, schemeDatas);
                }
            };

            while (iterator.HasNext()) {
                const std::string line = iterator.Next();
                if (StartsWith(line, TAG_PREFIX)) tags.push_back(line);
                if (StartsWith(line, TAG_PLAYLIST_TYPE)) {
                    const std::string playlistTypeString = ParseStringAttr(line, REGEX_PLAYLIST_TYPE, variableDefinitions);
                    if (playlistTypeString == "VOD") playlistType = HlsMediaPlaylist::PLAYLIST_TYPE_VOD;
                    else if (playlistTypeString == "EVENT") playlistType = HlsMediaPlaylist::PLAYLIST_TYPE_EVENT;
                } else if (line == TAG_IFRAME) {
                    isIFrameOnly = true;
                } else if (StartsWith(line, TAG_START)) {
                    startOffsetUs = SecondsToUs(ParseDoubleAttr(line, REGEX_TIME_OFFSET));
                    preciseStart = ParseOptionalBooleanAttribute(line, REGEX_PRECISE, false);
                } else if (StartsWith(line, TAG_SERVER_CONTROL)) {
                    serverControl = ParseServerControl(line);
                } else if (StartsWith(line, TAG_PART_INF)) {
                    partTargetDurationUs = SecondsToUs(ParseDoubleAttr(line, REGEX_PART_TARGET_DURATION));
                } else if (StartsWith(line, TAG_INIT_SEGMENT)) {
                    const std::string uri = ParseStringAttr(line, REGEX_URI, variableDefinitions);
                    if (const std::optional<std::string> byteRange = ParseOptionalStringAttr(line, REGEX_ATTR_BYTERANGE, variableDefinitions)) {
                        const std::vector<std::string> split = Split(*byteRange, "@");
                        segmentByteRangeLength = std::stoll(split[0]);
                        if (split.size() > 1) segmentByteRangeOffset = std::stoll(split[1]);
                    }
                    if (segmentByteRangeLength == kLengthUnset) segmentByteRangeOffset = 0;
                    if (!fullSegmentEncryptionKeyUri.empty() && fullSegmentEncryptionIV.empty())
                        throw ParserException("The encryption IV attribute must be present when an initialization segment is encrypted with METHOD=AES-128.");
                    initializationSegment = std::make_shared<const Segment>(
                        Segment::Init(uri, segmentByteRangeOffset, segmentByteRangeLength, fullSegmentEncryptionKeyUri, fullSegmentEncryptionIV));
                    if (segmentByteRangeLength != kLengthUnset) segmentByteRangeOffset += segmentByteRangeLength;
                    segmentByteRangeLength = kLengthUnset;
                } else if (StartsWith(line, TAG_TARGET_DURATION)) {
                    targetDurationUs = std::int64_t(ParseIntAttr(line, REGEX_TARGET_DURATION)) * kMicrosPerSecond;
                } else if (StartsWith(line, TAG_MEDIA_SEQUENCE)) {
                    mediaSequence = ParseLongAttr(line, REGEX_MEDIA_SEQUENCE);
                    segmentMediaSequence = mediaSequence;
                } else if (StartsWith(line, TAG_VERSION)) {
                    version = ParseIntAttr(line, REGEX_VERSION);
                } else if (StartsWith(line, TAG_DEFINE)) {
                    const std::optional<std::string> variableName = ParseOptionalStringAttr(line, REGEX_NAME, variableDefinitions);
                    const std::optional<std::string> queryParam = ParseOptionalStringAttr(line, REGEX_QUERY_PARAM, variableDefinitions);
                    if (variableName) {
                        VerifyVariableNameNotContainedOrThrow(*variableName, variableDefinitions);
                        variableDefinitions[*variableName] = ParseStringAttr(line, REGEX_VALUE, variableDefinitions);
                    } else if (queryParam) {
                        VerifyVariableNameNotContainedOrThrow(*queryParam, variableDefinitions);
                        const std::optional<std::string> value = GetQueryParameter(playlistUri, *queryParam);
                        if (!value) throw ParserException("QUERYPARAM \"" + *queryParam + "\" not found in playlist URI");
                        variableDefinitions[*queryParam] = *value;
                    } else {
                        const std::string importName = ParseStringAttr(line, REGEX_IMPORT, variableDefinitions);
                        VerifyVariableNameNotContainedOrThrow(importName, variableDefinitions);
                        const auto found = multivariantPlaylist.variableDefinitions.find(importName);
                        if (found != multivariantPlaylist.variableDefinitions.end()) variableDefinitions[importName] = found->second;
                    }
                } else if (StartsWith(line, TAG_MEDIA_DURATION)) {
                    segmentDurationUs = ParseTimeSecondsToUs(line, REGEX_MEDIA_DURATION);
                    segmentTitle = ParseOptionalStringAttr(line, REGEX_MEDIA_TITLE, "", variableDefinitions);
                } else if (StartsWith(line, TAG_SKIP)) {
                    const int skippedSegmentCount = ParseIntAttr(line, REGEX_SKIPPED_SEGMENTS);
                    if (!(previousMediaPlaylist && segments.empty())) throw std::logic_error("EXT-X-SKIP without a previous playlist");
                    const int startIndex = int(mediaSequence - previousMediaPlaylist->mediaSequence);
                    const int endIndex = startIndex + skippedSegmentCount;
                    if (startIndex < 0 || endIndex > int(previousMediaPlaylist->segments.size())) throw HlsPlaylistParser::DeltaUpdateException();
                    for (int i = startIndex; i < endIndex; i++) {
                        Segment segment = previousMediaPlaylist->segments[std::size_t(i)];
                        if (mediaSequence != previousMediaPlaylist->mediaSequence) {
                            const int newRelativeDiscontinuitySequence =
                                previousMediaPlaylist->discontinuitySequence - playlistDiscontinuitySequence + segment.relativeDiscontinuitySequence;
                            segment = segment.CopyWith(segmentStartTimeUs, newRelativeDiscontinuitySequence);
                        }
                        segmentStartTimeUs += segment.durationUs;
                        partStartTimeUs = segmentStartTimeUs;
                        if (segment.byteRangeLength != kLengthUnset) segmentByteRangeOffset = segment.byteRangeOffset + segment.byteRangeLength;
                        relativeDiscontinuitySequence = segment.relativeDiscontinuitySequence;
                        initializationSegment = segment.initializationSegment;
                        cachedDrmInitData = segment.drmInitData;
                        fullSegmentEncryptionKeyUri = segment.fullSegmentEncryptionKeyUri;
                        if (segment.encryptionIV.empty() || segment.encryptionIV != ToHex(segmentMediaSequence)) fullSegmentEncryptionIV = segment.encryptionIV;
                        segmentMediaSequence++;
                        segments.push_back(std::move(segment));
                    }
                } else if (StartsWith(line, TAG_KEY)) {
                    const std::string method = ParseStringAttr(line, REGEX_METHOD, variableDefinitions);
                    const std::string keyFormat = ParseOptionalStringAttr(line, REGEX_KEYFORMAT, KEYFORMAT_IDENTITY, variableDefinitions);
                    fullSegmentEncryptionKeyUri.clear();
                    fullSegmentEncryptionIV.clear();
                    if (method == METHOD_NONE) {
                        currentSchemeDatas.clear();
                        cachedDrmInitData.reset();
                    } else {
                        fullSegmentEncryptionIV = ParseOptionalStringAttr(line, REGEX_IV, variableDefinitions).value_or("");
                        if (keyFormat == KEYFORMAT_IDENTITY) {
                            if (method == METHOD_AES_128) fullSegmentEncryptionKeyUri = ParseStringAttr(line, REGEX_URI, variableDefinitions);
                        } else {
                            if (encryptionScheme.empty()) encryptionScheme = ParseEncryptionScheme(method);
                            if (std::optional<SchemeData> schemeData = ParseDrmSchemeData(line, keyFormat, variableDefinitions)) {
                                cachedDrmInitData.reset();
                                currentSchemeDatas[keyFormat] = *schemeData;
                            }
                        }
                    }
                } else if (StartsWith(line, TAG_BYTERANGE)) {
                    const std::vector<std::string> split = Split(ParseStringAttr(line, REGEX_BYTERANGE, variableDefinitions), "@");
                    segmentByteRangeLength = std::stoll(split[0]);
                    if (split.size() > 1) segmentByteRangeOffset = std::stoll(split[1]);
                } else if (StartsWith(line, TAG_DISCONTINUITY_SEQUENCE)) {
                    hasDiscontinuitySequence = true;
                    playlistDiscontinuitySequence = std::stoi(line.substr(line.find(':') + 1));
                } else if (line == TAG_DISCONTINUITY) {
                    relativeDiscontinuitySequence++;
                } else if (StartsWith(line, TAG_PROGRAM_DATE_TIME)) {
                    if (playlistStartTimeUs == 0) {
                        const std::int64_t programDatetimeUs = ParseXsDateTime(line.substr(line.find(':') + 1)) * 1000;
                        playlistStartTimeUs = programDatetimeUs - segmentStartTimeUs;
                    }
                } else if (line == TAG_GAP) {
                    hasGapTag = true;
                } else if (line == TAG_INDEPENDENT_SEGMENTS) {
                    hasIndependentSegmentsTag = true;
                } else if (line == TAG_ENDLIST) {
                    hasEndTag = true;
                } else if (StartsWith(line, TAG_RENDITION_REPORT)) {
                    const std::int64_t lastMediaSequence = ParseOptionalLongAttr(line, REGEX_LAST_MSN, stream::kIndexUnset);
                    const int lastPartIndex = ParseOptionalIntAttr(line, REGEX_LAST_PART, stream::kIndexUnset);
                    const std::string uri = ParseStringAttr(line, REGEX_URI, variableDefinitions);
                    renditionReports.push_back({ ResolveUri(baseUri, uri), lastMediaSequence, lastPartIndex });
                } else if (StartsWith(line, TAG_PRELOAD_HINT)) {
                    if (preloadPart) continue;
                    const std::string type = ParseStringAttr(line, REGEX_PRELOAD_HINT_TYPE, variableDefinitions);
                    if (type != TYPE_PART) continue;
                    const std::string url = ParseStringAttr(line, REGEX_URI, variableDefinitions);
                    const std::int64_t byteRangeStart = ParseOptionalLongAttr(line, REGEX_BYTERANGE_START, kLengthUnset);
                    const std::int64_t byteRangeLength = ParseOptionalLongAttr(line, REGEX_BYTERANGE_LENGTH, kLengthUnset);
                    const std::string segmentEncryptionIV = SegmentEncryptionIV(segmentMediaSequence, fullSegmentEncryptionKeyUri, fullSegmentEncryptionIV);
                    ensureDrmInitData();
                    if (byteRangeStart == kLengthUnset || byteRangeLength != kLengthUnset) {
                        Part part;
                        part.url = url;
                        part.initializationSegment = initializationSegment;
                        part.durationUs = 0;
                        part.relativeDiscontinuitySequence = relativeDiscontinuitySequence;
                        part.relativeStartTimeUs = partStartTimeUs;
                        part.drmInitData = cachedDrmInitData;
                        part.fullSegmentEncryptionKeyUri = fullSegmentEncryptionKeyUri;
                        part.encryptionIV = segmentEncryptionIV;
                        part.byteRangeOffset = byteRangeStart != kLengthUnset ? byteRangeStart : 0;
                        part.byteRangeLength = byteRangeLength;
                        part.isPreload = true;
                        preloadPart = part;
                    }
                } else if (StartsWith(line, TAG_PART)) {
                    const std::string segmentEncryptionIV = SegmentEncryptionIV(segmentMediaSequence, fullSegmentEncryptionKeyUri, fullSegmentEncryptionIV);
                    const std::string url = ParseStringAttr(line, REGEX_URI, variableDefinitions);
                    const std::int64_t partDurationUs = SecondsToUs(ParseDoubleAttr(line, REGEX_ATTR_DURATION));
                    bool isIndependent = ParseOptionalBooleanAttribute(line, REGEX_INDEPENDENT, false);
                    isIndependent |= hasIndependentSegmentsTag && trailingParts.empty();
                    const bool isGap = ParseOptionalBooleanAttribute(line, REGEX_GAP, false);
                    std::int64_t partByteRangeLength = kLengthUnset;
                    if (const std::optional<std::string> byteRange = ParseOptionalStringAttr(line, REGEX_ATTR_BYTERANGE, variableDefinitions)) {
                        const std::vector<std::string> split = Split(*byteRange, "@");
                        partByteRangeLength = std::stoll(split[0]);
                        if (split.size() > 1) partByteRangeOffset = std::stoll(split[1]);
                    }
                    if (partByteRangeLength == kLengthUnset) partByteRangeOffset = 0;
                    ensureDrmInitData();
                    Part part;
                    part.url = url;
                    part.initializationSegment = initializationSegment;
                    part.durationUs = partDurationUs;
                    part.relativeDiscontinuitySequence = relativeDiscontinuitySequence;
                    part.relativeStartTimeUs = partStartTimeUs;
                    part.drmInitData = cachedDrmInitData;
                    part.fullSegmentEncryptionKeyUri = fullSegmentEncryptionKeyUri;
                    part.encryptionIV = segmentEncryptionIV;
                    part.byteRangeOffset = partByteRangeOffset;
                    part.byteRangeLength = partByteRangeLength;
                    part.hasGapTag = isGap;
                    part.isIndependent = isIndependent;
                    trailingParts.push_back(part);
                    partStartTimeUs += partDurationUs;
                    if (partByteRangeLength != kLengthUnset) partByteRangeOffset += partByteRangeLength;
                } else if (StartsWith(line, TAG_DATERANGE) && ParseOptionalStringAttr(line, REGEX_CLASS, "", variableDefinitions) == DATERANGE_CLASS_INTERSTITIALS) {
                    const std::string id = ParseStringAttr(line, REGEX_ID, variableDefinitions);
                    const std::optional<std::string> assetUri = ParseOptionalStringAttr(line, REGEX_ASSET_URI, variableDefinitions);
                    const std::optional<std::string> assetListUri = ParseOptionalStringAttr(line, REGEX_ASSET_LIST_URI, variableDefinitions);
                    std::int64_t startDateUnixUs = kTimeUnset, endDateUnixUs = kTimeUnset;
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_START_DATE, variableDefinitions)) startDateUnixUs = ParseXsDateTime(*s) * 1000;
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_END_DATE, variableDefinitions)) endDateUnixUs = ParseXsDateTime(*s) * 1000;
                    const std::vector<std::string> cue = ParseListAttr(line, REGEX_CUE, variableDefinitions,
                                                                       { Interstitial::CUE_TRIGGER_ONCE, Interstitial::CUE_TRIGGER_POST, Interstitial::CUE_TRIGGER_PRE });
                    auto optionalUs = [&](const Pattern& p) {
                        const double sec = ParseOptionalDoubleAttr(line, p, -1.0);
                        return sec >= 0 ? SecondsToUs(sec) : kTimeUnset;
                    };
                    const std::int64_t durationUs = optionalUs(REGEX_ATTR_DURATION_PREFIXED);
                    const std::int64_t plannedDurationUs = optionalUs(REGEX_PLANNED_DURATION);
                    const bool endOnNext = ParseOptionalBooleanAttribute(line, REGEX_END_ON_NEXT, false);
                    // Double.MIN_VALUE: the smallest positive double, upstream's "absent" mark.
                    const double minValue = 4.9e-324;
                    const double resumeOffset = ParseOptionalDoubleAttr(line, REGEX_RESUME_OFFSET, minValue);
                    const std::int64_t resumeOffsetUs = resumeOffset != minValue ? SecondsToUs(resumeOffset) : kTimeUnset;
                    const std::int64_t playoutLimitUs = optionalUs(REGEX_PLAYOUT_LIMIT);
                    const std::vector<std::string> snapTypes =
                        ParseListAttr(line, REGEX_SNAP, variableDefinitions, { Interstitial::SNAP_TYPE_IN, Interstitial::SNAP_TYPE_OUT });
                    const std::vector<std::string> restrictions = ParseListAttr(line, REGEX_RESTRICT, variableDefinitions,
                                                                                { Interstitial::NAVIGATION_RESTRICTION_JUMP, Interstitial::NAVIGATION_RESTRICTION_SKIP });
                    std::optional<bool> contentMayVary;
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_CONTENT_MAY_VARY, variableDefinitions)) contentMayVary = *s != BOOLEAN_FALSE;
                    std::optional<std::string> timelineOccupies, timelineStyle;
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_TIMELINE_OCCUPIES, variableDefinitions)) {
                        if (*s == Interstitial::TIMELINE_OCCUPIES_RANGE || *s == Interstitial::TIMELINE_OCCUPIES_POINT) timelineOccupies = *s;
                    }
                    if (const std::optional<std::string> s = ParseOptionalStringAttr(line, REGEX_TIMELINE_STYLE, variableDefinitions)) {
                        if (*s == Interstitial::TIMELINE_STYLE_PRIMARY || *s == Interstitial::TIMELINE_STYLE_HIGHLIGHT) timelineStyle = *s;
                    }
                    const std::int64_t skipControlOffsetUs = optionalUs(REGEX_SKIP_CONTROL_OFFSET);
                    const std::int64_t skipControlDurationUs = optionalUs(REGEX_SKIP_CONTROL_DURATION);
                    const std::optional<std::string> skipControlLabelId = ParseOptionalStringAttr(line, REGEX_SKIP_CONTROL_LABEL_ID, variableDefinitions);
                    std::vector<ClientDefinedAttribute> clientDefinedAttributes;
                    const std::string attributes = line.substr(std::string("#EXT-X-DATERANGE:").size());
                    static const std::set<std::string> known = { "X-ASSET-URI=", "X-ASSET-LIST=", "X-RESUME-OFFSET=", "X-PLAYOUT-LIMIT=",
                                                                 "X-SNAP=", "X-RESTRICT=", "X-CONTENT-MAY-VARY=", "X-TIMELINE-OCCUPIES=",
                                                                 "X-TIMELINE-STYLE=", "X-SKIP-CONTROL-OFFSET=", "X-SKIP-CONTROL-DURATION=",
                                                                 "X-SKIP-CONTROL-LABEL-ID=" };
                    for (auto it = std::sregex_iterator(attributes.begin(), attributes.end(), REGEX_CLIENT_DEFINED_ATTRIBUTE_PREFIX.re); it != std::sregex_iterator(); ++it) {
                        const std::string attributePrefix = (*it)[0].str();
                        if (known.count(attributePrefix)) continue;
                        clientDefinedAttributes.push_back(
                            ParseClientDefinedAttribute(attributes, attributePrefix.substr(0, attributePrefix.size() - 1), variableDefinitions));
                    }
                    Interstitial::Builder* builder = nullptr;
                    for (auto& [k, b] : interstitialBuilderMap)
                        if (k == id) builder = &b;
                    if (!builder) builder = &interstitialBuilderMap.emplace_back(id, Interstitial::Builder(id)).second;
                    builder->SetAssetUri(assetUri)
                        .SetAssetListUri(assetListUri)
                        .SetStartDateUnixUs(startDateUnixUs)
                        .SetEndDateUnixUs(endDateUnixUs)
                        .SetDurationUs(durationUs)
                        .SetPlannedDurationUs(plannedDurationUs)
                        .SetCue(cue)
                        .SetEndOnNext(endOnNext)
                        .SetResumeOffsetUs(resumeOffsetUs)
                        .SetPlayoutLimitUs(playoutLimitUs)
                        .SetSnapTypes(snapTypes)
                        .SetRestrictions(restrictions)
                        .SetClientDefinedAttributes(clientDefinedAttributes)
                        .SetContentMayVary(contentMayVary)
                        .SetTimelineOccupies(timelineOccupies)
                        .SetTimelineStyle(timelineStyle)
                        .SetSkipControlOffsetUs(skipControlOffsetUs)
                        .SetSkipControlDurationUs(skipControlDurationUs)
                        .SetSkipControlLabelId(skipControlLabelId);
                } else if (!StartsWith(line, "#")) {
                    const std::string segmentEncryptionIV = SegmentEncryptionIV(segmentMediaSequence, fullSegmentEncryptionKeyUri, fullSegmentEncryptionIV);
                    segmentMediaSequence++;
                    const std::string segmentUri = ReplaceVariableReferences(line, variableDefinitions);
                    std::shared_ptr<const Segment> inferredInitSegment;
                    if (const auto found = urlToInferredInitSegment.find(segmentUri); found != urlToInferredInitSegment.end()) inferredInitSegment = found->second;
                    if (segmentByteRangeLength == kLengthUnset) {
                        segmentByteRangeOffset = 0;
                    } else if (isIFrameOnly && !initializationSegment && !inferredInitSegment) {
                        inferredInitSegment = std::make_shared<const Segment>(Segment::Init(segmentUri, 0, segmentByteRangeOffset, "", ""));
                        urlToInferredInitSegment[segmentUri] = inferredInitSegment;
                    }
                    ensureDrmInitData();
                    Segment segment;
                    segment.url = segmentUri;
                    segment.initializationSegment = initializationSegment ? initializationSegment : inferredInitSegment;
                    segment.title = segmentTitle;
                    segment.durationUs = segmentDurationUs;
                    segment.relativeDiscontinuitySequence = relativeDiscontinuitySequence;
                    segment.relativeStartTimeUs = segmentStartTimeUs;
                    segment.drmInitData = cachedDrmInitData;
                    segment.fullSegmentEncryptionKeyUri = fullSegmentEncryptionKeyUri;
                    segment.encryptionIV = segmentEncryptionIV;
                    segment.byteRangeOffset = segmentByteRangeOffset;
                    segment.byteRangeLength = segmentByteRangeLength;
                    segment.hasGapTag = hasGapTag;
                    segment.parts = std::move(trailingParts);
                    segments.push_back(std::move(segment));
                    segmentStartTimeUs += segmentDurationUs;
                    partStartTimeUs = segmentStartTimeUs;
                    segmentDurationUs = 0;
                    segmentTitle.clear();
                    trailingParts = {};
                    if (segmentByteRangeLength != kLengthUnset) segmentByteRangeOffset += segmentByteRangeLength;
                    segmentByteRangeLength = kLengthUnset;
                    hasGapTag = false;
                }
            }
            std::map<std::string, HlsMediaPlaylist::RenditionReport> renditionReportMap;
            for (const HlsMediaPlaylist::RenditionReport& report : renditionReports) {
                std::int64_t lastMediaSequence = report.lastMediaSequence;
                if (lastMediaSequence == stream::kIndexUnset) lastMediaSequence = mediaSequence + std::int64_t(segments.size()) - (trailingParts.empty() ? 1 : 0);
                int lastPartIndex = report.lastPartIndex;
                if (lastPartIndex == stream::kIndexUnset && partTargetDurationUs != kTimeUnset) {
                    const std::vector<Part>& lastParts = trailingParts.empty() ? segments.back().parts : trailingParts;
                    lastPartIndex = int(lastParts.size()) - 1;
                }
                renditionReportMap[report.playlistUri] = { report.playlistUri, lastMediaSequence, lastPartIndex };
            }
            if (preloadPart) trailingParts.push_back(*preloadPart);
            std::vector<Interstitial> interstitials;
            for (const auto& [id, builder] : interstitialBuilderMap)
                if (std::optional<Interstitial> interstitial = builder.Build()) interstitials.push_back(std::move(*interstitial));
            if (playlistStartTimeUs == 0 && previousMediaPlaylist && previousMediaPlaylist->hasProgramDateTime) playlistStartTimeUs = previousMediaPlaylist->startTimeUs;
            return std::make_shared<HlsMediaPlaylist>(playlistType, playlistUri, std::move(tags), startOffsetUs, preciseStart, playlistStartTimeUs,
                                                      hasDiscontinuitySequence, playlistDiscontinuitySequence, mediaSequence, version, targetDurationUs,
                                                      partTargetDurationUs, hasIndependentSegmentsTag, hasEndTag, playlistStartTimeUs != 0,
                                                      std::move(playlistProtectionSchemes), std::move(segments), std::move(trailingParts), serverControl,
                                                      std::move(renditionReportMap), std::move(interstitials), std::move(initializationSegment));
        }

    }

    HlsPlaylistParser::HlsPlaylistParser() : HlsPlaylistParser(nullptr, nullptr) {}

    HlsPlaylistParser::HlsPlaylistParser(std::shared_ptr<const HlsMultivariantPlaylist> multivariantPlaylist, std::shared_ptr<const HlsMediaPlaylist> previous)
        : m_Multivariant(std::move(multivariantPlaylist)), m_Previous(std::move(previous)) {}

    std::shared_ptr<HlsPlaylist> HlsPlaylistParser::Parse(const std::string& uri, const std::string& content) const {
        const std::optional<std::size_t> start = CheckPlaylistHeader(content);
        if (!start) throw ParserException("Input does not start with the #EXTM3U header.");
        // The header check reads the line break; the rest of that line (nothing) follows.
        LineIterator reader({}, content, *start);
        std::deque<std::string> extraLines;
        while (std::optional<std::string> raw = reader.ReadLine()) {
            const std::string line = JavaTrim(*raw);
            if (line.empty()) {
            } else if (StartsWith(line, TAG_STREAM_INF)) {
                extraLines.push_back(line);
                LineIterator iterator(std::move(extraLines), content, reader.Position());
                return ParseMultivariantPlaylist(iterator, uri);
            } else if (StartsWith(line, TAG_TARGET_DURATION) || StartsWith(line, TAG_MEDIA_SEQUENCE) || StartsWith(line, TAG_MEDIA_DURATION) ||
                       StartsWith(line, TAG_KEY) || StartsWith(line, TAG_BYTERANGE) || line == TAG_DISCONTINUITY || line == TAG_DISCONTINUITY_SEQUENCE ||
                       line == TAG_ENDLIST) {
                extraLines.push_back(line);
                LineIterator iterator(std::move(extraLines), content, reader.Position());
                return ParseMediaPlaylist(m_Multivariant ? *m_Multivariant : HlsMultivariantPlaylist::Empty(), m_Previous.get(), iterator, uri);
            } else {
                extraLines.push_back(line);
            }
        }
        throw ParserException("Failed to parse the playlist, could not identify any tags.");
    }

}
