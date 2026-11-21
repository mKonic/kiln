// Ported from media3 exoplayer-rtsp's RtspClientTest (against a port of its RtspServer test
// helper), and RtspPlaybackTest's cases through RtspSession over real TCP and UDP transports.
#include "Check.h"
#include "RtspTestData.h"

#include "kiln/media/codec/CodecUtil.h"
#include "kiln/media/rtsp/RtspSession.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>

using namespace kiln::media;
using namespace kiln::media::rtsp;
using namespace std::chrono_literals;

namespace {
    template <typename F> bool WaitUntil(F&& condition, int ms = 5000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(5ms);
        }
        return true;
    }

    // RtspServer: answers each request from a provider, adding its CSeq.
    class FakeRtspServer {
    public:
        struct Provider {
            std::function<RtspResponse()> options;
            std::function<RtspResponse(const std::string& uri, const RtspHeaders& headers)> describe, setup;
            std::function<RtspResponse(const RtspHeaders& headers)> play;
            std::function<RtspResponse()> pause;
            std::function<RtspResponse()> teardown = [] { return RtspResponse{ 200, {}, "" }; };
        };
        explicit FakeRtspServer(Provider provider) : m_Provider(std::move(provider)), m_Thread([this] { Listen(); }) {}
        ~FakeRtspServer() {
            m_Canceled = true;
            m_Listener.Close();
            m_Thread.join();
            std::vector<std::unique_ptr<RtspMessageChannel>> clients;
            {
                std::lock_guard lock(m_Mutex);
                clients = std::move(m_Clients);
            }
            for (auto& c : clients) c->Close();
        }
        int Port() const { return m_Listener.Port(); }
        std::vector<RtspRequest> Requests() {
            std::lock_guard lock(m_Mutex);
            return m_Requests;
        }
        int Count(RtspMethod method) {
            int n = 0;
            for (const auto& r : Requests()) n += r.method == method;
            return n;
        }
        // Interleaved data to the latest client.
        void SendInterleaved(int channel, const std::vector<std::uint8_t>& data) {
            std::lock_guard lock(m_Mutex);
            if (!m_Clients.empty()) m_Clients.back()->SendInterleaved(channel, data);
        }
        // Called after each PLAY response (the transmitter's start).
        std::function<void(const std::string& transport)> onPlay;

    private:
        void Listen() {
            while (!m_Canceled) {
                TcpSocket socket = m_Listener.Accept();
                if (!socket.IsOpen()) return;
                std::lock_guard lock(m_Mutex);
                auto* raw = new RtspMessageChannel({});
                m_Clients.emplace_back(raw);
                raw->~RtspMessageChannel();
                new (raw) RtspMessageChannel(RtspMessageChannel::Listener{ [this, raw](std::vector<std::string> message) { Handle(raw, message); }, nullptr });
                raw->Open(std::move(socket));
            }
        }
        void Handle(RtspMessageChannel* client, const std::vector<std::string>& message) {
            const RtspRequest request = RtspMessageUtil::ParseRequest(message);
            {
                std::lock_guard lock(m_Mutex);
                m_Requests.push_back(request);
            }
            RtspResponse response{ 454, {}, "" };   // RTSP_ERROR_METHOD_NOT_ALLOWED
            switch (request.method) {
                case RtspMethod::Options: response = m_Provider.options(); break;
                case RtspMethod::Describe: if (m_Provider.describe) response = m_Provider.describe(request.uri, request.headers); break;
                case RtspMethod::Setup:
                    if (m_Provider.setup) response = m_Provider.setup(request.uri, request.headers);
                    if (response.status == 200) m_Transport = request.headers.Get(RtspHeaders::TRANSPORT).value_or("");
                    break;
                case RtspMethod::Play: if (m_Provider.play) response = m_Provider.play(request.headers); break;
                case RtspMethod::Pause: if (m_Provider.pause) response = m_Provider.pause(); break;
                case RtspMethod::Teardown: response = m_Provider.teardown(); break;
                default: break;
            }
            response.headers = response.headers.BuildUpon().Add(RtspHeaders::CSEQ, request.headers.Get(RtspHeaders::CSEQ).value_or("0")).Build();
            client->Send(RtspMessageUtil::SerializeResponse(response));
            if (request.method == RtspMethod::Play && response.status == 200 && onPlay) onPlay(m_Transport);
        }

        Provider m_Provider;
        TcpListener m_Listener;
        std::atomic<bool> m_Canceled{ false };
        std::mutex m_Mutex;
        std::vector<std::unique_ptr<RtspMessageChannel>> m_Clients;
        std::vector<RtspRequest> m_Requests;
        std::string m_Transport;
        std::thread m_Thread;
    };

    const std::string kSessionDescription = "v=0\r\no=- 1606776316530225 1 IN IP4 127.0.0.1\r\ns=Exoplayer test\r\nt=0 0\r\na=range:npt=0-50.46\r\n";

    // RtspTestUtils.newDescribeResponseWithSdpMessage.
    RtspResponse DescribeResponse(const std::string& session, const std::vector<const rtsp_test_data::Dump*>& dumps, const std::string& uri) {
        std::string sdp = session;
        for (const auto* d : dumps) sdp += std::string(d->mediaDescription) + "\r\n";
        return { 200, RtspHeaders::Builder().Add(RtspHeaders::CONTENT_BASE, uri).Add(RtspHeaders::CONTENT_LENGTH, std::to_string(sdp.size())).Build(), sdp };
    }
    std::string TestUri(int port) { return "rtsp://localhost:" + std::to_string(port) + "/test"; }
    const std::vector<const rtsp_test_data::Dump*> kDumps = { &rtsp_test_data::kH264, &rtsp_test_data::kAac, &rtsp_test_data::kMpeg2ts };

    struct ClientHarness : RtspClient::SessionInfoListener, RtspClient::PlaybackEventListener {
        TaskRunner handler;
        std::mutex mutex;
        std::optional<std::vector<RtspMediaTrack>> tracks;
        std::optional<std::string> failure;
        std::unique_ptr<RtspClient> client;
        void OnSessionTimelineUpdated(const RtspSessionTiming&, const std::vector<RtspMediaTrack>& t) override {
            std::lock_guard lock(mutex);
            tracks = t;
        }
        void OnSessionTimelineRequestFailed(const std::string& message) override {
            std::lock_guard lock(mutex);
            failure = message;
        }
        void OnRtspSetupCompleted() override {}
        void OnPlaybackStarted(std::int64_t, const std::vector<RtspTrackTiming>&) override {}
        void OnPlaybackError(const std::string&, bool) override {}
        void Start(const std::string& uri, RtspClient::SocketFactory factory = nullptr) {
            handler.RunAndWait([&] {
                RtspClient::SessionInfoListener& s = *this;
                RtspClient::PlaybackEventListener& p = *this;
                client = std::make_unique<RtspClient>(handler, s, p, "ExoPlayer:RtspClientTest", uri, factory);
                client->Start();
            });
        }
        bool HasTracks() {
            std::lock_guard lock(mutex);
            return tracks.has_value();
        }
        bool Failed() {
            std::lock_guard lock(mutex);
            return failure.has_value();
        }
        RtspClient::State State() {
            RtspClient::State state;
            handler.RunAndWait([&] { state = client->GetState(); });
            return state;
        }
        ~ClientHarness() override {
            handler.RunAndWait([&] {
                if (client) client->Close();
                client.reset();
            });
            handler.Stop();
        }
    };

    FakeRtspServer::Provider DescribingProvider(std::string publicHeader = "OPTIONS, DESCRIBE") {
        FakeRtspServer::Provider p;
        p.options = [publicHeader] {
            RtspHeaders::Builder h;
            if (!publicHeader.empty()) h.Add(RtspHeaders::PUBLIC, publicHeader);
            return RtspResponse{ 200, h.Build(), "" };
        };
        p.describe = [](const std::string& uri, const RtspHeaders&) { return DescribeResponse(kSessionDescription, kDumps, uri); };
        return p;
    }
}

TEST(RtspClient_usesTheSocketFactoryAndDescribes) {
    std::atomic<bool> describeAcceptsSdp{ false };
    FakeRtspServer::Provider p = DescribingProvider();
    p.describe = [&](const std::string& uri, const RtspHeaders& headers) {
        describeAcceptsSdp = headers.Get(RtspHeaders::ACCEPT) == "application/sdp";
        return DescribeResponse(kSessionDescription, kDumps, uri);
    };
    FakeRtspServer server(p);
    std::atomic<bool> createdSocket{ false };
    ClientHarness h;
    h.Start(TestUri(server.Port()), [&](const std::string& host, int port) {
        createdSocket = true;
        return TcpSocket::Connect(host, port);
    });
    CHECK(WaitUntil([&] { return h.HasTracks(); }));
    CHECK(createdSocket && describeAcceptsSdp);
    CHECK_EQ(h.tracks->size(), std::size_t(2));   // H.264 and AAC; MPEG-2 TS has no reader
    CHECK(h.State() == RtspClient::RTSP_STATE_UNINITIALIZED);
}

TEST(RtspClient_describeRedirects) {
    FakeRtspServer::Provider p = DescribingProvider("");
    p.describe = [](const std::string& uri, const RtspHeaders&) {
        if (uri.find("redirect") == std::string::npos) return RtspResponse{ 301, RtspHeaders::Builder().Add(RtspHeaders::LOCATION, uri + "/redirect").Build(), "" };
        return DescribeResponse(kSessionDescription, kDumps, uri);
    };
    FakeRtspServer server(p);
    ClientHarness h;
    h.Start(TestUri(server.Port()));
    CHECK(WaitUntil([&] { return h.HasTracks(); }));
    CHECK(h.tracks->size() == 2 && h.State() == RtspClient::RTSP_STATE_UNINITIALIZED);
    CHECK(h.tracks->at(0).uri.find("/test/redirect/track1") != std::string::npos);
}

TEST(RtspClient_noPublicHeader_describes) {
    FakeRtspServer server(DescribingProvider(""));
    ClientHarness h;
    h.Start(TestUri(server.Port()));
    CHECK(WaitUntil([&] { return h.HasTracks(); }));
    CHECK(h.tracks->size() == 2 && h.State() == RtspClient::RTSP_STATE_UNINITIALIZED);
}

TEST(RtspClient_serverWithoutDescribe_fails) {
    FakeRtspServer server(DescribingProvider("OPTIONS"));
    ClientHarness h;
    h.Start(TestUri(server.Port()));
    CHECK(WaitUntil([&] { return h.Failed(); }));
    CHECK(h.failure->find("DESCRIBE not supported.") != std::string::npos);
    CHECK(server.Count(RtspMethod::Describe) == 0 && h.State() == RtspClient::RTSP_STATE_UNINITIALIZED);
}

TEST(RtspClient_sdpWithoutTracksOrWithABadFmtp_fails) {
    for (const std::string sdp : { std::string("v=0\r\n"),
                                   std::string("v=0\r\no=- 1600785369059721 1 IN IP4 192.168.2.176\r\ns=video, streamed by ExoPlayer\r\ni=test.mkv\r\nt=0 0\r\na=tool:ExoPlayer\r\n"
                                               "a=type:broadcast\r\na=control:*\r\na=range:npt=0-30.102\r\nm=video 0 RTP/AVP 96\r\nc=IN IP4 0.0.0.0\r\nb=AS:500\r\n"
                                               "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;profile-level-id=64001F;sprop-parameter-sets=\r\na=control:track1\r\n") }) {
        FakeRtspServer::Provider p = DescribingProvider();
        p.describe = [sdp](const std::string& uri, const RtspHeaders&) { return DescribeResponse(sdp, {}, uri); };
        FakeRtspServer server(p);
        ClientHarness h;
        h.Start(TestUri(server.Port()));
        CHECK(WaitUntil([&] { return h.Failed(); }));
        CHECK(!h.HasTracks() && h.State() == RtspClient::RTSP_STATE_UNINITIALIZED);
    }
}

TEST(RtspClient_authentication) {
    auto provider = [](std::atomic<bool>& sawDigest) {
        FakeRtspServer::Provider p = DescribingProvider();
        p.describe = [&sawDigest](const std::string& uri, const RtspHeaders& headers) {
            const auto authorization = headers.Get(RtspHeaders::AUTHORIZATION);
            if (!authorization)
                return RtspResponse{ 401,
                                     RtspHeaders::Builder()
                                         .Add(RtspHeaders::WWW_AUTHENTICATE, R"(Digest realm="RTSP server", nonce="0cdfe9719e7373b7d5bb2913e2115f3f", opaque="5ccc069c403ebaf9f0171e9517f40e41")")
                                         .Add(RtspHeaders::WWW_AUTHENTICATE, R"(BASIC realm="WallyWorld")")
                                         .Build(),
                                     "" };
            if (authorization->find("Digest") == std::string::npos) return RtspResponse{ 401, {}, "" };
            sawDigest = true;
            return DescribeResponse(kSessionDescription, kDumps, uri);
        };
        return p;
    };
    {   // Without credentials: the 401 ends it.
        std::atomic<bool> sawDigest{ false };
        FakeRtspServer server(provider(sawDigest));
        ClientHarness h;
        h.Start(TestUri(server.Port()));
        CHECK(WaitUntil([&] { return h.Failed(); }));
        CHECK(!sawDigest && h.State() == RtspClient::RTSP_STATE_UNINITIALIZED);
    }
    {   // With them in the URI: DESCRIBE again with Digest (preferred over Basic).
        std::atomic<bool> sawDigest{ false };
        FakeRtspServer server(provider(sawDigest));
        ClientHarness h;
        h.Start("rtsp://username:password@localhost:" + std::to_string(server.Port()) + "/test");
        CHECK(WaitUntil([&] { return h.HasTracks(); }));
        CHECK(sawDigest && server.Count(RtspMethod::Describe) == 2);
        const auto requests = server.Requests();
        CHECK(requests.back().uri.find("username") == std::string::npos);   // the user info stays out of requests
    }
}

// --- RtspSession (RtspPlaybackTest) --------------------------------------------------------------
namespace {
    // RtspPlaybackTest.ResponseProvider: SETUP for the AAC track answers with the transport (TCP only
    // when asked), PLAY with RTP-Info; the packets go out after PLAY.
    struct PlaybackServer {
        std::atomic<int> setups{ 0 };
        bool tcpOnly = false, seekable = false;
        std::string sessionTimeout;
        std::unique_ptr<FakeRtspServer> server;
        std::thread transmitter;
        std::atomic<bool> stop{ false };
        std::atomic<int> plays{ 0 };
        // Sequence numbers go on across PLAYs (as a server's do): each PLAY starts where the last
        // one's packets ended.
        int NextBase() const { return (53049 + 290 * plays.load()) & 0xFFFF; }
        explicit PlaybackServer(std::vector<const rtsp_test_data::Dump*> dumps, bool onlyTcp = false, std::string timeout = "") : tcpOnly(onlyTcp), sessionTimeout(std::move(timeout)) {
            FakeRtspServer::Provider p;
            p.options = [] { return RtspResponse{ 200, RtspHeaders::Builder().Add(RtspHeaders::PUBLIC, "OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN").Build(), "" }; };
            p.describe = [dumps](const std::string& uri, const RtspHeaders&) {
                return DescribeResponse("v=0\r\no=- 1606776316530225 1 IN IP4 127.0.0.1\r\ns=Exoplayer test\r\nt=0 0\r\n", dumps, uri);
            };
            p.setup = [this](const std::string&, const RtspHeaders& headers) {
                const std::string transport = headers.Get(RtspHeaders::TRANSPORT).value_or("");
                if (tcpOnly && transport.find("TCP") == std::string::npos) return RtspResponse{ 461, {}, "" };
                ++setups;
                return RtspResponse{ 200, RtspHeaders::Builder().Add(RtspHeaders::TRANSPORT, transport).Add(RtspHeaders::SESSION, "00000000" + sessionTimeout).Build(), "" };
            };
            p.play = [this](const RtspHeaders& headers) {
                RtspHeaders::Builder h;
                h.Add(RtspHeaders::RTP_INFO, "url=" + TestUri(server->Port()) + "/track2;seq=" + std::to_string(NextBase()) + ";rtptime=588391363");
                if (seekable) h.Add(RtspHeaders::RANGE, headers.Get(RtspHeaders::RANGE).value_or("npt=0.000-"));
                return RtspResponse{ 200, h.Build(), "" };
            };
            p.pause = [] { return RtspResponse{ 200, {}, "" }; };
            server = std::make_unique<FakeRtspServer>(p);
            server->onPlay = [this](const std::string& transport) {
                // Each PLAY sends the packets again (the dump's timestamps with the RTP-Info's).
                stop = true;
                if (transmitter.joinable()) transmitter.join();
                stop = false;
                transmitter = std::thread([this, transport] { Transmit(transport); });
            };
        }
        ~PlaybackServer() {
            stop = true;
            if (transmitter.joinable()) transmitter.join();
        }
        // RtpPacketTransmitter: the AAC dump's packets, interleaved or to the client's UDP port.
        void Transmit(const std::string& transport) {
            std::smatch m;
            const bool tcp = transport.find("TCP") != std::string::npos;
            int target = 0;
            if (tcp && std::regex_search(transport, m, std::regex("interleaved=(\\d+)"))) target = std::stoi(m[1].str());
            if (!tcp && std::regex_search(transport, m, std::regex("client_port=(\\d+)"))) target = std::stoi(m[1].str());
            UdpSocket udp;
            const int base = NextBase();
            ++plays;
            int index = 0;
            for (const char* hex : rtsp_test_data::kAac.packets) {
                if (stop) return;
                auto packet = codec::BytesFromHexString(hex);
                const int seq = (base + index++) & 0xFFFF;
                packet[2] = std::uint8_t(seq >> 8);
                packet[3] = std::uint8_t(seq);
                if (tcp) server->SendInterleaved(target, packet);
                else udp.SendTo(target, packet.data(), int(packet.size()));
                std::this_thread::sleep_for(1ms);
            }
        }
    };

    struct Played {
        std::vector<SampleQueue::Item> samples;
        std::vector<Format> formats;
        bool ended = false;
    };
    Played PlayToEnd(RtspSession& session, int ms = 10'000) {
        Played out;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto item = session.Read(0)) {
                if (item->kind == SampleQueue::Item::Kind::Format) out.formats.push_back(item->format);
                else out.samples.push_back(std::move(*item));
                continue;
            }
            if (session.IsEnded(0)) {
                out.ended = true;
                break;
            }
            std::this_thread::sleep_for(2ms);
        }
        return out;
    }
    RtspSession::Options FastOptions(bool tcp) {
        RtspSession::Options o;
        o.timeoutMs = 400;   // the end of the packets comes sooner
        o.forceUseRtpTcp = tcp;
        return o;
    }
}

TEST(RtspSession_playsAacUntilEnded_overTcpAndUdp) {
    for (bool tcp : { true, false }) {
        PlaybackServer server({ &rtsp_test_data::kAac, &rtsp_test_data::kMpeg2ts });
        RtspSession session(TestUri(server.server->Port()), FastOptions(tcp));
        session.Prepare();
        CHECK(WaitUntil([&] { return session.IsPrepared() || session.Error(); }));
        CHECK(!session.Error());
        const std::vector<Format> formats = session.TrackFormats();
        CHECK(formats.size() == 1 && formats[0].sampleMimeType == stream::MimeTypes::AUDIO_AAC && formats[0].channelCount == 6 && formats[0].sampleRate == 48000);
        CHECK(session.GetTimeline().known && session.GetTimeline().live);   // no range: npt=0-
        session.SelectTracks({ 0 }, 0);
        const Played played = PlayToEnd(session);
        CHECK(played.ended && server.setups == 1);
        CHECK(played.formats.size() == 1 && !played.formats[0].initializationData.empty());
        CHECK(played.samples.size() > 200);
        bool ordered = !played.samples.empty() && played.samples[0].timeUs == 0;
        for (std::size_t i = 1; i < played.samples.size(); ++i) ordered = ordered && played.samples[i].timeUs >= played.samples[i - 1].timeUs;
        CHECK(ordered);
    }
}

TEST(RtspSession_udpUnsupported_fallsBackToTcp) {
    PlaybackServer server({ &rtsp_test_data::kAac }, /*onlyTcp=*/true);
    RtspSession session(TestUri(server.server->Port()), FastOptions(false));
    session.Prepare();
    CHECK(WaitUntil([&] { return session.IsPrepared() || session.Error(); }));
    session.SelectTracks({ 0 }, 0);
    const Played played = PlayToEnd(session);
    CHECK(played.ended && played.samples.size() > 200 && !session.Error());
    const auto requests = server.server->Requests();
    int udpSetups = 0, tcpSetups = 0;
    for (const auto& r : requests)
        if (r.method == RtspMethod::Setup) (r.headers.Get(RtspHeaders::TRANSPORT).value_or("").find("TCP") != std::string::npos ? tcpSetups : udpSetups)++;
    CHECK(udpSetups == 1 && tcpSetups == 1);
}

TEST(RtspSession_noSupportedTrack_failsToPrepare) {
    PlaybackServer server({ &rtsp_test_data::kMpeg2ts });
    RtspSession session(TestUri(server.server->Port()), FastOptions(false));
    session.Prepare();
    CHECK(WaitUntil([&] { return session.Error().has_value(); }));
    CHECK(session.Error()->find("No playable track.") != std::string::npos && !session.IsPrepared());
}

TEST(RtspSession_customSessionTimeout_sendsKeepAlive) {
    PlaybackServer server({ &rtsp_test_data::kAac }, false, ";timeout=1");
    RtspSession session(TestUri(server.server->Port()), FastOptions(true));
    session.Prepare();
    CHECK(WaitUntil([&] { return session.IsPrepared(); }));
    session.SelectTracks({ 0 }, 0);
    // OPTIONS every half of the 1 s timeout after PLAY (the first OPTIONS opened the session).
    CHECK(WaitUntil([&] { return server.server->Count(RtspMethod::Options) >= 2; }, 3000));
}

// A seek beyond what is loaded: PAUSE, then PLAY from the position; the samples carry on from the
// position the server answers with (onPlaybackStarted's seekToUs).
TEST(RtspSession_seeksWithPauseAndPlay) {
    PlaybackServer server({ &rtsp_test_data::kAac });
    server.seekable = true;
    RtspSession session(TestUri(server.server->Port()), FastOptions(true));
    session.Prepare();
    CHECK(WaitUntil([&] { return session.IsPrepared(); }));
    session.SelectTracks({ 0 }, 0);
    const Played first = PlayToEnd(session);   // the packets end; loading finishes
    CHECK(first.ended && !first.samples.empty() && first.samples.front().timeUs == 0);
    session.SeekToUs(6'000'000);
    CHECK(WaitUntil([&] { return server.server->Count(RtspMethod::Play) == 2; }));
    const Played after = PlayToEnd(session);
    CHECK(after.ended && after.samples.size() > 200);
    // Packets that come before the PLAY response are dropped while the seek is pending; the rest
    // are timed from the position.
    CHECK(!after.samples.empty() && after.samples.front().timeUs >= 6'000'000 && after.samples.front().timeUs < 6'200'000);
    const auto requests = server.server->Requests();
    bool rangeAsked = false;
    for (const auto& r : requests)
        if (r.method == RtspMethod::Play && r.headers.Get(RtspHeaders::RANGE) == "npt=6.000-") rangeAsked = true;
    CHECK(rangeAsked);
}
