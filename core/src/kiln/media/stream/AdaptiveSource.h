#pragma once

// An adaptive stream's manifest as track groups and segments: HLS (HlsMediaPeriod's wrappers,
// DefaultHlsPlaylistTracker's snapshots and refreshes, HlsMediaSource's timeline) and DASH
// (DashMediaPeriod's groups, DefaultDashChunkSource's segment numbering, DashMediaSource's live
// window). Presentation times are on the period's timeline; Window() says where the window the
// player shows lies on it.

#include "kiln/media/TrackSelection.h"
#include "kiln/media/stream/BandwidthMeter.h"
#include "kiln/media/stream/StreamUtil.h"
#include "kiln/media/stream/TimestampAdjuster.h"
#include "kiln/net/Http.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kiln::media::stream {

    // How a segment's bytes are packaged (DefaultHlsExtractorFactory / the DASH container type).
    enum class SegmentContainer { Unknown, Ts, Mp4, PackedAudio, WebVtt, WebM };

    // A segment (or an initialization segment) to download.
    struct MediaSegment {
        std::string url;
        std::int64_t offset = 0, length = kLengthUnset;   // byte range
        std::int64_t startUs = 0, durationUs = 0;        // on the period's timeline
        std::int64_t sequence = 0;                       // HLS media sequence / DASH segment number
        int discontinuity = 0;                           // HLS discontinuity sequence / DASH period
        std::string keyUrl, iv;                          // AES-128 ("" for none)
        bool gap = false;                                // EXT-X-GAP: nothing to load
        // DASH: the sample time to presentation time offset (period start - presentation time
        // offset). kTimeUnset for HLS, where the timestamp adjusters decide.
        std::int64_t timeOffsetUs = kTimeUnset;
        std::shared_ptr<const MediaSegment> init;        // its initialization segment, if any
        std::int64_t EndUs() const { return startUs + durationUs; }
    };

    // A rendition's segments as the manifest has them now.
    struct SegmentTimeline {
        std::vector<MediaSegment> segments;
        bool ended = true;           // no segments will be added (VOD, EXT-X-ENDLIST, a static MPD)
        SegmentContainer container = SegmentContainer::Unknown;
    };

    // A group of renditions the player may switch between (a TrackGroup).
    struct StreamGroup {
        std::string id;   // "main", "audio:<name>" (HLS) / "<period>.<set>" (DASH)
        TrackType type = TrackType::Unknown;
        std::vector<Format> formats;
        // HLS: the variants (the primary timestamp source). Their segments may carry audio too:
        // embeddedAudio is that audio's format when it plays from them (no audio renditions).
        bool primary = false;
        std::optional<Format> embeddedAudio;
    };

    // Where the player's window lies on the period's timeline.
    struct StreamWindow {
        std::int64_t startUs = 0;
        std::int64_t durationUs = kTimeUnset;           // kTimeUnset while unknown
        std::int64_t defaultPositionUs = 0;             // in the window
        bool dynamic = false;                           // live: the window moves
        bool seekable = true;
        std::int64_t targetLiveOffsetUs = kTimeUnset;   // the live configuration's target offset
    };

    // HTTP for the stream: GETs with byte ranges, redirects followed, transfers counted by the
    // bandwidth meter.
    class SegmentFetcher {
    public:
        SegmentFetcher(std::shared_ptr<net::HttpEngine> engine, BandwidthMeter* meter) : m_Engine(std::move(engine)), m_Meter(meter) {}
        virtual ~SegmentFetcher() = default;
        // The bytes of [offset, offset + length) (length kLengthUnset: to the end). Throws
        // net::HttpError, or HttpStatusError for a status that is not 2xx. `call` (optional)
        // cancels it; `finalUrl` (optional) receives the URL that answered, after redirects
        // (what a manifest's relative URLs resolve against).
        virtual std::vector<std::uint8_t> Get(const std::string& url, std::int64_t offset, std::int64_t length, net::HttpCall* call = nullptr,
                                              std::string* finalUrl = nullptr);
        BandwidthMeter* Meter() const { return m_Meter; }

    private:
        std::shared_ptr<net::HttpEngine> m_Engine;
        BandwidthMeter* m_Meter;
    };

    // InvalidResponseCodeException.
    class HttpStatusError : public std::runtime_error {
    public:
        HttpStatusError(int status, const std::string& url) : std::runtime_error("HTTP " + std::to_string(status) + " for " + url), status(status) {}
        int status;
    };

    class AdaptiveSource {
    public:
        // The stream's protocol from the item's MIME type, else the URI's extension, else the
        // manifest's first bytes.
        enum class Protocol { Hls, Dash };
        static std::optional<Protocol> InferProtocol(const std::string& uri, const std::string& mimeType);
        // Loads the manifest (and HLS's first media playlist). Throws net::HttpError,
        // HttpStatusError or ParserException.
        static std::shared_ptr<AdaptiveSource> Open(const std::string& uri, const std::string& mimeType, std::shared_ptr<SegmentFetcher> fetcher,
                                                    std::function<std::int64_t()> nowUnixTimeMs = nullptr);
        virtual ~AdaptiveSource() = default;

        Protocol GetProtocol() const { return m_Protocol; }
        const std::vector<StreamGroup>& Groups() const { return m_Groups; }
        // The window now (a live one moves as the loaders refresh the playlists); never blocks
        // on the network.
        virtual StreamWindow Window() = 0;
        // The track's segments, refreshing a live playlist that is due (blocking: the loader's
        // thread). Throws as Open.
        virtual SegmentTimeline Segments(int group, int track) = 0;
        // How long after a refresh found nothing new to try again.
        virtual std::int64_t RetryIntervalUs(int group, int track) = 0;

        TimestampAdjusterProvider& Adjusters() { return m_Adjusters; }
        SegmentFetcher& Fetcher() { return *m_Fetcher; }
        // Keys by URL, loaded once (HlsChunkSource's FullSegmentEncryptionKeyCache).
        std::vector<std::uint8_t> Key(const std::string& url, net::HttpCall* call);

    protected:
        AdaptiveSource(Protocol protocol, std::shared_ptr<SegmentFetcher> fetcher, std::function<std::int64_t()> nowUnixTimeMs);
        std::int64_t NowUnixTimeMs() const;
        std::int64_t NowElapsedMs() const;

        Protocol m_Protocol;
        std::shared_ptr<SegmentFetcher> m_Fetcher;
        std::function<std::int64_t()> m_NowUnixTimeMs;
        std::vector<StreamGroup> m_Groups;
        TimestampAdjusterProvider m_Adjusters;
        std::mutex m_KeyMutex;
        std::map<std::string, std::vector<std::uint8_t>> m_Keys;
    };

    // FileTypes.inferFileTypeFromUri for HLS segments, by the path's extension.
    SegmentContainer InferHlsContainer(const std::string& url);

}
