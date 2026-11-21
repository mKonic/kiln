#include "kiln/media/stream/SegmentStream.h"

#include "kiln/media/stream/SegmentProbe.h"
#include "kiln/media/smoothstreaming/SsInitSegment.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace kiln::media::stream {

    namespace {
        bool SameInit(const std::shared_ptr<const MediaSegment>& a, const std::shared_ptr<const MediaSegment>& b) {
            if (a == b) return true;
            if (!a || !b) return false;
            return a->url == b->url && a->offset == b->offset && a->length == b->length && a->keyUrl == b->keyUrl && a->data == b->data;
        }
        // DefaultLoadErrorHandlingPolicy.getExclusionInfo: statuses that exclude a track for a minute.
        bool ExcludesTrack(int status) { return status == 403 || status == 404 || status == 410 || status == 416 || status == 500 || status == 503; }
        // The platform's MP4 extractor refuses a segment index whose references do not start with
        // a stream access point (MPEG4Extractor::parseSegmentIndex), which audio segments often
        // have. A piece is never seeked within, so top-level sidx boxes become free boxes.
        void NeutralizeSegmentIndices(std::vector<std::uint8_t>& bytes) {
            std::size_t at = 0;
            while (at + 8 <= bytes.size()) {
                std::uint64_t size = std::uint64_t(bytes[at]) << 24 | std::uint64_t(bytes[at + 1]) << 16 | std::uint64_t(bytes[at + 2]) << 8 | bytes[at + 3];
                if (size == 1 && at + 16 <= bytes.size()) {
                    size = 0;
                    for (int i = 0; i < 8; ++i) size = size << 8 | bytes[at + 8 + std::size_t(i)];
                } else if (size == 0) {
                    size = bytes.size() - at;
                }
                if (size < 8) return;
                if (std::memcmp(&bytes[at + 4], "sidx", 4) == 0) std::memcpy(&bytes[at + 4], "free", 4);
                at += std::size_t(size);
            }
        }
        constexpr std::int64_t kExclusionMs = 60'000;   // DEFAULT_TRACK_EXCLUSION_MS
        constexpr int kMinLoadableRetryCount = 3;        // DEFAULT_MIN_LOADABLE_RETRY_COUNT
    }

    // --- StreamPiece -----------------------------------------------------------------------------
    StreamPiece::StreamPiece(int track, Format format, SegmentContainer container, int readers, const MediaSegment& first,
                             std::shared_ptr<TimestampAdjuster> adjuster, std::int64_t minPresentationUs)
        : m_Track(track), m_Format(std::move(format)), m_Container(container), m_StartUs(first.startUs), m_Discontinuity(first.discontinuity),
          m_Init(first.init), m_DashTimeOffsetUs(first.timeOffsetUs), m_Adjuster(std::move(adjuster)),
          m_MinPresentationUs(minPresentationUs), m_LoadedEndUs(first.startUs), m_ReaderPositions(std::size_t(std::max(readers, 1)), 0),
          m_ReaderLast(std::size_t(std::max(readers, 1)), 0), m_NonBlocking(std::size_t(std::max(readers, 1)), false),
          m_Opening(std::size_t(std::max(readers, 1)), false), m_Waiting(std::size_t(std::max(readers, 1)), false) {
        // WebVTT: cue times are on the period's timeline in DASH; HLS maps them by the segment's
        // X-TIMESTAMP-MAP (left to the reader).
        if (m_Container == SegmentContainer::WebVtt) {
            if (m_DashTimeOffsetUs != kTimeUnset) m_TimeOffsetUs = m_DashTimeOffsetUs;
        } else if (m_Container == SegmentContainer::WebM && m_DashTimeOffsetUs != kTimeUnset) m_TimeOffsetUs = m_DashTimeOffsetUs;
        else if (m_Container != SegmentContainer::Ts && m_Container != SegmentContainer::PackedAudio) m_TimeOffsetUs = m_StartUs;
        else if (m_DashTimeOffsetUs != kTimeUnset) m_TimeOffsetUs = m_StartUs;   // DASH TS: counted from its first PTS
    }

    std::size_t StreamPiece::PartAt(std::int64_t offset) const {
        auto it = std::upper_bound(m_Parts.begin(), m_Parts.end(), offset, [](std::int64_t o, const Part& p) { return o < p.offset; });
        if (it == m_Parts.begin()) return m_Parts.empty() ? 0 : (offset < m_Parts.front().offset ? std::size_t(-1) : 0);
        const std::size_t i = std::size_t(it - m_Parts.begin()) - 1;
        return offset < m_Parts[i].offset + m_Parts[i].length ? i : m_Parts.size();
    }

    std::int64_t StreamPiece::ReadAt(int reader, std::int64_t offset, std::uint8_t* buffer, std::size_t size) {
        std::unique_lock lock(m_Mutex);
        const std::size_t r = std::size_t(std::clamp(reader, 0, int(m_ReaderPositions.size()) - 1));
        if (!m_Opening[r]) {
            m_ReaderPositions[r] = std::max(m_ReaderPositions[r], offset + std::int64_t(size));
            if (m_ReaderLast[r] != INT64_MAX) m_ReaderLast[r] = offset + std::int64_t(size);
        }
        // The whole range, or what there is of it once the piece is complete: a short read is
        // the end of the stream to an extractor.
        for (;;) {
            if (m_Cancelled) return -1;
            if (offset + std::int64_t(size) <= m_LoadedBytes) break;
            if (m_Complete) {
                if (offset >= m_LoadedBytes) return 0;
                break;
            }
            if (m_NonBlocking[r]) return -1;
            m_Waiting[r] = true;
            m_Cv.wait(lock);
            m_Waiting[r] = false;
        }
        std::size_t copied = 0;
        std::int64_t at = offset;
        while (copied < size && at < m_LoadedBytes) {
            const std::size_t i = PartAt(at);
            if (i >= m_Parts.size()) return copied ? std::int64_t(copied) : -1;   // evicted
            const Part& p = m_Parts[i];
            if (p.bytes.empty() && p.length > 0) return copied ? std::int64_t(copied) : -1;   // evicted
            const std::size_t from = std::size_t(at - p.offset);
            const std::size_t n = std::min(size - copied, p.bytes.size() - from);
            std::memcpy(buffer + copied, p.bytes.data() + from, n);
            copied += n;
            at += std::int64_t(n);
        }
        Evict();
        return std::int64_t(copied);
    }

    // Media segments every reader is past (with one segment's margin: the MP4 extractor reads
    // back to a fragment's start) are dropped; the initialization segment stays.
    void StreamPiece::Evict() {
        if (std::find(m_Opening.begin(), m_Opening.end(), true) != m_Opening.end()) return;
        const std::int64_t slowest = *std::min_element(m_ReaderLast.begin(), m_ReaderLast.end());
        const std::size_t current = PartAt(slowest);
        if (current == std::size_t(-1) || current == 0) return;
        const std::size_t keepFrom = std::min(current, m_Parts.size()) - 1;
        for (std::size_t i = 0; i < keepFrom; ++i)
            if (!m_Parts[i].init) std::vector<std::uint8_t>().swap(m_Parts[i].bytes);
    }

    bool StreamPiece::IsComplete() const {
        std::lock_guard lock(m_Mutex);
        return m_Complete;
    }

    std::int64_t StreamPiece::Size() const {
        std::lock_guard lock(m_Mutex);
        return m_Complete ? m_LoadedBytes : kLengthUnset;
    }

    bool StreamPiece::IsWaiting(int reader) const {
        std::lock_guard lock(m_Mutex);
        return m_Waiting.at(std::size_t(std::clamp(reader, 0, int(m_Waiting.size()) - 1)));
    }

    bool StreamPiece::CanOpen() const {
        std::lock_guard lock(m_Mutex);
        if (m_Cancelled) return true;
        if (m_MediaSegments == 0) return m_Complete;
        // TS and packed audio resolve their timestamps from the bytes; fMP4 from the first sample.
        if ((m_Container == SegmentContainer::Ts || m_Container == SegmentContainer::PackedAudio) && !m_TimeOffsetUs) return false;
        return true;
    }

    void StreamPiece::Detach(int reader) {
        {
            std::lock_guard lock(m_Mutex);
            const std::size_t r = std::size_t(std::clamp(reader, 0, int(m_ReaderPositions.size()) - 1));
            m_ReaderPositions[r] = INT64_MAX;
            m_ReaderLast[r] = INT64_MAX;
        }
    }

    void StreamPiece::SetOpening(int reader, bool opening) {
        std::lock_guard lock(m_Mutex);
        m_Opening.at(std::size_t(std::clamp(reader, 0, int(m_Opening.size()) - 1))) = opening;
    }

    void StreamPiece::SetNonBlocking(int reader, bool nonBlocking) {
        std::lock_guard lock(m_Mutex);
        m_NonBlocking.at(std::size_t(std::clamp(reader, 0, int(m_NonBlocking.size()) - 1))) = nonBlocking;
    }

    bool StreamPiece::Ended() const {
        std::lock_guard lock(m_Mutex);
        return m_Ended;
    }
    std::shared_ptr<StreamPiece> StreamPiece::Next() const {
        std::lock_guard lock(m_Mutex);
        return m_Next;
    }
    bool StreamPiece::IsLast() const {
        std::lock_guard lock(m_Mutex);
        return m_Ended && !m_HasNext;
    }
    std::int64_t StreamPiece::LoadedEndUs() const {
        std::lock_guard lock(m_Mutex);
        return m_LoadedEndUs;
    }

    std::optional<std::int64_t> StreamPiece::TimeOffsetUs() const {
        std::lock_guard lock(m_Mutex);
        return m_TimeOffsetUs;
    }

    void StreamPiece::Append(std::vector<std::uint8_t> bytes, std::int64_t endUs, bool init) {
        {
            std::lock_guard lock(m_Mutex);
            const std::int64_t n = std::int64_t(bytes.size());
            m_Parts.push_back({ m_LoadedBytes, n, std::move(bytes), endUs, init });
            m_LoadedBytes += n;
            if (!init) {
                m_LoadedEndUs = std::max(m_LoadedEndUs, endUs);
                ++m_MediaSegments;
            }
        }
        m_Cv.notify_all();
    }
    void StreamPiece::Complete() {
        {
            std::lock_guard lock(m_Mutex);
            m_Complete = true;
        }
        m_Cv.notify_all();
    }
    void StreamPiece::End(std::shared_ptr<StreamPiece> next) {
        {
            std::lock_guard lock(m_Mutex);
            m_Complete = true;
            m_Ended = true;
            m_HasNext = bool(next);
            m_Next = std::move(next);
        }
        m_Cv.notify_all();
    }
    void StreamPiece::Cancel() {
        {
            std::lock_guard lock(m_Mutex);
            m_Cancelled = true;
        }
        m_Cv.notify_all();
    }
    void StreamPiece::SetTimeOffset(std::int64_t offsetUs) {
        {
            std::lock_guard lock(m_Mutex);
            m_TimeOffsetUs = offsetUs;
        }
        m_Cv.notify_all();
    }

    // --- SegmentStream ---------------------------------------------------------------------------
    SegmentStream::SegmentStream(std::shared_ptr<AdaptiveSource> source, Config config)
        : m_Source(std::move(source)), m_Config(std::move(config)), m_Group(m_Source->Groups().at(std::size_t(m_Config.group))),
          m_Detached(std::size_t(std::max(m_Config.readers, 1)), false) {
        if (m_Config.tracks.empty())
            for (int i = 0; i < int(m_Group.formats.size()); ++i) m_Config.tracks.push_back(i);
        TrackGroup group{ m_Group.formats };
        if (m_Config.tracks.size() == 1 || !m_Config.bandwidthMeter) {
            m_Selection = std::make_unique<FixedTrackSelection>(group, m_Config.tracks.back());
        } else {
            AdaptiveTrackSelection::Factory defaults;
            AdaptiveTrackSelection::Factory& factory = m_Config.selectionFactory ? *m_Config.selectionFactory : defaults;
            auto selections = factory.CreateTrackSelections({ AdaptiveTrackSelection::Definition{ group, m_Config.tracks } }, m_Config.bandwidthMeter);
            m_Selection = std::move(selections.at(0));
        }
        m_Selection->Enable();
        m_SelectedTrack = m_Selection->GetSelectedIndexInTrackGroup();
        m_Thread = std::thread([this] { Run(); });
    }

    SegmentStream::~SegmentStream() {
        std::shared_ptr<net::HttpCall> call;
        {
            std::lock_guard lock(m_Mutex);
            m_Quit = true;
            call = m_Call;
            if (m_Piece) m_Piece->Cancel();
            if (m_First) m_First->Cancel();
        }
        if (call) call->Cancel();
        m_Cv.notify_all();
        m_Thread.join();
        m_Selection->Disable();
    }

    void SegmentStream::Start(std::int64_t positionUs) {
        std::shared_ptr<net::HttpCall> call;
        {
            std::lock_guard lock(m_Mutex);
            ++m_Generation;
            for (auto* piece : { &m_First, &m_Piece })
                if (*piece) (*piece)->Cancel();
            m_First.reset();
            m_Piece.reset();
            m_LoadPositionUs = positionUs;
            m_PlaybackUs = positionUs;
            m_LastSequence = -1;
            m_LastEndUs = kTimeUnset;
            m_LastTrack = -1;
            m_BufferedUs = kTimeUnset;
            m_LiveEdgeUs = kTimeUnset;
            m_Ended = false;
            m_Error.clear();
            m_ErrorCount = 0;
            m_Queue.clear();
            call = m_Call;
        }
        if (call) call->Cancel();
        m_Cv.notify_all();
    }

    std::shared_ptr<StreamPiece> SegmentStream::FirstPiece() const {
        std::lock_guard lock(m_Mutex);
        return m_First;
    }
    void SegmentStream::SetPlaybackPosition(std::int64_t positionUs, float speed) {
        {
            std::lock_guard lock(m_Mutex);
            m_PlaybackUs = positionUs;
            m_Speed = speed;
            // Chunks played out leave the queue the selection looks at.
            std::erase_if(m_Queue, [&](const std::shared_ptr<const MediaChunk>& c) { return c->endTimeUs <= positionUs; });
        }
        m_Cv.notify_all();
    }
    std::int64_t SegmentStream::BufferedPositionUs() const {
        std::lock_guard lock(m_Mutex);
        return m_BufferedUs;
    }
    bool SegmentStream::IsEnded() const {
        std::lock_guard lock(m_Mutex);
        return m_Ended;
    }
    bool SegmentStream::IsLoading() const {
        std::lock_guard lock(m_Mutex);
        return m_Loading;
    }
    std::string SegmentStream::Error() const {
        std::lock_guard lock(m_Mutex);
        return m_Error;
    }
    void SegmentStream::DetachReader(int reader) {
        std::lock_guard lock(m_Mutex);
        if (reader >= 0 && reader < int(m_Detached.size())) m_Detached[std::size_t(reader)] = true;
        for (auto* piece : { &m_First, &m_Piece }) {
            // Every piece from the first on (those before were read).
            for (std::shared_ptr<StreamPiece> p = *piece; p; p = p->Next()) p->Detach(reader);
        }
    }

    int SegmentStream::SelectedTrack() const {
        std::lock_guard lock(m_Mutex);
        return m_SelectedTrack;
    }

    void SegmentStream::Wait(std::unique_lock<std::mutex>& lock, std::int64_t us) {
        const std::uint64_t generation = m_Generation;
        m_Cv.wait_for(lock, std::chrono::microseconds(us), [&] { return m_Quit || m_Generation != generation; });
    }

    void SegmentStream::Run() {
        std::unique_lock lock(m_Mutex);
        while (!m_Quit) {
            if (!m_Error.empty() || m_Ended) {
                const std::uint64_t generation = m_Generation;
                m_Cv.wait(lock, [&] { return m_Quit || m_Generation != generation; });
                continue;
            }
            // DefaultLoadControl.shouldContinueLoading: below the minimum buffer (in media time
            // at the playback speed), load; at the maximum, stop.
            const std::int64_t buffered = m_BufferedUs == kTimeUnset ? 0 : m_BufferedUs - m_PlaybackUs;
            const std::int64_t minBufferUs = std::int64_t(double(m_Config.minBufferUs) * std::max(m_Speed, 1.0f));
            if (m_BufferedUs != kTimeUnset && buffered >= std::max(minBufferUs, m_Config.maxBufferUs)) {
                m_Loading = false;
                Wait(lock, 200'000);
                continue;
            }
            m_Loading = true;
            LoadNext(lock);
        }
    }

    std::vector<std::uint8_t> SegmentStream::Fetch(const MediaSegment& segment) {
        if (segment.data) return *segment.data;
        std::vector<std::uint8_t> bytes = m_Source->Fetcher().Get(segment.url, segment.offset, segment.length, m_Call.get());
        if (!segment.keyUrl.empty()) {
            const std::vector<std::uint8_t> key = m_Source->Key(segment.keyUrl, m_Call.get());
            bytes = Aes128CbcDecrypt(key, HlsEncryptionIv(segment.iv), bytes);
        }
        return bytes;
    }

    // HlsChunkSource.getNextChunk / DefaultDashChunkSource.getNextChunk: choose the track and the
    // next segment, then load it into the current piece (or a new one).
    bool SegmentStream::LoadNext(std::unique_lock<std::mutex>& lock) {
        const std::uint64_t generation = m_Generation;
        const std::int64_t playbackUs = m_PlaybackUs;
        const std::int64_t bufferedUs = m_BufferedUs == kTimeUnset ? 0 : std::max<std::int64_t>(0, m_BufferedUs - playbackUs);
        m_Selection->OnPlaybackSpeed(m_Speed);
        // Live: how far the live edge is (HlsChunkSource.resolveTimeToLiveEdgeUs,
        // DefaultDashChunkSource.getAvailableLiveDurationUs), from the latest timeline.
        const std::int64_t availableUs = m_LiveEdgeUs == kTimeUnset ? kTimeUnset : std::max<std::int64_t>(0, m_LiveEdgeUs - playbackUs);
        m_Selection->UpdateSelectedTrack(playbackUs, bufferedUs, availableUs, m_Queue, {});
        int track = m_Selection->GetSelectedIndexInTrackGroup();
        const std::int64_t loadPositionUs = m_LoadPositionUs;
        const std::int64_t lastSequence = m_LastSequence, lastEndUs = m_LastEndUs;
        const int lastTrack = m_LastTrack;
        std::shared_ptr<StreamPiece> piece = m_Piece;
        const std::vector<bool> detached = m_Detached;
        auto call = std::make_shared<net::HttpCall>();
        m_Call = call;
        lock.unlock();

        auto fail = [&](const std::string& message, int status) {
            lock.lock();
            if (m_Generation != generation) return false;
            ++m_ErrorCount;
            // A track that answers 404 and the like is excluded for a minute; another plays.
            const int index = m_Selection->IndexOfTrack(track);
            if (status != 0 && ExcludesTrack(status) && m_Selection->Length() > 1 && index != kIndexUnset && m_Selection->ExcludeTrack(index, kExclusionMs)) {
                m_ErrorCount = 0;
                return false;
            }
            if (m_ErrorCount > kMinLoadableRetryCount) {
                m_Error = message;
                if (m_Piece) m_Piece->Cancel();
                return false;
            }
            Wait(lock, std::min<std::int64_t>(std::int64_t(m_ErrorCount - 1) * 1'000'000, 5'000'000));
            return false;
        };

        SegmentTimeline timeline;
        try {
            timeline = m_Source->Segments(m_Config.group, track);
        } catch (const HttpStatusError& e) {
            return fail(e.what(), e.status);
        } catch (const std::exception& e) {
            return fail(e.what(), 0);
        }
        {
            std::lock_guard l(m_Mutex);
            m_LiveEdgeUs = timeline.ended || timeline.segments.empty() ? kTimeUnset : timeline.segments.back().EndUs();
        }

        // The next segment.
        const auto& segments = timeline.segments;
        std::size_t next = segments.size();
        std::int64_t minPresentationUs = kTimeUnset;
        if (lastSequence < 0) {
            // From the load position: the segment containing it.
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (segments[i].startUs <= loadPositionUs) next = i;
                else break;
            }
            if (next == segments.size() && !segments.empty()) next = 0;
            if (next < segments.size() && segments[next].EndUs() <= loadPositionUs && segments[next].durationUs > 0 && next + 1 < segments.size()) ++next;
        } else if (track == lastTrack && m_Source->GetProtocol() == AdaptiveSource::Protocol::Hls) {
            // The same playlist: by media sequence.
            for (std::size_t i = 0; i < segments.size(); ++i)
                if (segments[i].sequence > lastSequence) {
                    next = i;
                    break;
                }
            if (next == segments.size() && !segments.empty() && segments.front().sequence > lastSequence + 1) next = 0;   // behind the live window
        } else {
            // By time: the segment that starts where the last one ended (10 ms of slack), else the
            // one containing that point, whose earlier samples are not shown.
            constexpr std::int64_t kSlackUs = 10'000;
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (segments[i].startUs >= lastEndUs - kSlackUs) {
                    next = i;
                    break;
                }
                if (segments[i].EndUs() > lastEndUs + kSlackUs) {
                    next = i;
                    minPresentationUs = lastEndUs;
                    break;
                }
            }
        }

        if (next >= segments.size()) {
            lock.lock();
            if (m_Generation != generation) return false;
            if (timeline.ended) {
                m_Ended = true;
                m_Loading = false;
                if (m_Piece) m_Piece->End(nullptr);
                else {
                    // Nothing to play at all: an ended, empty first piece.
                    MediaSegment none;
                    none.startUs = loadPositionUs;
                    auto empty = std::make_shared<StreamPiece>(track, m_Group.formats.at(std::size_t(track)), timeline.container, m_Config.readers, none, nullptr, kTimeUnset);
                    empty->End(nullptr);
                    m_First = m_Piece = empty;
                }
                return false;
            }
            // Live: wait for the playlist to grow.
            m_Loading = false;
            Wait(lock, m_Source->RetryIntervalUs(m_Config.group, track));
            return false;
        }

        const MediaSegment& segment = segments[next];
        // Packed audio and WebVTT: a piece per segment (upstream makes an extractor per chunk).
        const bool packed = timeline.container == SegmentContainer::PackedAudio || timeline.container == SegmentContainer::WebVtt;
        const bool newPiece = !piece || track != piece->Track() || segment.discontinuity != piece->m_Discontinuity || !SameInit(segment.init, piece->m_Init)
                           || segment.timeOffsetUs != piece->m_DashTimeOffsetUs || packed || segment.gap || minPresentationUs != kTimeUnset;
        std::shared_ptr<StreamPiece> target = piece;
        if (newPiece) {
            std::shared_ptr<TimestampAdjuster> adjuster =
                m_Source->GetProtocol() == AdaptiveSource::Protocol::Hls ? m_Source->Adjusters().GetAdjuster(segment.discontinuity) : nullptr;
            target = std::make_shared<StreamPiece>(track, m_Group.formats.at(std::size_t(track)), timeline.container, m_Config.readers, segment,
                                                   std::move(adjuster), minPresentationUs);
            for (std::size_t r = 0; r < detached.size(); ++r)
                if (detached[r]) target->Detach(int(r));
        }

        try {
            if (!segment.gap) {
                if (newPiece && segment.init) target->Append(Fetch(*segment.init), segment.startUs, true);
                std::vector<std::uint8_t> bytes = Fetch(segment);
                // fMP4 in HLS: the primary's first fragment starts the discontinuity's timestamps
                // (its tfdt at the segment's start), which WebVTT's X-TIMESTAMP-MAP refers to. The
                // samples themselves are placed by the segment's start.
                if (newPiece && target->m_Adjuster && timeline.container == SegmentContainer::Mp4 && m_Group.primary && !target->m_Adjuster->IsInitialized()) {
                    std::vector<std::uint8_t> init;
                    {
                        std::lock_guard pl(target->m_Mutex);
                        for (const auto& part : target->m_Parts)
                            if (part.init) init.insert(init.end(), part.bytes.begin(), part.bytes.end());
                    }
                    if (std::optional<std::int64_t> decodeUs = FirstFragmentDecodeTimeUs(init, bytes))
                        if (target->m_Adjuster->TrySharedInitialize(true, segment.startUs)) target->m_Adjuster->AdjustSampleTimestamp(*decodeUs);
                }
                if (newPiece && target->m_Adjuster && (timeline.container == SegmentContainer::Ts || timeline.container == SegmentContainer::PackedAudio)) {
                    // The timestamps of TS and packed audio, from the bytes (HlsMediaChunk).
                    std::shared_ptr<TimestampAdjuster> adjuster = target->m_Adjuster;
                    while (!adjuster->SharedInitializeOrWait(m_Group.primary, segment.startUs, 100)) {
                        std::lock_guard l(m_Mutex);
                        if (m_Quit || m_Generation != generation) return false;
                    }
                    std::int64_t offsetUs = segment.startUs;
                    if (timeline.container == SegmentContainer::Ts) {
                        if (std::optional<std::int64_t> base = FirstTsTimestampBase(bytes)) offsetUs = adjuster->AdjustTsTimestamp(*base);
                    } else {   // packed audio
                        const std::int64_t id3 = PeekId3PrivTimestamp(bytes);
                        if (id3 != kTimeUnset) offsetUs = adjuster->AdjustTsTimestamp(id3);
                        bytes.erase(bytes.begin(), bytes.begin() + std::ptrdiff_t(Id3TagsLength(bytes)));
                    }
                    target->SetTimeOffset(offsetUs);
                }
                if (timeline.container == SegmentContainer::Mp4) NeutralizeSegmentIndices(bytes);
                if (segment.fragmentTrackId) ss::SetFragmentTrackIds(bytes, segment.fragmentTrackId);
                target->Append(std::move(bytes), segment.EndUs(), false);
            }
            // A piece per packed audio segment (upstream makes an extractor per chunk); a gap's
            // piece has nothing.
            if (packed || segment.gap) target->Complete();
        } catch (const net::HttpError& e) {
            if (e.kind == net::HttpError::Kind::Cancelled) {
                lock.lock();
                return false;
            }
            return fail(e.what(), 0);
        } catch (const HttpStatusError& e) {
            return fail(e.what(), e.status);
        } catch (const std::exception& e) {
            return fail(e.what(), 0);
        }

        lock.lock();
        if (m_Generation != generation) return false;
        m_ErrorCount = 0;
        m_LastSequence = segment.sequence;
        m_LastEndUs = segment.EndUs();
        m_LastTrack = track;
        m_BufferedUs = std::max(m_BufferedUs == kTimeUnset ? segment.startUs : m_BufferedUs, segment.EndUs());
        auto chunk = std::make_shared<MediaChunk>();
        chunk->trackFormat = m_Group.formats.at(std::size_t(track));
        chunk->trackSelectionReason = m_Selection->GetSelectionReason();
        chunk->startTimeUs = segment.startUs;
        chunk->endTimeUs = segment.EndUs();
        m_Queue.push_back(std::move(chunk));
        if (newPiece) {
            // Linked in once its first segment is loaded: a reader at the end of the previous
            // piece moves on to bytes that are there.
            if (m_Piece) m_Piece->End(target);
            else m_First = target;
            m_Piece = target;
            m_SelectedTrack = track;
        }
        return true;
    }

}
