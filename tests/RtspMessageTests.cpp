// Ported from media3 exoplayer-rtsp: RtspHeadersTest, RtspMessageUtilTest,
// RtspAuthenticationInfoTest, RtspSessionTimingTest, RtspTrackTimingTest.
#include "Check.h"

#include "kiln/media/rtsp/RtspMessage.h"

using namespace kiln::media;
using namespace kiln::media::rtsp;
using Lines = std::vector<std::string>;
using MultiMap = std::vector<std::pair<std::string, std::vector<std::string>>>;

namespace {
    template <typename E, typename F> bool Throws(F&& f) {
        try {
            f();
        } catch (const E&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }
    const Lines kHeaderLines = { "Accept: application/sdp ", "CSeq:3", "Content-Length: 707", "Transport: RTP/AVP;unicast;client_port=65458-65459\r\n" };
}

// --- RtspHeadersTest ----------------------------------------------------------------------------
TEST(RtspHeaders_build_withHeaderLines) {
    const RtspHeaders h = RtspHeaders::Builder().AddAll(kHeaderLines).Build();
    CHECK(h.Get("Accept") == "application/sdp" && h.Get("CSeq") == "3" && h.Get("Content-Length") == "707");
    CHECK(h.Get("Transport") == "RTP/AVP;unicast;client_port=65458-65459");
}

TEST(RtspHeaders_build_withHeaderLinesAsMap) {
    const RtspHeaders h = RtspHeaders::Builder()
                              .AddAll(std::vector<std::pair<std::string, std::string>>{ { "Accept", " application/sdp " }, { "CSeq", "3" }, { "Content-Length", "707" },
                                                                                        { "Transport", "RTP/AVP;unicast;client_port=65458-65459\r\n" } })
                              .Build();
    CHECK(h.Get("Accept") == "application/sdp" && h.Get("CSeq") == "3" && h.Get("Content-Length") == "707");
    CHECK(h.Get("Transport") == "RTP/AVP;unicast;client_port=65458-65459");
}

TEST(RtspHeaders_buildUpon) {
    const RtspHeaders h = RtspHeaders::Builder().Add("Content-Length", "707").Add("Transport", "RTP/AVP;unicast;client_port=65458-65459\r\n").Build();
    CHECK(h.BuildUpon().Build() == h);
    const RtspHeaders one = RtspHeaders::Builder().Add("Content-Length", "707").Build();
    CHECK(one.BuildUpon().Add("Content-Encoding", "utf-8").Build() == RtspHeaders::Builder().Add("Content-Length", "707").Add("Content-Encoding", "utf-8").Build());
}

TEST(RtspHeaders_get_caseInsensitivelyAndTheMostRecentValue) {
    const RtspHeaders h = RtspHeaders::Builder()
                              .AddAll(Lines{ "ACCEPT: application/sdp ", "Cseq:3", "Content-LENGTH: 707", "transport: RTP/AVP;unicast;client_port=65458-65459\r\n" })
                              .Build();
    CHECK(h.Get("Accept") == "application/sdp" && h.Get("CSeq") == "3" && h.Get("Content-Length") == "707");
    CHECK(h.Get("Transport") == "RTP/AVP;unicast;client_port=65458-65459");
    const RtspHeaders auth = RtspHeaders::Builder()
                                 .AddAll(Lines{ R"(WWW-Authenticate: Digest realm="2857be52f47f", nonce="f4cba07ad14b5bf181ac77c5a92ba65f", stale="FALSE")",
                                                R"(WWW-Authenticate: Basic realm="2857be52f47f")" })
                                 .Build();
    CHECK(auth.Get("WWW-Authenticate") == R"(Basic realm="2857be52f47f")");
    CHECK(auth.Values("WWW-Authenticate") == (Lines{ R"(Digest realm="2857be52f47f", nonce="f4cba07ad14b5bf181ac77c5a92ba65f", stale="FALSE")", R"(Basic realm="2857be52f47f")" }));
    CHECK(RtspHeaders::Builder().Build().Values("WWW-Authenticate").empty());
}

TEST(RtspHeaders_asMultiMap_inAdditionOrder) {
    CHECK(RtspHeaders::Builder().AddAll(kHeaderLines).Build().AsMultiMap() ==
          (MultiMap{ { "Accept", { "application/sdp" } }, { "CSeq", { "3" } }, { "Content-Length", { "707" } }, { "Transport", { "RTP/AVP;unicast;client_port=65458-65459" } } }));
    const RtspHeaders h = RtspHeaders::Builder()
                              .AddAll(Lines{ "Accept: application/sdp ", "Accept: application/sip ", "CSeq:3", "CSeq:5", "Transport: RTP/AVP;unicast;client_port=65456-65457",
                                             "Transport: RTP/AVP;unicast;client_port=65458-65459\r\n" })
                              .Build();
    CHECK(h.AsMultiMap() == (MultiMap{ { RtspHeaders::ACCEPT, { "application/sdp", "application/sip" } }, { RtspHeaders::CSEQ, { "3", "5" } },
                                       { RtspHeaders::TRANSPORT, { "RTP/AVP;unicast;client_port=65456-65457", "RTP/AVP;unicast;client_port=65458-65459" } } }));
}

// --- RtspMessageUtilTest ------------------------------------------------------------------------
TEST(RtspMessageUtil_parseRequestsAndResponses) {
    RtspRequest options = RtspMessageUtil::ParseRequest({ "OPTIONS rtsp://localhost:554/foo.bar RTSP/1.0", "CSeq: 2", "User-Agent: LibVLC/3.0.11", "" });
    CHECK(options.method == RtspMethod::Options && options.messageBody.empty());
    CHECK(options.headers.AsMultiMap() == (MultiMap{ { RtspHeaders::CSEQ, { "2" } }, { RtspHeaders::USER_AGENT, { "LibVLC/3.0.11" } } }));

    RtspResponse optionsResponse = RtspMessageUtil::ParseResponse(
        { "RTSP/1.0 200 OK", "CSeq: 2", "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER", "" });
    CHECK(optionsResponse.status == 200 && optionsResponse.messageBody.empty());
    CHECK(optionsResponse.headers.Get(RtspHeaders::PUBLIC) == "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER");

    RtspRequest describe = RtspMessageUtil::ParseRequest({ "DESCRIBE rtsp://localhost:554/foo.bar RTSP/1.0", "CSeq: 3", "User-Agent: LibVLC/3.0.11", "Accept: application/sdp", "" });
    CHECK(describe.method == RtspMethod::Describe && describe.headers.Get(RtspHeaders::ACCEPT) == "application/sdp");

    RtspResponse describeResponse = RtspMessageUtil::ParseResponse({ "RTSP/1.0 200 OK", "CSeq: 3", "Content-Base: rtsp://127.0.0.1/test.mkv/", "Content-Type: application/sdp",
                                                                     "Content-Length: 707", "", "v=0", "o=- 1606776316530225 1 IN IP4 192.168.2.176", "i=imax.mkv",
                                                                     "m=video 0 RTP/AVP 96", "a=rtpmap:96 H264/90000", "a=control:track1", "m=audio 0 RTP/AVP 97",
                                                                     "a=rtpmap:97 AC3/48000", "a=control:track2" });
    CHECK(describeResponse.status == 200);
    CHECK(describeResponse.headers.AsMultiMap() == (MultiMap{ { RtspHeaders::CSEQ, { "3" } }, { RtspHeaders::CONTENT_BASE, { "rtsp://127.0.0.1/test.mkv/" } },
                                                              { RtspHeaders::CONTENT_TYPE, { "application/sdp" } }, { RtspHeaders::CONTENT_LENGTH, { "707" } } }));
    CHECK_EQ(describeResponse.messageBody, std::string("v=0\r\no=- 1606776316530225 1 IN IP4 192.168.2.176\r\ni=imax.mkv\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
                                                       "a=control:track1\r\nm=audio 0 RTP/AVP 97\r\na=rtpmap:97 AC3/48000\r\na=control:track2"));

    RtspResponse unauthorized = RtspMessageUtil::ParseResponse(
        { "RTSP/1.0 401 Unauthorized", "CSeq: 3", R"(WWW-Authenticate: BASIC realm="wow")", R"(WWW-Authenticate: DIGEST realm="wow", nonce="nonce")", "" });
    CHECK(unauthorized.status == 401 && unauthorized.messageBody.empty());
    CHECK(unauthorized.headers.AsMultiMap() ==
          (MultiMap{ { RtspHeaders::CSEQ, { "3" } }, { RtspHeaders::WWW_AUTHENTICATE, { R"(BASIC realm="wow")", R"(DIGEST realm="wow", nonce="nonce")" } } }));

    RtspRequest setParameter = RtspMessageUtil::ParseRequest(
        { "SET_PARAMETER rtsp://localhost:554/foo.bar RTSP/1.0", "CSeq: 3", "User-Agent: LibVLC/3.0.11", "Content-Length: 20", "Content-Type: text/parameters", "", "param: stuff" });
    CHECK(setParameter.method == RtspMethod::SetParameter && setParameter.messageBody == "param: stuff");

    RtspResponse getParameter = RtspMessageUtil::ParseResponse(
        { "RTSP/1.0 200 OK", "CSeq: 431", "Content-Length: 46", "Content-Type: text/parameters", "", "packets_received: 10", "jitter: 0.3838" });
    CHECK(getParameter.messageBody == "packets_received: 10\r\njitter: 0.3838");
}

TEST(RtspMessageUtil_serialize) {
    RtspRequest setup{ "rtsp://127.0.0.1/test.mkv/track1", RtspMethod::Setup,
                       RtspHeaders::Builder().Add(RtspHeaders::CSEQ, "4").Add(RtspHeaders::TRANSPORT, "RTP/AVP;unicast;client_port=65458-65459").Build(), "" };
    const Lines lines = RtspMessageUtil::SerializeRequest(setup);
    CHECK(lines == (Lines{ "SETUP rtsp://127.0.0.1/test.mkv/track1 RTSP/1.0", "CSeq: 4", "Transport: RTP/AVP;unicast;client_port=65458-65459", "", "" }));
    CHECK_EQ(RtspMessageUtil::ConvertMessageToBytes(lines),
             std::string("SETUP rtsp://127.0.0.1/test.mkv/track1 RTSP/1.0\r\nCSeq: 4\r\nTransport: RTP/AVP;unicast;client_port=65458-65459\r\n\r\n"));

    RtspResponse setupResponse{ 200, RtspHeaders::Builder().Add(RtspHeaders::CSEQ, "4").Add(RtspHeaders::TRANSPORT, "RTP/AVP;unicast;client_port=65458-65459;server_port=5354-5355").Build(), "" };
    CHECK(RtspMessageUtil::SerializeResponse(setupResponse) ==
          (Lines{ "RTSP/1.0 200 OK", "CSeq: 4", "Transport: RTP/AVP;unicast;client_port=65458-65459;server_port=5354-5355", "", "" }));

    const std::string body = "v=0\r\no=- 1606776316530225 1 IN IP4 192.168.2.176\r\ni=test.mkv\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\na=control:track1\r\n"
                             "m=audio 0 RTP/AVP 97\r\na=rtpmap:97 AC3/48000\r\na=control:track2";
    RtspResponse describe{ 200,
                           RtspHeaders::Builder()
                               .Add(RtspHeaders::CSEQ, "4")
                               .Add(RtspHeaders::CONTENT_BASE, "rtsp://127.0.0.1/test.mkv/")
                               .Add(RtspHeaders::CONTENT_TYPE, "application/sdp")
                               .Add(RtspHeaders::CONTENT_LENGTH, "707")
                               .Build(),
                           body };
    const Lines describeLines = RtspMessageUtil::SerializeResponse(describe);
    CHECK(describeLines == (Lines{ "RTSP/1.0 200 OK", "CSeq: 4", "Content-Base: rtsp://127.0.0.1/test.mkv/", "Content-Type: application/sdp", "Content-Length: 707", "", body }));
    CHECK_EQ(RtspMessageUtil::ConvertMessageToBytes(describeLines),
             "RTSP/1.0 200 OK\r\nCSeq: 4\r\nContent-Base: rtsp://127.0.0.1/test.mkv/\r\nContent-Type: application/sdp\r\nContent-Length: 707\r\n\r\n" + body);

    CHECK(Throws<std::invalid_argument>([] { RtspMessageUtil::SerializeRequest({ "rtsp://127.0.0.1/test.mkv/track1", RtspMethod::Options, {}, "" }); }));
    CHECK(Throws<std::invalid_argument>([] { RtspMessageUtil::SerializeResponse({ 200, {}, "" }); }));
    RtspResponse failed{ 454, RtspHeaders::Builder().Add(RtspHeaders::CSEQ, "4").Build(), "" };
    CHECK(RtspMessageUtil::SerializeResponse(failed) == (Lines{ "RTSP/1.0 454 Session Not Found", "CSeq: 4", "", "" }));
    CHECK_EQ(RtspMessageUtil::ConvertMessageToBytes(RtspMessageUtil::SerializeResponse(failed)), std::string("RTSP/1.0 454 Session Not Found\r\nCSeq: 4\r\n\r\n"));
}

TEST(RtspMessageUtil_startLinesAndHeaders) {
    CHECK(RtspMessageUtil::IsRtspResponse({ "RTSP/1.0 200 OK", "CSeq: 2", "Public: OPTIONS", "" }));
    CHECK(RtspMessageUtil::IsRtspResponse({ "RTSP/1.0 405 Method Not Allowed", "CSeq: 2", "" }));
    CHECK(!RtspMessageUtil::IsRtspResponse({ "OPTIONS rtsp://localhost:554/foo.bar RTSP/1.0", "CSeq: 2", "" }));
    CHECK(RtspMessageUtil::IsRtspStartLine("OPTIONS rtsp://localhost/test RTSP/1.0"));
    CHECK(RtspMessageUtil::IsRtspStartLine("RTSP/1.0 456 Header Field Not Valid for Resource"));
    CHECK(!RtspMessageUtil::IsRtspStartLine("Transport: RTP/AVP;unicast;client_port=1000-1001"));
    CHECK_EQ(RtspMessageUtil::ParseContentLengthHeader("Content-Length: 1000000000000000"), std::int64_t(1000000000000000));
    const RtspSessionHeader session = RtspMessageUtil::ParseSessionHeader("610a63df-9b57.4856_97ac$665f+56e9c04");
    CHECK(session.sessionId == "610a63df-9b57.4856_97ac$665f+56e9c04" && session.timeoutMs == RtspMessageUtil::kDefaultRtspTimeoutMs);
    CHECK_EQ(RtspMessageUtil::ParseSessionHeader("610a63df-9b57.4856_97ac$665f+56e9c04;timeout=30").timeoutMs, std::int64_t(30'000));
    CHECK(RtspMessageUtil::SplitRtspMessageBody("line1\r\nline2\r\nline3") == (Lines{ "line1", "line2", "line3" }));
    CHECK(RtspMessageUtil::SplitRtspMessageBody("line1\nline2\nline3") == (Lines{ "line1", "line2", "line3" }));
}

TEST(RtspMessageUtil_userInfo) {
    CHECK_EQ(RtspMessageUtil::RemoveUserInfo("rtsp://user:pass@foo.bar/foo.mkv"), std::string("rtsp://foo.bar/foo.mkv"));
    CHECK_EQ(RtspMessageUtil::RemoveUserInfo("rtsp://user:pass@foo.bar:5050/foo.mkv"), std::string("rtsp://foo.bar:5050/foo.mkv"));
    CHECK_EQ(RtspMessageUtil::RemoveUserInfo("rtsp://@foo.bar:5050/foo.mkv"), std::string("rtsp://foo.bar:5050/foo.mkv"));
    CHECK_EQ(RtspMessageUtil::RemoveUserInfo("rtsp://foo.bar:5050/foo.mkv"), std::string("rtsp://foo.bar:5050/foo.mkv"));
    CHECK_EQ(RtspMessageUtil::RemoveUserInfo("rtsp://user%40name:pass@foo.bar/foo.mkv"), std::string("rtsp://foo.bar/foo.mkv"));
    CHECK(!RtspMessageUtil::ParseUserInfo("rtsp://username@mediaserver.com/stream1"));
    CHECK(!RtspMessageUtil::ParseUserInfo("rtsp://mediaserver.com/stream1"));
    const auto user = RtspMessageUtil::ParseUserInfo("rtsp://username:pass:word@mediaserver.com/stream1");
    CHECK(user && user->username == "username" && user->password == "pass:word");
}

TEST(RtspMessageUtil_parseWwwAuthenticateHeader) {
    RtspAuthenticationInfo basic = RtspMessageUtil::ParseWwwAuthenticateHeader(R"(Basic realm="Wally - World")");
    CHECK(basic.authenticationMechanism == RtspAuthenticationInfo::BASIC && basic.nonce.empty() && basic.realm == "Wally - World");
    RtspAuthenticationInfo domain = RtspMessageUtil::ParseWwwAuthenticateHeader(
        R"(Digest realm="test-realm@host.com", domain="host.com", nonce="dcd98b7102dd2f0e8b11d0f600bfb0c093",  opaque="5ccc069c403ebaf9f0171e9517f40e41")");
    CHECK(domain.authenticationMechanism == RtspAuthenticationInfo::DIGEST && domain.nonce == "dcd98b7102dd2f0e8b11d0f600bfb0c093");
    CHECK(domain.realm == "test-realm@host.com" && domain.opaque.empty());
    RtspAuthenticationInfo optional = RtspMessageUtil::ParseWwwAuthenticateHeader(
        R"(Digest realm="testrealm@host.com", nonce="dcd98b7102dd2f0e8b11d0f600bfb0c093", opaque="5ccc069c403ebaf9f0171e9517f40e41", stale="stalev", algorithm="md5")");
    CHECK(optional.realm == "testrealm@host.com" && optional.opaque == "5ccc069c403ebaf9f0171e9517f40e41");
    RtspAuthenticationInfo digest = RtspMessageUtil::ParseWwwAuthenticateHeader(R"(Digest realm="RTSP server", nonce="0cdfe9719e7373b7d5bb2913e2115f3f")");
    CHECK(digest.nonce == "0cdfe9719e7373b7d5bb2913e2115f3f" && digest.realm == "RTSP server" && digest.opaque.empty());
}

// --- RtspAuthenticationInfoTest -----------------------------------------------------------------
TEST(RtspAuthenticationInfo_headerValues) {
    const RtspAuthenticationInfo basic{ RtspAuthenticationInfo::BASIC, "WallyWorld", "", "" };
    CHECK_EQ(basic.GetAuthorizationHeaderValue({ "Aladdin", "open sesame" }, "", RtspMethod::Describe), std::string("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==\n"));
    const RtspAuthenticationInfo digest{ RtspAuthenticationInfo::DIGEST, "RTSP server", "0cdfe9719e7373b7d5bb2913e2115f3f", "5ccc069c403ebaf9f0171e9517f40e41" };
    CHECK_EQ(digest.GetAuthorizationHeaderValue({ "username", "password" }, "rtsp://localhost:554/imax_cd_2k_264_6ch.mkv", RtspMethod::Describe),
             std::string(R"(Digest username="username", realm="RTSP server", nonce="0cdfe9719e7373b7d5bb2913e2115f3f", uri="rtsp://localhost:554/imax_cd_2k_264_6ch.mkv", )"
                         R"(response="cb635712efbdd027f0c823d0623449f6", opaque="5ccc069c403ebaf9f0171e9517f40e41")"));
    CHECK_EQ(detail::Md5Hex(""), std::string("d41d8cd98f00b204e9800998ecf8427e"));
}

// --- RtspSessionTimingTest ----------------------------------------------------------------------
TEST(RtspSessionTiming_parseTiming) {
    for (const char* live : { "npt=now-", "npt=0-", "npt=0.000-" }) {
        const RtspSessionTiming t = RtspSessionTiming::ParseTiming(live);
        CHECK(t.GetDurationMs() == kTimeUnset && t.IsLive());
    }
    for (const char* range : { "npt=0.000-32.054", "npt:0.000-32.054" }) {
        const RtspSessionTiming t = RtspSessionTiming::ParseTiming(range);
        CHECK(t.GetDurationMs() == 32054 && !t.IsLive());
    }
    CHECK(Throws<ParserException>([] { RtspSessionTiming::ParseTiming("npt=10.000-2.054"); }));
    CHECK_EQ(RtspSessionTiming::GetOffsetStartTimeTiming(32054), std::string("npt=32.054-"));
}

// --- RtspTrackTimingTest ------------------------------------------------------------------------
TEST(RtspTrackTiming_parseTiming) {
    auto one = RtspTrackTiming::ParseTrackTiming("url=rtsp://video.example.com/twister/video;seq=12312232;rtptime=78712811", "rtsp://video.example.com");
    CHECK(one.size() == 1 && one[0].uri == "rtsp://video.example.com/twister/video" && one[0].sequenceNumber == 12312232 && one[0].rtpTimestamp == 78712811);
    auto two = RtspTrackTiming::ParseTrackTiming("url=rtsp://foo.com/bar.avi/streamid=0;seq=45102,url=rtsp://foo.com/bar.avi/streamid=1;seq=30211", "rtsp://foo.com");
    CHECK(two.size() == 2 && two[0].uri == "rtsp://foo.com/bar.avi/streamid=0" && two[0].sequenceNumber == 45102 && two[0].rtpTimestamp == kTimeUnset);
    CHECK(two[1].uri == "rtsp://foo.com/bar.avi/streamid=1" && two[1].sequenceNumber == 30211);
    CHECK(Throws<ParserException>([] { RtspTrackTiming::ParseTrackTiming("url=rtsp://video.example.com/twister/video;seq=123abc", "rtsp://video.example.com/twister"); }));
    CHECK(Throws<ParserException>([] { RtspTrackTiming::ParseTrackTiming("url=rtsp://video.example.com/twister/video", "rtsp://video.example.com/twister"); }));
    CHECK(Throws<ParserException>([] { RtspTrackTiming::ParseTrackTiming("seq=35421887", "rtsp://video.example.com/twister"); }));
}

TEST(RtspTrackTiming_resolveUri) {
    CHECK_EQ(RtspTrackTiming::ResolveUri("rtsp://video.example.com/twister/video=1?a2bfc09887ce", "rtsp://video.example.com/twister"),
             std::string("rtsp://video.example.com/twister/video=1?a2bfc09887ce"));
    CHECK_EQ(RtspTrackTiming::ResolveUri("video.example.com/twister/video=1", "rtsp://video.example.com/twister"), std::string("rtsp://video.example.com/twister/video=1"));
    CHECK_EQ(RtspTrackTiming::ResolveUri("video=1", "rtsp://video.example.com/twister"), std::string("rtsp://video.example.com/twister/video=1"));
    CHECK_EQ(RtspTrackTiming::ResolveUri("container/video=1", "rtsp://video.example.com/twister"), std::string("rtsp://video.example.com/twister/container/video=1"));
    CHECK_EQ(RtspTrackTiming::ResolveUri("video=1", "rtsp://127.0.0.1:18888/test"), std::string("rtsp://127.0.0.1:18888/test/video=1"));
    CHECK_EQ(RtspTrackTiming::ResolveUri("video=1", "rtsp://127.0.0.1:18888/test/"), std::string("rtsp://127.0.0.1:18888/test/video=1"));
    CHECK(Throws<std::invalid_argument>([] { RtspTrackTiming::ResolveUri("video=1", "127.0.0.1:18888/test"); }));
}
