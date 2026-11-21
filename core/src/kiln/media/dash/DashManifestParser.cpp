// media3-exoplayer-dash DashManifestParser: an MPD (ISO/IEC 23009-1, with the DVB, DASH-IF,
// Dolby, DTS and SCTE extensions upstream reads) into a DashManifest.

#include "kiln/media/dash/DashManifest.h"

#include "kiln/Log.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <regex>

namespace kiln::media::dash {

    using namespace stream;
    using Xpp = XmlPullParser;

    namespace {

        const std::regex FRAME_RATE_PATTERN("(\\d+)(?:/(\\d+))?");
        const std::regex CEA_608_ACCESSIBILITY_PATTERN("CC([1-4])=.*");
        const std::regex CEA_708_ACCESSIBILITY_PATTERN("([1-9]|[1-5][0-9]|6[0-3])=.*");
        constexpr int DOLBY_AC4_CHANNEL_CONFIGURATION_MAPPING[] = { 2, 1, 2, 2, 2, 2, 1, 2, 2, 1, 1, 1, 1, 2, 1, 1, 2, 2, 2 };
        constexpr int MPEG_CHANNEL_CONFIGURATION_MAPPING[] = { -1, 1, 2, 3, 4, 5, 6, 8, 2, 3, 4, 7, 8, 24, 8, 12, 10, 12, 14, 12, 14 };

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return s;
        }
        bool EqualsIgnoreCase(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }

        // Integer.parseInt / Long.parseLong / Float.parseFloat: the whole string, or
        // std::invalid_argument (NumberFormatException).
        std::int64_t ParseJavaLong(const std::string& s, int radix = 10) {
            std::size_t used = 0;
            const long long v = std::stoll(s, &used, radix);
            if (used != s.size()) throw std::invalid_argument("not a number: " + s);
            return v;
        }
        int ParseJavaInt(const std::string& s, int radix = 10) {
            const std::int64_t v = ParseJavaLong(s, radix);
            if (v < INT32_MIN || v > INT32_MAX) throw std::invalid_argument("out of range: " + s);
            return int(v);
        }
        float ParseJavaFloat(const std::string& s) {
            std::size_t used = 0;
            const float v = std::stof(s, &used);
            for (std::size_t i = used; i < s.size(); i++)
                if (static_cast<unsigned char>(s[i]) > ' ') throw std::invalid_argument("not a number: " + s);
            return v;
        }

        std::vector<std::string> SplitOn(const std::string& s, const std::string& regex) {
            const std::regex re(regex);
            std::vector<std::string> out(std::sregex_token_iterator(s.begin(), s.end(), re, -1), std::sregex_token_iterator());
            // String.split drops trailing empty strings.
            while (!out.empty() && out.back().empty()) out.pop_back();
            return out;
        }

        bool IsStartTag(const Xpp& xpp, const char* name) { return xpp.GetEventType() == Xpp::START_TAG && xpp.GetName() == name; }
        bool IsStartTag(const Xpp& xpp) { return xpp.GetEventType() == Xpp::START_TAG; }
        bool IsEndTag(const Xpp& xpp, const char* name) { return xpp.GetEventType() == Xpp::END_TAG && xpp.GetName() == name; }
        bool IsEndTag(const Xpp& xpp) { return xpp.GetEventType() == Xpp::END_TAG; }
        std::string StripPrefix(const std::string& name) {
            const std::size_t i = name.find(':');
            return i == std::string::npos ? name : name.substr(i + 1);
        }
        bool IsStartTagIgnorePrefix(const Xpp& xpp, const char* name) { return IsStartTag(xpp) && StripPrefix(xpp.GetName()) == name; }
        std::optional<std::string> GetAttributeValueIgnorePrefix(const Xpp& xpp, const std::string& name) {
            for (int i = 0; i < xpp.GetAttributeCount(); i++)
                if (StripPrefix(xpp.GetAttributeName(i)) == name) return xpp.GetAttributeValue(i);
            return std::nullopt;
        }

        int ParseInt(const Xpp& xpp, const char* name, int defaultValue) {
            const std::optional<std::string> v = xpp.GetAttributeValue(name);
            return v ? ParseJavaInt(*v) : defaultValue;
        }
        std::int64_t ParseLong(const Xpp& xpp, const char* name, std::int64_t defaultValue) {
            const std::optional<std::string> v = xpp.GetAttributeValue(name);
            return v ? ParseJavaLong(*v) : defaultValue;
        }
        float ParseFloat(const Xpp& xpp, const char* name, float defaultValue) {
            const std::optional<std::string> v = xpp.GetAttributeValue(name);
            return v ? ParseJavaFloat(*v) : defaultValue;
        }
        std::string ParseString(const Xpp& xpp, const char* name, const std::string& defaultValue) { return xpp.GetAttributeValue(name).value_or(defaultValue); }
        std::int64_t ParseDuration(const Xpp& xpp, const char* name, std::int64_t defaultValue) {
            const std::optional<std::string> v = xpp.GetAttributeValue(name);
            return v ? ParseXsDuration(*v) : defaultValue;
        }
        std::int64_t ParseDateTime(const Xpp& xpp, const char* name, std::int64_t defaultValue) {
            const std::optional<std::string> v = xpp.GetAttributeValue(name);
            return v ? ParseXsDateTime(*v) : defaultValue;
        }

        std::string ParseText(Xpp& xpp, const char* label) {
            std::string text;
            do {
                xpp.Next();
                if (xpp.GetEventType() == Xpp::TEXT) text = xpp.GetText();
                else DashManifestParser::MaybeSkipTag(xpp);
            } while (!IsEndTag(xpp, label));
            return text;
        }

        float ParseFrameRate(const Xpp& xpp, float defaultValue) {
            float frameRate = defaultValue;
            if (const std::optional<std::string> attribute = xpp.GetAttributeValue("frameRate")) {
                std::smatch m;
                if (std::regex_match(*attribute, m, FRAME_RATE_PATTERN)) {
                    const int numerator = ParseJavaInt(m[1].str());
                    if (m[2].matched && m[2].length() > 0) frameRate = float(numerator) / float(ParseJavaInt(m[2].str()));
                    else frameRate = float(numerator);
                }
            }
            return frameRate;
        }

        Descriptor ParseDescriptor(Xpp& xpp, const char* tag) {
            Descriptor d{ ParseString(xpp, "schemeIdUri", ""), ParseString(xpp, "value", ""), ParseString(xpp, "id", "") };
            do {
                xpp.Next();
            } while (!IsEndTag(xpp, tag));
            return d;
        }

        TrackType ParseContentType(const Xpp& xpp) {
            const std::string contentType = xpp.GetAttributeValue("contentType").value_or("");
            if (contentType.empty()) return TrackType::Unknown;
            if (contentType == "audio") return TrackType::Audio;
            if (contentType == "video") return TrackType::Video;
            if (contentType == "text") return TrackType::Text;
            if (contentType == "image") return TrackType::Image;
            return TrackType::Unknown;
        }

        std::vector<std::string> ParseProfiles(const Xpp& xpp, const char* attributeName) {
            const std::optional<std::string> v = xpp.GetAttributeValue(attributeName);
            if (!v) return {};
            return SplitOn(*v, ",");
        }

        bool IsDvbProfileDeclared(const std::vector<std::string>& profiles) {
            for (const std::string& profile : profiles)
                if (profile.rfind("urn:dvb:dash:profile:dvb-dash:", 0) == 0) return true;
            return false;
        }

        std::int64_t ParseAvailabilityTimeOffsetUs(const Xpp& xpp, std::int64_t parentAvailabilityTimeOffsetUs) {
            const std::optional<std::string> value = xpp.GetAttributeValue("availabilityTimeOffset");
            if (!value) return parentAvailabilityTimeOffsetUs;
            if (*value == "INF") return INT64_MAX;
            return std::int64_t(ParseJavaFloat(*value) * float(kMicrosPerSecond));
        }

        std::int64_t GetFinalAvailabilityTimeOffset(std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t segmentBaseAvailabilityTimeOffsetUs) {
            std::int64_t availabilityTimeOffsetUs = segmentBaseAvailabilityTimeOffsetUs;
            if (availabilityTimeOffsetUs == kTimeUnset) availabilityTimeOffsetUs = baseUrlAvailabilityTimeOffsetUs;
            if (availabilityTimeOffsetUs == INT64_MAX) availabilityTimeOffsetUs = kTimeUnset;
            return availabilityTimeOffsetUs;
        }

        std::vector<BaseUrl> ParseBaseUrl(Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, bool dvbProfileDeclared) {
            const std::optional<std::string> priorityValue = xpp.GetAttributeValue("dvb:priority");
            int priority = priorityValue ? ParseJavaInt(*priorityValue) : (dvbProfileDeclared ? BaseUrl::DEFAULT_DVB_PRIORITY : BaseUrl::PRIORITY_UNSET);
            const std::optional<std::string> weightValue = xpp.GetAttributeValue("dvb:weight");
            int weight = weightValue ? ParseJavaInt(*weightValue) : BaseUrl::DEFAULT_WEIGHT;
            std::optional<std::string> serviceLocation = xpp.GetAttributeValue("serviceLocation");
            const std::string baseUrl = ParseText(xpp, "BaseURL");
            if (IsAbsoluteUri(baseUrl)) return { BaseUrl(baseUrl, serviceLocation.value_or(baseUrl), priority, weight) };
            std::vector<BaseUrl> baseUrls;
            for (const BaseUrl& parentBaseUrl : parentBaseUrls) {
                const std::string resolvedBaseUri = ResolveUri(parentBaseUrl.url, baseUrl);
                std::string resolvedServiceLocation = serviceLocation.value_or(resolvedBaseUri);
                if (dvbProfileDeclared) {
                    priority = parentBaseUrl.priority;
                    weight = parentBaseUrl.weight;
                    resolvedServiceLocation = parentBaseUrl.serviceLocation;
                }
                baseUrls.emplace_back(resolvedBaseUri, resolvedServiceLocation, priority, weight);
            }
            return baseUrls;
        }

        RangedUri ParseRangedUrl(const Xpp& xpp, const char* urlAttribute, const char* rangeAttribute) {
            const std::string urlText = xpp.GetAttributeValue(urlAttribute).value_or("");
            std::int64_t rangeStart = 0, rangeLength = kLengthUnset;
            if (const std::optional<std::string> rangeText = xpp.GetAttributeValue(rangeAttribute)) {
                const std::vector<std::string> range = SplitOn(*rangeText, "-");
                rangeStart = ParseJavaLong(range.at(0));
                if (range.size() == 2) rangeLength = ParseJavaLong(range[1]) - rangeStart + 1;
            }
            return RangedUri(urlText, rangeStart, rangeLength);
        }
        RangedUri ParseInitialization(const Xpp& xpp) { return ParseRangedUrl(xpp, "sourceURL", "range"); }
        RangedUri ParseSegmentUrl(const Xpp& xpp) { return ParseRangedUrl(xpp, "media", "mediaRange"); }

        std::optional<UrlTemplate> ParseUrlTemplate(const Xpp& xpp, const char* name, const std::optional<UrlTemplate>& defaultValue) {
            if (const std::optional<std::string> value = xpp.GetAttributeValue(name)) return UrlTemplate::Compile(*value);
            return defaultValue;
        }

        int ParseMpegChannelConfiguration(const Xpp& xpp) {
            const int index = ParseInt(xpp, "value", kIndexUnset);
            return 0 <= index && index < int(std::size(MPEG_CHANNEL_CONFIGURATION_MAPPING)) ? MPEG_CHANNEL_CONFIGURATION_MAPPING[index] : -1;
        }
        int ParseDtsChannelConfiguration(const Xpp& xpp) {
            const int channelCount = ParseInt(xpp, "value", -1);
            return 0 < channelCount && channelCount < 33 ? channelCount : -1;
        }
        int ParseDtsxChannelConfiguration(const Xpp& xpp) {
            const std::optional<std::string> value = xpp.GetAttributeValue("value");
            if (!value) return -1;
            const int channelCount = std::popcount(std::uint32_t(ParseJavaInt(*value, 16)));
            return channelCount == 0 ? -1 : channelCount;
        }
        int ParseDolbyChannelConfiguration(const Xpp& xpp) {
            const std::optional<std::string> value = xpp.GetAttributeValue("value");
            if (!value) return -1;
            const std::string v = Lower(*value);
            if (v == "4000") return 1;
            if (v == "a000") return 2;
            if (v == "f800") return 5;
            if (v == "f801") return 6;
            if (v == "fa01") return 8;
            return -1;
        }
        int ParseDolbyAc4ObjectBasedChannelConfiguration(const std::string& codecs) {
            const std::vector<std::string> codecList = SplitCodecs(codecs);
            if (codecList.empty()) return -1;
            std::string first = Lower(codecList[0]);
            first.erase(0, first.find_first_not_of(" \t\n\r\f\v"));
            first.erase(first.find_last_not_of(" \t\n\r\f\v") + 1);
            std::vector<std::string> parts;
            std::size_t start = 0;
            for (std::size_t dot = first.find('.'); dot != std::string::npos; dot = first.find('.', start)) {
                parts.push_back(first.substr(start, dot - start));
                start = dot + 1;
            }
            parts.push_back(first.substr(start));
            if (parts.size() != 4 || parts[0] != "ac-4") return -1;
            if (parts[3] == "03") return 18;   // IMMERSIVE_STEREO... AC-4 level 3
            if (parts[3] == "04") return 21;
            return -1;
        }
        int ParseDolbyAc4ChannelConfiguration(const Xpp& xpp, const std::string& codecs) {
            const std::optional<std::string> value = xpp.GetAttributeValue("value");
            if (!value || value->size() != 6) return -1;
            const int objectBasedAudioFlag = 1 << 23;
            const int ac4ChannelMask = ParseJavaInt(*value, 16);
            if ((ac4ChannelMask & objectBasedAudioFlag) != 0) return ParseDolbyAc4ObjectBasedChannelConfiguration(codecs);
            int channelCount = 0;
            for (int i = 0; i < int(std::size(DOLBY_AC4_CHANNEL_CONFIGURATION_MAPPING)); i++)
                channelCount += ((ac4ChannelMask >> i) & 0x1) * DOLBY_AC4_CHANNEL_CONFIGURATION_MAPPING[i];
            return channelCount == 0 ? -1 : channelCount;
        }

        int ParseAudioChannelConfiguration(Xpp& xpp, const std::string& codecs) {
            const std::string schemeIdUri = ParseString(xpp, "schemeIdUri", "");
            int audioChannels;
            if (schemeIdUri == "urn:mpeg:dash:23003:3:audio_channel_configuration:2011") audioChannels = ParseInt(xpp, "value", -1);
            else if (schemeIdUri == "urn:mpeg:mpegB:cicp:ChannelConfiguration") audioChannels = ParseMpegChannelConfiguration(xpp);
            else if (schemeIdUri == "tag:dts.com,2014:dash:audio_channel_configuration:2012" || schemeIdUri == "urn:dts:dash:audio_channel_configuration:2012")
                audioChannels = ParseDtsChannelConfiguration(xpp);
            else if (schemeIdUri == "tag:dts.com,2018:uhd:audio_channel_configuration") audioChannels = ParseDtsxChannelConfiguration(xpp);
            else if (schemeIdUri == "tag:dolby.com,2014:dash:audio_channel_configuration:2011" || schemeIdUri == "urn:dolby:dash:audio_channel_configuration:2011")
                audioChannels = ParseDolbyChannelConfiguration(xpp);
            else if (schemeIdUri == "tag:dolby.com,2015:dash:audio_channel_configuration:2015") audioChannels = ParseDolbyAc4ChannelConfiguration(xpp, codecs);
            else audioChannels = -1;
            do {
                xpp.Next();
            } while (!IsEndTag(xpp, "AudioChannelConfiguration"));
            return audioChannels;
        }

        int ParseSelectionFlagsFromDashRoleScheme(const std::string& value) {
            return value == "forced_subtitle" || value == "forced-subtitle" ? kSelectionFlagForced : 0;
        }
        int ParseRoleFlagsFromDashRoleScheme(const std::string& value) {
            if (value == "main") return kRoleFlagMain;
            if (value == "alternate") return kRoleFlagAlternate;
            if (value == "supplementary") return kRoleFlagSupplementary;
            if (value == "commentary") return kRoleFlagCommentary;
            if (value == "dub") return kRoleFlagDub;
            if (value == "emergency") return kRoleFlagEmergency;
            if (value == "caption") return kRoleFlagCaption;
            if (value == "forced_subtitle" || value == "forced-subtitle" || value == "subtitle") return kRoleFlagSubtitle;
            if (value == "sign") return kRoleFlagSign;
            if (value == "description") return kRoleFlagDescribesVideo;
            if (value == "enhanced-audio-intelligibility") return kRoleFlagEnhancedDialogIntelligibility;
            return 0;
        }
        int ParseTvaAudioPurposeCsValue(const std::string& value) {
            if (value == "1") return kRoleFlagDescribesVideo;   // Audio description for the visually impaired.
            if (value == "2") return kRoleFlagEnhancedDialogIntelligibility;   // Audio description for the hard of hearing.
            if (value == "3") return kRoleFlagSupplementary;   // Supplemental commentary.
            if (value == "4") return kRoleFlagCommentary;      // Director's commentary.
            if (value == "6") return kRoleFlagMain;            // Main programme audio.
            return 0;
        }
        int ParseSelectionFlagsFromRoleDescriptors(const std::vector<Descriptor>& roleDescriptors) {
            int result = 0;
            for (const Descriptor& d : roleDescriptors)
                if (EqualsIgnoreCase("urn:mpeg:dash:role:2011", d.schemeIdUri)) result |= ParseSelectionFlagsFromDashRoleScheme(d.value);
            return result;
        }
        int ParseRoleFlagsFromRoleDescriptors(const std::vector<Descriptor>& roleDescriptors) {
            int result = 0;
            for (const Descriptor& d : roleDescriptors)
                if (EqualsIgnoreCase("urn:mpeg:dash:role:2011", d.schemeIdUri)) result |= ParseRoleFlagsFromDashRoleScheme(d.value);
            return result;
        }
        int ParseRoleFlagsFromAccessibilityDescriptors(const std::vector<Descriptor>& accessibilityDescriptors) {
            int result = 0;
            for (const Descriptor& d : accessibilityDescriptors) {
                if (EqualsIgnoreCase("urn:mpeg:dash:role:2011", d.schemeIdUri)) result |= ParseRoleFlagsFromDashRoleScheme(d.value);
                else if (EqualsIgnoreCase("urn:tva:metadata:cs:AudioPurposeCS:2007", d.schemeIdUri)) result |= ParseTvaAudioPurposeCsValue(d.value);
            }
            return result;
        }
        int ParseRoleFlagsFromProperties(const std::vector<Descriptor>& properties) {
            int result = 0;
            for (const Descriptor& d : properties)
                if (EqualsIgnoreCase("http://dashif.org/guidelines/trickmode", d.schemeIdUri)) result |= kRoleFlagTrickPlay;
            return result;
        }

        std::optional<std::pair<int, int>> ParseTileCountFromProperties(const std::vector<Descriptor>& essentialProperties) {
            for (const Descriptor& d : essentialProperties) {
                if ((EqualsIgnoreCase("http://dashif.org/thumbnail_tile", d.schemeIdUri) ||
                     EqualsIgnoreCase("http://dashif.org/guidelines/thumbnail_tile", d.schemeIdUri)) &&
                    !d.value.empty()) {
                    // Util.split keeps trailing empty strings.
                    const std::size_t x = d.value.find('x');
                    if (x == std::string::npos || d.value.find('x', x + 1) != std::string::npos) continue;
                    try {
                        return std::pair{ ParseJavaInt(d.value.substr(0, x)), ParseJavaInt(d.value.substr(x + 1)) };
                    } catch (const std::exception&) {
                        // Ignore property if it's malformed.
                    }
                }
            }
            return std::nullopt;
        }

        std::string ParseEac3SupplementalProperties(const std::vector<Descriptor>& supplementalProperties) {
            for (const Descriptor& d : supplementalProperties) {
                if ((d.schemeIdUri == "tag:dolby.com,2018:dash:EC3_ExtensionType:2018" && d.value == "JOC") ||
                    (d.schemeIdUri == "tag:dolby.com,2014:dash:DolbyDigitalPlusExtensionType:2014" && d.value == "ec+3"))
                    return MimeTypes::AUDIO_E_AC3_JOC;
            }
            return MimeTypes::AUDIO_E_AC3;
        }

        std::int64_t ParseLastSegmentNumberSupplementalProperty(const std::vector<Descriptor>& supplementalProperties) {
            for (const Descriptor& d : supplementalProperties)
                if (EqualsIgnoreCase("http://dashif.org/guidelines/last-segment-number", d.schemeIdUri)) return ParseJavaLong(d.value);
            return kIndexUnset;
        }

        std::string GetSampleMimeType(const std::string& containerMimeType, const std::string& codecs) {
            if (MimeTypes::IsAudio(containerMimeType)) return MimeTypes::GetAudioMediaMimeType(codecs);
            if (MimeTypes::IsVideo(containerMimeType)) return MimeTypes::GetVideoMediaMimeType(codecs);
            if (MimeTypes::IsText(containerMimeType)) return containerMimeType;
            if (MimeTypes::IsImage(containerMimeType)) return containerMimeType;
            if (containerMimeType == MimeTypes::APPLICATION_MP4) {
                const std::string mimeType = MimeTypes::GetMediaMimeType(codecs);
                return mimeType == MimeTypes::TEXT_VTT ? std::string(MimeTypes::APPLICATION_MP4VTT) : mimeType;
            }
            return {};
        }

        std::string CheckLanguageConsistency(const std::string& firstLanguage, const std::string& secondLanguage) {
            if (firstLanguage.empty()) return secondLanguage;
            if (secondLanguage.empty()) return firstLanguage;
            if (firstLanguage != secondLanguage) throw std::logic_error("inconsistent languages in an adaptation set");
            return firstLanguage;
        }
        TrackType CheckContentTypeConsistency(TrackType firstType, TrackType secondType) {
            if (firstType == TrackType::Unknown) return secondType;
            if (secondType == TrackType::Unknown) return firstType;
            if (firstType != secondType) throw std::logic_error("inconsistent content types in an adaptation set");
            return firstType;
        }

        bool IsStandaloneTextRepresentation(const Format& format) { return !format.containerMimeType.empty() && MimeTypes::IsText(format.containerMimeType); }

        void FilterRedundantIncompleteSchemeDatas(std::vector<SchemeData>& schemeDatas) {
            for (int i = int(schemeDatas.size()) - 1; i >= 0; i--) {
                const SchemeData schemeData = schemeDatas[std::size_t(i)];
                if (!schemeData.HasData()) {
                    for (std::size_t j = 0; j < schemeDatas.size(); j++) {
                        if (schemeDatas[j].CanReplace(schemeData)) {
                            schemeDatas.erase(schemeDatas.begin() + i);
                            break;
                        }
                    }
                }
            }
        }

        void FillInClearKeyInformation(std::vector<SchemeData>& schemeDatas) {
            std::optional<std::string> clearKeyLicenseServerUrl;
            for (std::size_t i = 0; i < schemeDatas.size(); i++) {
                if (schemeDatas[i].uuid == kClearkeyUuid && !schemeDatas[i].licenseServerUrl.empty()) {
                    clearKeyLicenseServerUrl = schemeDatas[i].licenseServerUrl;
                    schemeDatas.erase(schemeDatas.begin() + std::ptrdiff_t(i));
                    break;
                }
            }
            if (!clearKeyLicenseServerUrl) return;
            for (SchemeData& schemeData : schemeDatas)
                if (schemeData.uuid == kCommonPsshUuid && schemeData.licenseServerUrl.empty())
                    schemeData = SchemeData{ kClearkeyUuid, *clearKeyLicenseServerUrl, schemeData.mimeType, schemeData.data };
        }

        // The <ContentProtection> scheme type ("" for none) and its scheme data.
        std::pair<std::string, std::optional<SchemeData>> ParseContentProtection(Xpp& xpp) {
            std::string schemeType;
            std::string licenseServerUrl;
            std::optional<std::vector<std::uint8_t>> data;
            std::optional<Uuid> uuid;
            if (const std::optional<std::string> schemeIdUri = xpp.GetAttributeValue("schemeIdUri")) {
                const std::string scheme = Lower(*schemeIdUri);
                if (scheme == "urn:mpeg:dash:mp4protection:2011") {
                    schemeType = xpp.GetAttributeValue("value").value_or("");
                    const std::string defaultKid = GetAttributeValueIgnorePrefix(xpp, "default_KID").value_or("");
                    if (!defaultKid.empty() && defaultKid != "00000000-0000-0000-0000-000000000000") {
                        std::vector<Uuid> defaultKids;
                        for (const std::string& kid : SplitOn(defaultKid, "\\s+")) defaultKids.push_back(ParseUuid(kid));
                        data = BuildPsshAtom(kCommonPsshUuid, &defaultKids, nullptr);
                        uuid = kCommonPsshUuid;
                    } else {
                        log::Warn("MpdParser", "Ignoring <ContentProtection> with schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" (ClearKey) due to missing "
                                               "required default_KID attribute.");
                    }
                } else if (scheme == "urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95") {
                    uuid = kPlayreadyUuid;
                } else if (scheme == "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed") {
                    uuid = kWidevineUuid;
                } else if (scheme == "urn:uuid:e2719d58-a985-b3c9-781a-b030af78d30e") {
                    uuid = kClearkeyUuid;
                }
            }
            do {
                xpp.Next();
                if ((IsStartTag(xpp, "clearkey:Laurl") || IsStartTag(xpp, "dashif:Laurl")) && xpp.Next() == Xpp::TEXT) {
                    licenseServerUrl = xpp.GetText();
                } else if (IsStartTag(xpp, "ms:laurl")) {
                    licenseServerUrl = xpp.GetAttributeValue("licenseUrl").value_or("");
                } else if (!data && IsStartTagIgnorePrefix(xpp, "pssh") && xpp.Next() == Xpp::TEXT) {
                    data = Base64Decode(xpp.GetText());
                    uuid = ParsePsshUuid(*data);
                    if (!uuid) {
                        log::Warn("MpdParser", "Skipping malformed cenc:pssh data");
                        data.reset();
                    }
                } else if (!data && uuid == kPlayreadyUuid && IsStartTag(xpp, "mspr:pro") && xpp.Next() == Xpp::TEXT) {
                    data = BuildPsshAtom(kPlayreadyUuid, Base64Decode(xpp.GetText()));
                } else {
                    DashManifestParser::MaybeSkipTag(xpp);
                }
            } while (!IsEndTag(xpp, "ContentProtection"));
            std::optional<SchemeData> schemeData;
            if (uuid) schemeData = SchemeData{ *uuid, licenseServerUrl, MimeTypes::VIDEO_MP4, data };
            return { schemeType, schemeData };
        }

        std::shared_ptr<SingleSegmentBase> ParseSegmentBase(Xpp& xpp, const SingleSegmentBase* parent) {
            const std::int64_t timescale = ParseLong(xpp, "timescale", parent ? parent->timescale : 1);
            const std::int64_t presentationTimeOffset = ParseLong(xpp, "presentationTimeOffset", parent ? parent->presentationTimeOffset : 0);
            std::int64_t indexStart = parent ? parent->indexStart : 0;
            std::int64_t indexLength = parent ? parent->indexLength : 0;
            if (const std::optional<std::string> indexRangeText = xpp.GetAttributeValue("indexRange")) {
                const std::vector<std::string> indexRange = SplitOn(*indexRangeText, "-");
                indexStart = ParseJavaLong(indexRange.at(0));
                indexLength = ParseJavaLong(indexRange.at(1)) - indexStart + 1;
            }
            std::optional<RangedUri> initialization = parent ? parent->initialization : std::nullopt;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "Initialization")) initialization = ParseInitialization(xpp);
                else DashManifestParser::MaybeSkipTag(xpp);
            } while (!IsEndTag(xpp, "SegmentBase"));
            return std::make_shared<SingleSegmentBase>(initialization, timescale, presentationTimeOffset, indexStart, indexLength);
        }

        struct EventAndTime {
            std::int64_t presentationTimeUs;
            EventMessage event;
        };

        std::vector<std::uint8_t> ParseEventObject(Xpp& xpp) {
            XmlSerializer xmlSerializer;
            xpp.NextToken();
            while (!IsEndTag(xpp, "Event")) {
                switch (xpp.GetEventType()) {
                    case Xpp::START_DOCUMENT: xmlSerializer.StartDocument(false); break;
                    case Xpp::END_DOCUMENT: xmlSerializer.EndDocument(); break;
                    case Xpp::START_TAG:
                        xmlSerializer.StartTag(xpp.GetNamespace(), xpp.GetName());
                        for (int i = 0; i < xpp.GetAttributeCount(); i++)
                            xmlSerializer.Attribute(xpp.GetAttributeNamespace(i), xpp.GetAttributeName(i), xpp.GetAttributeValue(i));
                        break;
                    case Xpp::END_TAG: xmlSerializer.EndTag(xpp.GetNamespace(), xpp.GetName()); break;
                    case Xpp::TEXT: xmlSerializer.Text(xpp.GetText()); break;
                    case Xpp::CDSECT: xmlSerializer.Cdsect(xpp.GetText()); break;
                    case Xpp::ENTITY_REF: xmlSerializer.EntityRef(xpp.GetName()); break;
                    case Xpp::IGNORABLE_WHITESPACE: xmlSerializer.IgnorableWhitespace(xpp.GetText()); break;
                    case Xpp::PROCESSING_INSTRUCTION: xmlSerializer.ProcessingInstruction(xpp.GetText()); break;
                    case Xpp::COMMENT: xmlSerializer.Comment(xpp.GetText()); break;
                    case Xpp::DOCDECL: xmlSerializer.Docdecl(xpp.GetText()); break;
                    default: break;
                }
                xpp.NextToken();
            }
            const std::string& out = xmlSerializer.Output();
            return std::vector<std::uint8_t>(out.begin(), out.end());
        }

        EventAndTime ParseEvent(Xpp& xpp, const std::string& schemeIdUri, const std::string& value, std::int64_t timescale, std::int64_t presentationTimeOffset) {
            const std::int64_t id = ParseLong(xpp, "id", 0);
            const std::int64_t duration = ParseLong(xpp, "duration", kTimeUnset);
            const std::int64_t presentationTime = ParseLong(xpp, "presentationTime", 0);
            const std::int64_t durationMs = ScaleLargeTimestamp(duration, 1000, timescale);
            const std::int64_t presentationTimesUs = ScaleLargeTimestamp(presentationTime - presentationTimeOffset, kMicrosPerSecond, timescale);
            const std::optional<std::string> messageData = xpp.GetAttributeValue("messageData");
            std::vector<std::uint8_t> eventObject = ParseEventObject(xpp);
            EventMessage event{ schemeIdUri, value, durationMs, id,
                                messageData ? std::vector<std::uint8_t>(messageData->begin(), messageData->end()) : std::move(eventObject) };
            return { presentationTimesUs, std::move(event) };
        }

        EventStream ParseEventStream(Xpp& xpp) {
            EventStream stream;
            stream.schemeIdUri = ParseString(xpp, "schemeIdUri", "");
            stream.value = ParseString(xpp, "value", "");
            stream.timescale = ParseLong(xpp, "timescale", 1);
            const std::int64_t presentationTimeOffset = ParseLong(xpp, "presentationTimeOffset", 0);
            do {
                xpp.Next();
                if (IsStartTag(xpp, "Event")) {
                    EventAndTime event = ParseEvent(xpp, stream.schemeIdUri, stream.value, stream.timescale, presentationTimeOffset);
                    stream.presentationTimesUs.push_back(event.presentationTimeUs);
                    stream.events.push_back(std::move(event.event));
                } else {
                    DashManifestParser::MaybeSkipTag(xpp);
                }
            } while (!IsEndTag(xpp, "EventStream"));
            return stream;
        }

        UtcTimingElement ParseUtcTiming(const Xpp& xpp) {
            return { xpp.GetAttributeValue("schemeIdUri").value_or(""), xpp.GetAttributeValue("value").value_or("") };
        }

        ServiceDescriptionElement ParseServiceDescription(Xpp& xpp) {
            ServiceDescriptionElement d;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "Latency")) {
                    d.targetOffsetMs = ParseLong(xpp, "target", kTimeUnset);
                    d.minOffsetMs = ParseLong(xpp, "min", kTimeUnset);
                    d.maxOffsetMs = ParseLong(xpp, "max", kTimeUnset);
                } else if (IsStartTag(xpp, "PlaybackRate")) {
                    d.minPlaybackSpeed = ParseFloat(xpp, "min", kRateUnset);
                    d.maxPlaybackSpeed = ParseFloat(xpp, "max", kRateUnset);
                }
            } while (!IsEndTag(xpp, "ServiceDescription"));
            return d;
        }

        ProgramInformation ParseProgramInformation(Xpp& xpp) {
            ProgramInformation p;
            p.moreInformationURL = ParseString(xpp, "moreInformationURL", "");
            p.lang = ParseString(xpp, "lang", "");
            do {
                xpp.Next();
                if (IsStartTag(xpp, "Title")) p.title = xpp.NextText();
                else if (IsStartTag(xpp, "Source")) p.source = xpp.NextText();
                else if (IsStartTag(xpp, "Copyright")) p.copyright = xpp.NextText();
                else DashManifestParser::MaybeSkipTag(xpp);
            } while (!IsEndTag(xpp, "ProgramInformation"));
            return p;
        }

        struct RepresentationInfo {
            Format format;
            std::vector<BaseUrl> baseUrls;
            std::shared_ptr<const SegmentBase> segmentBase;
            std::string drmSchemeType;
            std::vector<SchemeData> drmSchemeDatas;
            std::vector<Descriptor> inbandEventStreams;
            std::vector<Descriptor> essentialProperties;
            std::vector<Descriptor> supplementalProperties;
            std::int64_t revisionId;
        };

        // Upstream's parser state: the methods DashManifestParser reads an MPD with.
        class Reader {
        public:
            explicit Reader(const DashManifestParser& parser) : m_Parser(parser) {}

            std::shared_ptr<DashManifest> ParseMediaPresentationDescription(Xpp& xpp, const std::string& documentBaseUri);

        private:
            std::pair<Period, std::int64_t> ParsePeriod(Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, std::int64_t defaultStartMs,
                                                        std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t availabilityStartTimeMs,
                                                        std::int64_t timeShiftBufferDepthMs, bool dvbProfileDeclared);
            AdaptationSet ParseAdaptationSet(Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, std::shared_ptr<const SegmentBase> segmentBase,
                                             std::int64_t periodDurationMs, std::int64_t baseUrlAvailabilityTimeOffsetUs,
                                             std::int64_t segmentBaseAvailabilityTimeOffsetUs, std::int64_t periodStartUnixTimeMs,
                                             std::int64_t timeShiftBufferDepthMs, bool dvbProfileDeclared);
            RepresentationInfo ParseRepresentation(Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, const std::string& adaptationSetMimeType,
                                                   const std::string& adaptationSetCodecs, const std::string& adaptationSetSupplementalCodecs,
                                                   const std::string& adaptationSetSupplementalProfiles, int adaptationSetWidth, int adaptationSetHeight,
                                                   float adaptationSetFrameRate, int adaptationSetAudioChannels, int adaptationSetAudioSamplingRate,
                                                   const std::string& adaptationSetLanguage, const std::vector<Descriptor>& adaptationSetRoleDescriptors,
                                                   const std::vector<Descriptor>& adaptationSetAccessibilityDescriptors,
                                                   const std::vector<Descriptor>& adaptationSetEssentialProperties,
                                                   const std::vector<Descriptor>& adaptationSetSupplementalProperties, std::shared_ptr<const SegmentBase> segmentBase,
                                                   std::int64_t periodStartUnixTimeMs, std::int64_t periodDurationMs, std::int64_t baseUrlAvailabilityTimeOffsetUs,
                                                   std::int64_t segmentBaseAvailabilityTimeOffsetUs, std::int64_t timeShiftBufferDepthMs, bool dvbProfileDeclared);
            Format BuildFormat(const std::string& id, const std::string& containerMimeType, int width, int height, float frameRate, int audioChannels,
                               int audioSamplingRate, int bitrate, const std::string& language, const std::vector<Descriptor>& roleDescriptors,
                               const std::vector<Descriptor>& accessibilityDescriptors, std::string codecs, const std::string& supplementalCodecs,
                               const std::string& supplementalProfiles, const std::vector<Descriptor>& essentialProperties,
                               const std::vector<Descriptor>& supplementalProperties);
            std::shared_ptr<Representation> BuildRepresentation(RepresentationInfo& info, const std::string& label, const std::vector<Label>& labels,
                                                                const std::string& extraDrmSchemeType, const std::vector<SchemeData>& extraDrmSchemeDatas,
                                                                const std::vector<Descriptor>& extraInbandEventStreams);
            std::shared_ptr<SegmentList> ParseSegmentList(Xpp& xpp, const SegmentList* parent, std::int64_t periodStartUnixTimeMs, std::int64_t periodDurationMs,
                                                          std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t segmentBaseAvailabilityTimeOffsetUs,
                                                          std::int64_t timeShiftBufferDepthMs);
            std::shared_ptr<SegmentTemplate> ParseSegmentTemplate(Xpp& xpp, const SegmentTemplate* parent,
                                                                  const std::vector<Descriptor>& adaptationSetSupplementalProperties,
                                                                  std::int64_t periodStartUnixTimeMs, std::int64_t periodDurationMs,
                                                                  std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t segmentBaseAvailabilityTimeOffsetUs,
                                                                  std::int64_t timeShiftBufferDepthMs);

            const DashManifestParser& m_Parser;
        };

        template<typename T>
        const T* As(const std::shared_ptr<const SegmentBase>& base, SegmentBase::Kind kind) {
            // Upstream casts the inherited segment base to the element's type; a different kind
            // there is a malformed manifest (a ClassCastException upstream).
            if (!base) return nullptr;
            if (base->kind != kind) throw ParserException("a segment base element of a different kind than its parent's");
            return static_cast<const T*>(base.get());
        }

        std::shared_ptr<DashManifest> Reader::ParseMediaPresentationDescription(Xpp& xpp, const std::string& documentBaseUri) {
            const bool dvbProfileDeclared = IsDvbProfileDeclared(ParseProfiles(xpp, "profiles"));
            const std::int64_t availabilityStartTime = ParseDateTime(xpp, "availabilityStartTime", kTimeUnset);
            std::int64_t durationMs = ParseDuration(xpp, "mediaPresentationDuration", kTimeUnset);
            const std::int64_t minBufferTimeMs = ParseDuration(xpp, "minBufferTime", kTimeUnset);
            const bool dynamic = xpp.GetAttributeValue("type") == std::optional<std::string>("dynamic");
            const std::int64_t minUpdateTimeMs = dynamic ? ParseDuration(xpp, "minimumUpdatePeriod", kTimeUnset) : kTimeUnset;
            const std::int64_t timeShiftBufferDepthMs = dynamic ? ParseDuration(xpp, "timeShiftBufferDepth", kTimeUnset) : kTimeUnset;
            const std::int64_t suggestedPresentationDelayMs = dynamic ? ParseDuration(xpp, "suggestedPresentationDelay", kTimeUnset) : kTimeUnset;
            const std::int64_t publishTimeMs = ParseDateTime(xpp, "publishTime", kTimeUnset);
            std::optional<ProgramInformation> programInformation;
            std::optional<UtcTimingElement> utcTiming;
            std::vector<Location> locations;
            std::optional<ServiceDescriptionElement> serviceDescription;
            std::int64_t baseUrlAvailabilityTimeOffsetUs = dynamic ? 0 : kTimeUnset;
            const BaseUrl documentBaseUrl(documentBaseUri, documentBaseUri, dvbProfileDeclared ? BaseUrl::DEFAULT_DVB_PRIORITY : BaseUrl::PRIORITY_UNSET,
                                          BaseUrl::DEFAULT_WEIGHT);
            const std::vector<BaseUrl> parentBaseUrls = { documentBaseUrl };
            std::vector<Period> periods;
            std::vector<BaseUrl> baseUrls;
            std::int64_t nextPeriodStartMs = dynamic ? kTimeUnset : 0;
            bool seenEarlyAccessPeriod = false;
            bool seenFirstBaseUrl = false;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "BaseURL")) {
                    if (!seenFirstBaseUrl) {
                        baseUrlAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, baseUrlAvailabilityTimeOffsetUs);
                        seenFirstBaseUrl = true;
                    }
                    std::vector<BaseUrl> parsed = ParseBaseUrl(xpp, parentBaseUrls, dvbProfileDeclared);
                    baseUrls.insert(baseUrls.end(), parsed.begin(), parsed.end());
                } else if (IsStartTag(xpp, "ProgramInformation")) {
                    programInformation = ParseProgramInformation(xpp);
                } else if (IsStartTag(xpp, "UTCTiming")) {
                    utcTiming = ParseUtcTiming(xpp);
                } else if (IsStartTag(xpp, "Location")) {
                    const std::optional<std::string> serviceLocation = xpp.GetAttributeValue("serviceLocation");
                    const std::string locationUrlString = xpp.NextText();
                    const std::string resolvedUrl = ResolveUri(documentBaseUri, locationUrlString);
                    locations.push_back(serviceLocation ? Location(resolvedUrl, *serviceLocation) : Location(resolvedUrl));
                } else if (IsStartTag(xpp, "ServiceDescription")) {
                    serviceDescription = ParseServiceDescription(xpp);
                } else if (IsStartTag(xpp, "Period") && !seenEarlyAccessPeriod) {
                    auto [period, periodDurationMs] = ParsePeriod(xpp, !baseUrls.empty() ? baseUrls : parentBaseUrls, nextPeriodStartMs,
                                                                  baseUrlAvailabilityTimeOffsetUs, availabilityStartTime, timeShiftBufferDepthMs, dvbProfileDeclared);
                    if (period.startMs == kTimeUnset) {
                        // This is an early access period. Ignore it. All subsequent periods must also be early access.
                        if (dynamic) seenEarlyAccessPeriod = true;
                        else throw ParserException("Unable to determine start of period " + std::to_string(periods.size()));
                    } else {
                        nextPeriodStartMs = periodDurationMs == kTimeUnset ? kTimeUnset : period.startMs + periodDurationMs;
                        periods.push_back(std::move(period));
                    }
                } else {
                    DashManifestParser::MaybeSkipTag(xpp);
                }
            } while (!IsEndTag(xpp, "MPD"));
            if (durationMs == kTimeUnset) {
                if (nextPeriodStartMs != kTimeUnset) durationMs = nextPeriodStartMs;
                else if (!dynamic) throw ParserException("Unable to determine duration of static manifest.");
            }
            if (periods.empty()) throw ParserException("No periods found.");
            return std::make_shared<DashManifest>(availabilityStartTime, durationMs, minBufferTimeMs, dynamic, minUpdateTimeMs, timeShiftBufferDepthMs,
                                                  suggestedPresentationDelayMs, publishTimeMs, std::move(programInformation), std::move(utcTiming),
                                                  std::move(serviceDescription), std::move(periods), std::move(locations));
        }

        std::pair<Period, std::int64_t> Reader::ParsePeriod(Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, std::int64_t defaultStartMs,
                                                            std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t availabilityStartTimeMs,
                                                            std::int64_t timeShiftBufferDepthMs, bool dvbProfileDeclared) {
            Period period;
            period.id = xpp.GetAttributeValue("id").value_or("");
            period.startMs = ParseDuration(xpp, "start", defaultStartMs);
            const std::int64_t periodStartUnixTimeMs = availabilityStartTimeMs != kTimeUnset ? availabilityStartTimeMs + period.startMs : kTimeUnset;
            const std::int64_t durationMs = ParseDuration(xpp, "duration", kTimeUnset);
            std::shared_ptr<const SegmentBase> segmentBase;
            std::vector<BaseUrl> baseUrls;
            bool seenFirstBaseUrl = false;
            std::int64_t segmentBaseAvailabilityTimeOffsetUs = kTimeUnset;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "BaseURL")) {
                    if (!seenFirstBaseUrl) {
                        baseUrlAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, baseUrlAvailabilityTimeOffsetUs);
                        seenFirstBaseUrl = true;
                    }
                    std::vector<BaseUrl> parsed = ParseBaseUrl(xpp, parentBaseUrls, dvbProfileDeclared);
                    baseUrls.insert(baseUrls.end(), parsed.begin(), parsed.end());
                } else if (IsStartTag(xpp, "AdaptationSet")) {
                    period.adaptationSets.push_back(ParseAdaptationSet(xpp, !baseUrls.empty() ? baseUrls : parentBaseUrls, segmentBase, durationMs,
                                                                       baseUrlAvailabilityTimeOffsetUs, segmentBaseAvailabilityTimeOffsetUs,
                                                                       periodStartUnixTimeMs, timeShiftBufferDepthMs, dvbProfileDeclared));
                } else if (IsStartTag(xpp, "EventStream")) {
                    period.eventStreams.push_back(ParseEventStream(xpp));
                } else if (IsStartTag(xpp, "SegmentBase")) {
                    segmentBase = ParseSegmentBase(xpp, nullptr);
                } else if (IsStartTag(xpp, "SegmentList")) {
                    segmentBaseAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, kTimeUnset);
                    segmentBase = ParseSegmentList(xpp, nullptr, periodStartUnixTimeMs, durationMs, baseUrlAvailabilityTimeOffsetUs,
                                                   segmentBaseAvailabilityTimeOffsetUs, timeShiftBufferDepthMs);
                } else if (IsStartTag(xpp, "SegmentTemplate")) {
                    segmentBaseAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, kTimeUnset);
                    segmentBase = ParseSegmentTemplate(xpp, nullptr, {}, periodStartUnixTimeMs, durationMs, baseUrlAvailabilityTimeOffsetUs,
                                                       segmentBaseAvailabilityTimeOffsetUs, timeShiftBufferDepthMs);
                } else if (IsStartTag(xpp, "AssetIdentifier")) {
                    period.assetIdentifier = ParseDescriptor(xpp, "AssetIdentifier");
                } else {
                    DashManifestParser::MaybeSkipTag(xpp);
                }
            } while (!IsEndTag(xpp, "Period"));
            return { std::move(period), durationMs };
        }

        AdaptationSet Reader::ParseAdaptationSet(Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, std::shared_ptr<const SegmentBase> segmentBase,
                                                 std::int64_t periodDurationMs, std::int64_t baseUrlAvailabilityTimeOffsetUs,
                                                 std::int64_t segmentBaseAvailabilityTimeOffsetUs, std::int64_t periodStartUnixTimeMs,
                                                 std::int64_t timeShiftBufferDepthMs, bool dvbProfileDeclared) {
            const std::int64_t id = ParseLong(xpp, "id", AdaptationSet::ID_UNSET);
            TrackType contentType = ParseContentType(xpp);
            const std::string mimeType = xpp.GetAttributeValue("mimeType").value_or("");
            const std::string codecs = xpp.GetAttributeValue("codecs").value_or("");
            const std::string supplementalCodecs = xpp.GetAttributeValue("scte214:supplementalCodecs").value_or("");
            const std::string supplementalProfiles = xpp.GetAttributeValue("scte214:supplementalProfiles").value_or("");
            const int width = ParseInt(xpp, "width", -1);
            const int height = ParseInt(xpp, "height", -1);
            const float frameRate = ParseFrameRate(xpp, -1);
            int audioChannels = -1;
            const int audioSamplingRate = ParseInt(xpp, "audioSamplingRate", -1);
            std::string language = xpp.GetAttributeValue("lang").value_or("");
            const std::string label = xpp.GetAttributeValue("label").value_or("");
            std::vector<Label> labels;
            std::string drmSchemeType;
            std::vector<SchemeData> drmSchemeDatas;
            std::vector<Descriptor> inbandEventStreams, accessibilityDescriptors, roleDescriptors, essentialProperties, supplementalProperties;
            std::vector<RepresentationInfo> representationInfos;
            std::vector<BaseUrl> baseUrls;
            bool seenFirstBaseUrl = false;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "BaseURL")) {
                    if (!seenFirstBaseUrl) {
                        baseUrlAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, baseUrlAvailabilityTimeOffsetUs);
                        seenFirstBaseUrl = true;
                    }
                    std::vector<BaseUrl> parsed = ParseBaseUrl(xpp, parentBaseUrls, dvbProfileDeclared);
                    baseUrls.insert(baseUrls.end(), parsed.begin(), parsed.end());
                } else if (IsStartTag(xpp, "ContentProtection")) {
                    auto [schemeType, schemeData] = ParseContentProtection(xpp);
                    if (!schemeType.empty()) drmSchemeType = schemeType;
                    if (schemeData) drmSchemeDatas.push_back(std::move(*schemeData));
                } else if (IsStartTag(xpp, "ContentComponent")) {
                    language = CheckLanguageConsistency(language, xpp.GetAttributeValue("lang").value_or(""));
                    contentType = CheckContentTypeConsistency(contentType, ParseContentType(xpp));
                } else if (IsStartTag(xpp, "Role")) {
                    roleDescriptors.push_back(ParseDescriptor(xpp, "Role"));
                } else if (IsStartTag(xpp, "AudioChannelConfiguration")) {
                    audioChannels = ParseAudioChannelConfiguration(xpp, codecs);
                } else if (IsStartTag(xpp, "Accessibility")) {
                    accessibilityDescriptors.push_back(ParseDescriptor(xpp, "Accessibility"));
                } else if (IsStartTag(xpp, "EssentialProperty")) {
                    essentialProperties.push_back(ParseDescriptor(xpp, "EssentialProperty"));
                } else if (IsStartTag(xpp, "SupplementalProperty")) {
                    supplementalProperties.push_back(ParseDescriptor(xpp, "SupplementalProperty"));
                } else if (IsStartTag(xpp, "Representation")) {
                    RepresentationInfo info = ParseRepresentation(
                        xpp, !baseUrls.empty() ? baseUrls : parentBaseUrls, mimeType, codecs, supplementalCodecs, supplementalProfiles, width, height, frameRate,
                        audioChannels, audioSamplingRate, language, roleDescriptors, accessibilityDescriptors, essentialProperties, supplementalProperties,
                        segmentBase, periodStartUnixTimeMs, periodDurationMs, baseUrlAvailabilityTimeOffsetUs, segmentBaseAvailabilityTimeOffsetUs,
                        timeShiftBufferDepthMs, dvbProfileDeclared);
                    contentType = CheckContentTypeConsistency(contentType, MimeTypes::GetTrackType(info.format.sampleMimeType));
                    representationInfos.push_back(std::move(info));
                } else if (IsStartTag(xpp, "SegmentBase")) {
                    segmentBase = ParseSegmentBase(xpp, As<SingleSegmentBase>(segmentBase, SegmentBase::Kind::Single));
                } else if (IsStartTag(xpp, "SegmentList")) {
                    segmentBaseAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, segmentBaseAvailabilityTimeOffsetUs);
                    segmentBase = ParseSegmentList(xpp, As<SegmentList>(segmentBase, SegmentBase::Kind::List), periodStartUnixTimeMs, periodDurationMs,
                                                   baseUrlAvailabilityTimeOffsetUs, segmentBaseAvailabilityTimeOffsetUs, timeShiftBufferDepthMs);
                } else if (IsStartTag(xpp, "SegmentTemplate")) {
                    segmentBaseAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, segmentBaseAvailabilityTimeOffsetUs);
                    segmentBase = ParseSegmentTemplate(xpp, As<SegmentTemplate>(segmentBase, SegmentBase::Kind::Template), supplementalProperties,
                                                       periodStartUnixTimeMs, periodDurationMs, baseUrlAvailabilityTimeOffsetUs,
                                                       segmentBaseAvailabilityTimeOffsetUs, timeShiftBufferDepthMs);
                } else if (IsStartTag(xpp, "InbandEventStream")) {
                    inbandEventStreams.push_back(ParseDescriptor(xpp, "InbandEventStream"));
                } else if (IsStartTag(xpp, "Label")) {
                    labels.push_back(m_Parser.ParseLabel(xpp));
                } else if (IsStartTag(xpp)) {
                    DashManifestParser::MaybeSkipTag(xpp);   // parseAdaptationSetChild
                }
            } while (!IsEndTag(xpp, "AdaptationSet"));
            AdaptationSet set;
            set.id = id;
            for (RepresentationInfo& info : representationInfos)
                set.representations.push_back(BuildRepresentation(info, label, labels, drmSchemeType, drmSchemeDatas, inbandEventStreams));
            set.type = contentType;
            set.accessibilityDescriptors = std::move(accessibilityDescriptors);
            set.essentialProperties = std::move(essentialProperties);
            set.supplementalProperties = std::move(supplementalProperties);
            return set;
        }

        RepresentationInfo Reader::ParseRepresentation(
            Xpp& xpp, const std::vector<BaseUrl>& parentBaseUrls, const std::string& adaptationSetMimeType, const std::string& adaptationSetCodecs,
            const std::string& adaptationSetSupplementalCodecs, const std::string& adaptationSetSupplementalProfiles, int adaptationSetWidth, int adaptationSetHeight,
            float adaptationSetFrameRate, int adaptationSetAudioChannels, int adaptationSetAudioSamplingRate, const std::string& adaptationSetLanguage,
            const std::vector<Descriptor>& adaptationSetRoleDescriptors, const std::vector<Descriptor>& adaptationSetAccessibilityDescriptors,
            const std::vector<Descriptor>& adaptationSetEssentialProperties, const std::vector<Descriptor>& adaptationSetSupplementalProperties,
            std::shared_ptr<const SegmentBase> segmentBase, std::int64_t periodStartUnixTimeMs, std::int64_t periodDurationMs,
            std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t segmentBaseAvailabilityTimeOffsetUs, std::int64_t timeShiftBufferDepthMs,
            bool dvbProfileDeclared) {
            const std::string id = xpp.GetAttributeValue("id").value_or("");
            const int bandwidth = ParseInt(xpp, "bandwidth", -1);
            const std::string mimeType = ParseString(xpp, "mimeType", adaptationSetMimeType);
            const std::string codecs = ParseString(xpp, "codecs", adaptationSetCodecs);
            const std::string supplementalCodecs = ParseString(xpp, "scte214:supplementalCodecs", adaptationSetSupplementalCodecs);
            const std::string supplementalProfiles = ParseString(xpp, "scte214:supplementalProfiles", adaptationSetSupplementalProfiles);
            const int width = ParseInt(xpp, "width", adaptationSetWidth);
            const int height = ParseInt(xpp, "height", adaptationSetHeight);
            const float frameRate = ParseFrameRate(xpp, adaptationSetFrameRate);
            int audioChannels = adaptationSetAudioChannels;
            const int audioSamplingRate = ParseInt(xpp, "audioSamplingRate", adaptationSetAudioSamplingRate);
            std::string drmSchemeType;
            std::vector<SchemeData> drmSchemeDatas;
            std::vector<Descriptor> inbandEventStreams;
            std::vector<Descriptor> essentialProperties = adaptationSetEssentialProperties;
            std::vector<Descriptor> supplementalProperties = adaptationSetSupplementalProperties;
            std::vector<BaseUrl> baseUrls;
            bool seenFirstBaseUrl = false;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "BaseURL")) {
                    if (!seenFirstBaseUrl) {
                        baseUrlAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, baseUrlAvailabilityTimeOffsetUs);
                        seenFirstBaseUrl = true;
                    }
                    std::vector<BaseUrl> parsed = ParseBaseUrl(xpp, parentBaseUrls, dvbProfileDeclared);
                    baseUrls.insert(baseUrls.end(), parsed.begin(), parsed.end());
                } else if (IsStartTag(xpp, "AudioChannelConfiguration")) {
                    audioChannels = ParseAudioChannelConfiguration(xpp, codecs);
                } else if (IsStartTag(xpp, "SegmentBase")) {
                    segmentBase = ParseSegmentBase(xpp, As<SingleSegmentBase>(segmentBase, SegmentBase::Kind::Single));
                } else if (IsStartTag(xpp, "SegmentList")) {
                    segmentBaseAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, segmentBaseAvailabilityTimeOffsetUs);
                    segmentBase = ParseSegmentList(xpp, As<SegmentList>(segmentBase, SegmentBase::Kind::List), periodStartUnixTimeMs, periodDurationMs,
                                                   baseUrlAvailabilityTimeOffsetUs, segmentBaseAvailabilityTimeOffsetUs, timeShiftBufferDepthMs);
                } else if (IsStartTag(xpp, "SegmentTemplate")) {
                    segmentBaseAvailabilityTimeOffsetUs = ParseAvailabilityTimeOffsetUs(xpp, segmentBaseAvailabilityTimeOffsetUs);
                    segmentBase = ParseSegmentTemplate(xpp, As<SegmentTemplate>(segmentBase, SegmentBase::Kind::Template), adaptationSetSupplementalProperties,
                                                       periodStartUnixTimeMs, periodDurationMs, baseUrlAvailabilityTimeOffsetUs,
                                                       segmentBaseAvailabilityTimeOffsetUs, timeShiftBufferDepthMs);
                } else if (IsStartTag(xpp, "ContentProtection")) {
                    auto [schemeType, schemeData] = ParseContentProtection(xpp);
                    if (!schemeType.empty()) drmSchemeType = schemeType;
                    if (schemeData) drmSchemeDatas.push_back(std::move(*schemeData));
                } else if (IsStartTag(xpp, "InbandEventStream")) {
                    inbandEventStreams.push_back(ParseDescriptor(xpp, "InbandEventStream"));
                } else if (IsStartTag(xpp, "EssentialProperty")) {
                    essentialProperties.push_back(ParseDescriptor(xpp, "EssentialProperty"));
                } else if (IsStartTag(xpp, "SupplementalProperty")) {
                    supplementalProperties.push_back(ParseDescriptor(xpp, "SupplementalProperty"));
                } else {
                    DashManifestParser::MaybeSkipTag(xpp);
                }
            } while (!IsEndTag(xpp, "Representation"));
            Format format = BuildFormat(id, mimeType, width, height, frameRate, audioChannels, audioSamplingRate, bandwidth, adaptationSetLanguage,
                                        adaptationSetRoleDescriptors, adaptationSetAccessibilityDescriptors, codecs, supplementalCodecs, supplementalProfiles,
                                        essentialProperties, supplementalProperties);
            if (!segmentBase) segmentBase = std::make_shared<SingleSegmentBase>();
            if (IsStandaloneTextRepresentation(format) && segmentBase->presentationTimeOffset != 0) segmentBase = segmentBase->CopyWithPresentationTimeOffset(0);
            return RepresentationInfo{ std::move(format),
                                       !baseUrls.empty() ? baseUrls : parentBaseUrls,
                                       segmentBase,
                                       drmSchemeType,
                                       std::move(drmSchemeDatas),
                                       std::move(inbandEventStreams),
                                       std::move(essentialProperties),
                                       std::move(supplementalProperties),
                                       Representation::REVISION_ID_DEFAULT };
        }

        Format Reader::BuildFormat(const std::string& id, const std::string& containerMimeType, int width, int height, float frameRate, int audioChannels,
                                   int audioSamplingRate, int bitrate, const std::string& language, const std::vector<Descriptor>& roleDescriptors,
                                   const std::vector<Descriptor>& accessibilityDescriptors, std::string codecs, const std::string& supplementalCodecs,
                                   const std::string& supplementalProfiles, const std::vector<Descriptor>& essentialProperties,
                                   const std::vector<Descriptor>& supplementalProperties) {
            std::string sampleMimeType = GetSampleMimeType(containerMimeType, codecs);
            if (sampleMimeType == MimeTypes::AUDIO_E_AC3) {
                sampleMimeType = ParseEac3SupplementalProperties(supplementalProperties);
                if (sampleMimeType == MimeTypes::AUDIO_E_AC3_JOC) codecs = MimeTypes::CODEC_E_AC3_JOC;
            }
            Format format;
            if (MimeTypes::IsDolbyVisionCodec(codecs, supplementalCodecs)) {
                sampleMimeType = MimeTypes::VIDEO_DOLBY_VISION;
                ApplyColorInfoForDolbyVision(format, codecs, supplementalCodecs, supplementalProfiles);
                codecs = !supplementalCodecs.empty() ? supplementalCodecs : codecs;
            }
            int roleFlags = ParseRoleFlagsFromRoleDescriptors(roleDescriptors);
            roleFlags |= ParseRoleFlagsFromAccessibilityDescriptors(accessibilityDescriptors);
            roleFlags |= ParseRoleFlagsFromProperties(essentialProperties);
            roleFlags |= ParseRoleFlagsFromProperties(supplementalProperties);
            const std::optional<std::pair<int, int>> tileCounts = ParseTileCountFromProperties(essentialProperties);
            format.id = id;
            format.containerMimeType = containerMimeType;
            format.sampleMimeType = sampleMimeType;
            format.codecs = codecs;
            format.SetBitrates(-1, bitrate);
            format.selectionFlags = ParseSelectionFlagsFromRoleDescriptors(roleDescriptors);
            format.roleFlags = roleFlags;
            format.language = language;
            format.tileCountHorizontal = tileCounts ? tileCounts->first : -1;
            format.tileCountVertical = tileCounts ? tileCounts->second : -1;
            if (MimeTypes::IsVideo(sampleMimeType)) {
                format.width = width;
                format.height = height;
                format.frameRate = frameRate;
            } else if (MimeTypes::IsAudio(sampleMimeType)) {
                format.channelCount = audioChannels;
                format.sampleRate = audioSamplingRate;
            } else if (MimeTypes::IsText(sampleMimeType)) {
                int accessibilityChannel = -1;
                if (sampleMimeType == MimeTypes::APPLICATION_CEA608) accessibilityChannel = DashManifestParser::ParseCea608AccessibilityChannel(accessibilityDescriptors);
                else if (sampleMimeType == MimeTypes::APPLICATION_CEA708)
                    accessibilityChannel = DashManifestParser::ParseCea708AccessibilityChannel(accessibilityDescriptors);
                format.accessibilityChannel = accessibilityChannel;
            } else if (MimeTypes::IsImage(sampleMimeType)) {
                format.width = width;
                format.height = height;
            }
            FinishFormat(format);
            return format;
        }

        std::shared_ptr<Representation> Reader::BuildRepresentation(RepresentationInfo& info, const std::string& label, const std::vector<Label>& labels,
                                                                    const std::string& extraDrmSchemeType, const std::vector<SchemeData>& extraDrmSchemeDatas,
                                                                    const std::vector<Descriptor>& extraInbandEventStreams) {
            Format format = info.format;   // buildUpon(): the builder starts from the built format's fields
            if (!label.empty() && labels.empty()) {
                format.label = label;
                format.labels.clear();
            } else {
                format.labels = labels;
                format.label.clear();
            }
            FinishFormat(format);
            std::string drmSchemeType = info.drmSchemeType;
            if (drmSchemeType.empty()) drmSchemeType = extraDrmSchemeType;
            std::vector<SchemeData> drmSchemeDatas = info.drmSchemeDatas;
            drmSchemeDatas.insert(drmSchemeDatas.end(), extraDrmSchemeDatas.begin(), extraDrmSchemeDatas.end());
            if (!drmSchemeDatas.empty()) {
                FillInClearKeyInformation(drmSchemeDatas);
                FilterRedundantIncompleteSchemeDatas(drmSchemeDatas);
                format.drmInitData = DrmInitData(drmSchemeType, drmSchemeDatas);
            }
            std::vector<Descriptor> inbandEventStreams = info.inbandEventStreams;
            inbandEventStreams.insert(inbandEventStreams.end(), extraInbandEventStreams.begin(), extraInbandEventStreams.end());
            return Representation::NewInstance(info.revisionId, std::move(format), info.baseUrls, info.segmentBase, std::move(inbandEventStreams),
                                               info.essentialProperties, info.supplementalProperties, "");
        }

        std::shared_ptr<SegmentList> Reader::ParseSegmentList(Xpp& xpp, const SegmentList* parent, std::int64_t periodStartUnixTimeMs, std::int64_t periodDurationMs,
                                                              std::int64_t baseUrlAvailabilityTimeOffsetUs, std::int64_t segmentBaseAvailabilityTimeOffsetUs,
                                                              std::int64_t timeShiftBufferDepthMs) {
            const std::int64_t timescale = ParseLong(xpp, "timescale", parent ? parent->timescale : 1);
            const std::int64_t presentationTimeOffset = ParseLong(xpp, "presentationTimeOffset", parent ? parent->presentationTimeOffset : 0);
            const std::int64_t duration = ParseLong(xpp, "duration", parent ? parent->duration : kTimeUnset);
            const std::int64_t startNumber = ParseLong(xpp, "startNumber", parent ? parent->startNumber : 1);
            const std::int64_t availabilityTimeOffsetUs = GetFinalAvailabilityTimeOffset(baseUrlAvailabilityTimeOffsetUs, segmentBaseAvailabilityTimeOffsetUs);
            std::optional<RangedUri> initialization;
            std::optional<std::vector<SegmentTimelineElement>> timeline;
            std::optional<std::vector<RangedUri>> segments;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "Initialization")) {
                    initialization = ParseInitialization(xpp);
                } else if (IsStartTag(xpp, "SegmentTimeline")) {
                    timeline = m_Parser.ParseSegmentTimeline(xpp, timescale, periodDurationMs);
                } else if (IsStartTag(xpp, "SegmentURL")) {
                    if (!segments) segments.emplace();
                    segments->push_back(ParseSegmentUrl(xpp));
                } else {
                    DashManifestParser::MaybeSkipTag(xpp);
                }
            } while (!IsEndTag(xpp, "SegmentList"));
            if (parent) {
                if (!initialization) initialization = parent->initialization;
                if (!timeline) timeline = parent->segmentTimeline;
                if (!segments) segments = parent->mediaSegments;
            }
            return std::make_shared<SegmentList>(initialization, timescale, presentationTimeOffset, startNumber, duration, timeline, availabilityTimeOffsetUs,
                                                 segments, MsToUs(timeShiftBufferDepthMs), MsToUs(periodStartUnixTimeMs));
        }

        std::shared_ptr<SegmentTemplate> Reader::ParseSegmentTemplate(Xpp& xpp, const SegmentTemplate* parent,
                                                                      const std::vector<Descriptor>& adaptationSetSupplementalProperties,
                                                                      std::int64_t periodStartUnixTimeMs, std::int64_t periodDurationMs,
                                                                      std::int64_t baseUrlAvailabilityTimeOffsetUs,
                                                                      std::int64_t segmentBaseAvailabilityTimeOffsetUs, std::int64_t timeShiftBufferDepthMs) {
            const std::int64_t timescale = ParseLong(xpp, "timescale", parent ? parent->timescale : 1);
            const std::int64_t presentationTimeOffset = ParseLong(xpp, "presentationTimeOffset", parent ? parent->presentationTimeOffset : 0);
            const std::int64_t duration = ParseLong(xpp, "duration", parent ? parent->duration : kTimeUnset);
            const std::int64_t startNumber = ParseLong(xpp, "startNumber", parent ? parent->startNumber : 1);
            const std::int64_t endNumber = ParseLastSegmentNumberSupplementalProperty(adaptationSetSupplementalProperties);
            const std::int64_t availabilityTimeOffsetUs = GetFinalAvailabilityTimeOffset(baseUrlAvailabilityTimeOffsetUs, segmentBaseAvailabilityTimeOffsetUs);
            const std::optional<UrlTemplate> mediaTemplate = ParseUrlTemplate(xpp, "media", parent ? parent->mediaTemplate : std::nullopt);
            const std::optional<UrlTemplate> initializationTemplate =
                ParseUrlTemplate(xpp, "initialization", parent ? parent->initializationTemplate : std::nullopt);
            std::optional<RangedUri> initialization;
            std::optional<std::vector<SegmentTimelineElement>> timeline;
            do {
                xpp.Next();
                if (IsStartTag(xpp, "Initialization")) initialization = ParseInitialization(xpp);
                else if (IsStartTag(xpp, "SegmentTimeline")) timeline = m_Parser.ParseSegmentTimeline(xpp, timescale, periodDurationMs);
                else DashManifestParser::MaybeSkipTag(xpp);
            } while (!IsEndTag(xpp, "SegmentTemplate"));
            if (parent) {
                if (!initialization) initialization = parent->initialization;
                if (!timeline) timeline = parent->segmentTimeline;
            }
            return std::make_shared<SegmentTemplate>(initialization, timescale, presentationTimeOffset, startNumber, endNumber, duration, timeline,
                                                     availabilityTimeOffsetUs, initializationTemplate, mediaTemplate, MsToUs(timeShiftBufferDepthMs),
                                                     MsToUs(periodStartUnixTimeMs));
        }

        std::int64_t AddSegmentTimelineElementsToList(std::vector<SegmentTimelineElement>& segmentTimeline, std::int64_t startTime, std::int64_t elementDuration,
                                                      int elementRepeatCount, std::int64_t endTime) {
            const int count = elementRepeatCount >= 0 ? 1 + elementRepeatCount : int(CeilDivide(endTime - startTime, elementDuration));
            for (int i = 0; i < count; i++) {
                segmentTimeline.push_back({ startTime, elementDuration });
                startTime += elementDuration;
            }
            return startTime;
        }

    }

    std::shared_ptr<DashManifest> DashManifestParser::Parse(const std::string& uri, const std::string& content) const {
        try {
            XmlPullParser xpp(content);
            const int eventType = xpp.Next();
            if (eventType != XmlPullParser::START_TAG || xpp.GetName() != "MPD")
                throw ParserException("inputStream does not contain a valid media presentation description");
            return Reader(*this).ParseMediaPresentationDescription(xpp, uri);
        } catch (const XmlPullParserException& e) {
            throw ParserException(std::string("malformed manifest: ") + e.what());
        }
    }

    std::vector<SegmentTimelineElement> DashManifestParser::ParseSegmentTimeline(XmlPullParser& xpp, std::int64_t timescale, std::int64_t periodDurationMs) const {
        std::vector<SegmentTimelineElement> segmentTimeline;
        std::int64_t startTime = 0;
        std::int64_t elementDuration = kTimeUnset;
        int elementRepeatCount = 0;
        bool havePreviousTimelineElement = false;
        do {
            xpp.Next();
            if (IsStartTag(xpp, "S")) {
                const std::int64_t newStartTime = ParseLong(xpp, "t", kTimeUnset);
                if (havePreviousTimelineElement)
                    startTime = AddSegmentTimelineElementsToList(segmentTimeline, startTime, elementDuration, elementRepeatCount, newStartTime);
                if (newStartTime != kTimeUnset) startTime = newStartTime;
                elementDuration = ParseLong(xpp, "d", kTimeUnset);
                elementRepeatCount = ParseInt(xpp, "r", 0);
                havePreviousTimelineElement = true;
            } else {
                MaybeSkipTag(xpp);
            }
        } while (!IsEndTag(xpp, "SegmentTimeline"));
        if (havePreviousTimelineElement) {
            const std::int64_t periodDuration = ScaleLargeTimestamp(periodDurationMs, timescale, 1000);
            AddSegmentTimelineElementsToList(segmentTimeline, startTime, elementDuration, elementRepeatCount, periodDuration);
        }
        return segmentTimeline;
    }

    Label DashManifestParser::ParseLabel(XmlPullParser& xpp) const {
        Label label;
        label.language = xpp.GetAttributeValue("lang").value_or("");
        label.value = ParseText(xpp, "Label");
        return label;
    }

    int DashManifestParser::ParseCea608AccessibilityChannel(const std::vector<Descriptor>& accessibilityDescriptors) {
        for (const Descriptor& d : accessibilityDescriptors) {
            if (d.schemeIdUri == "urn:scte:dash:cc:cea-608:2015" && !d.value.empty()) {
                std::smatch m;
                if (std::regex_match(d.value, m, CEA_608_ACCESSIBILITY_PATTERN)) return ParseJavaInt(m[1].str());
                log::Warn("MpdParser", "Unable to parse CEA-608 channel number from: {}", d.value);
            }
        }
        return -1;
    }

    int DashManifestParser::ParseCea708AccessibilityChannel(const std::vector<Descriptor>& accessibilityDescriptors) {
        for (const Descriptor& d : accessibilityDescriptors) {
            if (d.schemeIdUri == "urn:scte:dash:cc:cea-708:2015" && !d.value.empty()) {
                std::smatch m;
                if (std::regex_match(d.value, m, CEA_708_ACCESSIBILITY_PATTERN)) return ParseJavaInt(m[1].str());
                log::Warn("MpdParser", "Unable to parse CEA-708 service block number from: {}", d.value);
            }
        }
        return -1;
    }

    void DashManifestParser::MaybeSkipTag(XmlPullParser& xpp) {
        if (!IsStartTag(xpp)) return;
        int depth = 1;
        while (depth != 0) {
            xpp.Next();
            if (IsStartTag(xpp)) depth++;
            else if (IsEndTag(xpp)) depth--;
        }
    }

}
