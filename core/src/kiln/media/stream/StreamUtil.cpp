#include "kiln/media/stream/StreamUtil.h"

#include "kiln/Uri.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace kiln::media::stream {

    namespace {
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return s;
        }
        std::string Trim(const std::string& s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
            return s.substr(b, e - b);
        }
        bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
        std::string TopLevelType(const std::string& mimeType) {
            const std::size_t slash = mimeType.find('/');
            return slash == std::string::npos ? std::string() : mimeType.substr(0, slash);
        }
    }

    // --- MimeTypes ---------------------------------------------------------------------------

    namespace MimeTypes {

        bool IsAudio(const std::string& m) { return TopLevelType(m) == "audio"; }
        bool IsVideo(const std::string& m) { return TopLevelType(m) == "video"; }
        bool IsImage(const std::string& m) { return TopLevelType(m) == "image" || m == "application/x-image-uri"; }
        bool IsText(const std::string& m) {
            return TopLevelType(m) == "text" || m == "application/x-media3-cues" || m == APPLICATION_CEA608 || m == APPLICATION_CEA708 ||
                   m == APPLICATION_MP4CEA608 || m == APPLICATION_SUBRIP || m == APPLICATION_TTML || m == APPLICATION_TX3G || m == APPLICATION_MP4VTT ||
                   m == APPLICATION_RAWCC || m == APPLICATION_VOBSUB || m == APPLICATION_PGS || m == APPLICATION_DVBSUBS;
        }

        std::string GetMimeTypeFromMp4ObjectType(int objectType) {
            switch (objectType) {
                case 0x20: return VIDEO_MP4V;
                case 0x21: return VIDEO_H264;
                case 0x23: return VIDEO_H265;
                case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: return VIDEO_MPEG2;
                case 0x6A: return VIDEO_MPEG;
                case 0x69: case 0x6B: return AUDIO_MPEG;
                case 0x6C: return IMAGE_JPEG;
                case 0xA3: return VIDEO_VC1;
                case 0xB1: return VIDEO_VP9;
                case 0x40: case 0x66: case 0x67: case 0x68: return AUDIO_AAC;
                case 0xA5: return AUDIO_AC3;
                case 0xA6: return AUDIO_E_AC3;
                case 0xA9: case 0xAC: return AUDIO_DTS;
                case 0xAA: case 0xAB: return AUDIO_DTS_HD;
                case 0xAD: return AUDIO_OPUS;
                case 0xAE: return AUDIO_AC4;
                case 0xDD: return AUDIO_VORBIS;
                default: return {};
            }
        }

        std::string GetMediaMimeType(const std::string& rawCodec) {
            if (rawCodec.empty()) return {};
            const std::string codec = Lower(Trim(rawCodec));
            auto sw = [&](const char* p) { return StartsWith(codec, p); };
            if (sw("avc1") || sw("avc3")) return VIDEO_H264;
            if (sw("hev1") || sw("hvc1")) return VIDEO_H265;
            if (sw("vvc1") || sw("vvi1")) return VIDEO_H266;
            if (sw("dvav") || sw("dva1") || sw("dvhe") || sw("dvh1") || sw("dav1")) return VIDEO_DOLBY_VISION;
            if (sw("av01")) return VIDEO_AV1;
            if (sw("vp9") || sw("vp09")) return VIDEO_VP9;
            if (sw("vp8") || sw("vp08")) return VIDEO_VP8;
            if (sw("mp4a")) {
                std::string mimeType;
                if (sw("mp4a.")) {
                    static const std::regex pattern("^mp4a\\.([a-zA-Z0-9]{2})(?:\\.([0-9]{1,2}))?$");
                    std::smatch m;
                    if (std::regex_match(codec, m, pattern)) mimeType = GetMimeTypeFromMp4ObjectType(std::stoi(m[1].str(), nullptr, 16));
                }
                return mimeType.empty() ? std::string(AUDIO_AAC) : mimeType;
            }
            if (sw("mha1")) return AUDIO_MPEGH_MHA1;
            if (sw("mhm1")) return AUDIO_MPEGH_MHM1;
            if (sw("ac-3") || sw("dac3")) return AUDIO_AC3;
            if (sw("ec-3") || sw("dec3")) return AUDIO_E_AC3;
            if (sw(CODEC_E_AC3_JOC)) return AUDIO_E_AC3_JOC;
            if (sw("ac-4") || sw("dac4")) return AUDIO_AC4;
            if (sw("dtsc")) return AUDIO_DTS;
            if (sw("dtse")) return AUDIO_DTS_EXPRESS;
            if (sw("dtsh") || sw("dtsl")) return AUDIO_DTS_HD;
            if (sw("dtsx")) return AUDIO_DTS_UHD_P2;
            if (sw("opus")) return AUDIO_OPUS;
            if (sw("vorbis")) return AUDIO_VORBIS;
            if (sw("flac")) return AUDIO_FLAC;
            if (sw("stpp")) return APPLICATION_TTML;
            if (sw("wvtt")) return TEXT_VTT;
            if (codec.find("cea708") != std::string::npos) return APPLICATION_CEA708;
            if (codec.find("eia608") != std::string::npos || codec.find("cea608") != std::string::npos) return APPLICATION_CEA608;
            return {};   // no custom MIME types registered
        }

        namespace {
            std::string FirstOf(const std::string& codecs, bool (*predicate)(const std::string&)) {
                for (const std::string& codec : SplitCodecs(codecs)) {
                    const std::string m = GetMediaMimeType(codec);
                    if (!m.empty() && predicate(m)) return m;
                }
                return {};
            }
        }
        std::string GetVideoMediaMimeType(const std::string& codecs) { return FirstOf(codecs, IsVideo); }
        std::string GetAudioMediaMimeType(const std::string& codecs) { return FirstOf(codecs, IsAudio); }
        std::string GetTextMediaMimeType(const std::string& codecs) { return FirstOf(codecs, IsText); }

        bool IsDolbyVisionCodec(const std::string& codecs, const std::string& supplementalCodecs) {
            if (codecs.empty()) return false;
            if (StartsWith(codecs, "dvhe") || StartsWith(codecs, "dvh1") || StartsWith(codecs, "dav1")) return true;
            if (supplementalCodecs.empty()) return false;
            return (StartsWith(supplementalCodecs, "dvhe") && StartsWith(codecs, "hev1")) ||
                   (StartsWith(supplementalCodecs, "dvh1") && StartsWith(codecs, "hvc1")) ||
                   (StartsWith(supplementalCodecs, "dvav") && StartsWith(codecs, "avc3")) ||
                   (StartsWith(supplementalCodecs, "dva1") && StartsWith(codecs, "avc1")) ||
                   (StartsWith(supplementalCodecs, "dav1") && StartsWith(codecs, "av01"));
        }

        TrackType GetTrackType(const std::string& m) {
            if (m.empty()) return TrackType::Unknown;
            if (IsAudio(m)) return TrackType::Audio;
            if (IsVideo(m)) return TrackType::Video;
            if (IsText(m)) return TrackType::Text;
            if (IsImage(m)) return TrackType::Image;
            if (m == APPLICATION_ID3 || m == APPLICATION_EMSG || m == "application/x-scte35" || m == "application/x-icy" || m == "application/vnd.dvb.ait" ||
                m == "application/meta" || m == "application/x-itut-t35")
                return TrackType::Metadata;
            return TrackType::Unknown;
        }

        TrackType GetTrackTypeOfCodec(const std::string& codec) { return GetTrackType(GetMediaMimeType(codec)); }

        std::string NormalizeMimeType(const std::string& mimeType) {
            const std::string m = Lower(mimeType);
            if (m == "video/x-mvhevc") return "video/mv-hevc";
            if (m == "audio/x-flac") return AUDIO_FLAC;
            if (m == "audio/mp3") return AUDIO_MPEG;
            if (m == "audio/x-wav") return "audio/wav";
            if (m == "application/x-mpegurl") return APPLICATION_M3U8;
            if (m == "audio/mpeg-l1") return "audio/mpeg-L1";
            if (m == "audio/mpeg-l2") return "audio/mpeg-L2";
            return m;
        }

        bool IsMatroska(const std::string& m) {
            return StartsWith(m, VIDEO_WEBM) || StartsWith(m, AUDIO_WEBM) || StartsWith(m, APPLICATION_WEBM) || StartsWith(m, "video/x-matroska") ||
                   StartsWith(m, "audio/x-matroska") || StartsWith(m, "application/x-matroska");
        }

    }

    // --- Util ------------------------------------------------------------------------------------

    std::vector<std::string> SplitCodecs(const std::string& codecs) {
        std::vector<std::string> out;
        const std::string trimmed = Trim(codecs);
        if (trimmed.empty()) return out;
        static const std::regex separator("\\s*,\\s*");
        out.assign(std::sregex_token_iterator(trimmed.begin(), trimmed.end(), separator, -1), std::sregex_token_iterator());
        return out;
    }

    namespace {
        std::string CodecsFiltered(const std::string& codecs, TrackType trackType, bool keep) {
            const std::vector<std::string> codecArray = SplitCodecs(codecs);
            if (codecArray.empty()) return {};
            std::string out;
            for (const std::string& codec : codecArray) {
                if ((MimeTypes::GetTrackTypeOfCodec(codec) == trackType) == keep) {
                    if (!out.empty()) out += ",";
                    out += codec;
                }
            }
            return out;
        }
    }
    std::string GetCodecsOfType(const std::string& codecs, TrackType trackType) { return CodecsFiltered(codecs, trackType, true); }
    std::string GetCodecsWithoutType(const std::string& codecs, TrackType trackType) { return CodecsFiltered(codecs, trackType, false); }
    int GetCodecCountOfType(const std::string& codecs, TrackType trackType) {
        int count = 0;
        for (const std::string& codec : SplitCodecs(codecs))
            if (MimeTypes::GetTrackTypeOfCodec(codec) == trackType) count++;
        return count;
    }

    bool ApplyColorInfoForDolbyVision(Format& format, const std::string& codecs, const std::string& supplementalCodecs,
                                      const std::string& supplementalProfiles) {
        if (codecs.empty() || !MimeTypes::IsDolbyVisionCodec(codecs, supplementalCodecs)) return false;
        int colorSpace = -1, colorRange = -1, colorTransfer = -1;
        if (StartsWith(codecs, "dvhe") || StartsWith(codecs, "dvh1") || StartsWith(codecs, "dav1")) {
            colorSpace = kColorSpaceBt2020;
            colorTransfer = kColorTransferSt2084;
            colorRange = kColorRangeFull;
        } else if (!supplementalProfiles.empty()) {
            if (supplementalProfiles == "db1p") {
                colorSpace = kColorSpaceBt2020;
                colorTransfer = kColorTransferSt2084;
                colorRange = kColorRangeLimited;
            } else if (StartsWith(supplementalProfiles, "db4")) {
                colorSpace = kColorSpaceBt2020;
                colorTransfer = kColorTransferHlg;
                colorRange = kColorRangeLimited;
            }
        }
        if (colorSpace == -1) return false;
        format.colorSpace = colorSpace;
        format.colorRange = colorRange;
        format.colorTransfer = colorTransfer;
        return true;
    }

    // --- UriUtil ---------------------------------------------------------------------------------

    namespace {
        constexpr int SCHEME_COLON = 0, PATH = 1, QUERY = 2, FRAGMENT = 3;

        std::array<int, 4> GetUriIndices(const std::string& uri) {
            std::array<int, 4> indices{};
            if (uri.empty()) {
                indices[SCHEME_COLON] = -1;
                return indices;
            }
            const int length = int(uri.size());
            auto indexOf = [&](char c, int from = 0) {
                const std::size_t i = uri.find(c, std::size_t(from));
                return i == std::string::npos ? -1 : int(i);
            };
            int fragmentIndex = indexOf('#');
            if (fragmentIndex == -1) fragmentIndex = length;
            int queryIndex = indexOf('?');
            if (queryIndex == -1 || queryIndex > fragmentIndex) queryIndex = fragmentIndex;
            int schemeIndexLimit = indexOf('/');
            if (schemeIndexLimit == -1 || schemeIndexLimit > queryIndex) schemeIndexLimit = queryIndex;
            int schemeIndex = indexOf(':');
            if (schemeIndex > schemeIndexLimit) schemeIndex = -1;
            const bool hasAuthority = schemeIndex + 2 < queryIndex && uri[std::size_t(schemeIndex + 1)] == '/' && uri[std::size_t(schemeIndex + 2)] == '/';
            int pathIndex;
            if (hasAuthority) {
                pathIndex = indexOf('/', schemeIndex + 3);
                if (pathIndex == -1 || pathIndex > queryIndex) pathIndex = queryIndex;
            } else {
                pathIndex = schemeIndex + 1;
            }
            indices[SCHEME_COLON] = schemeIndex;
            indices[PATH] = pathIndex;
            indices[QUERY] = queryIndex;
            indices[FRAGMENT] = fragmentIndex;
            return indices;
        }

        std::string RemoveDotSegments(std::string& uri, int offset, int limit) {
            if (offset >= limit) return uri;
            if (uri[std::size_t(offset)] == '/') offset++;
            int segmentStart = offset;
            int i = offset;
            while (i <= limit) {
                int nextSegmentStart;
                if (i == limit) {
                    nextSegmentStart = i;
                } else if (uri[std::size_t(i)] == '/') {
                    nextSegmentStart = i + 1;
                } else {
                    i++;
                    continue;
                }
                if (i == segmentStart + 1 && uri[std::size_t(segmentStart)] == '.') {
                    uri.erase(std::size_t(segmentStart), std::size_t(nextSegmentStart - segmentStart));
                    limit -= nextSegmentStart - segmentStart;
                    i = segmentStart;
                } else if (i == segmentStart + 2 && uri[std::size_t(segmentStart)] == '.' && uri[std::size_t(segmentStart + 1)] == '.') {
                    // lastIndexOf("/", segmentStart - 2): -1 when segmentStart - 2 < 0.
                    int prevSegmentStart = 0;
                    if (segmentStart - 2 >= 0) {
                        const std::size_t slash = uri.rfind('/', std::size_t(segmentStart - 2));
                        prevSegmentStart = slash == std::string::npos ? 0 : int(slash) + 1;
                    }
                    const int removeFrom = prevSegmentStart > offset ? prevSegmentStart : offset;
                    uri.erase(std::size_t(removeFrom), std::size_t(nextSegmentStart - removeFrom));
                    limit -= nextSegmentStart - removeFrom;
                    segmentStart = prevSegmentStart;
                    i = prevSegmentStart;
                } else {
                    i++;
                    segmentStart = i;
                }
            }
            return uri;
        }
    }

    std::string ResolveUri(const std::string& baseUri, const std::string& referenceUri) {
        std::string uri;
        const std::array<int, 4> refIndices = GetUriIndices(referenceUri);
        if (refIndices[SCHEME_COLON] != -1) {
            uri = referenceUri;
            RemoveDotSegments(uri, refIndices[PATH], refIndices[QUERY]);
            return uri;
        }
        const std::array<int, 4> baseIndices = GetUriIndices(baseUri);
        if (refIndices[FRAGMENT] == 0) return baseUri.substr(0, std::size_t(baseIndices[FRAGMENT])) + referenceUri;
        if (refIndices[QUERY] == 0) return baseUri.substr(0, std::size_t(baseIndices[QUERY])) + referenceUri;
        if (refIndices[PATH] != 0) {
            const int baseLimit = baseIndices[SCHEME_COLON] + 1;
            uri = baseUri.substr(0, std::size_t(baseLimit)) + referenceUri;
            return RemoveDotSegments(uri, baseLimit + refIndices[PATH], baseLimit + refIndices[QUERY]);
        }
        if (referenceUri[std::size_t(refIndices[PATH])] == '/') {
            uri = baseUri.substr(0, std::size_t(baseIndices[PATH])) + referenceUri;
            return RemoveDotSegments(uri, baseIndices[PATH], baseIndices[PATH] + refIndices[QUERY]);
        }
        if (baseIndices[SCHEME_COLON] + 2 < baseIndices[PATH] && baseIndices[PATH] == baseIndices[QUERY]) {
            uri = baseUri.substr(0, std::size_t(baseIndices[PATH])) + "/" + referenceUri;
            return RemoveDotSegments(uri, baseIndices[PATH], baseIndices[PATH] + refIndices[QUERY] + 1);
        }
        int lastSlashIndex = -1;
        if (baseIndices[QUERY] - 1 >= 0) {
            const std::size_t slash = baseUri.rfind('/', std::size_t(baseIndices[QUERY] - 1));
            lastSlashIndex = slash == std::string::npos ? -1 : int(slash);
        }
        const int baseLimit = lastSlashIndex == -1 ? baseIndices[PATH] : lastSlashIndex + 1;
        uri = baseUri.substr(0, std::size_t(baseLimit)) + referenceUri;
        return RemoveDotSegments(uri, baseIndices[PATH], baseLimit + refIndices[QUERY]);
    }

    bool IsAbsoluteUri(const std::string& uri) { return GetUriIndices(uri)[SCHEME_COLON] != -1; }

    std::optional<std::string> GetQueryParameter(const std::string& uri, const std::string& name) {
        const std::vector<std::string> values = Uri(uri).QueryParameters(name);
        if (values.empty()) return std::nullopt;
        return values.front();
    }

    // --- Dates, Base64, UUIDs, PSSH --------------------------------------------------------------

    std::int64_t ParseXsDateTime(const std::string& value) {
        static const std::regex pattern(
            "(\\d\\d\\d\\d)\\-(\\d\\d)\\-(\\d\\d)[Tt ](\\d\\d):(\\d\\d):(\\d\\d)([\\.,](\\d+))?([Zz]|((\\+|\\-)(\\d?\\d):?(\\d\\d)?))?");
        std::smatch m;
        if (!std::regex_match(value, m, pattern)) throw ParserException("Invalid date/time format: " + value);
        int timezoneShift = 0;
        if (m[9].matched && Lower(m[9].str()) != "z") {
            timezoneShift = std::stoi(m[12].str()) * 60;
            if (m[13].matched) timezoneShift += std::stoi(m[13].str());
            if (m[11].str() == "-") timezoneShift *= -1;
        }
        // days_from_civil (proleptic Gregorian, as GregorianCalendar after 1582).
        auto daysFromCivil = [](std::int64_t y, unsigned mo, unsigned d) {
            y -= mo <= 2;
            const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = unsigned(y - era * 400);
            const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + std::int64_t(doe) - 719468;
        };
        const std::int64_t days = daysFromCivil(std::stoll(m[1].str()), unsigned(std::stoi(m[2].str())), unsigned(std::stoi(m[3].str())));
        std::int64_t time = ((days * 24 + std::stoll(m[4].str())) * 60 + std::stoll(m[5].str())) * 60 + std::stoll(m[6].str());
        time *= 1000;
        if (m[8].matched && !m[8].str().empty()) {
            // BigDecimal("0." + digits).movePointRight(3).intValue(): the first three digits.
            std::string digits = m[8].str();
            digits = (digits + "000").substr(0, 3);
            time += std::stoll(digits);
        }
        if (timezoneShift != 0) time -= std::int64_t(timezoneShift) * 60000;
        return time;
    }

    std::vector<std::uint8_t> Base64Decode(const std::string& text) {
        auto value = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+' || c == '-') return 62;
            if (c == '/' || c == '_') return 63;
            return -1;
        };
        std::vector<std::uint8_t> out;
        std::uint32_t buffer = 0;
        int bits = 0;
        for (char c : text) {
            if (c == '=') break;
            const int v = value(c);
            if (v < 0) continue;   // android.util.Base64 skips characters outside the alphabet
            buffer = (buffer << 6) | std::uint32_t(v);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(std::uint8_t((buffer >> bits) & 0xFF));
            }
        }
        return out;
    }

    Uuid ParseUuid(const std::string& text) {
        Uuid u{};
        int n = 0;
        for (char c : text) {
            if (c == '-') continue;
            const int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
            if (v < 0 || v > 15 || n >= 32) throw std::invalid_argument("bad UUID " + text);
            u[std::size_t(n / 2)] = std::uint8_t(u[std::size_t(n / 2)] | (n % 2 == 0 ? v << 4 : v));
            n++;
        }
        if (n != 32) throw std::invalid_argument("bad UUID " + text);
        return u;
    }

    std::vector<std::uint8_t> BuildPsshAtom(const Uuid& systemId, const std::vector<std::uint8_t>& data) {
        const std::uint32_t size = 32 + std::uint32_t(data.size());
        std::vector<std::uint8_t> out;
        auto put32 = [&](std::uint32_t v) {
            for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
        };
        put32(size);
        out.insert(out.end(), { 'p', 's', 's', 'h' });
        put32(0);   // version 0, no flags
        out.insert(out.end(), systemId.begin(), systemId.end());
        put32(std::uint32_t(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    DrmInitData::DrmInitData(std::string type, std::vector<SchemeData> datas) : schemeType(std::move(type)), schemeDatas(std::move(datas)) {
        // compare: C.UUID_NIL sorts after the rest, others by UUID.compareTo (signed halves).
        auto halves = [](const Uuid& u) {
            std::int64_t msb = 0, lsb = 0;
            for (int i = 0; i < 8; i++) msb = std::int64_t(std::uint64_t(msb) << 8 | u[std::size_t(i)]);
            for (int i = 8; i < 16; i++) lsb = std::int64_t(std::uint64_t(lsb) << 8 | u[std::size_t(i)]);
            return std::pair{ msb, lsb };
        };
        auto compare = [&](const SchemeData& a, const SchemeData& b) {
            if (a.uuid == kUuidNil) return b.uuid == kUuidNil ? 0 : 1;
            const auto ha = halves(a.uuid), hb = halves(b.uuid);
            return ha < hb ? -1 : ha > hb ? 1 : 0;
        };
        std::stable_sort(schemeDatas.begin(), schemeDatas.end(), [&](const SchemeData& a, const SchemeData& b) { return compare(a, b) < 0; });
    }

}
