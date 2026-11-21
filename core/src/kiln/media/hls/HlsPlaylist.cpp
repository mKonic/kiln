#include "kiln/media/hls/HlsPlaylist.h"

#include <algorithm>
#include <stdexcept>

namespace kiln::media::hls {

    // --- HlsMultivariantPlaylist ---------------------------------------------------------------

    HlsMultivariantPlaylist::Variant HlsMultivariantPlaylist::Variant::CreateMediaPlaylistVariantUrl(const std::string& url) {
        Variant v;
        v.url = url;
        v.format.id = "0";
        v.format.containerMimeType = stream::MimeTypes::APPLICATION_M3U8;
        return v;
    }

    namespace {
        void AddMediaPlaylistUrl(std::vector<std::string>& out, const std::string& url) {
            if (!url.empty() && std::find(out.begin(), out.end(), url) == out.end()) out.push_back(url);
        }

        template<typename T>
        std::vector<T> CopyStreams(const std::vector<T>& streams, int groupIndex, const std::vector<StreamKey>& streamKeys) {
            std::vector<T> copied;
            for (std::size_t i = 0; i < streams.size(); i++) {
                for (const StreamKey& key : streamKeys) {
                    if (key.groupIndex == groupIndex && key.streamIndex == int(i)) {
                        copied.push_back(streams[i]);
                        break;
                    }
                }
            }
            return copied;
        }
    }

    HlsMultivariantPlaylist::HlsMultivariantPlaylist(std::string baseUri, std::vector<std::string> tags, std::vector<Variant> variants,
                                                     std::vector<Rendition> videos, std::vector<Rendition> audios, std::vector<Rendition> subtitles,
                                                     std::vector<Rendition> closedCaptions, std::optional<Format> muxedAudioFormat,
                                                     std::optional<std::vector<Format>> muxedCaptionFormats, bool hasIndependentSegments,
                                                     std::map<std::string, std::string> variableDefinitions, std::vector<DrmInitData> sessionKeyDrmInitData,
                                                     std::optional<ContentSteeringInfo> contentSteeringInfo)
        : HlsPlaylist(std::move(baseUri), std::move(tags), hasIndependentSegments), variants(std::move(variants)), videos(std::move(videos)),
          audios(std::move(audios)), subtitles(std::move(subtitles)), closedCaptions(std::move(closedCaptions)), muxedAudioFormat(std::move(muxedAudioFormat)),
          muxedCaptionFormats(std::move(muxedCaptionFormats)), variableDefinitions(std::move(variableDefinitions)),
          sessionKeyDrmInitData(std::move(sessionKeyDrmInitData)), contentSteeringInfo(std::move(contentSteeringInfo)) {
        for (const Variant& v : this->variants) AddMediaPlaylistUrl(mediaPlaylistUrls, v.url);
        for (const auto* list : { &this->videos, &this->audios, &this->subtitles, &this->closedCaptions })
            for (const Rendition& r : *list) AddMediaPlaylistUrl(mediaPlaylistUrls, r.url);
    }

    const HlsMultivariantPlaylist& HlsMultivariantPlaylist::Empty() {
        static const HlsMultivariantPlaylist empty("", {}, {}, {}, {}, {}, {}, std::nullopt, std::vector<Format>{}, false, {}, {}, std::nullopt);
        return empty;
    }

    HlsMultivariantPlaylist HlsMultivariantPlaylist::CreateSingleVariantMultivariantPlaylist(const std::string& variantUrl) {
        return HlsMultivariantPlaylist("", {}, { Variant::CreateMediaPlaylistVariantUrl(variantUrl) }, {}, {}, {}, {}, std::nullopt, std::nullopt, false, {},
                                       {}, std::nullopt);
    }

    HlsMultivariantPlaylist HlsMultivariantPlaylist::Copy(const std::vector<StreamKey>& streamKeys) const {
        return HlsMultivariantPlaylist(baseUri, tags, CopyStreams(variants, GROUP_INDEX_VARIANT, streamKeys), {}, CopyStreams(audios, GROUP_INDEX_AUDIO, streamKeys),
                                       CopyStreams(subtitles, GROUP_INDEX_SUBTITLE, streamKeys), {}, muxedAudioFormat, muxedCaptionFormats,
                                       hasIndependentSegments, variableDefinitions, sessionKeyDrmInitData, contentSteeringInfo);
    }

    // --- HlsMediaPlaylist ----------------------------------------------------------------------

    HlsMediaPlaylist::Segment HlsMediaPlaylist::Segment::Init(std::string uri, std::int64_t byteRangeOffset, std::int64_t byteRangeLength, std::string keyUri,
                                                              std::string iv) {
        Segment s;
        s.url = std::move(uri);
        s.relativeDiscontinuitySequence = -1;
        s.relativeStartTimeUs = kTimeUnset;
        s.fullSegmentEncryptionKeyUri = std::move(keyUri);
        s.encryptionIV = std::move(iv);
        s.byteRangeOffset = byteRangeOffset;
        s.byteRangeLength = byteRangeLength;
        return s;
    }

    HlsMediaPlaylist::Segment HlsMediaPlaylist::Segment::CopyWith(std::int64_t relativeStartTimeUs, int relativeDiscontinuitySequence) const {
        Segment s = *this;
        s.relativeStartTimeUs = relativeStartTimeUs;
        s.relativeDiscontinuitySequence = relativeDiscontinuitySequence;
        std::int64_t partStart = relativeStartTimeUs;
        for (Part& part : s.parts) {
            part = part.CopyWith(partStart, relativeDiscontinuitySequence);
            partStart += part.durationUs;
        }
        return s;
    }

    HlsMediaPlaylist::ClientDefinedAttribute::ClientDefinedAttribute(std::string n, std::string value, int t) : name(std::move(n)), type(t), m_Text(std::move(value)) {
        if (t == TYPE_HEX_TEXT && m_Text.rfind("0x", 0) != 0 && m_Text.rfind("0X", 0) != 0) throw std::logic_error("hex text without 0x");
    }
    double HlsMediaPlaylist::ClientDefinedAttribute::GetDoubleValue() const {
        if (type != TYPE_DOUBLE) throw std::logic_error("not a double attribute");
        return m_Double;
    }
    const std::string& HlsMediaPlaylist::ClientDefinedAttribute::GetTextValue() const {
        if (type == TYPE_DOUBLE) throw std::logic_error("not a text attribute");
        return m_Text;
    }

    namespace {
        template<typename T>
        void Check(const T& current, const T& next, const char* what) {
            if (current != next) throw std::invalid_argument(std::string("Can't change ") + what);
        }
    }

    using Builder = HlsMediaPlaylist::Interstitial::Builder;

    Builder& Builder::SetAssetUri(const std::optional<std::string>& v) {
        if (!v) return *this;
        if (m_AssetUri) Check(*m_AssetUri, *v, "assetUri");
        m_AssetUri = v;
        return *this;
    }
    Builder& Builder::SetAssetListUri(const std::optional<std::string>& v) {
        if (!v) return *this;
        if (m_AssetListUri) Check(*m_AssetListUri, *v, "assetListUri");
        m_AssetListUri = v;
        return *this;
    }
#define KILN_TIME_SETTER(Name, member, what)                       \
    Builder& Builder::Name(std::int64_t v) {                       \
        if (v == kTimeUnset) return *this;                         \
        if (member != kTimeUnset) Check(member, v, what);          \
        member = v;                                                \
        return *this;                                              \
    }
    KILN_TIME_SETTER(SetStartDateUnixUs, m_StartDateUnixUs, "startDateUnixUs")
    KILN_TIME_SETTER(SetEndDateUnixUs, m_EndDateUnixUs, "endDateUnixUs")
    KILN_TIME_SETTER(SetDurationUs, m_DurationUs, "durationUs")
    KILN_TIME_SETTER(SetPlannedDurationUs, m_PlannedDurationUs, "plannedDurationUs")
    KILN_TIME_SETTER(SetResumeOffsetUs, m_ResumeOffsetUs, "resumeOffsetUs")
    KILN_TIME_SETTER(SetPlayoutLimitUs, m_PlayoutLimitUs, "playoutLimitUs")
    KILN_TIME_SETTER(SetSkipControlOffsetUs, m_SkipControlOffsetUs, "skipControlOffsetUs")
    KILN_TIME_SETTER(SetSkipControlDurationUs, m_SkipControlDurationUs, "skipControlDurationUs")
#undef KILN_TIME_SETTER
#define KILN_LIST_SETTER(Name, member, what)                       \
    Builder& Builder::Name(const std::vector<std::string>& v) {   \
        if (v.empty()) return *this;                               \
        if (!member.empty()) Check(member, v, what);               \
        member = v;                                                \
        return *this;                                              \
    }
    KILN_LIST_SETTER(SetCue, m_Cue, "cue")
    KILN_LIST_SETTER(SetSnapTypes, m_SnapTypes, "snapTypes")
    KILN_LIST_SETTER(SetRestrictions, m_Restrictions, "restrictions")
#undef KILN_LIST_SETTER
#define KILN_STRING_SETTER(Name, member, what)                       \
    Builder& Builder::Name(const std::optional<std::string>& v) {   \
        if (!v) return *this;                                        \
        if (member) Check(*member, *v, what);                        \
        member = v;                                                  \
        return *this;                                                \
    }
    KILN_STRING_SETTER(SetTimelineOccupies, m_TimelineOccupies, "timelineOccupies")
    KILN_STRING_SETTER(SetTimelineStyle, m_TimelineStyle, "timelineStyle")
    KILN_STRING_SETTER(SetSkipControlLabelId, m_SkipControlLabelId, "skipControlLabelId")
#undef KILN_STRING_SETTER

    Builder& Builder::SetEndOnNext(bool v) {
        if (!v) return *this;
        m_EndOnNext = true;
        return *this;
    }
    Builder& Builder::SetContentMayVary(std::optional<bool> v) {
        if (!v) return *this;
        if (m_ContentMayVary) Check(*m_ContentMayVary, *v, "contentMayVary");
        m_ContentMayVary = v;
        return *this;
    }
    Builder& Builder::SetClientDefinedAttributes(const std::vector<ClientDefinedAttribute>& v) {
        for (const ClientDefinedAttribute& attribute : v) {
            const auto existing = m_ClientDefinedAttributes.find(attribute.name);
            if (existing != m_ClientDefinedAttributes.end()) {
                if (!(existing->second == attribute)) throw std::invalid_argument("Can't change " + attribute.name);
                existing->second = attribute;
            } else {
                m_ClientDefinedAttributes.emplace(attribute.name, attribute);
            }
        }
        return *this;
    }

    std::optional<HlsMediaPlaylist::Interstitial> Builder::Build() const {
        if (!(((!m_AssetListUri && m_AssetUri) || (m_AssetListUri && !m_AssetUri)) && m_StartDateUnixUs != kTimeUnset)) return std::nullopt;
        Interstitial i;
        i.id = m_Id;
        i.assetUri = m_AssetUri.value_or("");
        i.assetListUri = m_AssetListUri.value_or("");
        i.startDateUnixUs = m_StartDateUnixUs;
        i.endDateUnixUs = m_EndDateUnixUs;
        i.durationUs = m_DurationUs;
        i.plannedDurationUs = m_PlannedDurationUs;
        i.cue = m_Cue;
        i.endOnNext = m_EndOnNext;
        i.resumeOffsetUs = m_ResumeOffsetUs;
        i.playoutLimitUs = m_PlayoutLimitUs;
        i.snapTypes = m_SnapTypes;
        i.restrictions = m_Restrictions;
        for (const auto& [name, attribute] : m_ClientDefinedAttributes) i.clientDefinedAttributes.push_back(attribute);   // map: sorted by name
        i.contentMayVary = !m_ContentMayVary || *m_ContentMayVary;
        i.timelineOccupies = m_TimelineOccupies.value_or(Interstitial::TIMELINE_OCCUPIES_POINT);
        i.timelineStyle = m_TimelineStyle.value_or(Interstitial::TIMELINE_STYLE_HIGHLIGHT);
        i.skipControlOffsetUs = m_SkipControlOffsetUs;
        i.skipControlDurationUs = m_SkipControlDurationUs;
        i.skipControlLabelId = m_SkipControlLabelId.value_or("");
        return i;
    }

    HlsMediaPlaylist::HlsMediaPlaylist(int type, std::string baseUri, std::vector<std::string> tags, std::int64_t startOffset, bool precise,
                                       std::int64_t startTime, bool hasDiscontinuitySeq, int discontinuitySeq, std::int64_t mediaSeq, int ver,
                                       std::int64_t targetDuration, std::int64_t partTargetDuration, bool hasIndependentSegments, bool endTag,
                                       bool programDateTime, std::optional<DrmInitData> protection, std::vector<Segment> segs, std::vector<Part> parts,
                                       ServerControl control, std::map<std::string, RenditionReport> reports, std::vector<Interstitial> inters,
                                       std::shared_ptr<const Segment> lastInit)
        : HlsPlaylist(std::move(baseUri), std::move(tags), hasIndependentSegments), playlistType(type), preciseStart(precise), startTimeUs(startTime),
          hasDiscontinuitySequence(hasDiscontinuitySeq), discontinuitySequence(discontinuitySeq), mediaSequence(mediaSeq), version(ver),
          targetDurationUs(targetDuration), partTargetDurationUs(partTargetDuration), hasEndTag(endTag), hasProgramDateTime(programDateTime),
          protectionSchemes(std::move(protection)), segments(std::move(segs)), trailingParts(std::move(parts)), renditionReports(std::move(reports)),
          serverControl(control), interstitials(std::move(inters)), lastSeenInitSegment(std::move(lastInit)) {
        if (!trailingParts.empty()) durationUs = trailingParts.back().relativeStartTimeUs + trailingParts.back().durationUs;
        else if (!segments.empty()) durationUs = segments.back().relativeStartTimeUs + segments.back().durationUs;
        else durationUs = 0;
        startOffsetUs = startOffset == kTimeUnset ? kTimeUnset : startOffset >= 0 ? std::min(durationUs, startOffset) : std::max<std::int64_t>(0, durationUs + startOffset);
        hasPositiveStartOffset = startOffset >= 0;
    }

    bool HlsMediaPlaylist::IsNewerThan(const HlsMediaPlaylist* other) const {
        if (other == nullptr || mediaSequence > other->mediaSequence) return true;
        if (mediaSequence < other->mediaSequence) return false;
        const long segmentCountDifference = long(segments.size()) - long(other->segments.size());
        if (segmentCountDifference != 0) return segmentCountDifference > 0;
        const std::size_t partCount = trailingParts.size(), otherPartCount = other->trailingParts.size();
        return partCount > otherPartCount || (partCount == otherPartCount && hasEndTag && !other->hasEndTag);
    }

    HlsMediaPlaylist HlsMediaPlaylist::CopyWith(std::int64_t newStartTimeUs, int newDiscontinuitySequence) const {
        return HlsMediaPlaylist(playlistType, baseUri, tags, startOffsetUs, preciseStart, newStartTimeUs, true, newDiscontinuitySequence, mediaSequence, version,
                                targetDurationUs, partTargetDurationUs, hasIndependentSegments, hasEndTag, hasProgramDateTime, protectionSchemes, segments,
                                trailingParts, serverControl, renditionReports, interstitials, lastSeenInitSegment);
    }

    HlsMediaPlaylist HlsMediaPlaylist::CopyWithEndTag() const {
        if (hasEndTag) return *this;
        return HlsMediaPlaylist(playlistType, baseUri, tags, startOffsetUs, preciseStart, startTimeUs, hasDiscontinuitySequence, discontinuitySequence,
                                mediaSequence, version, targetDurationUs, partTargetDurationUs, hasIndependentSegments, true, hasProgramDateTime,
                                protectionSchemes, segments, trailingParts, serverControl, renditionReports, interstitials, lastSeenInitSegment);
    }

}
