// kiln's adaptive stream loader (AdaptiveSource, SegmentStream) against a fake HTTP engine:
// HLS variants and renditions, live playlist refreshes, AES-128, ABR, track exclusion after a
// 404, and DASH templates and segment indices. The behaviours follow HlsMediaPeriod,
// DefaultHlsPlaylistTracker, HlsChunkSource, DashMediaSource and DefaultDashChunkSource.
#include "Check.h"

#include "kiln/media/stream/AdaptiveSource.h"
#include "kiln/media/stream/SegmentProbe.h"
#include "kiln/media/stream/SegmentStream.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace kiln;
using namespace kiln::media;
using namespace kiln::media::stream;
using Bytes = std::vector<std::uint8_t>;

namespace {
    class FakeHttp final : public net::HttpEngine {
    public:
        net::HttpResponse Execute(const net::HttpRequest& request, net::HttpCall& call) override {
            std::lock_guard lock(m_Mutex);
            requests.push_back(request.url);
            net::HttpResponse r;
            r.url = request.url;
            if (auto redirect = redirects.find(request.url); redirect != redirects.end()) {
                r.status = 302;
                r.headers.Set("Location", redirect->second);
                return r;
            }
            auto it = bodies.find(request.url);
            if (it == bodies.end() || failing.count(request.url)) {
                r.status = 404;
                return r;
            }
            std::string body = it->second;
            if (auto range = request.headers.Get("Range")) {
                const std::size_t dash = range->find('-');
                const std::int64_t from = std::stoll(range->substr(6, dash - 6));
                const std::string to = range->substr(dash + 1);
                const std::int64_t end = to.empty() ? std::int64_t(body.size()) : std::stoll(to) + 1;
                body = body.substr(std::size_t(from), std::size_t(end - from));
                r.status = 206;
                r.headers.Set("Content-Range", "bytes " + std::to_string(from) + "-" + std::to_string(end - 1) + "/*");
            } else {
                r.status = 200;
            }
            call.Progress(std::int64_t(body.size()), std::int64_t(body.size()));
            r.body = std::move(body);
            return r;
        }
        void Set(const std::string& url, std::string body) {
            std::lock_guard lock(m_Mutex);
            bodies[url] = std::move(body);
        }
        void Set(const std::string& url, const Bytes& body) { Set(url, std::string(body.begin(), body.end())); }
        int Count(const std::string& url) {
            std::lock_guard lock(m_Mutex);
            return int(std::count(requests.begin(), requests.end(), url));
        }
        std::map<std::string, std::string> bodies, redirects;
        std::set<std::string> failing;
        std::vector<std::string> requests;

    private:
        std::mutex m_Mutex;
    };

    class FixedMeter final : public BandwidthMeter, public TransferListener {
    public:
        explicit FixedMeter(std::int64_t bitrate) : bitrate(bitrate) {}
        std::int64_t GetBitrateEstimate() override { return bitrate; }
        TransferListener* GetTransferListener() override { return this; }
        void OnTransferStart(bool, bool) override { ++starts; }
        void OnBytesTransferred(bool, int bytes, bool) override { transferred += bytes; }
        void OnTransferEnd(bool, bool) override { ++ends; }
        std::atomic<std::int64_t> bitrate;
        std::atomic<int> starts{ 0 }, ends{ 0 };
        std::atomic<std::int64_t> transferred{ 0 };
    };

    // A TS segment: PAT, PMT, a video and an audio PES with PTS ptsBase (90 kHz), the next video PES.
    Bytes TsSegment(std::int64_t pts, std::uint8_t fill) {
        auto packet = [](int pid, bool start, int cc, const Bytes& payload) {
            Bytes p(188, 0xff);
            p[0] = 0x47;
            p[1] = std::uint8_t((start ? 0x40 : 0) | ((pid >> 8) & 0x1f));
            p[2] = std::uint8_t(pid & 0xff);
            const std::size_t stuffing = 184 - payload.size();
            p[3] = std::uint8_t(0x30 | (cc & 0x0f));
            p[4] = std::uint8_t(stuffing - 1);
            if (stuffing > 1) p[5] = 0;
            std::memcpy(&p[4 + stuffing], payload.data(), payload.size());
            return p;
        };
        auto pes = [&](int streamId, std::int64_t t) {
            return Bytes{ 0x00, 0x00, 0x01, std::uint8_t(streamId), 0x00, 0x00, 0x80, 0x80, 0x05,
                          std::uint8_t(0x21 | ((t >> 29) & 0x0e)), std::uint8_t((t >> 22) & 0xff), std::uint8_t(((t >> 14) & 0xfe) | 1),
                          std::uint8_t((t >> 7) & 0xff), std::uint8_t(((t << 1) & 0xfe) | 1), fill, fill };
        };
        Bytes out;
        for (const Bytes& p : { packet(0, true, 0, { 0x00, 0x00, 0xb0, 13, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1, 0x00, 0, 0, 0, 0 }),
                                packet(0x100, true, 0, { 0x00, 0x02, 0xb0, 23, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x01, 0xf0, 0x00, 0x1b, 0xe1, 0x01,
                                                         0xf0, 0x00, 0x0f, 0xe1, 0x02, 0xf0, 0x00, 0, 0, 0, 0 }),
                                packet(0x101, true, 0, pes(0xe0, pts)), packet(0x102, true, 0, pes(0xc0, pts)), packet(0x101, true, 1, pes(0xe0, pts + 3000)) })
            out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    template<typename F>
    bool WaitFor(F&& condition, int ms = 3000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }

    // Everything a piece holds, read the way an extractor does.
    Bytes ReadAll(StreamPiece& piece, int reader = 0) {
        Bytes out;
        std::uint8_t buffer[1000];
        for (;;) {
            const std::int64_t n = piece.ReadAt(reader, std::int64_t(out.size()), buffer, sizeof buffer);
            if (n <= 0) break;
            out.insert(out.end(), buffer, buffer + n);
        }
        return out;
    }

    const char* kMultivariant = R"(#EXTM3U
#EXT-X-INDEPENDENT-SEGMENTS
#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=640x360,CODECS="avc1.64001e,mp4a.40.2"
low/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720,CODECS="avc1.64001f,mp4a.40.2"
high/index.m3u8
)";
    std::string MediaPlaylist(int firstSequence, int count, bool ended, const std::string& extraHeader = "") {
        std::string s = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:" + std::to_string(firstSequence) + "\n" + extraHeader;
        for (int i = firstSequence; i < firstSequence + count; ++i) s += "#EXTINF:4.0,\nseg" + std::to_string(i) + ".ts\n";
        if (ended) s += "#EXT-X-ENDLIST\n";
        return s;
    }

    struct HlsFixture {
        std::shared_ptr<FakeHttp> http = std::make_shared<FakeHttp>();
        FixedMeter meter{ 1'000'000 };
        std::shared_ptr<SegmentFetcher> fetcher = std::make_shared<SegmentFetcher>(http, &meter);
        std::atomic<std::int64_t> nowMs{ 1'000'000 };
        HlsFixture() {
            http->Set("https://x/master.m3u8", kMultivariant);
            for (const char* v : { "low", "high" }) {
                http->Set(std::string("https://x/") + v + "/index.m3u8", MediaPlaylist(0, 3, true));
                for (int i = 0; i < 3; ++i)
                    http->Set(std::string("https://x/") + v + "/seg" + std::to_string(i) + ".ts", TsSegment(900'000 + i * 360'000, std::uint8_t(v[0] + i)));
            }
        }
        std::shared_ptr<AdaptiveSource> Open(const std::string& uri = "https://x/master.m3u8") {
            return AdaptiveSource::Open(uri, "", fetcher, [this] { return nowMs.load(); });
        }
    };
}

TEST(AdaptiveStream_inferProtocol) {
    CHECK(AdaptiveSource::InferProtocol("https://a/b.m3u8?x=1", "") == AdaptiveSource::Protocol::Hls);
    CHECK(AdaptiveSource::InferProtocol("https://a/b.MPD", "") == AdaptiveSource::Protocol::Dash);
    CHECK(AdaptiveSource::InferProtocol("https://a/b", "application/x-mpegURL") == AdaptiveSource::Protocol::Hls);
    CHECK(AdaptiveSource::InferProtocol("https://a/b", "application/dash+xml") == AdaptiveSource::Protocol::Dash);
    CHECK(!AdaptiveSource::InferProtocol("https://a/b.mp4", "").has_value());
    CHECK(InferHlsContainer("https://a/s.ts?t=1") == SegmentContainer::Ts);
    CHECK(InferHlsContainer("https://a/s.aac") == SegmentContainer::PackedAudio);
    CHECK(InferHlsContainer("https://a/s.m4s") == SegmentContainer::Mp4);
    CHECK(InferHlsContainer("https://a/s.vtt") == SegmentContainer::WebVtt);
}

TEST(AdaptiveStream_hls_variantsFormOneVideoGroupWithMuxedAudio) {
    HlsFixture f;
    auto source = f.Open();
    CHECK(source->GetProtocol() == AdaptiveSource::Protocol::Hls);
    CHECK_EQ(source->Groups().size(), std::size_t(1));
    const StreamGroup& g = source->Groups()[0];
    CHECK_EQ(g.id, std::string("main"));
    CHECK(g.type == TrackType::Video);
    CHECK(g.primary);
    CHECK_EQ(g.formats.size(), std::size_t(2));
    CHECK_EQ(g.formats[1].sampleMimeType, std::string("video/avc"));
    CHECK_EQ(g.formats[1].codecs, std::string("avc1.64001f"));
    CHECK_EQ(g.formats[1].height, 720);
    CHECK_EQ(g.formats[1].bitrate, 2'000'000);
    CHECK(g.embeddedAudio.has_value());
    CHECK_EQ(g.embeddedAudio->sampleMimeType, std::string("audio/mp4a-latm"));
    const StreamWindow w = source->Window();
    CHECK(!w.dynamic);
    CHECK_EQ(w.durationUs, std::int64_t(12'000'000));
    CHECK_EQ(w.defaultPositionUs, std::int64_t(0));
    const SegmentTimeline t = source->Segments(0, 1);
    CHECK(t.ended);
    CHECK(t.container == SegmentContainer::Ts);
    CHECK_EQ(t.segments.size(), std::size_t(3));
    CHECK_EQ(t.segments[2].url, std::string("https://x/high/seg2.ts"));
    CHECK_EQ(t.segments[2].startUs, std::int64_t(8'000'000));
    CHECK_EQ(t.segments[2].sequence, std::int64_t(2));
}

TEST(AdaptiveStream_hls_redirect_resolvesAgainstTheUrlThatAnswered) {
    HlsFixture f;
    f.http->redirects["https://short/s"] = "https://x/master.m3u8";
    auto source = f.Open("https://short/s");
    CHECK(source->GetProtocol() == AdaptiveSource::Protocol::Hls);
    CHECK_EQ(source->Segments(0, 0).segments[0].url, std::string("https://x/low/seg0.ts"));
}

TEST(AdaptiveStream_hls_audioRenditionsGroupedByName) {
    HlsFixture f;
    f.http->Set("https://x/master.m3u8", R"(#EXTM3U
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="English",LANGUAGE="en",DEFAULT=YES,URI="en/index.m3u8"
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="Deutsch",LANGUAGE="de",URI="de/index.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=640x360,CODECS="avc1.64001e,mp4a.40.2",AUDIO="aac"
low/index.m3u8
)");
    auto source = f.Open();
    CHECK_EQ(source->Groups().size(), std::size_t(3));
    CHECK(!source->Groups()[0].embeddedAudio.has_value());   // the audio comes from the renditions
    CHECK_EQ(source->Groups()[1].id, std::string("audio:English"));
    CHECK(source->Groups()[1].type == TrackType::Audio);
    CHECK_EQ(source->Groups()[2].formats[0].language, std::string("de"));
}

TEST(AdaptiveStream_hls_mediaPlaylistAlone) {
    HlsFixture f;
    auto source = f.Open("https://x/low/index.m3u8");
    CHECK_EQ(source->Groups().size(), std::size_t(1));
    CHECK_EQ(source->Groups()[0].formats.size(), std::size_t(1));
    CHECK(source->Groups()[0].embeddedAudio.has_value());
    CHECK_EQ(source->Segments(0, 0).segments.size(), std::size_t(3));
}

TEST(AdaptiveStream_hls_fixedTrack_loadsOnePieceWithTsTimestamps) {
    HlsFixture f;
    auto source = f.Open();
    SegmentStream::Config config;
    config.tracks = { 0 };
    SegmentStream stream(source, config);
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    auto piece = stream.FirstPiece();
    CHECK(piece != nullptr);
    CHECK(piece->CanOpen());
    CHECK(piece->IsLast());
    CHECK_EQ(piece->Track(), 0);
    // The first PTS (10 s) maps to the first segment's start (0).
    CHECK(piece->TimeOffsetUs() == std::optional<std::int64_t>(0));
    Bytes expected;
    for (int i = 0; i < 3; ++i) {
        const std::string& s = f.http->bodies["https://x/low/seg" + std::to_string(i) + ".ts"];
        expected.insert(expected.end(), s.begin(), s.end());
    }
    CHECK(ReadAll(*piece) == expected);
    CHECK_EQ(stream.BufferedPositionUs(), std::int64_t(12'000'000));
    CHECK(f.meter.starts.load() >= 4);   // the playlists and segments are measured
    CHECK_EQ(f.meter.starts.load(), f.meter.ends.load());
}

TEST(AdaptiveStream_hls_startPosition_choosesTheContainingSegment) {
    HlsFixture f;
    auto source = f.Open();
    SegmentStream::Config config;
    config.tracks = { 1 };
    SegmentStream stream(source, config);
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    stream.Start(5'000'000);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    auto piece = stream.FirstPiece();
    CHECK_EQ(piece->StartUs(), std::int64_t(4'000'000));
    // Segment 1's PTS (14 s) on the timeline the first load set up.
    CHECK(piece->TimeOffsetUs() == std::optional<std::int64_t>(4'000'000));
}

TEST(AdaptiveStream_hls_adaptive_switchesRenditionIntoANewPiece) {
    HlsFixture f;
    f.meter.bitrate = 100'000'000;   // plenty: the high variant
    auto source = f.Open();
    SegmentStream::Config config;
    config.bandwidthMeter = &f.meter;
    config.minBufferUs = config.maxBufferUs = 6'000'000;   // two segments ahead at a time
    SegmentStream stream(source, config);
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.FirstPiece() != nullptr; }));
    auto first = stream.FirstPiece();
    CHECK_EQ(first->Track(), 1);
    CHECK(WaitFor([&] { return stream.BufferedPositionUs() == 8'000'000; }));
    // Bandwidth drops: the next segment comes from the low variant, in a piece of its own.
    f.meter.bitrate = 300'000;
    stream.SetPlaybackPosition(7'000'000);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    CHECK(first->Ended());
    auto second = first->Next();
    CHECK(second != nullptr);
    CHECK_EQ(second->Track(), 0);
    CHECK_EQ(second->StartUs(), std::int64_t(8'000'000));
    CHECK(second->IsLast());
    CHECK(second->TimeOffsetUs() == std::optional<std::int64_t>(8'000'000));
    CHECK_EQ(stream.SelectedTrack(), 0);
}

TEST(AdaptiveStream_hls_404_excludesTheTrack) {
    HlsFixture f;
    f.meter.bitrate = 100'000'000;
    f.http->failing.insert("https://x/high/seg0.ts");
    auto source = f.Open();
    SegmentStream::Config config;
    config.bandwidthMeter = &f.meter;
    SegmentStream stream(source, config);
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.FirstPiece() != nullptr; }));
    CHECK_EQ(stream.FirstPiece()->Track(), 0);
    CHECK(stream.Error().empty());
}

TEST(AdaptiveStream_hls_aes128) {
    HlsFixture f;
    const Bytes key = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
    const Bytes cipher = { 0x0b, 0xd3, 0xb0, 0x37, 0x0b, 0xd8, 0xec, 0x6a, 0x41, 0x93, 0xcf, 0xeb, 0x80, 0x2b, 0x13, 0x93, 0xd8, 0xda, 0xfa, 0x58, 0x5a, 0x8b,
                           0x04, 0xb1, 0x11, 0x91, 0xb6, 0x94, 0x54, 0x9e, 0x8f, 0xfa, 0x66, 0xa7, 0x6b, 0x89, 0x6a, 0xf8, 0xe4, 0x10, 0x8b, 0xcf, 0xd6, 0x94,
                           0x89, 0x42, 0x96, 0x9c, 0xc0, 0x3e, 0x1e, 0x67, 0x8f, 0xf8, 0x6d, 0xcc, 0xd6, 0x7a, 0xea, 0x4a, 0x8f, 0x5e, 0xd3, 0xbf };
    f.http->Set("https://x/key.bin", key);
    f.http->Set("https://x/enc.m3u8", "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\",IV=0x000102030405060708090a0b0c0d0e0f\n"
                                      "#EXTINF:4.0,\nenc.aac\n#EXT-X-ENDLIST\n");
    f.http->Set("https://x/enc.aac", cipher);
    auto source = f.Open("https://x/enc.m3u8");
    SegmentStream stream(source, {});
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    auto piece = stream.FirstPiece();
    CHECK(piece->Container() == SegmentContainer::PackedAudio);
    const Bytes plain = ReadAll(*piece);
    CHECK_EQ(std::string(plain.begin(), plain.end()), std::string("kiln adaptive streaming: AES-128 segment decryption check!"));
    // Packed audio without an ID3 timestamp starts at the segment's start.
    CHECK(piece->TimeOffsetUs() == std::optional<std::int64_t>(0));
}

TEST(AdaptiveStream_hls_live_refreshesAndWaitsForSegments) {
    HlsFixture f;
    f.http->Set("https://x/live.m3u8", MediaPlaylist(10, 4, false));
    for (int i = 10; i < 20; ++i) f.http->Set("https://x/seg" + std::to_string(i) + ".ts", TsSegment(900'000 + i * 360'000, std::uint8_t(i)));
    auto source = f.Open("https://x/live.m3u8");
    StreamWindow w = source->Window();
    CHECK(w.dynamic);
    CHECK_EQ(w.durationUs, std::int64_t(16'000'000));
    CHECK_EQ(w.targetLiveOffsetUs, std::int64_t(12'000'000));   // three target durations
    CHECK_EQ(w.defaultPositionUs, std::int64_t(4'000'000));      // the segment containing 16 - 12 s
    SegmentStream stream(source, {});
    stream.Start(w.startUs + w.defaultPositionUs);
    CHECK(WaitFor([&] { return stream.BufferedPositionUs() == 16'000'000; }));
    CHECK_EQ(stream.FirstPiece()->StartUs(), std::int64_t(4'000'000));
    CHECK(!stream.IsEnded());
    // The playlist slides on; it is loaded again after the target duration.
    f.http->Set("https://x/live.m3u8", MediaPlaylist(12, 4, false));
    f.nowMs += 4000;
    CHECK(WaitFor([&] { return stream.BufferedPositionUs() == 24'000'000; }));
    // Start time of the new playlist: segment 12 starts 8 s after segment 10.
    w = source->Window();
    CHECK_EQ(w.startUs, std::int64_t(8'000'000));
    // The end tag ends the stream.
    f.http->Set("https://x/live.m3u8", MediaPlaylist(12, 5, true));
    f.nowMs += 4000;
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    CHECK_EQ(stream.BufferedPositionUs(), std::int64_t(28'000'000));
    CHECK(stream.FirstPiece()->Next() == nullptr);   // one piece throughout
}

TEST(AdaptiveStream_piece_readersAndEviction) {
    HlsFixture f;
    auto source = f.Open();
    SegmentStream::Config config;
    config.tracks = { 0 };
    config.readers = 2;
    SegmentStream stream(source, config);
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    auto piece = stream.FirstPiece();
    const Bytes a = ReadAll(*piece, 0);
    // The other reader still holds the segments back: it reads the same bytes.
    CHECK(ReadAll(*piece, 1) == a);
    CHECK(!piece->IsWaiting(0));
    // Both read to the end: early segments are gone, a read there fails.
    std::uint8_t byte = 0;
    CHECK_EQ(piece->ReadAt(0, 0, &byte, 1), std::int64_t(-1));
    CHECK_EQ(piece->ReadAt(0, std::int64_t(a.size()), &byte, 1), std::int64_t(0));
}

// --- DASH ------------------------------------------------------------------------------------------
namespace {
    const char* kTemplateMpd = R"(<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT8S" minBufferTime="PT2S">
  <Period id="0" start="PT0S">
    <AdaptationSet id="1" mimeType="video/mp4" segmentAlignment="true">
      <SegmentTemplate timescale="1000" duration="2000" initialization="init-$RepresentationID$.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="1" presentationTimeOffset="500"/>
      <Representation id="v1" bandwidth="400000" width="640" height="360" codecs="avc1.4d401e"/>
      <Representation id="v2" bandwidth="1500000" width="1280" height="720" codecs="avc1.4d401f"/>
    </AdaptationSet>
    <AdaptationSet id="2" mimeType="audio/mp4" lang="en">
      <SegmentTemplate timescale="1000" duration="2000" initialization="init-$RepresentationID$.mp4" media="$RepresentationID$-$Number$.m4s" startNumber="1"/>
      <Representation id="a1" bandwidth="128000" codecs="mp4a.40.2" audioSamplingRate="48000"/>
    </AdaptationSet>
  </Period>
</MPD>
)";
}

TEST(AdaptiveStream_dash_template_groupsSegmentsAndOffsets) {
    HlsFixture f;
    f.http->Set("https://d/m.mpd", kTemplateMpd);
    for (const char* r : { "v1", "v2", "a1" }) {
        f.http->Set(std::string("https://d/init-") + r + ".mp4", std::string("INIT") + r);
        for (int n = 1; n <= 4; ++n) f.http->Set(std::string("https://d/") + r + "-" + std::to_string(n) + ".m4s", std::string("SEG") + r + std::to_string(n));
    }
    auto source = AdaptiveSource::Open("https://d/m.mpd", "", f.fetcher, [&] { return f.nowMs.load(); });
    CHECK(source->GetProtocol() == AdaptiveSource::Protocol::Dash);
    CHECK_EQ(source->Groups().size(), std::size_t(2));
    CHECK(source->Groups()[0].type == TrackType::Video);
    CHECK(source->Groups()[0].primary);
    CHECK_EQ(source->Groups()[0].formats.size(), std::size_t(2));
    CHECK(source->Groups()[1].type == TrackType::Audio);
    CHECK_EQ(source->Window().durationUs, std::int64_t(8'000'000));
    const SegmentTimeline t = source->Segments(0, 1);
    CHECK(t.ended);
    CHECK(t.container == SegmentContainer::Mp4);
    CHECK_EQ(t.segments.size(), std::size_t(4));
    CHECK_EQ(t.segments[0].url, std::string("https://d/v2-1.m4s"));
    CHECK_EQ(t.segments[3].startUs, std::int64_t(6'000'000));
    CHECK_EQ(t.segments[0].timeOffsetUs, std::int64_t(-500'000));   // period start - presentation time offset
    CHECK(t.segments[0].init != nullptr);
    CHECK_EQ(t.segments[0].init->url, std::string("https://d/init-v2.mp4"));

    SegmentStream::Config config;
    config.group = 1;
    SegmentStream audio(source, config);
    audio.Start(3'000'000);
    CHECK(WaitFor([&] { return audio.IsEnded(); }));
    auto piece = audio.FirstPiece();
    CHECK_EQ(piece->StartUs(), std::int64_t(2'000'000));
    // The platform's MP4 extractor counts from the piece's first fragment: its segment's start.
    CHECK(piece->TimeOffsetUs() == std::optional<std::int64_t>(2'000'000));
    const Bytes bytes = ReadAll(*piece);
    CHECK_EQ(std::string(bytes.begin(), bytes.end()), std::string("INITa1SEGa12SEGa13SEGa14"));
}

TEST(AdaptiveStream_dash_segmentBase_loadsTheSidx) {
    HlsFixture f;
    f.http->Set("https://d/s.mpd", R"(<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT5S">
  <Period>
    <AdaptationSet mimeType="audio/mp4">
      <Representation id="a" bandwidth="64000" codecs="mp4a.40.2">
        <BaseURL>audio.mp4</BaseURL>
        <SegmentBase indexRange="8-63"><Initialization range="0-7"/></SegmentBase>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)");
    Bytes file = { 0, 0, 0, 8, 'f', 't', 'y', 'p' };
    const Bytes sidx = { 0, 0, 0, 56, 's', 'i', 'd', 'x', 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0x03, 0xe8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
                         0, 0, 0, 4, 0, 0, 0x07, 0xd0, 0x90, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0x0b, 0xb8, 0x90, 0, 0, 0 };
    file.insert(file.end(), sidx.begin(), sidx.end());
    for (char c : std::string("AAAABBBBB")) file.push_back(std::uint8_t(c));
    f.http->Set("https://d/audio.mp4", file);
    auto source = AdaptiveSource::Open("https://d/s.mpd", "", f.fetcher, [&] { return f.nowMs.load(); });
    const SegmentTimeline t = source->Segments(0, 0);
    CHECK_EQ(t.segments.size(), std::size_t(2));
    CHECK_EQ(t.segments[1].offset, std::int64_t(68));
    CHECK_EQ(t.segments[1].length, std::int64_t(5));
    CHECK_EQ(t.segments[1].startUs, std::int64_t(2'000'000));
    CHECK_EQ(t.segments[1].durationUs, std::int64_t(3'000'000));
    CHECK_EQ(f.http->Count("https://d/audio.mp4"), 1);   // the init and index ranges in one request
    CHECK_EQ(source->Segments(0, 0).segments.size(), std::size_t(2));
    CHECK_EQ(f.http->Count("https://d/audio.mp4"), 1);   // kept
    SegmentStream stream(source, {});
    stream.Start(0);
    CHECK(WaitFor([&] { return stream.IsEnded(); }));
    const Bytes bytes = ReadAll(*stream.FirstPiece());
    CHECK_EQ(bytes.size(), std::size_t(17));   // the initialization range and the two fragments
    CHECK_EQ(std::string(bytes.begin() + 8, bytes.end()), std::string("AAAABBBBB"));
}

TEST(AdaptiveStream_dash_live_window) {
    HlsFixture f;
    // Two minutes after availabilityStartTime, 4 s segments, a 60 s time shift buffer.
    f.nowMs = 1'700'000'120'000;
    f.http->Set("https://d/live.mpd", R"(<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="dynamic" availabilityStartTime="2023-11-14T22:13:20Z" minimumUpdatePeriod="PT10S" timeShiftBufferDepth="PT60S" suggestedPresentationDelay="PT12S">
  <Period id="p" start="PT0S">
    <AdaptationSet mimeType="video/mp4">
      <SegmentTemplate timescale="1" duration="4" media="v-$Number$.m4s" initialization="v-init.mp4" startNumber="0"/>
      <Representation id="v" bandwidth="500000" width="640" height="360" codecs="avc1.4d401e"/>
    </AdaptationSet>
  </Period>
</MPD>
)");
    auto source = AdaptiveSource::Open("https://d/live.mpd", "", f.fetcher, [&] { return f.nowMs.load(); });
    const StreamWindow w = source->Window();
    CHECK(w.dynamic);
    CHECK_EQ(w.startUs + w.durationUs, std::int64_t(120'000'000));   // the last complete segment ends now
    CHECK_EQ(w.durationUs, std::int64_t(60'000'000));
    CHECK_EQ(w.targetLiveOffsetUs, std::int64_t(12'000'000));
    CHECK_EQ(w.defaultPositionUs, std::int64_t(48'000'000));
    const SegmentTimeline t = source->Segments(0, 0);
    CHECK(!t.ended);
    CHECK(!t.segments.empty());
    CHECK_EQ(t.segments.back().EndUs(), std::int64_t(120'000'000));
}
