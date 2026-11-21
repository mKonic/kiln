#pragma once

// media3-exoplayer-hls's playlists (HlsPlaylist, HlsMultivariantPlaylist, HlsMediaPlaylist):
// what HlsPlaylistParser reads from an M3U8. A multivariant playlist lists variants (one
// bitrate each) and renditions (alternative audio, video, subtitles); a media playlist lists
// segments, with low-latency parts, keys, discontinuities and interstitials.

#include "kiln/media/TrackSelection.h"
#include "kiln/media/stream/StreamUtil.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media::hls {

    using stream::DrmInitData;
    using stream::kLengthUnset;
    using stream::kTimeUnset;

    // StreamKey: a stream of a manifest (group, then index in it).
    struct StreamKey {
        int periodIndex = 0;
        int groupIndex = 0;
        int streamIndex = 0;
        bool operator==(const StreamKey&) const = default;
    };

    // HlsTrackMetadataEntry: a variant's (or rendition's) group, name and the variants sharing its URL.
    struct HlsTrackMetadataEntry {
        struct VariantInfo {
            int averageBitrate = -1;
            int peakBitrate = -1;
            std::string videoGroupId, audioGroupId, subtitleGroupId, captionGroupId;   // "" for null
            bool operator==(const VariantInfo&) const = default;
        };
        std::string groupId;   // "" for null
        std::string name;
        std::vector<VariantInfo> variantInfos;
        bool operator==(const HlsTrackMetadataEntry&) const = default;
    };

    class HlsPlaylist {
    public:
        HlsPlaylist(std::string baseUri, std::vector<std::string> tags, bool hasIndependentSegments)
            : baseUri(std::move(baseUri)), tags(std::move(tags)), hasIndependentSegments(hasIndependentSegments) {}
        virtual ~HlsPlaylist() = default;
        virtual bool IsMultivariant() const = 0;

        std::string baseUri;
        std::vector<std::string> tags;
        bool hasIndependentSegments;
    };

    class HlsMultivariantPlaylist final : public HlsPlaylist {
    public:
        static constexpr int GROUP_INDEX_VARIANT = 0;
        static constexpr int GROUP_INDEX_AUDIO = 1;
        static constexpr int GROUP_INDEX_SUBTITLE = 2;

        struct Variant {
            std::string url;
            Format format;
            std::string videoGroupId, audioGroupId, subtitleGroupId, captionGroupId, pathwayId, stableVariantId;   // "" for null
            std::optional<HlsTrackMetadataEntry> metadata;
            static Variant CreateMediaPlaylistVariantUrl(const std::string& url);
            Variant CopyWithFormat(Format f) const {
                Variant v = *this;
                v.format = std::move(f);
                return v;
            }
        };

        struct Rendition {
            std::string url;   // "" for null (a rendition muxed into the variants)
            Format format;
            std::string groupId, name, stableRenditionId;
            std::optional<HlsTrackMetadataEntry> metadata;
        };

        struct ContentSteeringInfo {
            std::string serverUri;
            std::string pathwayId;   // "" for null
        };

        HlsMultivariantPlaylist(std::string baseUri, std::vector<std::string> tags, std::vector<Variant> variants, std::vector<Rendition> videos,
                                std::vector<Rendition> audios, std::vector<Rendition> subtitles, std::vector<Rendition> closedCaptions,
                                std::optional<Format> muxedAudioFormat, std::optional<std::vector<Format>> muxedCaptionFormats, bool hasIndependentSegments,
                                std::map<std::string, std::string> variableDefinitions, std::vector<DrmInitData> sessionKeyDrmInitData,
                                std::optional<ContentSteeringInfo> contentSteeringInfo);
        static const HlsMultivariantPlaylist& Empty();
        static HlsMultivariantPlaylist CreateSingleVariantMultivariantPlaylist(const std::string& variantUrl);
        HlsMultivariantPlaylist Copy(const std::vector<StreamKey>& streamKeys) const;
        bool IsMultivariant() const override { return true; }

        std::vector<std::string> mediaPlaylistUrls;
        std::vector<Variant> variants;
        std::vector<Rendition> videos;
        std::vector<Rendition> audios;
        std::vector<Rendition> subtitles;
        std::vector<Rendition> closedCaptions;
        std::optional<Format> muxedAudioFormat;
        std::optional<std::vector<Format>> muxedCaptionFormats;   // nullopt: unknown; empty: none
        std::map<std::string, std::string> variableDefinitions;
        std::vector<DrmInitData> sessionKeyDrmInitData;
        std::optional<ContentSteeringInfo> contentSteeringInfo;
    };

    class HlsMediaPlaylist final : public HlsPlaylist {
    public:
        static constexpr int PLAYLIST_TYPE_UNKNOWN = 0;
        static constexpr int PLAYLIST_TYPE_VOD = 1;
        static constexpr int PLAYLIST_TYPE_EVENT = 2;

        struct ServerControl {
            std::int64_t skipUntilUs = kTimeUnset;
            bool canSkipDateRanges = false;
            std::int64_t holdBackUs = kTimeUnset;
            std::int64_t partHoldBackUs = kTimeUnset;
            bool canBlockReload = false;
        };

        struct Segment;

        // SegmentBase: a segment or a part.
        struct SegmentBase {
            std::string url;
            std::shared_ptr<const Segment> initializationSegment;
            std::int64_t durationUs = 0;
            int relativeDiscontinuitySequence = 0;
            std::int64_t relativeStartTimeUs = 0;
            std::optional<DrmInitData> drmInitData;
            std::string fullSegmentEncryptionKeyUri;   // "" for null
            std::string encryptionIV;                  // "" for null
            std::int64_t byteRangeOffset = 0;
            std::int64_t byteRangeLength = kLengthUnset;
            bool hasGapTag = false;
            int CompareTo(std::int64_t startTimeUs) const {
                return relativeStartTimeUs > startTimeUs ? 1 : relativeStartTimeUs < startTimeUs ? -1 : 0;
            }
        };

        struct Part : SegmentBase {
            bool isIndependent = false;
            bool isPreload = false;
            Part CopyWith(std::int64_t relativeStartTimeUs, int relativeDiscontinuitySequence) const {
                Part p = *this;
                p.relativeStartTimeUs = relativeStartTimeUs;
                p.relativeDiscontinuitySequence = relativeDiscontinuitySequence;
                return p;
            }
        };

        struct Segment : SegmentBase {
            std::string title;
            std::vector<Part> parts;
            // Segment(uri, byteRangeOffset, byteRangeLength, keyUri, iv): an initialization segment.
            static Segment Init(std::string uri, std::int64_t byteRangeOffset, std::int64_t byteRangeLength, std::string keyUri, std::string iv);
            Segment CopyWith(std::int64_t relativeStartTimeUs, int relativeDiscontinuitySequence) const;
        };

        struct RenditionReport {
            std::string playlistUri;
            std::int64_t lastMediaSequence = stream::kIndexUnset;
            int lastPartIndex = stream::kIndexUnset;
        };

        struct ClientDefinedAttribute {
            static constexpr int TYPE_TEXT = 0;
            static constexpr int TYPE_HEX_TEXT = 1;
            static constexpr int TYPE_DOUBLE = 2;
            ClientDefinedAttribute(std::string name, double value) : name(std::move(name)), type(TYPE_DOUBLE), m_Double(value) {}
            ClientDefinedAttribute(std::string name, std::string value, int type);
            double GetDoubleValue() const;
            const std::string& GetTextValue() const;
            bool operator==(const ClientDefinedAttribute& o) const {
                return type == o.type && m_Double == o.m_Double && name == o.name && m_Text == o.m_Text;
            }
            std::string name;
            int type;

        private:
            double m_Double = 0;
            std::string m_Text;
        };

        struct Interstitial {
            static constexpr const char* CUE_TRIGGER_PRE = "PRE";
            static constexpr const char* CUE_TRIGGER_POST = "POST";
            static constexpr const char* CUE_TRIGGER_ONCE = "ONCE";
            static constexpr const char* SNAP_TYPE_IN = "IN";
            static constexpr const char* SNAP_TYPE_OUT = "OUT";
            static constexpr const char* NAVIGATION_RESTRICTION_SKIP = "SKIP";
            static constexpr const char* NAVIGATION_RESTRICTION_JUMP = "JUMP";
            static constexpr const char* TIMELINE_OCCUPIES_POINT = "POINT";
            static constexpr const char* TIMELINE_OCCUPIES_RANGE = "RANGE";
            static constexpr const char* TIMELINE_STYLE_HIGHLIGHT = "HIGHLIGHT";
            static constexpr const char* TIMELINE_STYLE_PRIMARY = "PRIMARY";

            std::string id;
            std::string assetUri, assetListUri;   // exactly one is set
            std::int64_t startDateUnixUs = kTimeUnset;
            std::int64_t endDateUnixUs = kTimeUnset;
            std::int64_t durationUs = kTimeUnset;
            std::int64_t plannedDurationUs = kTimeUnset;
            std::vector<std::string> cue;
            bool endOnNext = false;
            std::int64_t resumeOffsetUs = kTimeUnset;
            std::int64_t playoutLimitUs = kTimeUnset;
            std::vector<std::string> snapTypes;
            std::vector<std::string> restrictions;
            std::vector<ClientDefinedAttribute> clientDefinedAttributes;   // sorted by name
            bool contentMayVary = true;
            std::string timelineOccupies = TIMELINE_OCCUPIES_POINT;
            std::string timelineStyle = TIMELINE_STYLE_HIGHLIGHT;
            std::int64_t skipControlOffsetUs = kTimeUnset;
            std::int64_t skipControlDurationUs = kTimeUnset;
            std::string skipControlLabelId;   // "" for null
            bool operator==(const Interstitial&) const = default;

            // Interstitial.Builder: the attributes of the date ranges with one id; a value may not
            // change between them (std::invalid_argument).
            class Builder {
            public:
                explicit Builder(std::string id) : m_Id(std::move(id)) {}
                Builder& SetAssetUri(const std::optional<std::string>& v);
                Builder& SetAssetListUri(const std::optional<std::string>& v);
                Builder& SetStartDateUnixUs(std::int64_t v);
                Builder& SetEndDateUnixUs(std::int64_t v);
                Builder& SetDurationUs(std::int64_t v);
                Builder& SetPlannedDurationUs(std::int64_t v);
                Builder& SetCue(const std::vector<std::string>& v);
                Builder& SetEndOnNext(bool v);
                Builder& SetResumeOffsetUs(std::int64_t v);
                Builder& SetPlayoutLimitUs(std::int64_t v);
                Builder& SetSnapTypes(const std::vector<std::string>& v);
                Builder& SetRestrictions(const std::vector<std::string>& v);
                Builder& SetClientDefinedAttributes(const std::vector<ClientDefinedAttribute>& v);
                Builder& SetContentMayVary(std::optional<bool> v);
                Builder& SetTimelineOccupies(const std::optional<std::string>& v);
                Builder& SetTimelineStyle(const std::optional<std::string>& v);
                Builder& SetSkipControlOffsetUs(std::int64_t v);
                Builder& SetSkipControlDurationUs(std::int64_t v);
                Builder& SetSkipControlLabelId(const std::optional<std::string>& v);
                // nullopt without exactly one asset URI or a start date.
                std::optional<Interstitial> Build() const;

            private:
                std::string m_Id;
                std::optional<std::string> m_AssetUri, m_AssetListUri;
                std::int64_t m_StartDateUnixUs = kTimeUnset, m_EndDateUnixUs = kTimeUnset, m_DurationUs = kTimeUnset, m_PlannedDurationUs = kTimeUnset;
                std::vector<std::string> m_Cue;
                bool m_EndOnNext = false;
                std::int64_t m_ResumeOffsetUs = kTimeUnset, m_PlayoutLimitUs = kTimeUnset;
                std::vector<std::string> m_SnapTypes, m_Restrictions;
                std::map<std::string, ClientDefinedAttribute> m_ClientDefinedAttributes;
                std::optional<bool> m_ContentMayVary;
                std::optional<std::string> m_TimelineOccupies, m_TimelineStyle;
                std::int64_t m_SkipControlOffsetUs = kTimeUnset, m_SkipControlDurationUs = kTimeUnset;
                std::optional<std::string> m_SkipControlLabelId;
            };
        };

        HlsMediaPlaylist(int playlistType, std::string baseUri, std::vector<std::string> tags, std::int64_t startOffsetUs, bool preciseStart,
                         std::int64_t startTimeUs, bool hasDiscontinuitySequence, int discontinuitySequence, std::int64_t mediaSequence, int version,
                         std::int64_t targetDurationUs, std::int64_t partTargetDurationUs, bool hasIndependentSegments, bool hasEndTag, bool hasProgramDateTime,
                         std::optional<DrmInitData> protectionSchemes, std::vector<Segment> segments, std::vector<Part> trailingParts,
                         ServerControl serverControl, std::map<std::string, RenditionReport> renditionReports, std::vector<Interstitial> interstitials,
                         std::shared_ptr<const Segment> lastSeenInitSegment);
        bool IsMultivariant() const override { return false; }
        bool IsNewerThan(const HlsMediaPlaylist* other) const;
        std::int64_t GetEndTimeUs() const { return startTimeUs + durationUs; }
        HlsMediaPlaylist CopyWith(std::int64_t startTimeUs, int discontinuitySequence) const;
        HlsMediaPlaylist CopyWithEndTag() const;

        int playlistType;
        std::int64_t startOffsetUs;
        bool hasPositiveStartOffset;
        bool preciseStart;
        std::int64_t startTimeUs;
        bool hasDiscontinuitySequence;
        int discontinuitySequence;
        std::int64_t mediaSequence;
        int version;
        std::int64_t targetDurationUs;
        std::int64_t partTargetDurationUs;
        bool hasEndTag;
        bool hasProgramDateTime;
        std::optional<DrmInitData> protectionSchemes;
        std::vector<Segment> segments;
        std::vector<Part> trailingParts;
        std::map<std::string, RenditionReport> renditionReports;
        std::int64_t durationUs;
        ServerControl serverControl;
        std::vector<Interstitial> interstitials;
        std::shared_ptr<const Segment> lastSeenInitSegment;
    };

    // HlsPlaylistParser: an M3U8 into a multivariant or a media playlist. A media playlist is read
    // against its multivariant playlist (imported variables) and the previous version of itself
    // (delta updates, the last initialization segment).
    class HlsPlaylistParser {
    public:
        // A delta update that skips segments the previous playlist does not have.
        class DeltaUpdateException : public std::runtime_error {
        public:
            DeltaUpdateException() : std::runtime_error("delta update") {}
        };

        HlsPlaylistParser();
        HlsPlaylistParser(std::shared_ptr<const HlsMultivariantPlaylist> multivariantPlaylist, std::shared_ptr<const HlsMediaPlaylist> previousMediaPlaylist);
        // Throws stream::ParserException (or DeltaUpdateException).
        std::shared_ptr<HlsPlaylist> Parse(const std::string& uri, const std::string& content) const;

    private:
        std::shared_ptr<const HlsMultivariantPlaylist> m_Multivariant;
        std::shared_ptr<const HlsMediaPlaylist> m_Previous;
    };

}
