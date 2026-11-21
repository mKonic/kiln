// media3 exoplayer-smoothstreaming: SsManifest and SsManifestParser.
#include "kiln/media/smoothstreaming/SsManifest.h"

#include "kiln/media/stream/Xml.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <variant>

namespace kiln::media::ss {

    using stream::ParserException;
    using stream::XmlPullParser;
    namespace MimeTypes = stream::MimeTypes;

    namespace {
        bool EqualsIgnoreCase(const std::string& a, const char* b) {
            const std::size_t n = std::char_traits<char>::length(b);
            if (a.size() != n) return false;
            for (std::size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
            return true;
        }
        void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
            for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
        }
        // Util.binarySearchFloor(array, value, inclusive = true, stayInBounds = true).
        int BinarySearchFloor(const std::vector<std::int64_t>& a, std::int64_t value) {
            auto it = std::upper_bound(a.begin(), a.end(), value);
            const int index = int(it - a.begin()) - 1;
            return std::max(index, 0);
        }
        // Util.getBytesFromHexString.
        std::vector<std::uint8_t> BytesFromHexString(const std::string& hex) {
            auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            std::vector<std::uint8_t> out(hex.size() / 2);
            for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::uint8_t((digit(hex[i * 2]) << 4) + digit(hex[i * 2 + 1]));
            return out;
        }
        // CodecSpecificDataUtil.splitNalUnits: the units after each 00 00 00 01 start code (start
        // codes kept), or nothing when the data does not start with one.
        bool IsNalStartCode(const std::vector<std::uint8_t>& d, std::size_t i) {
            if (d.size() <= i + 4) return false;
            return d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 && d[i + 3] == 1;
        }
        std::optional<std::vector<std::vector<std::uint8_t>>> SplitNalUnits(const std::vector<std::uint8_t>& data) {
            if (!IsNalStartCode(data, 0)) return std::nullopt;
            std::vector<std::size_t> starts;
            std::size_t index = 0;
            for (;;) {
                starts.push_back(index);
                std::size_t next = std::string::npos;
                for (std::size_t i = index + 4; data.size() >= 4 && i <= data.size() - 4; ++i)
                    if (IsNalStartCode(data, i)) {
                        next = i;
                        break;
                    }
                if (next == std::string::npos) break;
                index = next;
            }
            std::vector<std::vector<std::uint8_t>> split;
            for (std::size_t i = 0; i < starts.size(); ++i) {
                const std::size_t end = i + 1 < starts.size() ? starts[i + 1] : data.size();
                split.emplace_back(data.begin() + std::ptrdiff_t(starts[i]), data.begin() + std::ptrdiff_t(end));
            }
            return split;
        }
        // AacUtil.buildAacLcAudioSpecificConfig.
        std::vector<std::uint8_t> BuildAacLcAudioSpecificConfig(int sampleRate, int channelCount) {
            static constexpr int kRates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
            static constexpr int kChannels[] = { 0, 1, 2, 3, 4, 5, 6, 8, -1, -1, -1, 7, 8, -1, 8, -1 };
            int rateIndex = -1, channelConfig = -1;
            for (int i = 0; i < int(std::size(kRates)); ++i)
                if (sampleRate == kRates[i]) rateIndex = i;
            for (int i = 0; i < int(std::size(kChannels)); ++i)
                if (channelCount == kChannels[i]) channelConfig = i;
            if (sampleRate == -1 || channelConfig == -1)
                throw std::invalid_argument("Invalid sample rate or number of channels: " + std::to_string(sampleRate) + ", " + std::to_string(channelCount));
            constexpr int kAacLc = 2;
            return { std::uint8_t(((kAacLc << 3) & 0xF8) | ((rateIndex >> 1) & 0x07)), std::uint8_t(((rateIndex << 7) & 0x80) | ((channelConfig << 3) & 0x78)) };
        }

        // --- the element parsers ------------------------------------------------------------
        // Normalized attributes are inherited by child elements; null is a value (it hides a
        // parent's).
        using Value = std::variant<std::monostate, std::int64_t, std::string>;
        using Child = std::variant<std::monostate, SsManifest::StreamElement, SsManifest::ProtectionElement, Format>;

        std::optional<std::string> Attr(const XmlPullParser& p, const char* key) { return p.GetAttributeValue(key); }
        [[noreturn]] void Missing(const std::string& key) { throw ParserException("Missing required field: " + key); }
        std::int64_t ToLong(const std::string& v) {
            try {
                std::size_t used = 0;
                const long long n = std::stoll(v, &used);
                if (used != v.size()) throw std::invalid_argument(v);
                return n;
            } catch (const std::exception&) {
                throw ParserException("malformed manifest: not a number: " + v);
            }
        }
        int ToInt(const std::string& v) {
            const std::int64_t n = ToLong(v);
            if (n < INT32_MIN || n > INT32_MAX) throw ParserException("malformed manifest: not an int: " + v);
            return int(n);
        }

        class ElementParser {
        public:
            ElementParser(ElementParser* parent, std::string baseUri, std::string tag) : m_BaseUri(std::move(baseUri)), m_Tag(std::move(tag)), m_Parent(parent) {}
            virtual ~ElementParser() = default;

            // Reads the element (the parser at or before its start tag); monostate at the
            // document's end.
            Child Parse(XmlPullParser& xml) {
                bool foundStartTag = false;
                int skippingElementDepth = 0;
                for (;;) {
                    switch (xml.GetEventType()) {
                        case XmlPullParser::START_TAG: {
                            const std::string name = xml.GetName();
                            if (m_Tag == name) {
                                foundStartTag = true;
                                ParseStartTag(xml);
                            } else if (foundStartTag) {
                                if (skippingElementDepth > 0) {
                                    ++skippingElementDepth;
                                } else if (HandleChildInline(name)) {
                                    ParseStartTag(xml);
                                } else {
                                    std::unique_ptr<ElementParser> child = NewChildParser(this, name, m_BaseUri);
                                    if (!child) skippingElementDepth = 1;
                                    else AddChild(child->Parse(xml));
                                }
                            }
                            break;
                        }
                        case XmlPullParser::TEXT:
                            if (foundStartTag && skippingElementDepth == 0) ParseText(xml);
                            break;
                        case XmlPullParser::END_TAG:
                            if (foundStartTag) {
                                if (skippingElementDepth > 0) {
                                    --skippingElementDepth;
                                } else {
                                    const std::string name = xml.GetName();
                                    ParseEndTag(xml);
                                    if (!HandleChildInline(name)) return Build();
                                }
                            }
                            break;
                        case XmlPullParser::END_DOCUMENT:
                            return {};
                        default:
                            break;
                    }
                    xml.Next();
                }
            }

        protected:
            void PutNormalizedAttribute(const std::string& key, Value value) { m_Normalized.emplace_back(key, std::move(value)); }
            Value GetNormalizedAttribute(const std::string& key) const {
                for (const auto& [k, v] : m_Normalized)
                    if (k == key) return v;
                return m_Parent ? m_Parent->GetNormalizedAttribute(key) : Value{};
            }
            std::optional<std::string> NormalizedString(const std::string& key) const {
                Value v = GetNormalizedAttribute(key);
                if (auto* s = std::get_if<std::string>(&v)) return *s;
                return std::nullopt;
            }
            virtual bool HandleChildInline(const std::string&) { return false; }
            virtual void ParseStartTag(XmlPullParser&) {}
            virtual void ParseText(XmlPullParser&) {}
            virtual void ParseEndTag(XmlPullParser&) {}
            virtual void AddChild(Child) {}
            virtual Child Build() = 0;

            static std::string ParseRequiredString(const XmlPullParser& p, const char* key) {
                if (auto v = Attr(p, key)) return *v;
                Missing(key);
            }
            static int ParseInt(const XmlPullParser& p, const char* key, int defaultValue) {
                auto v = Attr(p, key);
                return v ? ToInt(*v) : defaultValue;
            }
            static int ParseRequiredInt(const XmlPullParser& p, const char* key) {
                if (auto v = Attr(p, key)) return ToInt(*v);
                Missing(key);
            }
            static std::int64_t ParseLong(const XmlPullParser& p, const char* key, std::int64_t defaultValue) {
                auto v = Attr(p, key);
                return v ? ToLong(*v) : defaultValue;
            }
            static std::int64_t ParseRequiredLong(const XmlPullParser& p, const char* key) {
                if (auto v = Attr(p, key)) return ToLong(*v);
                Missing(key);
            }
            // Boolean.parseBoolean: "true" in any case.
            static bool ParseBoolean(const XmlPullParser& p, const char* key, bool defaultValue) {
                auto v = Attr(p, key);
                return v ? EqualsIgnoreCase(*v, "true") : defaultValue;
            }

            std::string m_BaseUri;

        private:
            static std::unique_ptr<ElementParser> NewChildParser(ElementParser* parent, const std::string& name, const std::string& baseUri);

            std::string m_Tag;
            ElementParser* m_Parent;
            std::vector<std::pair<std::string, Value>> m_Normalized;
        };

        class QualityLevelParser final : public ElementParser {
        public:
            static constexpr const char* kTag = "QualityLevel";
            QualityLevelParser(ElementParser* parent, std::string baseUri) : ElementParser(parent, std::move(baseUri), kTag) {}

        protected:
            void ParseStartTag(XmlPullParser& p) override {
                Format f;
                std::string sampleMimeType = FourCcToMimeType(ParseRequiredString(p, "FourCC"));
                const Value typeValue = GetNormalizedAttribute("Type");
                const TrackType type = std::holds_alternative<std::int64_t>(typeValue) ? TrackType(std::get<std::int64_t>(typeValue)) : TrackType::Unknown;
                if (type == TrackType::Video) {
                    f.containerMimeType = MimeTypes::VIDEO_MP4;
                    f.width = ParseRequiredInt(p, "MaxWidth");
                    f.height = ParseRequiredInt(p, "MaxHeight");
                    f.initializationData = BuildCodecSpecificData(Attr(p, "CodecPrivateData").value_or(""));
                } else if (type == TrackType::Audio) {
                    if (sampleMimeType.empty()) sampleMimeType = MimeTypes::AUDIO_AAC;   // assumed AAC
                    const int channelCount = ParseRequiredInt(p, "Channels");
                    const int sampleRate = ParseRequiredInt(p, "SamplingRate");
                    auto csd = BuildCodecSpecificData(Attr(p, "CodecPrivateData").value_or(""));
                    if (csd.empty() && sampleMimeType == MimeTypes::AUDIO_AAC) csd = { BuildAacLcAudioSpecificConfig(sampleRate, channelCount) };
                    f.containerMimeType = MimeTypes::AUDIO_MP4;
                    f.channelCount = channelCount;
                    f.sampleRate = sampleRate;
                    f.initializationData = std::move(csd);
                } else if (type == TrackType::Text) {
                    int roleFlags = 0;
                    if (auto subType = NormalizedString("Subtype")) {
                        if (*subType == "CAPT") roleFlags = kRoleFlagCaption;
                        else if (*subType == "DESC") roleFlags = kRoleFlagDescribesMusicAndSound;
                    }
                    f.containerMimeType = MimeTypes::APPLICATION_MP4;
                    f.roleFlags = roleFlags;
                } else {
                    f.containerMimeType = MimeTypes::APPLICATION_MP4;
                }
                f.id = Attr(p, "Index").value_or("");
                if (auto name = NormalizedString("Name")) {
                    // Format.Builder.setLabel: the label, and labels with it alone.
                    f.label = *name;
                    f.labels = { Label{ .language = "", .value = *name } };
                }
                f.sampleMimeType = sampleMimeType;
                f.SetBitrates(ParseRequiredInt(p, "Bitrate"), f.peakBitrate);
                f.language = NormalizedString("Language").value_or("");
                m_Format = std::move(f);
            }
            Child Build() override { return m_Format; }

        private:
            static std::vector<std::vector<std::uint8_t>> BuildCodecSpecificData(const std::string& hex) {
                std::vector<std::vector<std::uint8_t>> csd;
                if (hex.empty()) return csd;
                std::vector<std::uint8_t> data = BytesFromHexString(hex);
                if (auto split = SplitNalUnits(data)) csd = std::move(*split);
                else csd.push_back(std::move(data));
                return csd;
            }
            static std::string FourCcToMimeType(const std::string& c) {
                if (EqualsIgnoreCase(c, "H264") || EqualsIgnoreCase(c, "X264") || EqualsIgnoreCase(c, "AVC1") || EqualsIgnoreCase(c, "DAVC")) return MimeTypes::VIDEO_H264;
                if (EqualsIgnoreCase(c, "AAC") || EqualsIgnoreCase(c, "AACL") || EqualsIgnoreCase(c, "AACH") || EqualsIgnoreCase(c, "AACP")) return MimeTypes::AUDIO_AAC;
                if (EqualsIgnoreCase(c, "TTML") || EqualsIgnoreCase(c, "DFXP")) return MimeTypes::APPLICATION_TTML;
                if (EqualsIgnoreCase(c, "ac-3") || EqualsIgnoreCase(c, "dac3")) return MimeTypes::AUDIO_AC3;
                if (EqualsIgnoreCase(c, "ec-3") || EqualsIgnoreCase(c, "dec3")) return MimeTypes::AUDIO_E_AC3;
                if (EqualsIgnoreCase(c, "dtsc")) return MimeTypes::AUDIO_DTS;
                if (EqualsIgnoreCase(c, "dtsh") || EqualsIgnoreCase(c, "dtsl")) return MimeTypes::AUDIO_DTS_HD;
                if (EqualsIgnoreCase(c, "dtse")) return MimeTypes::AUDIO_DTS_EXPRESS;
                if (EqualsIgnoreCase(c, "opus")) return MimeTypes::AUDIO_OPUS;
                return "";
            }
            Format m_Format;
        };

        class ProtectionParser final : public ElementParser {
        public:
            static constexpr const char* kTag = "Protection";
            ProtectionParser(ElementParser* parent, std::string baseUri) : ElementParser(parent, std::move(baseUri), kTag) {}

        protected:
            bool HandleChildInline(const std::string& tag) override { return tag == "ProtectionHeader"; }
            void ParseStartTag(XmlPullParser& p) override {
                if (p.GetName() != "ProtectionHeader") return;
                m_InProtectionHeader = true;
                std::string uuid = Attr(p, "SystemID").value_or("");
                if (uuid.size() >= 2 && uuid.front() == '{' && uuid.back() == '}') uuid = uuid.substr(1, uuid.size() - 2);
                m_Uuid = stream::ParseUuid(uuid);
            }
            void ParseText(XmlPullParser& p) override {
                if (m_InProtectionHeader) m_InitData = stream::Base64Decode(p.GetText());
            }
            void ParseEndTag(XmlPullParser& p) override {
                if (p.GetName() == "ProtectionHeader") m_InProtectionHeader = false;
            }
            Child Build() override {
                SsManifest::ProtectionElement e;
                e.uuid = m_Uuid;
                e.data = stream::BuildPsshAtom(m_Uuid, m_InitData);
                e.trackEncryptionBoxes = { { .isEncrypted = true, .perSampleIvSize = 8, .keyId = KeyId(m_InitData) } };
                return e;
            }

        private:
            // The PlayReady header's <KID> (UTF-16LE, read a byte in two), its GUID byte order
            // turned into a UUID's.
            static std::vector<std::uint8_t> KeyId(const std::vector<std::uint8_t>& initData) {
                std::string text;
                for (std::size_t i = 0; i < initData.size(); i += 2) text += char(initData[i]);
                const std::size_t start = text.find("<KID>"), end = text.find("</KID>");
                if (start == std::string::npos || end == std::string::npos || end < start + 5) throw ParserException("malformed manifest: no KID in the protection header");
                std::vector<std::uint8_t> keyId = stream::Base64Decode(text.substr(start + 5, end - start - 5));
                if (keyId.size() < 8) throw ParserException("malformed manifest: short KID");
                std::swap(keyId[0], keyId[3]);
                std::swap(keyId[1], keyId[2]);
                std::swap(keyId[4], keyId[5]);
                std::swap(keyId[6], keyId[7]);
                return keyId;
            }
            bool m_InProtectionHeader = false;
            stream::Uuid m_Uuid{};
            std::vector<std::uint8_t> m_InitData;
        };

        class StreamIndexParser final : public ElementParser {
        public:
            static constexpr const char* kTag = "StreamIndex";
            StreamIndexParser(ElementParser* parent, std::string baseUri) : ElementParser(parent, baseUri, kTag) {}

        protected:
            bool HandleChildInline(const std::string& tag) override { return tag == "c"; }
            void ParseStartTag(XmlPullParser& p) override {
                if (p.GetName() == "c") ParseStreamFragmentStartTag(p);
                else ParseStreamElementStartTag(p);
            }
            void AddChild(Child child) override {
                if (auto* f = std::get_if<Format>(&child)) m_Formats.push_back(std::move(*f));
            }
            Child Build() override {
                return SsManifest::StreamElement(m_BaseUri, m_Url, m_Type, m_SubType, m_Timescale, m_Name, m_MaxWidth, m_MaxHeight, m_DisplayWidth,
                                                 m_DisplayHeight, m_Language, m_Formats, m_StartTimes, m_LastChunkDuration);
            }

        private:
            void ParseStreamFragmentStartTag(const XmlPullParser& p) {
                const std::size_t chunkIndex = m_StartTimes.size();
                std::int64_t startTime = ParseLong(p, "t", kTimeUnset);
                if (startTime == kTimeUnset) {
                    if (chunkIndex == 0) startTime = 0;   // the track starts at t = 0
                    else if (m_LastChunkDuration != -1) startTime = m_StartTimes[chunkIndex - 1] + m_LastChunkDuration;   // C.INDEX_UNSET
                    else throw ParserException("Unable to infer start time");
                }
                m_StartTimes.push_back(startTime);
                m_LastChunkDuration = ParseLong(p, "d", kTimeUnset);
                const std::int64_t repeatCount = ParseLong(p, "r", 1);
                if (repeatCount > 1 && m_LastChunkDuration == kTimeUnset) throw ParserException("Repeated chunk with unspecified duration");
                for (std::int64_t i = 1; i < repeatCount; ++i) m_StartTimes.push_back(startTime + m_LastChunkDuration * i);
            }
            void ParseStreamElementStartTag(const XmlPullParser& p) {
                m_Type = ParseType(p);
                PutNormalizedAttribute("Type", std::int64_t(m_Type));
                std::optional<std::string> subType = m_Type == TrackType::Text ? std::optional(ParseRequiredString(p, "Subtype")) : Attr(p, "Subtype");
                m_SubType = subType.value_or("");
                PutNormalizedAttribute("Subtype", subType ? Value(*subType) : Value{});
                std::optional<std::string> name = Attr(p, "Name");
                m_Name = name.value_or("");
                PutNormalizedAttribute("Name", name ? Value(*name) : Value{});
                m_Url = ParseRequiredString(p, "Url");
                m_MaxWidth = ParseInt(p, "MaxWidth", -1);
                m_MaxHeight = ParseInt(p, "MaxHeight", -1);
                m_DisplayWidth = ParseInt(p, "DisplayWidth", -1);
                m_DisplayHeight = ParseInt(p, "DisplayHeight", -1);
                std::optional<std::string> language = Attr(p, "Language");
                m_Language = language.value_or("");
                PutNormalizedAttribute("Language", language ? Value(*language) : Value{});
                m_Timescale = ParseInt(p, "TimeScale", -1);
                if (m_Timescale == -1) {
                    const Value v = GetNormalizedAttribute("TimeScale");
                    m_Timescale = std::holds_alternative<std::int64_t>(v) ? std::get<std::int64_t>(v) : 10'000'000;
                }
                m_StartTimes.clear();
            }
            static TrackType ParseType(const XmlPullParser& p) {
                auto v = Attr(p, "Type");
                if (!v) Missing("Type");
                if (EqualsIgnoreCase(*v, "audio")) return TrackType::Audio;
                if (EqualsIgnoreCase(*v, "video")) return TrackType::Video;
                if (EqualsIgnoreCase(*v, "text")) return TrackType::Text;
                throw ParserException("Invalid key value[" + *v + "]");
            }

            std::vector<Format> m_Formats;
            TrackType m_Type = TrackType::Unknown;
            std::string m_SubType, m_Name, m_Url, m_Language;
            std::int64_t m_Timescale = 0;
            int m_MaxWidth = -1, m_MaxHeight = -1, m_DisplayWidth = -1, m_DisplayHeight = -1;
            std::vector<std::int64_t> m_StartTimes;
            std::int64_t m_LastChunkDuration = -1;
        };

        class SmoothStreamingMediaParser final : public ElementParser {
        public:
            static constexpr const char* kTag = "SmoothStreamingMedia";
            explicit SmoothStreamingMediaParser(std::string baseUri) : ElementParser(nullptr, std::move(baseUri), kTag) {}

        protected:
            void ParseStartTag(XmlPullParser& p) override {
                m_MajorVersion = ParseRequiredInt(p, "MajorVersion");
                m_MinorVersion = ParseRequiredInt(p, "MinorVersion");
                m_Timescale = ParseLong(p, "TimeScale", 10'000'000);
                m_Duration = ParseRequiredLong(p, "Duration");
                m_DvrWindowLength = ParseLong(p, "DVRWindowLength", 0);
                m_LookAheadCount = ParseInt(p, "LookaheadCount", SsManifest::kUnsetLookahead);
                m_IsLive = ParseBoolean(p, "IsLive", false);
                PutNormalizedAttribute("TimeScale", m_Timescale);
            }
            void AddChild(Child child) override {
                if (auto* e = std::get_if<SsManifest::StreamElement>(&child)) {
                    m_StreamElements.push_back(std::move(*e));
                } else if (auto* pe = std::get_if<SsManifest::ProtectionElement>(&child)) {
                    if (m_Protection) throw ParserException("malformed manifest: more than one Protection");
                    m_Protection = std::move(*pe);
                }
            }
            Child Build() override { return {}; }

        public:
            SsManifest Manifest() {
                if (m_Protection) {
                    stream::SchemeData scheme;
                    scheme.uuid = m_Protection->uuid;
                    scheme.mimeType = MimeTypes::VIDEO_MP4;
                    scheme.data = m_Protection->data;
                    const stream::DrmInitData drm("", { scheme });
                    for (auto& e : m_StreamElements)
                        if (e.type == TrackType::Video || e.type == TrackType::Audio)
                            for (auto& f : e.formats) f.drmInitData = drm;
                }
                return SsManifest(m_MajorVersion, m_MinorVersion, m_Timescale, m_Duration, m_DvrWindowLength, m_LookAheadCount, m_IsLive, m_Protection,
                                  m_StreamElements);
            }

        private:
            std::vector<SsManifest::StreamElement> m_StreamElements;
            int m_MajorVersion = 0, m_MinorVersion = 0, m_LookAheadCount = SsManifest::kUnsetLookahead;
            std::int64_t m_Timescale = 0, m_Duration = 0, m_DvrWindowLength = 0;
            bool m_IsLive = false;
            std::optional<SsManifest::ProtectionElement> m_Protection;
        };

        std::unique_ptr<ElementParser> ElementParser::NewChildParser(ElementParser* parent, const std::string& name, const std::string& baseUri) {
            if (name == QualityLevelParser::kTag) return std::make_unique<QualityLevelParser>(parent, baseUri);
            if (name == ProtectionParser::kTag) return std::make_unique<ProtectionParser>(parent, baseUri);
            if (name == StreamIndexParser::kTag) return std::make_unique<StreamIndexParser>(parent, baseUri);
            return nullptr;
        }
    }

    // --- SsManifest ---------------------------------------------------------------------------
    SsManifest::StreamElement::StreamElement(std::string baseUri, std::string chunkTemplate, TrackType type_, std::string subType_, std::int64_t timescale_,
                                             std::string name_, int maxWidth_, int maxHeight_, int displayWidth_, int displayHeight_, std::string language_,
                                             std::vector<Format> formats_, std::vector<std::int64_t> chunkStartTimes, std::int64_t lastChunkDuration)
        : type(type_), subType(std::move(subType_)), timescale(timescale_), name(std::move(name_)), maxWidth(maxWidth_), maxHeight(maxHeight_),
          displayWidth(displayWidth_), displayHeight(displayHeight_), language(std::move(language_)), formats(std::move(formats_)),
          chunkCount(int(chunkStartTimes.size())), m_BaseUri(std::move(baseUri)), m_ChunkTemplate(std::move(chunkTemplate)),
          m_ChunkStartTimes(std::move(chunkStartTimes)) {
        m_ChunkStartTimesUs.reserve(m_ChunkStartTimes.size());
        for (std::int64_t t : m_ChunkStartTimes) m_ChunkStartTimesUs.push_back(stream::ScaleLargeTimestamp(t, 1'000'000, timescale));
        m_LastChunkDurationUs = stream::ScaleLargeTimestamp(lastChunkDuration, 1'000'000, timescale);
    }

    SsManifest::StreamElement SsManifest::StreamElement::Copy(std::vector<Format> newFormats) const {
        StreamElement e = *this;
        e.formats = std::move(newFormats);
        return e;
    }

    int SsManifest::StreamElement::GetChunkIndex(std::int64_t timeUs) const { return BinarySearchFloor(m_ChunkStartTimesUs, timeUs); }

    std::int64_t SsManifest::StreamElement::GetChunkDurationUs(int chunkIndex) const {
        return chunkIndex == chunkCount - 1 ? m_LastChunkDurationUs
                                            : m_ChunkStartTimesUs.at(std::size_t(chunkIndex) + 1) - m_ChunkStartTimesUs.at(std::size_t(chunkIndex));
    }

    std::string SsManifest::StreamElement::BuildRequestUri(int track, int chunkIndex) const {
        const std::string bitrate = std::to_string(formats.at(std::size_t(track)).bitrate);
        const std::string startTime = std::to_string(m_ChunkStartTimes.at(std::size_t(chunkIndex)));
        std::string url = m_ChunkTemplate;
        ReplaceAll(url, "{bitrate}", bitrate);
        ReplaceAll(url, "{Bitrate}", bitrate);
        ReplaceAll(url, "{start time}", startTime);
        ReplaceAll(url, "{start_time}", startTime);
        return stream::ResolveUri(m_BaseUri, url);
    }

    SsManifest::SsManifest(int majorVersion_, int minorVersion_, std::int64_t timescale, std::int64_t duration, std::int64_t dvrWindowLength,
                           int lookAheadCount_, bool isLive_, std::optional<ProtectionElement> protectionElement_, std::vector<StreamElement> streamElements_)
        : majorVersion(majorVersion_), minorVersion(minorVersion_), lookAheadCount(lookAheadCount_), isLive(isLive_),
          protectionElement(std::move(protectionElement_)), streamElements(std::move(streamElements_)),
          durationUs(duration == 0 ? kTimeUnset : stream::ScaleLargeTimestamp(duration, 1'000'000, timescale)),
          dvrWindowLengthUs(dvrWindowLength == 0 ? kTimeUnset : stream::ScaleLargeTimestamp(dvrWindowLength, 1'000'000, timescale)) {}

    SsManifest SsManifest::Copy(std::vector<stream::StreamKey> keys) const {
        std::sort(keys.begin(), keys.end());
        const StreamElement* current = nullptr;
        std::vector<StreamElement> copied;
        std::vector<Format> copiedFormats;
        for (const auto& key : keys) {
            const StreamElement& element = streamElements.at(std::size_t(key.groupIndex));
            if (&element != current && current) {
                copied.push_back(current->Copy(copiedFormats));
                copiedFormats.clear();
            }
            current = &element;
            copiedFormats.push_back(element.formats.at(std::size_t(key.streamIndex)));
        }
        if (current) copied.push_back(current->Copy(copiedFormats));
        SsManifest m = *this;
        m.streamElements = std::move(copied);
        return m;
    }

    SsManifest SsManifestParser::Parse(const std::string& uri, const std::string& content) const {
        try {
            XmlPullParser xml(content);
            SmoothStreamingMediaParser parser(uri);
            bool found = false;
            // ElementParser.parse returns null at the document's end without the root element.
            for (int e = xml.GetEventType(); e != XmlPullParser::END_DOCUMENT; e = xml.Next()) {
                if (e == XmlPullParser::START_TAG && xml.GetName() == SmoothStreamingMediaParser::kTag) {
                    found = true;
                    break;
                }
            }
            if (!found) throw ParserException("malformed manifest: no SmoothStreamingMedia element");
            parser.Parse(xml);   // to the root's end tag
            return parser.Manifest();
        } catch (const stream::XmlPullParserException& e) {
            throw ParserException(std::string("malformed manifest: ") + e.what());
        }
    }

}
