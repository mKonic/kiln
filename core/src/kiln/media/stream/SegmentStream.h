#pragma once

// One track group of an adaptive stream, loaded ahead of playback on a thread of its own: the
// chunk source (HlsChunkSource / DefaultDashChunkSource) choosing each next segment with the
// track selection, and DefaultLoadControl's buffer targets. The loaded segments come out as
// pieces: consecutive segments of one rendition (one initialization segment, one discontinuity)
// that a platform extractor reads as a single byte stream. A rendition switch, a discontinuity
// or a new initialization segment starts the next piece.

#include "kiln/media/stream/AdaptiveSource.h"
#include "kiln/media/stream/AdaptiveTrackSelection.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kiln::media::stream {

    class StreamPiece {
    public:
        // The bytes at offset (blocking until loaded): the count read, 0 past the end of an ended
        // piece, -1 when cancelled, evicted, or (non-blocking) not loaded yet.
        std::int64_t ReadAt(int reader, std::int64_t offset, std::uint8_t* buffer, std::size_t size);
        // The size once every segment is in (kLengthUnset before).
        std::int64_t Size() const;
        // A reader is blocked in ReadAt, waiting for bytes the loader has not brought yet.
        bool IsWaiting(int reader) const;
        // The first segment is loaded (and, for TS and packed audio, its timestamps resolved): an
        // extractor can be made.
        bool CanOpen() const;
        // Reads past the loaded bytes fail instead of waiting (while the platform's TS extractor
        // probes for tracks, which reads on for two seconds of wall time otherwise).
        void SetNonBlocking(int reader, bool nonBlocking);
        // An extractor is being made: its sniffers read anywhere, so nothing is evicted meanwhile
        // and its reads do not count as progress.
        void SetOpening(int reader, bool opening);
        // The reader will not read this piece (its track is not there): it no longer holds
        // segments back.
        void Detach(int reader);
        // No more segments will be added and the next piece (if any) is known.
        bool Ended() const;
        // The piece after this one, once the loader started it; nullptr while unknown.
        std::shared_ptr<StreamPiece> Next() const;
        // Ended, with nothing after it: the stream's end.
        bool IsLast() const;
        // Presentation time = sample time + this (nullopt until resolved: HLS TS and packed audio
        // from their first segment's PTS). The platform's MP4 extractor counts time from the
        // first fragment it reads (MPEG4Extractor ignores tfdt), so an MP4 piece's samples start
        // at its first segment's start; Matroska's timecodes are absolute (the period start minus
        // the presentation time offset).
        std::optional<std::int64_t> TimeOffsetUs() const;
        // Samples before this time duplicate the previous piece (a switch to a rendition whose
        // segments are not aligned): decoded, not shown or played.
        std::int64_t MinPresentationUs() const { return m_MinPresentationUs; }

        int Track() const { return m_Track; }
        int Discontinuity() const { return m_Discontinuity; }
        // The discontinuity's timestamp adjuster (HLS), for timestamps read from the bytes (WebVTT's
        // X-TIMESTAMP-MAP).
        const std::shared_ptr<TimestampAdjuster>& Adjuster() const { return m_Adjuster; }
        // Every byte is in (reads past the end return 0).
        bool IsComplete() const;
        const Format& GetFormat() const { return m_Format; }
        SegmentContainer Container() const { return m_Container; }
        std::int64_t StartUs() const { return m_StartUs; }
        // The end of the last loaded segment on the period timeline.
        std::int64_t LoadedEndUs() const;
        // Reads fail from now on, waking a reader that waits (its readers stop).
        void Cancel();

        StreamPiece(int track, Format format, SegmentContainer container, int readers, const MediaSegment& first,
                    std::shared_ptr<TimestampAdjuster> adjuster, std::int64_t minPresentationUs);

    private:
        friend class SegmentStream;
        struct Part {
            std::int64_t offset, length;
            std::vector<std::uint8_t> bytes;   // empty once evicted
            std::int64_t endUs;
            bool init;
        };
        void Append(std::vector<std::uint8_t> bytes, std::int64_t endUs, bool init);
        // No more bytes (reads past the end return 0), the next piece still unknown.
        void Complete();
        void End(std::shared_ptr<StreamPiece> next);
        void SetTimeOffset(std::int64_t offsetUs);
        void Evict();
        std::size_t PartAt(std::int64_t offset) const;   // m_Parts.size() when past the loaded bytes

        const int m_Track;
        const Format m_Format;
        const SegmentContainer m_Container;
        const std::int64_t m_StartUs;
        const int m_Discontinuity;
        const std::shared_ptr<const MediaSegment> m_Init;
        const std::int64_t m_DashTimeOffsetUs;
        const std::shared_ptr<TimestampAdjuster> m_Adjuster;
        const std::int64_t m_MinPresentationUs;

        mutable std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::vector<Part> m_Parts;
        std::int64_t m_LoadedBytes = 0;
        std::int64_t m_LoadedEndUs;
        // Per reader: the furthest byte asked for (whether reads will wait) and the end of the
        // last read (how far it got: what may be evicted).
        std::vector<std::int64_t> m_ReaderPositions, m_ReaderLast;
        std::vector<bool> m_NonBlocking, m_Opening, m_Waiting;
        bool m_Complete = false, m_Ended = false, m_Cancelled = false, m_HasNext = false;
        std::shared_ptr<StreamPiece> m_Next;
        std::optional<std::int64_t> m_TimeOffsetUs;
        int m_MediaSegments = 0;
    };

    class SegmentStream {
    public:
        struct Config {
            int group = 0;
            // The tracks of the group it may play (an override's, else all); one: no adaptation.
            std::vector<int> tracks;
            // How many extractors read each piece (2 when audio plays from the video's segments).
            int readers = 1;
            // DefaultLoadControl.
            std::int64_t minBufferUs = 50'000'000, maxBufferUs = 50'000'000;
            BandwidthMeter* bandwidthMeter = nullptr;
            // Track selection; nullptr: AdaptiveTrackSelection's defaults.
            AdaptiveTrackSelection::Factory* selectionFactory = nullptr;
        };

        SegmentStream(std::shared_ptr<AdaptiveSource> source, Config config);
        ~SegmentStream();
        SegmentStream(const SegmentStream&) = delete;
        SegmentStream& operator=(const SegmentStream&) = delete;

        // Loads from the segment containing positionUs (period timeline), dropping what was loaded.
        void Start(std::int64_t positionUs);
        // The piece reading starts with after Start, once the loader made it (nullptr before).
        std::shared_ptr<StreamPiece> FirstPiece() const;
        // Where playback is (period timeline) and how fast: the buffer target and adaptation
        // follow it.
        void SetPlaybackPosition(std::int64_t positionUs, float speed = 1.0f);
        // Loaded up to (period timeline); kTimeUnset before anything loaded.
        std::int64_t BufferedPositionUs() const;
        // Every segment to the end is loaded.
        bool IsEnded() const;
        bool IsLoading() const;
        // A load that failed for good (the stream stops), "" when none.
        std::string Error() const;
        // The track the loader loads now (index in the group).
        int SelectedTrack() const;
        // A reader that reads no piece of this stream (StreamPiece::Detach, for this and later pieces).
        void DetachReader(int reader);
        const AdaptiveSource& Source() const { return *m_Source; }

    private:
        void Run();
        bool LoadNext(std::unique_lock<std::mutex>& lock);
        void Wait(std::unique_lock<std::mutex>& lock, std::int64_t us);
        std::vector<std::uint8_t> Fetch(const MediaSegment& segment);

        std::shared_ptr<AdaptiveSource> m_Source;
        Config m_Config;
        const StreamGroup m_Group;
        std::vector<bool> m_Detached;
        std::unique_ptr<BaseTrackSelection> m_Selection;

        mutable std::mutex m_Mutex;
        std::condition_variable m_Cv;
        bool m_Quit = false;
        std::uint64_t m_Generation = 0;
        std::int64_t m_PlaybackUs = 0;
        float m_Speed = 1.0f;
        // The loader's position (m_Mutex).
        std::int64_t m_LoadPositionUs = 0;
        std::int64_t m_LastSequence = -1, m_LastEndUs = kTimeUnset;
        int m_LastTrack = -1;
        std::shared_ptr<StreamPiece> m_First, m_Piece;
        std::int64_t m_BufferedUs = kTimeUnset;
        std::int64_t m_LiveEdgeUs = kTimeUnset;   // a live timeline's last segment end
        bool m_Ended = false, m_Loading = false;
        std::string m_Error;
        int m_SelectedTrack = 0;
        MediaChunkQueue m_Queue;
        int m_ErrorCount = 0;
        std::shared_ptr<net::HttpCall> m_Call;
        std::thread m_Thread;   // last
    };

}
