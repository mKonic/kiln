#include "kiln/media/dash/DashManifest.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <stdexcept>

namespace kiln::media::dash {

    std::optional<RangedUri> RangedUri::AttemptMerge(const std::optional<RangedUri>& other, const std::string& baseUri) const {
        const std::string resolvedUri = ResolveUriString(baseUri);
        if (!other || resolvedUri != other->ResolveUriString(baseUri)) return std::nullopt;
        if (length != kLengthUnset && start + length == other->start)
            return RangedUri(resolvedUri, start, other->length == kLengthUnset ? kLengthUnset : length + other->length);
        if (other->length != kLengthUnset && other->start + other->length == start)
            return RangedUri(resolvedUri, other->start, length == kLengthUnset ? kLengthUnset : other->length + length);
        return std::nullopt;
    }

    // --- UrlTemplate -------------------------------------------------------------------------

    namespace {
        constexpr int REPRESENTATION_ID = 1, NUMBER_ID = 2, BANDWIDTH_ID = 3, TIME_ID = 4;

        // String.format(Locale.US, tag, value) for the %0Nd / %0Nx / %0NX tags a template allows.
        std::string FormatValue(const std::string& tag, std::int64_t value) {
            std::string format = tag.substr(0, tag.size() - 1) + "ll" + tag.back();
            char buf[64];
            std::snprintf(buf, sizeof buf, format.c_str(), static_cast<long long>(value));
            return buf;
        }
    }

    UrlTemplate UrlTemplate::Compile(const std::string& t) {
        UrlTemplate result;
        auto& pieces = result.m_UrlPieces;
        auto& identifiers = result.m_Identifiers;
        auto& tags = result.m_FormatTags;
        pieces.push_back("");
        std::size_t index = 0;
        while (index < t.size()) {
            const std::size_t dollar = t.find('$', index);
            if (dollar == std::string::npos) {
                pieces[identifiers.size()] += t.substr(index);
                index = t.size();
            } else if (dollar != index) {
                pieces[identifiers.size()] += t.substr(index, dollar - index);
                index = dollar;
            } else if (t.compare(index, 2, "$$") == 0) {
                pieces[identifiers.size()] += "$";
                index += 2;
            } else {
                tags.push_back("");
                const std::size_t second = t.find('$', index + 1);
                if (second == std::string::npos) throw std::invalid_argument("Invalid template: " + t);
                std::string identifier = t.substr(index + 1, second - index - 1);
                if (identifier == "RepresentationID") {
                    identifiers.push_back(REPRESENTATION_ID);
                } else {
                    const std::size_t formatTagIndex = identifier.find("%0");
                    std::string formatTag = "%01d";
                    if (formatTagIndex != std::string::npos) {
                        formatTag = identifier.substr(formatTagIndex);
                        if (formatTag.back() != 'd' && formatTag.back() != 'x' && formatTag.back() != 'X') formatTag += "d";
                        identifier = identifier.substr(0, formatTagIndex);
                    }
                    if (identifier == "Number") identifiers.push_back(NUMBER_ID);
                    else if (identifier == "Bandwidth") identifiers.push_back(BANDWIDTH_ID);
                    else if (identifier == "Time") identifiers.push_back(TIME_ID);
                    else throw std::invalid_argument("Invalid template: " + t);
                    tags[identifiers.size() - 1] = formatTag;
                }
                pieces.push_back("");
                index = second + 1;
            }
        }
        return result;
    }

    std::string UrlTemplate::BuildUri(const std::string& representationId, std::int64_t segmentNumber, int bandwidth, std::int64_t time) const {
        std::string out;
        for (std::size_t i = 0; i < m_Identifiers.size(); i++) {
            out += m_UrlPieces[i];
            switch (m_Identifiers[i]) {
                case REPRESENTATION_ID: out += representationId; break;
                case NUMBER_ID: out += FormatValue(m_FormatTags[i], segmentNumber); break;
                case BANDWIDTH_ID: out += FormatValue(m_FormatTags[i], bandwidth); break;
                case TIME_ID: out += FormatValue(m_FormatTags[i], time); break;
                default: break;
            }
        }
        out += m_UrlPieces[m_Identifiers.size()];
        return out;
    }

    // --- MultiSegmentBase --------------------------------------------------------------------

    std::int64_t MultiSegmentBase::GetSegmentNum(std::int64_t timeUs, std::int64_t periodDurationUs) const {
        const std::int64_t firstSegmentNum = GetFirstSegmentNum();
        const std::int64_t segmentCount = GetSegmentCount(periodDurationUs);
        if (segmentCount == 0) return firstSegmentNum;
        if (!segmentTimeline) {
            const std::int64_t durationUs = (duration * stream::kMicrosPerSecond) / timescale;
            const std::int64_t segmentNum = startNumber + timeUs / durationUs;
            return segmentNum < firstSegmentNum ? firstSegmentNum
                   : segmentCount == DashSegmentIndex::INDEX_UNBOUNDED ? segmentNum
                                                                        : std::min(segmentNum, firstSegmentNum + segmentCount - 1);
        }
        std::int64_t lowIndex = firstSegmentNum;
        std::int64_t highIndex = firstSegmentNum + segmentCount - 1;
        while (lowIndex <= highIndex) {
            const std::int64_t midIndex = lowIndex + (highIndex - lowIndex) / 2;
            const std::int64_t midTimeUs = GetSegmentTimeUs(midIndex);
            if (midTimeUs < timeUs) lowIndex = midIndex + 1;
            else if (midTimeUs > timeUs) highIndex = midIndex - 1;
            else return midIndex;
        }
        return lowIndex == firstSegmentNum ? lowIndex : highIndex;
    }

    std::int64_t MultiSegmentBase::GetSegmentDurationUs(std::int64_t sequenceNumber, std::int64_t periodDurationUs) const {
        if (segmentTimeline) {
            const std::int64_t d = segmentTimeline->at(std::size_t(sequenceNumber - startNumber)).duration;
            return (d * stream::kMicrosPerSecond) / timescale;
        }
        const std::int64_t segmentCount = GetSegmentCount(periodDurationUs);
        return segmentCount != DashSegmentIndex::INDEX_UNBOUNDED && sequenceNumber == GetFirstSegmentNum() + segmentCount - 1
                   ? periodDurationUs - GetSegmentTimeUs(sequenceNumber)
                   : (duration * stream::kMicrosPerSecond) / timescale;
    }

    std::int64_t MultiSegmentBase::GetSegmentTimeUs(std::int64_t sequenceNumber) const {
        const std::int64_t unscaledSegmentTime = segmentTimeline ? segmentTimeline->at(std::size_t(sequenceNumber - startNumber)).startTime - presentationTimeOffset
                                                                 : (sequenceNumber - startNumber) * duration;
        return stream::ScaleLargeTimestamp(unscaledSegmentTime, stream::kMicrosPerSecond, timescale);
    }

    std::int64_t MultiSegmentBase::GetFirstAvailableSegmentNum(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const {
        const std::int64_t segmentCount = GetSegmentCount(periodDurationUs);
        if (segmentCount != DashSegmentIndex::INDEX_UNBOUNDED || timeShiftBufferDepthUs == kTimeUnset) return GetFirstSegmentNum();
        const std::int64_t liveEdgeTimeInPeriodUs = nowUnixTimeUs - periodStartUnixTimeUs;
        const std::int64_t timeShiftBufferStartInPeriodUs = liveEdgeTimeInPeriodUs - timeShiftBufferDepthUs;
        return std::max(GetFirstSegmentNum(), GetSegmentNum(timeShiftBufferStartInPeriodUs, periodDurationUs));
    }

    std::int64_t MultiSegmentBase::GetAvailableSegmentCount(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const {
        const std::int64_t segmentCount = GetSegmentCount(periodDurationUs);
        if (segmentCount != DashSegmentIndex::INDEX_UNBOUNDED) return segmentCount;
        const std::int64_t liveEdgeTimeInPeriodUs = nowUnixTimeUs - periodStartUnixTimeUs;
        const std::int64_t availabilityTimeUs = liveEdgeTimeInPeriodUs + availabilityTimeOffsetUs;
        const std::int64_t firstIncompleteSegmentNum = GetSegmentNum(availabilityTimeUs, periodDurationUs);
        const std::int64_t firstAvailableSegmentNum = GetFirstAvailableSegmentNum(periodDurationUs, nowUnixTimeUs);
        return std::int32_t(firstIncompleteSegmentNum - firstAvailableSegmentNum);   // upstream's (int) cast
    }

    std::int64_t MultiSegmentBase::GetNextSegmentAvailableTimeUs(std::int64_t periodDurationUs, std::int64_t nowUnixTimeUs) const {
        if (segmentTimeline) return kTimeUnset;
        const std::int64_t firstIncompleteSegmentNum =
            GetFirstAvailableSegmentNum(periodDurationUs, nowUnixTimeUs) + GetAvailableSegmentCount(periodDurationUs, nowUnixTimeUs);
        return GetSegmentTimeUs(firstIncompleteSegmentNum) + GetSegmentDurationUs(firstIncompleteSegmentNum, periodDurationUs) - availabilityTimeOffsetUs;
    }

    std::shared_ptr<SegmentBase> SegmentList::CopyWithPresentationTimeOffset(std::int64_t offset) const {
        return std::make_shared<SegmentList>(initialization, timescale, offset, startNumber, duration, segmentTimeline, availabilityTimeOffsetUs, mediaSegments,
                                             timeShiftBufferDepthUs, periodStartUnixTimeUs);
    }

    std::optional<RangedUri> SegmentTemplate::GetInitialization(const Format& format) const {
        if (initializationTemplate) return RangedUri(initializationTemplate->BuildUri(format.id, 0, format.bitrate, 0), 0, kLengthUnset);
        return SegmentBase::GetInitialization(format);
    }

    RangedUri SegmentTemplate::GetSegmentUrl(const Format& format, std::int64_t sequenceNumber) const {
        const std::int64_t time = segmentTimeline ? segmentTimeline->at(std::size_t(sequenceNumber - startNumber)).startTime : (sequenceNumber - startNumber) * duration;
        return RangedUri(mediaTemplate.value().BuildUri(format.id, sequenceNumber, format.bitrate, time), 0, kLengthUnset);
    }

    std::int64_t SegmentTemplate::GetSegmentCount(std::int64_t periodDurationUs) const {
        if (segmentTimeline) return std::int64_t(segmentTimeline->size());
        if (endNumber != kIndexUnset) return endNumber - startNumber + 1;
        if (periodDurationUs != kTimeUnset) {
            // BigIntegerMath.divide(periodDurationUs * timescale, duration * 1e6, CEILING), exact.
            const __int128 numerator = __int128(periodDurationUs) * timescale;
            const __int128 denominator = __int128(duration) * stream::kMicrosPerSecond;
            __int128 q = numerator / denominator;
            if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) q++;
            return std::int64_t(q);
        }
        return DashSegmentIndex::INDEX_UNBOUNDED;
    }

    std::shared_ptr<SegmentBase> SegmentTemplate::CopyWithPresentationTimeOffset(std::int64_t offset) const {
        return std::make_shared<SegmentTemplate>(initialization, timescale, offset, startNumber, endNumber, duration, segmentTimeline, availabilityTimeOffsetUs,
                                                 initializationTemplate, mediaTemplate, timeShiftBufferDepthUs, periodStartUnixTimeUs);
    }

    // --- Representation ----------------------------------------------------------------------

    Representation::Representation(Kind k, std::int64_t revision, Format f, std::vector<BaseUrl> urls, const SegmentBase& segmentBase,
                                   std::vector<Descriptor> inband, std::vector<Descriptor> essential, std::vector<Descriptor> supplemental)
        : kind(k), revisionId(revision), format(std::move(f)), baseUrls(std::move(urls)), inbandEventStreams(std::move(inband)),
          essentialProperties(std::move(essential)), supplementalProperties(std::move(supplemental)) {
        if (baseUrls.empty()) throw std::invalid_argument("a representation needs a base URL");
        m_InitializationUri = segmentBase.GetInitialization(format);
        presentationTimeOffsetUs = segmentBase.GetPresentationTimeOffsetUs();
    }

    std::shared_ptr<Representation> Representation::NewInstance(std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls,
                                                                std::shared_ptr<const SegmentBase> segmentBase, std::vector<Descriptor> inbandEventStreams,
                                                                std::vector<Descriptor> essentialProperties, std::vector<Descriptor> supplementalProperties,
                                                                std::string cacheKey) {
        if (segmentBase->kind == SegmentBase::Kind::Single)
            return std::make_shared<SingleSegmentRepresentation>(revisionId, std::move(format), std::move(baseUrls),
                                                                 std::static_pointer_cast<const SingleSegmentBase>(segmentBase), std::move(inbandEventStreams),
                                                                 std::move(essentialProperties), std::move(supplementalProperties), std::move(cacheKey), kLengthUnset);
        return std::make_shared<MultiSegmentRepresentation>(revisionId, std::move(format), std::move(baseUrls),
                                                            std::static_pointer_cast<const MultiSegmentBase>(segmentBase), std::move(inbandEventStreams),
                                                            std::move(essentialProperties), std::move(supplementalProperties));
    }

    SingleSegmentRepresentation::SingleSegmentRepresentation(std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls,
                                                             std::shared_ptr<const SingleSegmentBase> segmentBase, std::vector<Descriptor> inband,
                                                             std::vector<Descriptor> essential, std::vector<Descriptor> supplemental, std::string cacheKey,
                                                             std::int64_t length)
        : Representation(Kind::SingleSegment, revisionId, std::move(format), std::move(baseUrls), *segmentBase, std::move(inband), std::move(essential),
                         std::move(supplemental)),
          contentLength(length), m_CacheKey(std::move(cacheKey)) {
        uri = this->baseUrls.front().url;
        m_IndexUri = segmentBase->GetIndex();
        if (!m_IndexUri) m_SegmentIndex.emplace(RangedUri("", 0, contentLength));
    }

    MultiSegmentRepresentation::MultiSegmentRepresentation(std::int64_t revisionId, Format format, std::vector<BaseUrl> baseUrls,
                                                           std::shared_ptr<const MultiSegmentBase> base, std::vector<Descriptor> inband,
                                                           std::vector<Descriptor> essential, std::vector<Descriptor> supplemental)
        : Representation(Kind::MultiSegment, revisionId, std::move(format), std::move(baseUrls), *base, std::move(inband), std::move(essential),
                         std::move(supplemental)),
          segmentBase(std::move(base)) {}

    // --- Period, DashManifest ----------------------------------------------------------------

    int Period::GetAdaptationSetIndex(TrackType type) const {
        for (std::size_t i = 0; i < adaptationSets.size(); i++)
            if (adaptationSets[i].type == type) return int(i);
        return kIndexUnset;
    }

    DashManifest::DashManifest(std::int64_t availabilityStartTime, std::int64_t duration, std::int64_t minBufferTime, bool isDynamic, std::int64_t minUpdatePeriod,
                               std::int64_t timeShiftBufferDepth, std::int64_t suggestedPresentationDelay, std::int64_t publishTime,
                               std::optional<ProgramInformation> program, std::optional<UtcTimingElement> utc, std::optional<ServiceDescriptionElement> service,
                               std::vector<Period> periods, std::vector<Location> locs)
        : availabilityStartTimeMs(availabilityStartTime), durationMs(duration), minBufferTimeMs(minBufferTime), dynamic(isDynamic),
          minUpdatePeriodMs(minUpdatePeriod), timeShiftBufferDepthMs(timeShiftBufferDepth), suggestedPresentationDelayMs(suggestedPresentationDelay),
          publishTimeMs(publishTime), utcTiming(std::move(utc)), serviceDescription(std::move(service)), programInformation(std::move(program)),
          locations(std::move(locs)), m_Periods(std::move(periods)) {
        location = locations.empty() ? std::string() : locations.back().url;
    }

    std::int64_t DashManifest::GetPeriodDurationMs(int index) const {
        const std::size_t i = std::size_t(index);
        if (i == m_Periods.size() - 1) return durationMs == kTimeUnset ? kTimeUnset : durationMs - m_Periods[i].startMs;
        return m_Periods[i + 1].startMs - m_Periods[i].startMs;
    }

    DashManifest DashManifest::Copy(const std::vector<StreamKey>& streamKeys) const {
        std::deque<StreamKey> keys(streamKeys.begin(), streamKeys.end());
        std::sort(keys.begin(), keys.end());
        keys.push_back(StreamKey{ -1, -1, -1 });   // sentinel
        std::vector<Period> copyPeriods;
        std::int64_t shiftMs = 0;
        for (int periodIndex = 0; periodIndex < GetPeriodCount(); periodIndex++) {
            if (keys.front().periodIndex != periodIndex) {
                const std::int64_t periodDurationMs = GetPeriodDurationMs(periodIndex);
                if (periodDurationMs != kTimeUnset) shiftMs += periodDurationMs;
                continue;
            }
            const Period& period = GetPeriod(periodIndex);
            std::vector<AdaptationSet> copyAdaptationSets;
            StreamKey key = keys.front();
            keys.pop_front();
            do {
                const int adaptationSetIndex = key.groupIndex;
                const AdaptationSet& adaptationSet = period.adaptationSets.at(std::size_t(adaptationSetIndex));
                AdaptationSet copy = adaptationSet;
                copy.representations.clear();
                do {
                    copy.representations.push_back(adaptationSet.representations.at(std::size_t(key.streamIndex)));
                    key = keys.front();
                    keys.pop_front();
                } while (key.periodIndex == periodIndex && key.groupIndex == adaptationSetIndex);
                copyAdaptationSets.push_back(std::move(copy));
            } while (key.periodIndex == periodIndex);
            keys.push_front(key);
            copyPeriods.push_back(Period{ period.id, period.startMs - shiftMs, std::move(copyAdaptationSets), period.eventStreams, std::nullopt });
        }
        const std::int64_t newDuration = durationMs != kTimeUnset ? durationMs - shiftMs : kTimeUnset;
        return DashManifest(availabilityStartTimeMs, newDuration, minBufferTimeMs, dynamic, minUpdatePeriodMs, timeShiftBufferDepthMs, suggestedPresentationDelayMs,
                            publishTimeMs, programInformation, utcTiming, serviceDescription, std::move(copyPeriods), locations);
    }

}
