#include "kiln/media/TrackSelection.h"

#include <bit>
#include <algorithm>
#include <climits>
#include <unordered_map>

namespace kiln::media {

    namespace {
        const std::unordered_map<std::string, std::string>& LanguageReplacements() {
            static const std::unordered_map<std::string, std::string> map = [] {
                // ISO 639-2/T to ISO 639-1, as Locale.getISOLanguages / getISO3Language give them.
                std::unordered_map<std::string, std::string> m = {
                    { "aar", "aa" }, { "abk", "ab" }, { "ave", "ae" }, { "afr", "af" }, { "aka", "ak" }, { "amh", "am" }, { "arg", "an" }, { "ara", "ar" }, { "asm", "as" }, { "ava", "av" }, { "aym", "ay" }, { "aze", "az" }, { "bak", "ba" }, { "bel", "be" }, { "bul", "bg" }, { "bih", "bh" }, { "bis", "bi" }, { "bam", "bm" }, { "ben", "bn" }, { "bod", "bo" }, { "bre", "br" }, { "bos", "bs" }, { "cat", "ca" }, { "che", "ce" }, { "cha", "ch" }, { "cos", "co" }, { "cre", "cr" }, { "ces", "cs" }, { "chu", "cu" }, { "chv", "cv" }, { "cym", "cy" }, { "dan", "da" }, { "deu", "de" }, { "div", "dv" }, { "dzo", "dz" }, { "ewe", "ee" }, { "ell", "el" }, { "eng", "en" }, { "epo", "eo" }, { "spa", "es" }, { "est", "et" }, { "eus", "eu" }, { "fas", "fa" }, { "ful", "ff" }, { "fin", "fi" }, { "fij", "fj" }, { "fao", "fo" }, { "fra", "fr" }, { "fry", "fy" }, { "gle", "ga" }, { "gla", "gd" }, { "glg", "gl" }, { "grn", "gn" }, { "guj", "gu" }, { "glv", "gv" }, { "hau", "ha" }, { "heb", "he" }, { "hin", "hi" }, { "hmo", "ho" }, { "hrv", "hr" }, { "hat", "ht" }, { "hun", "hu" }, { "hye", "hy" }, { "her", "hz" }, { "ina", "ia" }, { "ind", "id" }, { "ile", "ie" }, { "ibo", "ig" }, { "iii", "ii" }, { "ipk", "ik" }, { "ind", "in" }, { "ido", "io" }, { "isl", "is" }, { "ita", "it" }, { "iku", "iu" }, { "heb", "iw" }, { "jpn", "ja" }, { "yid", "ji" }, { "jav", "jv" }, { "kat", "ka" }, { "kon", "kg" }, { "kik", "ki" }, { "kua", "kj" }, { "kaz", "kk" }, { "kal", "kl" }, { "khm", "km" }, { "kan", "kn" }, { "kor", "ko" }, { "kau", "kr" }, { "kas", "ks" }, { "kur", "ku" }, { "kom", "kv" }, { "cor", "kw" }, { "kir", "ky" }, { "lat", "la" }, { "ltz", "lb" }, { "lug", "lg" }, { "lim", "li" }, { "lin", "ln" }, { "lao", "lo" }, { "lit", "lt" }, { "lub", "lu" }, { "lav", "lv" }, { "mlg", "mg" }, { "mah", "mh" }, { "mri", "mi" }, { "mkd", "mk" }, { "mal", "ml" }, { "mon", "mn" }, { "mol", "mo" }, { "mar", "mr" }, { "msa", "ms" }, { "mlt", "mt" }, { "mya", "my" }, { "nau", "na" }, { "nob", "nb" }, { "nde", "nd" }, { "nep", "ne" }, { "ndo", "ng" }, { "nld", "nl" }, { "nno", "nn" }, { "nor", "no" }, { "nbl", "nr" }, { "nav", "nv" }, { "nya", "ny" }, { "oci", "oc" }, { "oji", "oj" }, { "orm", "om" }, { "ori", "or" }, { "oss", "os" }, { "pan", "pa" }, { "pli", "pi" }, { "pol", "pl" }, { "pus", "ps" }, { "por", "pt" }, { "que", "qu" }, { "roh", "rm" }, { "run", "rn" }, { "ron", "ro" }, { "rus", "ru" }, { "kin", "rw" }, { "san", "sa" }, { "srd", "sc" }, { "snd", "sd" }, { "sme", "se" }, { "sag", "sg" }, { "sin", "si" }, { "slk", "sk" }, { "slv", "sl" }, { "smo", "sm" }, { "sna", "sn" }, { "som", "so" }, { "sqi", "sq" }, { "srp", "sr" }, { "ssw", "ss" }, { "sot", "st" }, { "sun", "su" }, { "swe", "sv" }, { "swa", "sw" }, { "tam", "ta" }, { "tel", "te" }, { "tgk", "tg" }, { "tha", "th" }, { "tir", "ti" }, { "tuk", "tk" }, { "tgl", "tl" }, { "tsn", "tn" }, { "ton", "to" }, { "tur", "tr" }, { "tso", "ts" }, { "tat", "tt" }, { "twi", "tw" }, { "tah", "ty" }, { "uig", "ug" }, { "ukr", "uk" }, { "urd", "ur" }, { "uzb", "uz" }, { "ven", "ve" }, { "vie", "vi" }, { "vol", "vo" }, { "wln", "wa" }, { "wol", "wo" }, { "xho", "xh" }, { "yid", "yi" }, { "yor", "yo" }, { "zha", "za" }, { "zho", "zh" }, { "zul", "zu" }, 
                };
                // additionalIsoLanguageReplacements: bibliographic codes, deprecated codes and
                // macrolanguages.
                const char* additional[][2] = {
                    { "alb", "sq" }, { "arm", "hy" }, { "baq", "eu" }, { "bur", "my" }, { "tib", "bo" }, { "chi", "zh" }, { "cze", "cs" },
                    { "dut", "nl" }, { "ger", "de" }, { "gre", "el" }, { "fre", "fr" }, { "geo", "ka" }, { "ice", "is" }, { "mac", "mk" },
                    { "mao", "mi" }, { "may", "ms" }, { "per", "fa" }, { "rum", "ro" }, { "scc", "hbs-srp" }, { "slo", "sk" }, { "wel", "cy" },
                    { "id", "ms-ind" }, { "iw", "he" }, { "heb", "he" }, { "ji", "yi" }, { "arb", "ar-arb" }, { "in", "ms-ind" },
                    { "ind", "ms-ind" }, { "nb", "no-nob" }, { "nob", "no-nob" }, { "nn", "no-nno" }, { "nno", "no-nno" }, { "tw", "ak-twi" },
                    { "twi", "ak-twi" }, { "bs", "hbs-bos" }, { "bos", "hbs-bos" }, { "hr", "hbs-hrv" }, { "hrv", "hbs-hrv" },
                    { "sr", "hbs-srp" }, { "srp", "hbs-srp" }, { "cmn", "zh-cmn" }, { "hak", "zh-hak" }, { "nan", "zh-nan" }, { "hsn", "zh-hsn" },
                };
                for (auto& [from, to] : additional) m[from] = to;
                return m;
            }();
            return map;
        }
        // isoLegacyTagReplacements.
        constexpr const char* kLegacyTags[][2] = {
            { "i-lux", "lb" }, { "i-hak", "zh-hak" }, { "i-navajo", "nv" }, { "no-bok", "no-nob" }, { "no-nyn", "no-nno" },
            { "zh-guoyu", "zh-cmn" }, { "zh-hakka", "zh-hak" }, { "zh-min-nan", "zh-nan" }, { "zh-xiang", "zh-hsn" },
        };

        std::optional<std::string> NormalizeUndetermined(const std::optional<std::string>& language) {
            if (!language || language->empty() || *language == "und") return std::nullopt;
            return language;
        }
        std::string MainLanguage(const std::string& tag) { return tag.substr(0, tag.find('-')); }

        int RoleFlagMatchScore(int trackRoleFlags, int preferredRoleFlags) {
            if (trackRoleFlags != 0 && trackRoleFlags == preferredRoleFlags) return INT_MAX;   // a perfect match first
            return std::popcount(unsigned(trackRoleFlags & preferredRoleFlags));
        }
        int BestLabelMatchIndex(const std::string& label, const std::vector<std::string>& preferred) {
            for (std::size_t i = 0; i < preferred.size(); ++i)
                if (!label.empty() && label == preferred[i]) return int(i);
            return INT_MAX;
        }

        // TextTrackInfo.
        struct TextTrackInfo {
            bool withinCapabilities = false, eligible = false;
            bool isDefault = false, isForced = false, hasCaptionRoleFlags = false;
            int preferredLanguageIndex = INT_MAX, preferredLanguageScore = 0, preferredRoleFlagsScore = 0;
            int preferredLabelMatchIndex = INT_MAX, selectedAudioLanguageScore = 0;

            // ComparisonChain: negative, zero or positive as this is worse, as good or better.
            int CompareTo(const TextTrackInfo& o) const {
                auto cmp = [](auto a, auto b) { return a < b ? -1 : a > b ? 1 : 0; };
                int r = cmp(withinCapabilities, o.withinCapabilities);        // compareFalseFirst
                if (r == 0) r = cmp(o.preferredLanguageIndex, preferredLanguageIndex);   // reversed
                if (r == 0) r = cmp(preferredLanguageScore, o.preferredLanguageScore);
                if (r == 0) r = cmp(preferredRoleFlagsScore, o.preferredRoleFlagsScore);
                if (r == 0) r = cmp(o.preferredLabelMatchIndex, preferredLabelMatchIndex);   // reversed
                if (r == 0) r = cmp(isDefault, o.isDefault);
                // Forced over not, unless a preferred language matched (then the full track).
                if (r == 0) r = preferredLanguageScore == 0 ? cmp(isForced, o.isForced) : cmp(o.isForced, isForced);
                if (r == 0) r = cmp(selectedAudioLanguageScore, o.selectedAudioLanguageScore);
                if (r == 0 && preferredRoleFlagsScore == 0) r = cmp(o.hasCaptionRoleFlags, hasCaptionRoleFlags);   // compareTrueFirst
                return r;
            }
        };
    }

    // --- Tracks, overrides ---------------------------------------------------------------------

    bool Tracks::Group::IsSelected() const {
        for (bool b : trackSelected) if (b) return true;
        return false;
    }
    bool Tracks::Group::IsSupported(bool) const {
        for (bool b : trackSupported) if (b) return true;
        return false;
    }
    bool Tracks::ContainsType(TrackType type) const {
        for (const auto& g : groups) if (g.type == type) return true;
        return false;
    }
    bool Tracks::IsTypeSelected(TrackType type) const {
        for (const auto& g : groups) if (g.type == type && g.IsSelected()) return true;
        return false;
    }
    bool Tracks::IsTypeSupported(TrackType type, bool allow) const {
        for (const auto& g : groups) if (g.type == type && g.IsSupported(allow)) return true;
        return false;
    }

    TrackSelectionParameters& TrackSelectionParameters::AddOverride(TrackSelectionOverride o) {
        std::erase_if(overrides, [&](const TrackSelectionOverride& x) { return x.groupId == o.groupId; });
        overrides.push_back(std::move(o));
        return *this;
    }
    TrackSelectionParameters& TrackSelectionParameters::SetOverrideForType(TrackSelectionOverride o) {
        ClearOverridesOfType(o.type);
        overrides.push_back(std::move(o));
        return *this;
    }
    TrackSelectionParameters& TrackSelectionParameters::ClearOverridesOfType(TrackType type) {
        std::erase_if(overrides, [&](const TrackSelectionOverride& x) { return x.type == type; });
        return *this;
    }
    TrackSelectionParameters& TrackSelectionParameters::SetTrackTypeDisabled(TrackType type, bool disabled) {
        if (disabled) disabledTrackTypes.insert(type);
        else disabledTrackTypes.erase(type);
        return *this;
    }

    TrackSelectionParameters& TrackSelectionParameters::SetMaxVideoSize(int width, int height) {
        maxVideoWidth = width;
        maxVideoHeight = height;
        return *this;
    }
    TrackSelectionParameters& TrackSelectionParameters::SetMinVideoSize(int width, int height) {
        minVideoWidth = width;
        minVideoHeight = height;
        return *this;
    }
    TrackSelectionParameters& TrackSelectionParameters::SetViewportSize(int width, int height, bool orientationMayChange) {
        viewportWidth = width;
        viewportHeight = height;
        viewportOrientationMayChange = orientationMayChange;
        isViewportSizeLimitedByPhysicalDisplaySize = false;
        return *this;
    }
    TrackSelectionParameters& TrackSelectionParameters::SetViewportSizeToPhysicalDisplaySize(bool orientationMayChange) {
        isViewportSizeLimitedByPhysicalDisplaySize = true;
        viewportOrientationMayChange = orientationMayChange;
        return *this;
    }

    std::pair<int, int> MaxVideoSizeInViewport(bool orientationMayChange, int viewportWidth, int viewportHeight, int videoWidth, int videoHeight) {
        if (orientationMayChange && (videoWidth > videoHeight) != (viewportWidth > viewportHeight)) std::swap(viewportWidth, viewportHeight);
        auto ceilDivide = [](std::int64_t n, std::int64_t d) { return int((n + d - 1) / d); };
        if (std::int64_t(videoWidth) * viewportHeight >= std::int64_t(videoHeight) * viewportWidth)
            return { viewportWidth, ceilDivide(std::int64_t(viewportWidth) * videoHeight, videoWidth) };   // letter-boxed top and bottom
        return { ceilDivide(std::int64_t(viewportHeight) * videoWidth, videoHeight), viewportHeight };     // at the sides
    }

    namespace {
        constexpr float kFractionToConsiderFullscreen = 0.98f;
        constexpr float kMinReasonableFrameRate = 10;

        // getMaxVideoPixelsToRetainForViewport: the pixels of the smallest track at least as large
        // as the viewport shows it (larger ones are left out).
        long long MaxVideoPixelsToRetain(const std::vector<Format>& group, int viewportWidth, int viewportHeight, bool orientationMayChange) {
            constexpr long long kAll = 0x7fffffff;
            if (viewportWidth == 0x7fffffff || viewportHeight == 0x7fffffff) return kAll;
            long long retain = kAll;
            for (const Format& f : group) {
                if (f.width <= 0 || f.height <= 0) continue;
                const auto [maxWidth, maxHeight] = MaxVideoSizeInViewport(orientationMayChange, viewportWidth, viewportHeight, f.width, f.height);
                const long long pixels = (long long)f.width * f.height;
                if (f.width >= int(float(maxWidth) * kFractionToConsiderFullscreen) && f.height >= int(float(maxHeight) * kFractionToConsiderFullscreen) && pixels < retain)
                    retain = pixels;
            }
            return retain;
        }

        int BestLabelMatchIndex(const Format& f, const std::vector<std::string>& preferred) {
            for (std::size_t i = 0; i < preferred.size(); ++i)
                for (const Label& l : f.labels)
                    if (l.value == preferred[i]) return int(i);
            return 0x7fffffff;
        }
        int VideoCodecPreferenceScore(const std::string& mime) {
            if (mime == "video/dolby-vision") return 5;
            if (mime == "video/av01") return 4;
            if (mime == "video/hevc") return 3;
            if (mime == "video/x-vnd.on2.vp9") return 2;
            if (mime == "video/avc") return 1;
            return 0;
        }

        enum class Eligibility { No, Fixed, Adaptive };

        // VideoTrackInfo (the platform's decoder for a type is its primary one).
        struct VideoTrackInfo {
            int group = 0, track = 0;
            const Format* format = nullptr;
            bool withinMax = false, withinMin = false, withinCapabilities = false, reasonableFrameRate = false, mainOrNoRole = false, hdr = false;
            bool hardwareAccelerated = false;
            int bitrate = -1;
            long long pixels = -1;
            int preferredLanguageIndex = 0x7fffffff, preferredLanguageScore = 0, preferredRoleFlagsScore = 0, preferredLabelIndex = 0x7fffffff;
            int selectedAudioLanguageScore = 0, preferredMimeTypeIndex = 0x7fffffff, codecPreferenceScore = 0;
            Eligibility eligibility = Eligibility::No;
            const TrackSelectionParameters* parameters = nullptr;
        };

        // ComparisonChain over ints; compareFalseFirst: false before true.
        struct Chain {
            int result = 0;
            Chain& Compare(long long a, long long b) {
                if (result == 0 && a != b) result = a < b ? -1 : 1;
                return *this;
            }
            Chain& Reverse(long long a, long long b) { return Compare(b, a); }
            Chain& FalseFirst(bool a, bool b) { return Compare(a ? 1 : 0, b ? 1 : 0); }
            // FORMAT_VALUE_ORDERING: Format.NO_VALUE (-1) before every value.
            Chain& FormatValue(long long a, long long b, bool reverse) {
                if (result != 0) return *this;
                const int c = a == -1 ? (b == -1 ? 0 : -1) : b == -1 ? 1 : (a < b ? -1 : a > b ? 1 : 0);
                result = reverse ? -c : c;
                return *this;
            }
        };

        int CompareNonQuality(const VideoTrackInfo& a, const VideoTrackInfo& b) {
            return Chain()
                .FalseFirst(a.withinCapabilities, b.withinCapabilities)
                .Reverse(a.preferredLanguageIndex, b.preferredLanguageIndex)
                .Compare(a.preferredLanguageScore, b.preferredLanguageScore)
                .Compare(a.preferredRoleFlagsScore, b.preferredRoleFlagsScore)
                .Reverse(a.preferredLabelIndex, b.preferredLabelIndex)
                .FalseFirst(a.mainOrNoRole, b.mainOrNoRole)
                .Compare(a.selectedAudioLanguageScore, b.selectedAudioLanguageScore)
                .FalseFirst(a.reasonableFrameRate, b.reasonableFrameRate)
                .FalseFirst(a.withinMax, b.withinMax)
                .FalseFirst(a.withinMin, b.withinMin)
                .Reverse(a.preferredMimeTypeIndex, b.preferredMimeTypeIndex)
                .FalseFirst(a.hardwareAccelerated, b.hardwareAccelerated)
                .result;
        }
        int CompareQuality(const VideoTrackInfo& a, const VideoTrackInfo& b) {
            // Higher quality within the constraints and capabilities, else lower.
            const bool reverse = !(a.withinMax && a.withinCapabilities);
            Chain chain;
            if (a.parameters->forceLowestBitrate) chain.FormatValue(a.bitrate, b.bitrate, true);
            chain.FalseFirst(a.hdr, b.hdr).FormatValue(a.pixels, b.pixels, reverse);
            if (a.hardwareAccelerated) chain.Compare(a.codecPreferenceScore, b.codecPreferenceScore);
            chain.FormatValue(a.bitrate, b.bitrate, reverse);
            return chain.result;
        }
        // Collections.max: the first of the greatest.
        template<typename T, typename C>
        const T& MaxOf(const std::vector<T>& list, C compare) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < list.size(); ++i)
                if (compare(list[i], list[best]) > 0) best = i;
            return list[best];
        }
        int CompareSelections(const std::vector<VideoTrackInfo>& a, const std::vector<VideoTrackInfo>& b) {
            if (const int c = CompareNonQuality(MaxOf(a, CompareNonQuality), MaxOf(b, CompareNonQuality))) return c;
            if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
            return CompareQuality(MaxOf(a, CompareQuality), MaxOf(b, CompareQuality));
        }
    }

    VideoSelection SelectVideoTracks(const std::vector<std::vector<Format>>& groups, const std::vector<std::vector<int>>& support,
                                     const TrackSelectionParameters& p, const std::optional<std::string>& selectedAudioLanguage,
                                     std::optional<std::pair<int, int>> displaySize) {
        const int viewportWidth = p.isViewportSizeLimitedByPhysicalDisplaySize && displaySize ? displaySize->first : p.viewportWidth;
        const int viewportHeight = p.isViewportSizeLimitedByPhysicalDisplaySize && displaySize ? displaySize->second : p.viewportHeight;
        std::vector<std::string> languages;
        for (const auto& l : p.preferredVideoLanguages) languages.push_back(NormalizeLanguageCode(l));
        std::optional<std::string> audioLanguage;
        if (selectedAudioLanguage && !selectedAudioLanguage->empty() && *selectedAudioLanguage != "und") audioLanguage = NormalizeLanguageCode(*selectedAudioLanguage);
        std::vector<std::vector<VideoTrackInfo>> selections;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const auto& group = groups[g];
            const long long retain = MaxVideoPixelsToRetain(group, viewportWidth, viewportHeight, p.viewportOrientationMayChange);
            std::vector<VideoTrackInfo> infos;
            for (std::size_t i = 0; i < group.size(); ++i) {
                const Format& f = group[i];
                const int supportFlags = g < support.size() && i < support[g].size() ? support[g][i] : 2;
                const int formatSupport = supportFlags & 3;
                VideoTrackInfo t;
                t.group = int(g);
                t.track = int(i);
                t.format = &f;
                t.parameters = &p;
                const long long pixels = f.width > 0 && f.height > 0 ? (long long)f.width * f.height : -1;
                const bool suitableForViewport = retain == 0x7fffffff || (pixels != -1 && pixels <= retain);
                t.withinMax = suitableForViewport && (f.width == -1 || f.width <= p.maxVideoWidth) && (f.height == -1 || f.height <= p.maxVideoHeight)
                              && (f.frameRate == -1 || f.frameRate <= float(p.maxVideoFrameRate)) && (f.bitrate == -1 || f.bitrate <= p.maxVideoBitrate);
                t.withinMin = suitableForViewport && (f.width == -1 || f.width >= p.minVideoWidth) && (f.height == -1 || f.height >= p.minVideoHeight)
                              && (f.frameRate == -1 || f.frameRate >= float(p.minVideoFrameRate)) && (f.bitrate == -1 || f.bitrate >= p.minVideoBitrate);
                t.withinCapabilities = formatSupport == 2;
                t.hardwareAccelerated = (supportFlags & kDecoderHardwareAccelerated) != 0;
                t.reasonableFrameRate = f.frameRate != -1 && f.frameRate >= kMinReasonableFrameRate;
                t.bitrate = f.bitrate;
                t.pixels = pixels;
                for (std::size_t l = 0; l < languages.size(); ++l)
                    if (const int score = FormatLanguageScore(f.language, languages[l], false); score > 0) {
                        t.preferredLanguageIndex = int(l);
                        t.preferredLanguageScore = score;
                        break;
                    }
                t.preferredRoleFlagsScore = RoleFlagMatchScore(f.roleFlags, p.preferredVideoRoleFlags);
                t.mainOrNoRole = f.roleFlags == 0 || (f.roleFlags & kRoleFlagMain) != 0;
                t.selectedAudioLanguageScore = FormatLanguageScore(f.language, audioLanguage, !audioLanguage.has_value());
                for (std::size_t m = 0; m < p.preferredVideoMimeTypes.size(); ++m)
                    if (f.sampleMimeType == p.preferredVideoMimeTypes[m]) {
                        t.preferredMimeTypeIndex = int(m);
                        break;
                    }
                t.preferredLabelIndex = BestLabelMatchIndex(f, p.preferredVideoLabels);
                t.codecPreferenceScore = VideoCodecPreferenceScore(f.sampleMimeType);
                t.hdr = f.colorTransfer == 6 || f.colorTransfer == 7;   // ColorInfo.isTransferHdr: ST2084, HLG
                // evaluateSelectionEligibility.
                if (f.roleFlags & kRoleFlagTrickPlay) t.eligibility = Eligibility::No;
                else if (!(formatSupport == 2 || (p.exceedRendererCapabilitiesIfNecessary && formatSupport == 1))) t.eligibility = Eligibility::No;
                else if (!t.withinMax && !p.exceedVideoConstraintsIfNecessary) t.eligibility = Eligibility::No;
                else
                    t.eligibility = formatSupport == 2 && t.withinMin && t.withinMax && f.bitrate != -1 && !p.forceHighestSupportedBitrate && !p.forceLowestBitrate
                                        ? Eligibility::Adaptive
                                        : Eligibility::Fixed;
                infos.push_back(t);
            }
            // selectTracksForType: each eligible track with the adaptive ones of its type after it.
            std::vector<bool> used(infos.size(), false);
            for (std::size_t i = 0; i < infos.size(); ++i) {
                if (used[i] || infos[i].eligibility == Eligibility::No) continue;
                std::vector<VideoTrackInfo> selection{ infos[i] };
                if (infos[i].eligibility == Eligibility::Adaptive)
                    for (std::size_t j = i + 1; j < infos.size(); ++j)
                        if (infos[j].eligibility == Eligibility::Adaptive
                            && (p.allowVideoMixedMimeTypeAdaptiveness || infos[j].format->sampleMimeType == infos[i].format->sampleMimeType)
                            && infos[j].hardwareAccelerated == infos[i].hardwareAccelerated) {
                            selection.push_back(infos[j]);
                            used[j] = true;
                        }
                selections.push_back(std::move(selection));
            }
        }
        if (selections.empty()) return {};
        const auto& best = MaxOf(selections, CompareSelections);
        VideoSelection out;
        out.group = best.front().group;
        for (const auto& t : best) out.tracks.push_back(t.track);
        return out;
    }

    std::optional<OverrideChoice> OverrideForType(const TrackSelectionParameters& p, TrackType type) {
        for (const auto& o : p.overrides)
            if (o.type == type) return OverrideChoice{ o.groupId, o.trackIndices.empty() ? -1 : o.trackIndices.front() };
        return std::nullopt;
    }

    namespace {
        // FORMAT_VALUE_ORDERING: an unknown value (-1) below any known one.
        int CompareFormatValue(int a, int b) { return a == -1 ? (b == -1 ? 0 : -1) : (b == -1 ? 1 : (a > b) - (a < b)); }
        bool IsObjectBasedAudio(const std::string& mime) { return mime == "audio/eac3-joc" || mime == "audio/ac4" || mime == "audio/iamf"; }

        // AudioTrackInfo.
        struct AudioTrackInfo {
            bool withinCapabilities = false, withinConstraints = false, isDefault = false, hasMainOrNoRoleFlag = false, objectBased = false;
            std::optional<std::string> language;
            int preferredLanguageIndex = INT_MAX, preferredLanguageScore = 0, preferredRoleFlagsScore = 0, preferredLabelMatchIndex = INT_MAX;
            int localeLanguageMatchIndex = INT_MAX, localeLanguageScore = 0, preferredMimeTypeMatchIndex = INT_MAX;
            int channelCount = -1, sampleRate = -1, bitrate = -1;
            bool forceLowestBitrate = false;

            int CompareTo(const AudioTrackInfo& o) const {
                auto cmp = [](auto a, auto b) { return a < b ? -1 : a > b ? 1 : 0; };
                const bool qualityNatural = withinConstraints && withinCapabilities;
                auto quality = [&](int a, int b) { return qualityNatural ? CompareFormatValue(a, b) : -CompareFormatValue(a, b); };
                int r = cmp(withinCapabilities, o.withinCapabilities);
                if (r == 0) r = cmp(o.preferredLanguageIndex, preferredLanguageIndex);
                if (r == 0) r = cmp(preferredLanguageScore, o.preferredLanguageScore);
                if (r == 0) r = cmp(preferredRoleFlagsScore, o.preferredRoleFlagsScore);
                if (r == 0) r = cmp(o.preferredLabelMatchIndex, preferredLabelMatchIndex);
                if (r == 0) r = cmp(isDefault, o.isDefault);
                if (r == 0) r = cmp(hasMainOrNoRoleFlag, o.hasMainOrNoRoleFlag);
                if (r == 0) r = cmp(o.localeLanguageMatchIndex, localeLanguageMatchIndex);
                if (r == 0) r = cmp(localeLanguageScore, o.localeLanguageScore);
                if (r == 0) r = cmp(withinConstraints, o.withinConstraints);
                if (r == 0) r = cmp(o.preferredMimeTypeMatchIndex, preferredMimeTypeMatchIndex);
                if (r == 0 && forceLowestBitrate) r = -CompareFormatValue(bitrate, o.bitrate);
                // usesPrimaryDecoder / usesHardwareAcceleration: the same platform decoders here.
                if (r == 0) r = cmp(objectBased, o.objectBased);
                if (r == 0) r = quality(channelCount, o.channelCount);
                if (r == 0) r = quality(sampleRate, o.sampleRate);
                if (r == 0 && language == o.language) r = quality(bitrate, o.bitrate);
                return r;
            }
        };
    }

    int SelectAudioTrack(const std::vector<Format>& tracks, const std::vector<bool>& supported, const TrackSelectionParameters& p,
                         const std::vector<std::string>& systemLanguagesRaw) {
        if (p.disabledTrackTypes.contains(TrackType::Audio)) return -1;
        std::vector<std::string> preferred, system;
        for (const auto& l : p.preferredAudioLanguages) preferred.push_back(NormalizeLanguageCode(l));
        for (const auto& l : systemLanguagesRaw) system.push_back(NormalizeLanguageCode(l));
        int best = -1;
        AudioTrackInfo bestInfo;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const Format& f = tracks[i];
            const std::string language = f.language.empty() ? std::string() : NormalizeLanguageCode(f.language);
            AudioTrackInfo info;
            info.forceLowestBitrate = p.forceLowestBitrate;
            info.language = NormalizeUndetermined(language.empty() ? std::nullopt : std::optional(language));
            info.withinCapabilities = i < supported.size() && supported[i];
            for (std::size_t l = 0; l < preferred.size(); ++l)
                if (const int score = FormatLanguageScore(language, preferred[l], false); score > 0) {
                    info.preferredLanguageIndex = int(l);
                    info.preferredLanguageScore = score;
                    break;
                }
            info.preferredRoleFlagsScore = RoleFlagMatchScore(f.roleFlags, p.preferredAudioRoleFlags);
            info.preferredLabelMatchIndex = BestLabelMatchIndex(f.label, p.preferredAudioLabels);
            info.hasMainOrNoRoleFlag = f.roleFlags == 0 || (f.roleFlags & kRoleFlagMain) != 0;
            info.isDefault = (f.selectionFlags & kSelectionFlagDefault) != 0;
            info.objectBased = IsObjectBasedAudio(f.sampleMimeType);
            info.channelCount = f.channelCount;
            info.sampleRate = f.sampleRate;
            info.bitrate = f.bitrate;
            info.withinConstraints = (f.bitrate == -1 || f.bitrate <= p.maxAudioBitrate) && (f.channelCount == -1 || f.channelCount <= p.maxAudioChannelCount);
            for (std::size_t l = 0; l < system.size(); ++l)
                if (const int score = FormatLanguageScore(language, system[l], false); score > 0) {
                    info.localeLanguageMatchIndex = int(l);
                    info.localeLanguageScore = score;
                    break;
                }
            for (std::size_t m = 0; m < p.preferredAudioMimeTypes.size(); ++m)
                if (!f.sampleMimeType.empty() && f.sampleMimeType == p.preferredAudioMimeTypes[m]) {
                    info.preferredMimeTypeMatchIndex = int(m);
                    break;
                }
            const bool eligible = info.withinCapabilities && (info.withinConstraints || p.exceedAudioConstraintsIfNecessary);
            if (!eligible) continue;
            if (best < 0 || info.CompareTo(bestInfo) > 0) {
                best = int(i);
                bestInfo = info;
            }
        }
        return best;
    }

    std::string NormalizeLanguageCode(std::string_view language) {
        std::string tag(language);
        for (char& c : tag) if (c == '_') c = '-';
        if (tag.empty() || tag == "und") tag = std::string(language);
        for (char& c : tag) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        std::string main = MainLanguage(tag);
        if (auto it = LanguageReplacements().find(main); it != LanguageReplacements().end()) {
            tag = it->second + tag.substr(main.size());
            main = it->second;
        }
        if (main == "no" || main == "i" || main == "zh")
            for (auto& [from, to] : kLegacyTags)
                if (tag.starts_with(from)) return std::string(to) + tag.substr(std::string_view(from).size());
        return tag;
    }

    int FormatLanguageScore(const std::string& formatLanguageRaw, const std::optional<std::string>& languageRaw, bool allowUndeterminedFormatLanguage) {
        if (languageRaw && !languageRaw->empty() && *languageRaw == formatLanguageRaw) return 4;   // literal, "und" included
        const auto language = NormalizeUndetermined(languageRaw);
        const auto formatLanguage = NormalizeUndetermined(formatLanguageRaw.empty() ? std::nullopt : std::optional(formatLanguageRaw));
        if (!formatLanguage || !language) return allowUndeterminedFormatLanguage && !formatLanguage ? 1 : 0;
        if (formatLanguage->starts_with(*language) || language->starts_with(*formatLanguage)) return 3;
        if (MainLanguage(*formatLanguage) == MainLanguage(*language)) return 2;
        return 0;
    }

    int SelectTextTrack(const std::vector<MediaItem::SubtitleConfiguration>& tracks, const std::vector<bool>& supported,
                        const TrackSelectionParameters& p, const std::optional<std::string>& captioningLanguage,
                        const std::optional<std::string>& selectedAudioLanguage) {
        if (p.disabledTrackTypes.contains(TrackType::Text)) return -1;
        // Compared against empty (unset) without preferences, to allow an undetermined language.
        std::vector<std::string> preferred;
        for (const auto& l : p.preferredTextLanguages) preferred.push_back(NormalizeLanguageCode(l));
        if (captioningLanguage) preferred = { *captioningLanguage };
        else if (preferred.empty()) preferred = { "" };
        const int preferredRoleFlags = captioningLanguage ? kRoleFlagCaption | kRoleFlagDescribesMusicAndSound : p.preferredTextRoleFlags;
        const bool audioUndetermined = !NormalizeUndetermined(selectedAudioLanguage);

        int best = -1;
        TextTrackInfo bestInfo;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const auto& t = tracks[i];
            const std::string language = t.language.empty() ? std::string() : NormalizeLanguageCode(t.language);
            TextTrackInfo info;
            info.withinCapabilities = i < supported.size() && supported[i];
            const int flags = t.selectionFlags & ~p.ignoredTextSelectionFlags;
            info.isDefault = flags & kSelectionFlagDefault;
            info.isForced = flags & kSelectionFlagForced;
            for (std::size_t l = 0; l < preferred.size(); ++l) {
                const int score = FormatLanguageScore(language, preferred[l], p.selectUndeterminedTextLanguage);
                if (score > 0) {
                    info.preferredLanguageIndex = int(l);
                    info.preferredLanguageScore = score;
                    break;
                }
            }
            info.preferredRoleFlagsScore = RoleFlagMatchScore(t.roleFlags, preferredRoleFlags);
            info.hasCaptionRoleFlags = (t.roleFlags & (kRoleFlagCaption | kRoleFlagDescribesMusicAndSound)) != 0;
            info.preferredLabelMatchIndex = BestLabelMatchIndex(t.label, p.preferredTextLabels);
            info.selectedAudioLanguageScore = FormatLanguageScore(language, selectedAudioLanguage, audioUndetermined);
            const bool withinConstraints = info.preferredLanguageScore > 0
                                           || (p.preferredTextLanguages.empty() && info.preferredRoleFlagsScore > 0)
                                           || (p.preferredTextLanguages.empty() && info.preferredLabelMatchIndex != INT_MAX)
                                           || info.isDefault || (info.isForced && info.selectedAudioLanguageScore > 0)
                                           || p.selectTextByDefault;
            info.eligible = info.withinCapabilities && withinConstraints;
            if (!info.eligible) continue;
            // Collections.max: the first of equals stays.
            if (best < 0 || info.CompareTo(bestInfo) > 0) {
                best = int(i);
                bestInfo = info;
            }
        }
        return best;
    }

}
