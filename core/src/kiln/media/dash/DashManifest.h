#pragma once

// media3-exoplayer-dash's manifest model (DashManifest, Period, AdaptationSet, Representation,
// SegmentBase, DashSegmentIndex and the descriptors) and DashManifestParser, which reads an MPD.

#include "kiln/media/TrackSelection.h"
#include "kiln/media/stream/StreamUtil.h"
#include "kiln/media/stream/Xml.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kiln::media::dash {

    using stream::DrmInitData;
    using stream::kIndexUnset;
    using stream::kLengthUnset;
    using stream::kTimeUnset;
    using stream::SchemeData;
    using stream::StreamKey;

    // C.RATE_UNSET.
    using media::kRateUnset;

    // RangedUri: a byte range of a URI, possibly relative to a base URL.
    struct RangedUri {
        RangedUri(std::string referenceUri, std::int64_t start, std::int64_t length) : start(start), length(length), referenceUri(std::move(referenceUri)) {}
        std::string ResolveUriString(const std::string& baseUri) const { return stream::ResolveUri(baseUri, referenceUri); }
        // The range covering this and the other, when they resolve to one URI and adjoin.
        std::optional<RangedUri> AttemptMerge(const std::optional<RangedUri>& other, const std::string& baseUri) const;
        bool operator==(const RangedUri&) const = default;
        std::int64_t start;
        std::int64_t length;
        std::string referenceUri;
    };

    struct BaseUrl {
        static constexpr int DEFAULT_WEIGHT = 1;
        static constexpr int DEFAULT_DVB_PRIORITY = 1;
        static constexpr int PRIORITY_UNSET = std::numeric_limits<int>::min();
        explicit BaseUrl(std::string url) : url(url), serviceLocation(std::move(url)) {}
        BaseUrl(std::string url, std::string serviceLocation, int priority, int weight)
            : url(std::move(url)), serviceLocation(std::move(serviceLocation)), priority(priority), weight(weight) {}
        bool operator==(const BaseUrl&) const = default;
        std::string url;
        std::string serviceLocation;
        int priority = PRIORITY_UNSET;
        int weight = DEFAULT_WEIGHT;
    };

    // Descriptor: a scheme element (Role, Accessibility, EssentialProperty and the like). A null
    // value or id is "".
    struct Descriptor {
        std::string schemeIdUri, value, id;
        bool operator==(const Descriptor&) const = default;
    };

    struct UtcTimingElement {
        std::string schemeIdUri, value;
        bool operator==(const UtcTimingElement&) const = default;
    };

    struct ServiceDescriptionElement {
        std::int64_t targetOffsetMs = kTimeUnset, minOffsetMs = kTimeUnset, maxOffsetMs = kTimeUnset;
        float minPlaybackSpeed = kRateUnset, maxPlaybackSpeed = kRateUnset;
        bool operator==(const ServiceDescriptionElement&) const = default;
    };

    struct ProgramInformation {
        std::string title, source, copyright, moreInformationURL, lang;
        bool operator==(const ProgramInformation&) const = default;
    };

    struct Location {
        explicit Location(std::string url) : url(url), serviceLocation(std::move(url)) {}
        Location(std::string url, std::string serviceLocation) : url(std::move(url)), serviceLocation(std::move(serviceLocation)) {}
        bool operator==(const Location&) const = default;
        std::string url;
        std::string serviceLocation;
    };

    // EventMessage (media3-extractor emsg): an event of an EventStream.
    struct EventMessage {
        std::string schemeIdUri, value;
        std::int64_t durationMs = kTimeUnset;
        std::int64_t id = 0;
        std::vector<std::uint8_t> messageData;
        bool operator==(const EventMessage&) const = default;
    };

    struct EventStream {
        std::vector<EventMessage> events;
        std::vector<std::int64_t> presentationTimesUs;
        std::string schemeIdUri, value;
        std::int64_t timescale = 1;
        std::string Id() const { return schemeIdUri + "/" + value; }
    };

    // UrlTemplate: a SegmentTemplate's $RepresentationID$, $Number$, $Bandwidth$ and $Time$.
    class UrlTemplate {
    public:
        // std::invalid_argument for an unknown identifier.
        static UrlTemplate Compile(const std::string& templateString);
        std::string BuildUri(const std::string& representationId, std::int64_t segmentNumber, int bandwidth, std::int64_t time) const;

    private:
        std::vector<std::string> m_UrlPieces;
        std::vector<int> m_Identifiers;
        std::vector<std::string> m_FormatTags;
    };

    struct SegmentTimelineElement {
        std::int64_t startTime = 0;
        std::int64_t duration = 0;
        bool operator==(const SegmentTimelineElement&) const = default;
    };

    // DashSegmentIndex: the segments of a representation.
    class DashSegmentIndex {
    public:
        static constexpr int INDEX_UNBOUNDED = -1;
        virtual ~DashSegmentIndex() = default;
        virtual std::int64_t GetSegmentNum(std::int64_t timeUs, std::int64_t periodDurationUs) const = 0;
        virtual std::int64_t GetTimeUs(std::int64_t segmentNum) const = 0;
        virtual std::int64_t GetDurationUs(std::int64_t segmentNum, std::int64_t periodDurationUs) const = 0;
        virtual RangedUri GetSegmentUrl(std::int64_t segmentNum) const = 0;
        virtual std::int64_t GetFirstSegmentNum() const = 0;
        virtual std::int64_t GetFirstAvailableSegmentNum(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const = 0;
        virtual std::int64_t GetSegmentCount(std::int64_t periodDurationUs) const = 0;
        virtual std::int64_t GetAvailableSegmentCount(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const = 0;
        virtual std::int64_t GetNextSegmentAvailableTimeUs(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const = 0;
        virtual bool IsExplicit() const = 0;
    };

    // SingleSegmentIndex: one segment, the whole of a single-segment representation.
    class SingleSegmentIndex final : public DashSegmentIndex {
    public:
        explicit SingleSegmentIndex(RangedUri uri) : m_Uri(std::move(uri)) {}
        std::int64_t GetSegmentNum(std::int64_t, std::int64_t) const override { return 0; }
        std::int64_t GetTimeUs(std::int64_t) const override { return 0; }
        std::int64_t GetDurationUs(std::int64_t, std::int64_t periodDurationUs) const override { return periodDurationUs; }
        RangedUri GetSegmentUrl(std::int64_t) const override { return m_Uri; }
        std::int64_t GetFirstSegmentNum() const override { return 0; }
        std::int64_t GetFirstAvailableSegmentNum(std::int64_t, std::int64_t) const override { return 0; }
        std::int64_t GetSegmentCount(std::int64_t) const override { return 1; }
        std::int64_t GetAvailableSegmentCount(std::int64_t, std::int64_t) const override { return 1; }
        std::int64_t GetNextSegmentAvailableTimeUs(std::int64_t, std::int64_t) const override { return kTimeUnset; }
        bool IsExplicit() const override { return true; }

    private:
        RangedUri m_Uri;
    };

    // SegmentBase: how a representation's segments are addressed.
    class SegmentBase {
    public:
        enum class Kind { Single, List, Template };
        virtual ~SegmentBase() = default;
        // The initialization segment (a template builds it from the format's id and bitrate).
        virtual std::optional<RangedUri> GetInitialization(const Format&) const { return initialization; }
        std::int64_t GetPresentationTimeOffsetUs() const { return stream::ScaleLargeTimestamp(presentationTimeOffset, stream::kMicrosPerSecond, timescale); }
        virtual std::shared_ptr<SegmentBase> CopyWithPresentationTimeOffset(std::int64_t newPresentationTimeOffset) const = 0;

        Kind kind;
        std::optional<RangedUri> initialization;
        std::int64_t timescale;
        std::int64_t presentationTimeOffset;

    protected:
        SegmentBase(Kind kind, std::optional<RangedUri> initialization, std::int64_t timescale, std::int64_t presentationTimeOffset)
            : kind(kind), initialization(std::move(initialization)), timescale(timescale), presentationTimeOffset(presentationTimeOffset) {}
    };

    class SingleSegmentBase final : public SegmentBase {
    public:
        SingleSegmentBase() : SingleSegmentBase(std::nullopt, 1, 0, 0, 0) {}
        SingleSegmentBase(std::optional<RangedUri> initialization, std::int64_t timescale, std::int64_t presentationTimeOffset, std::int64_t indexStart,
                          std::int64_t indexLength)
            : SegmentBase(Kind::Single, std::move(initialization), timescale, presentationTimeOffset), indexStart(indexStart), indexLength(indexLength) {}
        std::optional<RangedUri> GetIndex() const {
            if (indexLength <= 0) return std::nullopt;
            return RangedUri("", indexStart, indexLength);
        }
        std::shared_ptr<SegmentBase> CopyWithPresentationTimeOffset(std::int64_t offset) const override {
            return std::make_shared<SingleSegmentBase>(initialization, timescale, offset, indexStart, indexLength);
        }
        std::int64_t indexStart;
        std::int64_t indexLength;
    };

    class MultiSegmentBase : public SegmentBase {
    public:
        std::int64_t GetSegmentNum(std::int64_t timeUs, std::int64_t periodDurationUs) const;
        std::int64_t GetSegmentDurationUs(std::int64_t sequenceNumber, std::int64_t periodDurationUs) const;
        std::int64_t GetSegmentTimeUs(std::int64_t sequenceNumber) const;
        virtual RangedUri GetSegmentUrl(const Format& format, std::int64_t index) const = 0;
        std::int64_t GetFirstSegmentNum() const { return startNumber; }
        std::int64_t GetFirstAvailableSegmentNum(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const;
        std::int64_t GetAvailableSegmentCount(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const;
        std::int64_t GetNextSegmentAvailableTimeUs(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const;
        virtual bool IsExplicit() const { return segmentTimeline.has_value(); }
        virtual std::int64_t GetSegmentCount(std::int64_t periodDurationUs) const = 0;

        std::int64_t startNumber;
        std::int64_t duration;
        std::optional<std::vector<SegmentTimelineElement>> segmentTimeline;
        std::int64_t timeShiftBufferDepthUs;
        std::int64_t periodStartUnixTimeUs;
        std::int64_t availabilityTimeOffsetUs;

    protected:
        MultiSegmentBase(Kind kind, std::optional<RangedUri> initialization, std::int64_t timescale, std::int64_t presentationTimeOffset, std::int64_t startNumber,
                         std::int64_t duration, std::optional<std::vector<SegmentTimelineElement>> segmentTimeline, std::int64_t availabilityTimeOffsetUs,
                         std::int64_t timeShiftBufferDepthUs, std::int64_t periodStartUnixTimeUs)
            : SegmentBase(kind, std::move(initialization), timescale, presentationTimeOffset), startNumber(startNumber), duration(duration),
              segmentTimeline(std::move(segmentTimeline)), timeShiftBufferDepthUs(timeShiftBufferDepthUs), periodStartUnixTimeUs(periodStartUnixTimeUs),
              availabilityTimeOffsetUs(availabilityTimeOffsetUs) {}
    };

    class SegmentList final : public MultiSegmentBase {
    public:
        SegmentList(std::optional<RangedUri> initialization, std::int64_t timescale, std::int64_t presentationTimeOffset, std::int64_t startNumber,
                    std::int64_t duration, std::optional<std::vector<SegmentTimelineElement>> segmentTimeline, std::int64_t availabilityTimeOffsetUs,
                    std::optional<std::vector<RangedUri>> mediaSegments, std::int64_t timeShiftBufferDepthUs, std::int64_t periodStartUnixTimeUs)
            : MultiSegmentBase(Kind::List, std::move(initialization), timescale, presentationTimeOffset, startNumber, duration, std::move(segmentTimeline),
                               availabilityTimeOffsetUs, timeShiftBufferDepthUs, periodStartUnixTimeUs),
              mediaSegments(std::move(mediaSegments)) {}
        RangedUri GetSegmentUrl(const Format&, std::int64_t sequenceNumber) const override {
            return mediaSegments.value().at(std::size_t(sequenceNumber - startNumber));
        }
        std::int64_t GetSegmentCount(std::int64_t) const override { return std::int64_t(mediaSegments.value().size()); }
        bool IsExplicit() const override { return true; }
        std::shared_ptr<SegmentBase> CopyWithPresentationTimeOffset(std::int64_t offset) const override;
        std::optional<std::vector<RangedUri>> mediaSegments;
    };

    class SegmentTemplate final : public MultiSegmentBase {
    public:
        SegmentTemplate(std::optional<RangedUri> initialization, std::int64_t timescale, std::int64_t presentationTimeOffset, std::int64_t startNumber,
                        std::int64_t endNumber, std::int64_t duration, std::optional<std::vector<SegmentTimelineElement>> segmentTimeline,
                        std::int64_t availabilityTimeOffsetUs, std::optional<UrlTemplate> initializationTemplate, std::optional<UrlTemplate> mediaTemplate,
                        std::int64_t timeShiftBufferDepthUs, std::int64_t periodStartUnixTimeUs)
            : MultiSegmentBase(Kind::Template, std::move(initialization), timescale, presentationTimeOffset, startNumber, duration, std::move(segmentTimeline),
                               availabilityTimeOffsetUs, timeShiftBufferDepthUs, periodStartUnixTimeUs),
              initializationTemplate(std::move(initializationTemplate)), mediaTemplate(std::move(mediaTemplate)), endNumber(endNumber) {}
        std::optional<RangedUri> GetInitialization(const Format& format) const override;
        RangedUri GetSegmentUrl(const Format& format, std::int64_t sequenceNumber) const override;
        std::int64_t GetSegmentCount(std::int64_t periodDurationUs) const override;
        std::shared_ptr<SegmentBase> CopyWithPresentationTimeOffset(std::int64_t offset) const override;
        std::optional<UrlTemplate> initializationTemplate;
        std::optional<UrlTemplate> mediaTemplate;
        std::int64_t endNumber;
    };

    // Representation: one encoding of an adaptation set (a single file, or segments).
    class Representation {
    public:
        static constexpr std::int64_t REVISION_ID_DEFAULT = -1;
        enum class Kind { SingleSegment, MultiSegment };
        static std::shared_ptr<Representation> NewInstance(std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls,
                                                           std::shared_ptr<const SegmentBase> segmentBase, std::vector<Descriptor> inbandEventStreams = {},
                                                           std::vector<Descriptor> essentialProperties = {}, std::vector<Descriptor> supplementalProperties = {},
                                                           std::string cacheKey = "");
        virtual ~Representation() = default;
        const std::optional<RangedUri>& GetInitializationUri() const { return m_InitializationUri; }
        virtual std::optional<RangedUri> GetIndexUri() const = 0;
        // The segment index; a single-segment representation with an index range has none until
        // its sidx box is loaded.
        virtual const DashSegmentIndex* GetIndex() const = 0;
        virtual const std::string& GetCacheKey() const = 0;

        Kind kind;
        std::int64_t revisionId;
        Format format;
        std::vector<BaseUrl> baseUrls;
        std::int64_t presentationTimeOffsetUs;
        std::vector<Descriptor> inbandEventStreams;
        std::vector<Descriptor> essentialProperties;
        std::vector<Descriptor> supplementalProperties;

    protected:
        Representation(Kind kind, std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls, const SegmentBase& segmentBase,
                       std::vector<Descriptor> inbandEventStreams, std::vector<Descriptor> essentialProperties, std::vector<Descriptor> supplementalProperties);

    private:
        std::optional<RangedUri> m_InitializationUri;
    };

    class SingleSegmentRepresentation final : public Representation {
    public:
        SingleSegmentRepresentation(std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls, std::shared_ptr<const SingleSegmentBase> segmentBase,
                                    std::vector<Descriptor> inbandEventStreams, std::vector<Descriptor> essentialProperties,
                                    std::vector<Descriptor> supplementalProperties, std::string cacheKey, std::int64_t contentLength);
        std::optional<RangedUri> GetIndexUri() const override { return m_IndexUri; }
        const DashSegmentIndex* GetIndex() const override { return m_SegmentIndex ? &*m_SegmentIndex : nullptr; }
        const std::string& GetCacheKey() const override { return m_CacheKey; }
        std::string uri;
        std::int64_t contentLength;

    private:
        std::string m_CacheKey;
        std::optional<RangedUri> m_IndexUri;
        std::optional<SingleSegmentIndex> m_SegmentIndex;
    };

    class MultiSegmentRepresentation final : public Representation, public DashSegmentIndex {
    public:
        MultiSegmentRepresentation(std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls, std::shared_ptr<const MultiSegmentBase> segmentBase,
                                   std::vector<Descriptor> inbandEventStreams, std::vector<Descriptor> essentialProperties,
                                   std::vector<Descriptor> supplementalProperties);
        std::optional<RangedUri> GetIndexUri() const override { return std::nullopt; }
        const DashSegmentIndex* GetIndex() const override { return this; }
        const std::string& GetCacheKey() const override { return m_NoCacheKey; }
        RangedUri GetSegmentUrl(std::int64_t segmentNum) const override { return segmentBase->GetSegmentUrl(format, segmentNum); }
        std::int64_t GetSegmentNum(std::int64_t timeUs, std::int64_t periodDurationUs) const override { return segmentBase->GetSegmentNum(timeUs, periodDurationUs); }
        std::int64_t GetTimeUs(std::int64_t segmentNum) const override { return segmentBase->GetSegmentTimeUs(segmentNum); }
        std::int64_t GetDurationUs(std::int64_t segmentNum, std::int64_t periodDurationUs) const override {
            return segmentBase->GetSegmentDurationUs(segmentNum, periodDurationUs);
        }
        std::int64_t GetFirstSegmentNum() const override { return segmentBase->GetFirstSegmentNum(); }
        std::int64_t GetFirstAvailableSegmentNum(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const override {
            return segmentBase->GetFirstAvailableSegmentNum(periodDurationUs, nowUnixTimeUs);
        }
        std::int64_t GetSegmentCount(std::int64_t periodDurationUs) const override { return segmentBase->GetSegmentCount(periodDurationUs); }
        std::int64_t GetAvailableSegmentCount(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const override {
            return segmentBase->GetAvailableSegmentCount(periodDurationUs, nowUnixTimeUs);
        }
        std::int64_t GetNextSegmentAvailableTimeUs(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const override {
            return segmentBase->GetNextSegmentAvailableTimeUs(periodDurationUs, nowUnixTimeUs);
        }
        bool IsExplicit() const override { return segmentBase->IsExplicit(); }
        std::shared_ptr<const MultiSegmentBase> segmentBase;

    private:
        const std::string m_NoCacheKey;
    };

    struct AdaptationSet {
        static constexpr std::int64_t ID_UNSET = -1;
        std::int64_t id = ID_UNSET;
        TrackType type = TrackType::Unknown;
        std::vector<std::shared_ptr<const Representation>> representations;
        std::vector<Descriptor> accessibilityDescriptors;
        std::vector<Descriptor> essentialProperties;
        std::vector<Descriptor> supplementalProperties;
    };

    struct Period {
        std::string id;   // "" for null
        std::int64_t startMs = 0;
        std::vector<AdaptationSet> adaptationSets;
        std::vector<EventStream> eventStreams;
        std::optional<Descriptor> assetIdentifier;
        // The first adaptation set of the type, or kIndexUnset.
        int GetAdaptationSetIndex(TrackType type) const;
    };

    class DashManifest {
    public:
        DashManifest(std::int64_t availabilityStartTimeMs, std::int64_t durationMs, std::int64_t minBufferTimeMs, bool dynamic, std::int64_t minUpdatePeriodMs,
                     std::int64_t timeShiftBufferDepthMs, std::int64_t suggestedPresentationDelayMs, std::int64_t publishTimeMs,
                     std::optional<ProgramInformation> programInformation, std::optional<UtcTimingElement> utcTiming,
                     std::optional<ServiceDescriptionElement> serviceDescription, std::vector<Period> periods, std::vector<Location> locations);
        int GetPeriodCount() const { return int(m_Periods.size()); }
        const Period& GetPeriod(int index) const { return m_Periods.at(std::size_t(index)); }
        std::int64_t GetPeriodDurationMs(int index) const;
        std::int64_t GetPeriodDurationUs(int index) const { return stream::MsToUs(GetPeriodDurationMs(index)); }
        // The manifest with only these streams (periods without any removed, later ones shifted).
        DashManifest Copy(const std::vector<StreamKey>& streamKeys) const;

        std::int64_t availabilityStartTimeMs;
        std::int64_t durationMs;
        std::int64_t minBufferTimeMs;
        bool dynamic;
        std::int64_t minUpdatePeriodMs;
        std::int64_t timeShiftBufferDepthMs;
        std::int64_t suggestedPresentationDelayMs;
        std::int64_t publishTimeMs;
        std::optional<UtcTimingElement> utcTiming;
        std::optional<ServiceDescriptionElement> serviceDescription;
        std::optional<ProgramInformation> programInformation;
        std::vector<Location> locations;
        std::string location;   // the last location's URL, "" without any

    private:
        std::vector<Period> m_Periods;
    };

    // DashManifestParser: an MPD into a DashManifest (stream::ParserException when malformed).
    // The pieces upstream's tests call are public.
    class DashManifestParser {
    public:
        std::shared_ptr<DashManifest> Parse(const std::string& uri, const std::string& content) const;

        std::vector<SegmentTimelineElement> ParseSegmentTimeline(stream::XmlPullParser& xpp, std::int64_t timescale, std::int64_t periodDurationMs) const;
        Label ParseLabel(stream::XmlPullParser& xpp) const;
        static int ParseCea608AccessibilityChannel(const std::vector<Descriptor>& accessibilityDescriptors);
        static int ParseCea708AccessibilityChannel(const std::vector<Descriptor>& accessibilityDescriptors);
        // Skips the element the parser is on the START_TAG of, children included.
        static void MaybeSkipTag(stream::XmlPullParser& xpp);
    };

}
