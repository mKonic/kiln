// Ported from media3 exoplayer-rtsp's reader tests: RtpH264ReaderTest, RtpH265ReaderTest (and
// below, the audio and other video readers'), with SampleCollector as FakeTrackOutput.
#include "Check.h"

#include "kiln/media/codec/CodecUtil.h"
#include "kiln/media/rtsp/RtpReaders.h"

#include <initializer_list>

using namespace kiln::media;
using namespace kiln::media::rtsp;
using Bytes = std::vector<std::uint8_t>;

namespace {
    Bytes Hex(const char* hex) { return codec::BytesFromHexString(hex); }
    Bytes Cat(std::initializer_list<Bytes> parts) {
        Bytes out;
        for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
        return out;
    }
    const Bytes kStartCode = { 0, 0, 0, 1 };
    struct Packet {
        std::int64_t timestamp = 0;
        int sequenceNumber = 0;
        bool marker = false;
        Bytes payload;
    };
    void Consume(RtpPayloadReader& reader, const Packet& p) {
        codec::ParsableByteArray data(p.payload);
        reader.Consume(data, p.timestamp, p.sequenceNumber, p.marker);
    }
    template <typename F> bool ThrowsParser(F&& f) {
        try {
            f();
        } catch (const stream::ParserException&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }
    std::int64_t Scaled(std::int64_t delta, int frequency) { return stream::ScaleLargeTimestamp(delta, 1'000'000, frequency); }
    RtpPayloadFormat VideoFormat(const char* mime, int payloadType, const char* encoding, std::map<std::string, std::string> fmtp, int width = 1920, int height = 1080) {
        Format f;
        f.sampleMimeType = mime;
        f.width = width;
        f.height = height;
        return { f, payloadType, 90'000, std::move(fmtp), encoding };
    }
}

// --- RtpH264ReaderTest ----------------------------------------------------------------------------
namespace {
    constexpr int kSeq = 12345;
    const Bytes kH264Nalu1 = Cat({ Hex("41"), Hex("0102030405060708090a") });
    const Bytes kH264Nalu2 = Cat({ Hex("65"), Hex("1112131415161718191a1b1c") });
    const RtpPayloadFormat kH264Format = VideoFormat(stream::MimeTypes::VIDEO_H264, 96, RtpPayloadFormat::RTP_MEDIA_H264, { { "packetization-mode", "1" } });
    Packet H264Fu(int seq, std::int64_t ts, bool marker, const Bytes& fuHeader, const Bytes& payload) { return { ts, seq, marker, Cat({ Hex("7c"), fuHeader, payload }) }; }
    struct H264 {
        SampleCollector out;
        std::unique_ptr<RtpPayloadReader> reader = RtpPayloadReader::Create(kH264Format);
        H264() { reader->CreateTracks(out); }
    };
}

TEST(RtpH264Reader_unsupportedModeAndStapA) {
    H264 t;
    CHECK(ThrowsParser([&] { Consume(*t.reader, { 9000, kSeq, false, { 0x1F } }); }));
    H264 s;
    const Packet stapA{ 9'000'000, kSeq, true, Cat({ Hex("78"), Hex("000b"), kH264Nalu1, Hex("000d"), kH264Nalu2 }) };
    s.reader->OnReceivingFirstPacket(stapA.timestamp, stapA.sequenceNumber);
    Consume(*s.reader, stapA);
    CHECK(s.out.samples.size() == 1 && s.out.samples[0].data == Cat({ kStartCode, kH264Nalu1, kStartCode, kH264Nalu2 }) && s.out.samples[0].data.size() == 32);
    CHECK(s.out.formats.size() == 1 && s.out.formats[0] == kH264Format.format);
}

TEST(RtpH264Reader_accessUnits) {
    {   // An FU continuation at an access unit's start: the whole access unit goes.
        H264 t;
        Consume(*t.reader, H264Fu(kSeq, 9'000'000, false, Hex("05"), Hex("010203")));
        Consume(*t.reader, { 9'000'000, kSeq + 1, true, kH264Nalu1 });
        CHECK(t.out.samples.empty());
    }
    {   // Two single NAL units in one access unit: one sample of both.
        H264 t;
        Consume(*t.reader, { 9'000'000, kSeq, false, kH264Nalu1 });
        Consume(*t.reader, { 9'000'000, kSeq + 1, true, kH264Nalu2 });
        CHECK(t.out.samples.size() == 1 && t.out.samples[0].data.size() == kH264Nalu1.size() + kH264Nalu2.size() + 8);
    }
    {   // An FU interrupted by another FU start: discarded.
        H264 t;
        Consume(*t.reader, H264Fu(100, 9'000'000, false, Hex("85"), Hex("0102")));
        Consume(*t.reader, H264Fu(101, 9'000'000, true, Hex("85"), Hex("0304")));
        CHECK(t.out.samples.empty());
    }
    {   // A new access unit resets the state: sizes do not accumulate.
        H264 t;
        Consume(*t.reader, { 9000, kSeq, false, kH264Nalu1 });
        Consume(*t.reader, { 9000, kSeq + 1, true, kH264Nalu1 });
        Consume(*t.reader, H264Fu(kSeq + 3, 18000, false, Hex("05"), Hex("010203")));
        Consume(*t.reader, { 27000, kSeq + 4, true, kH264Nalu1 });
        CHECK(t.out.samples.size() == 2 && t.out.samples[1].data.size() == kH264Nalu1.size() + 4);
    }
    {   // seek: a clean state, times from the seek position.
        H264 t;
        Consume(*t.reader, H264Fu(kSeq, 9000, false, Hex("05"), Hex("010203")));
        t.reader->Seek(18000, 1'000'000);
        Consume(*t.reader, { 18000, kSeq + 1, true, kH264Nalu1 });
        CHECK(t.out.samples.size() == 1 && t.out.samples[0].data.size() == kH264Nalu1.size() + 4 && t.out.samples[0].timeUs == 1'000'000);
    }
}

// --- RtpH265ReaderTest ----------------------------------------------------------------------------
namespace {
    const Bytes kH265Nalu1 = Cat({ Hex("4001"), Hex("0102030405060708090a") });
    const Bytes kH265Nalu2 = Cat({ Hex("4201"), Hex("1112131415161718191a1b1c") });
    const Bytes kH265Nalu1Delimited = Cat({ kStartCode, kH265Nalu1 });
    const Bytes kH265Nalu2Delimited = Cat({ kStartCode, kH265Nalu2 });
    const Bytes kH265Both = Cat({ kH265Nalu1Delimited, kH265Nalu2Delimited });
    const RtpPayloadFormat kH265Format = VideoFormat(stream::MimeTypes::VIDEO_H265, 98, RtpPayloadFormat::RTP_MEDIA_H265,
                                                     { { "packetization-mode", "1" }, { "profile-level-id", "010101" }, { "sprop-pps", "RAHA4MisDBRSQA==" },
                                                       { "sprop-sps", "QgEBAUAAAAMAgAAAAwAAAwC0oAPAgBDlja5JG2a5cQB/FiU=" }, { "sprop-vps", "QAEMAf//AUAAAAMAgAAAAwAAAwC0rAk=" } });
    constexpr std::int64_t kApTs = 9'000'000, kAp2Ts = 9'000'040, kSingle1Ts = 9'000'040, kSingle2Ts = 9'000'080, kFuTs = 9'000'000;
    constexpr int kFuSeq = 12342;
    Packet H265Ap(int seq, std::int64_t ts, std::initializer_list<Bytes> parts) {
        Bytes payload = Hex("6001");
        for (const auto& p : parts) payload.insert(payload.end(), p.begin(), p.end());
        return { ts, seq, true, payload };
    }
    Packet H265Fu(int seq, std::int64_t ts, bool marker, const Bytes& fuHeader, const Bytes& payload) { return { ts, seq, marker, Cat({ Hex("6201"), fuHeader, payload }) }; }
    const Packet kSingle1{ kSingle1Ts, kSeq + 1, true, kH265Nalu1 };
    const Packet kSingle2{ kSingle2Ts, kSeq + 2, true, kH265Nalu2 };
    const Packet kAp = H265Ap(kSeq, kApTs, { Hex("000c"), kH265Nalu1, Hex("000e"), kH265Nalu2 });
    const Packet kAp2 = H265Ap(kSeq + 1, kAp2Ts, { Hex("000c"), kH265Nalu1, Hex("000e"), kH265Nalu2 });
    const Packet kFu1 = H265Fu(kFuSeq, kFuTs, false, Hex("a0"), Hex("010203"));
    const Packet kFu2 = H265Fu(kFuSeq + 1, kFuTs, false, Hex("20"), Hex("040506"));
    const Packet kFu3 = H265Fu(kFuSeq + 2, kFuTs, true, Hex("60"), Hex("0708090a"));
    const Packet kFu2BadSequence = H265Fu(kFuSeq + 3, kFuTs, false, Hex("20"), Hex("040506"));
    struct H265 {
        SampleCollector out;
        std::unique_ptr<RtpPayloadReader> reader = RtpPayloadReader::Create(kH265Format);
        H265() { reader->CreateTracks(out); }
    };
}

TEST(RtpH265Reader_aggregationPackets) {
    H265 bad;
    CHECK(ThrowsParser([&] { Consume(*bad.reader, { 9000, kSeq, false, { 0x7E, 0x01 } }); }));
    H265 t;
    t.reader->OnReceivingFirstPacket(kAp.timestamp, kAp.sequenceNumber);
    Consume(*t.reader, kAp);
    Consume(*t.reader, kAp2);
    CHECK(t.out.samples.size() == 2 && t.out.samples[0].data == kH265Both && t.out.samples[0].timeUs == 0);
    CHECK(t.out.samples[1].data == kH265Both && t.out.samples[1].timeUs == Scaled(kAp2Ts - kApTs, 90'000));
    H265 mixed;
    Consume(*mixed.reader, kAp);
    Consume(*mixed.reader, kSingle1);
    Consume(*mixed.reader, kSingle2);
    CHECK(mixed.out.samples.size() == 3 && mixed.out.samples[1].data == kH265Nalu1Delimited && mixed.out.samples[1].timeUs == Scaled(kSingle1Ts - kApTs, 90'000));
    CHECK(mixed.out.samples[2].data == kH265Nalu2Delimited && mixed.out.samples[2].timeUs == Scaled(kSingle2Ts - kApTs, 90'000));
    const Bytes nalu2Short(kH265Nalu2.begin(), kH265Nalu2.end() - 1);
    for (const Packet& p : { H265Ap(kSeq, kApTs, { Hex("000c"), kH265Nalu1, Hex("000e"), kH265Nalu2, { 0x0a } }),   // an extra byte
                             H265Ap(kSeq, kApTs, { Hex("000c"), kH265Nalu1, Hex("000e"), nalu2Short }),            // a missing byte
                             H265Ap(kSeq, kApTs, { Hex("00ff"), kH265Nalu1, Hex("000e"), Hex("4201") }),           // a bad length
                             H265Ap(kSeq, kApTs, { Hex("000c"), kH265Nalu1 }) }) {                                // one NAL unit
        H265 e;
        e.reader->OnReceivingFirstPacket(p.timestamp, p.sequenceNumber);
        CHECK(ThrowsParser([&] { Consume(*e.reader, p); }));
    }
}

TEST(RtpH265Reader_fragmentationUnits) {
    H265 t;
    Consume(*t.reader, kFu1);
    Consume(*t.reader, kFu2);
    Consume(*t.reader, kFu3);
    Consume(*t.reader, kSingle1);
    Consume(*t.reader, kSingle2);
    CHECK(t.out.samples.size() == 3 && t.out.samples[0].data == kH265Nalu1Delimited && t.out.samples[0].timeUs == 0);   // reassembled
    CHECK(t.out.samples[1].data == kH265Nalu1Delimited && t.out.samples[1].timeUs == Scaled(kSingle1Ts - kFuTs, 90'000));
    CHECK(t.out.samples[2].data == kH265Nalu2Delimited && t.out.samples[2].timeUs == Scaled(kSingle2Ts - kFuTs, 90'000));
    for (bool missingEnd : { false, true }) {   // a packet out of sequence, or no end packet
        H265 e;
        e.reader->OnReceivingFirstPacket(kFu1.timestamp, kFu1.sequenceNumber);
        Consume(*e.reader, kFu1);
        Consume(*e.reader, missingEnd ? kFu2 : kFu2BadSequence);
        if (!missingEnd) Consume(*e.reader, kFu3);
        Consume(*e.reader, kSingle1);
        Consume(*e.reader, kSingle2);
        CHECK(e.out.samples.size() == 2 && e.out.samples[0].data == kH265Nalu1Delimited && e.out.samples[0].timeUs == Scaled(kSingle1Ts - kFuTs, 90'000));
        CHECK(e.out.samples[1].data == kH265Nalu2Delimited && e.out.samples[1].timeUs == Scaled(kSingle2Ts - kFuTs, 90'000));
    }
}

TEST(RtpH265Reader_accessUnits) {
    {
        H265 t;
        Consume(*t.reader, { 9'000'000, kSeq, false, kH265Nalu1 });
        Consume(*t.reader, { 9'000'000, kSeq + 1, true, kH265Nalu2 });
        CHECK(t.out.samples.size() == 1 && t.out.samples[0].data == kH265Both);
    }
    {   // An FU then a single NAL unit in one access unit.
        H265 t;
        Consume(*t.reader, H265Fu(100, 9'000'000, false, Hex("a0"), Hex("010203")));
        Consume(*t.reader, H265Fu(101, 9'000'000, false, Hex("20"), Hex("040506")));
        Consume(*t.reader, H265Fu(102, 9'000'000, false, Hex("60"), Hex("0708090a")));
        Consume(*t.reader, { 9'000'000, 103, true, kH265Nalu1 });
        CHECK(t.out.samples.size() == 1 && t.out.samples[0].data.size() == 2 * kH265Nalu1Delimited.size());
    }
    {   // An unfinished FU then a single NAL unit in the same access unit: discarded.
        H265 t;
        Consume(*t.reader, H265Fu(kSeq, 9'000'000, false, Hex("a0"), Hex("010203")));
        Consume(*t.reader, { 9'000'000, kSeq + 1, true, kH265Nalu1 });
        CHECK(t.out.samples.empty());
    }
    {   // An FU continuation at an access unit's start.
        H265 t;
        Consume(*t.reader, H265Fu(kSeq, 9'000'000, false, Hex("20"), Hex("040506")));
        Consume(*t.reader, { 9'000'000, kSeq + 1, true, kH265Nalu1 });
        CHECK(t.out.samples.empty());
    }
    {   // Timestamps roll over.
        H265 t;
        Consume(*t.reader, H265Fu(100, 0xFFFFFFFFLL, true, Hex("a0"), Hex("010203")));
        Consume(*t.reader, H265Fu(101, 0, true, Hex("a0"), Hex("010203")));
        CHECK(t.out.samples.size() == 2 && t.out.samples[1].timeUs > t.out.samples[0].timeUs);
    }
    {
        H265 t;
        Consume(*t.reader, H265Fu(kSeq, 9000, false, Hex("20"), Hex("040506")));
        t.reader->Seek(18000, 1'000'000);
        Consume(*t.reader, { 18000, kSeq + 1, true, kH265Nalu1 });
        CHECK(t.out.samples.size() == 1 && t.out.samples[0].data.size() == kH265Nalu1Delimited.size() && t.out.samples[0].timeUs == 1'000'000);
    }
}

// --- RtpReaderUtilsTest, and the audio readers' tests ----------------------------------------------
TEST(RtpReaderUtils_toSampleTimeUs_withWrappedTimestamp) {
    CHECK_EQ(ToSampleTimeUs(0, 9'000'040LL, 4'285'967'336LL, 90'000), std::int64_t(200'000'000));
}

namespace {
    RtpPayloadFormat AudioFormat(const char* mime, int channels, int rate, const char* encoding, int pcmEncoding = -1, std::map<std::string, std::string> fmtp = {}) {
        Format f;
        f.sampleMimeType = mime;
        f.channelCount = channels;
        f.sampleRate = rate;
        f.pcmEncoding = pcmEncoding;
        return { f, 97, rate, std::move(fmtp), encoding };
    }
    struct Reader {
        SampleCollector out;
        std::unique_ptr<RtpPayloadReader> reader;
        explicit Reader(const RtpPayloadFormat& f) : reader(RtpPayloadReader::Create(f)) { reader->CreateTracks(out); }
        void Feed(std::initializer_list<Packet> packets) {
            for (const Packet& p : packets) Consume(*reader, p);
        }
        bool Sample(std::size_t i, const Bytes& data, std::int64_t timeUs) const {
            return i < out.samples.size() && out.samples[i].data == data && out.samples[i].timeUs == timeUs;
        }
    };
    template <typename F> bool ThrowsInvalidArgument(F&& f) {
        try {
            f();
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }
}

TEST(RtpAc3Reader_fragments) {
    const RtpPayloadFormat ac3 = AudioFormat(stream::MimeTypes::AUDIO_AC3, 6, 48'000, RtpPayloadFormat::RTP_MEDIA_AC3);
    const Packet f1a{ 2599168056LL, 40289, false, Hex("02020102") }, f1b{ 2599168056LL, 40290, true, Hex("03020304") };
    const Packet f2a{ 2599169592LL, 40292, false, Hex("02020506") }, f2b{ 2599169592LL, 40293, true, Hex("03020708") };
    Reader all(ac3);
    all.reader->OnReceivingFirstPacket(f1a.timestamp, f1a.sequenceNumber);
    all.Feed({ f1a, f1b, f2a, f2b });
    CHECK(all.out.samples.size() == 2 && all.Sample(0, Hex("01020304"), 0) && all.Sample(1, Hex("05060708"), 32000));
    Reader noFirst(ac3);
    noFirst.reader->OnReceivingFirstPacket(f1a.timestamp, f1a.sequenceNumber);
    noFirst.Feed({ f1b, f2a, f2b });
    CHECK(noFirst.out.samples.size() == 2 && noFirst.Sample(0, Hex("0304"), 0) && noFirst.Sample(1, Hex("05060708"), 32000));
    Reader noBoundary(ac3);
    noBoundary.reader->OnReceivingFirstPacket(f1a.timestamp, f1a.sequenceNumber);
    noBoundary.Feed({ f1a, f2a, f2b });
    CHECK(noBoundary.out.samples.size() == 2 && noBoundary.Sample(0, Hex("0102"), 0) && noBoundary.Sample(1, Hex("05060708"), 32000));
}

TEST(RtpAmrReader_narrowAndWideBand) {
    const RtpPayloadFormat nb = AudioFormat(kMimeAudioAmrNb, 1, 8000, RtpPayloadFormat::RTP_MEDIA_AMR);
    const RtpPayloadFormat wb = AudioFormat(kMimeAudioAmrWb, 1, 16000, RtpPayloadFormat::RTP_MEDIA_AMR_WB);
    Reader n(nb);
    n.reader->OnReceivingFirstPacket(2599168056LL, 40289);
    n.Feed({ { 2599168056LL, 40289, false, Hex("00400102030405") }, { 2599169592LL, 40290, false, Hex("60C0060708090A") } });
    CHECK(n.out.samples.size() == 2 && n.Sample(0, Hex("400102030405"), 0) && n.Sample(1, Hex("C0060708090A"), 192000));
    for (const char* bad : { "00680102030405", "004001020304" }) {   // a bad frame type, a bad frame size
        Reader e(nb);
        e.reader->OnReceivingFirstPacket(2599168056LL, 40289);
        CHECK(ThrowsInvalidArgument([&] { e.Feed({ { 2599168056LL, 40289, false, Hex(bad) } }); }));
    }
    Reader w(wb);
    w.reader->OnReceivingFirstPacket(2599168056LL, 40289);
    w.Feed({ { 2599168056LL, 40289, false, Hex("60870102030405060708090A0B0C0D0E0F1011") }, { 2599169592LL, 40290, false, Hex("007E") } });
    CHECK(w.out.samples.size() == 2 && w.Sample(0, Hex("870102030405060708090A0B0C0D0E0F1011"), 0) && w.Sample(1, Hex("7E"), 96000));
    for (const char* bad : { "00D501020304", "00A50102" }) {
        Reader e(wb);
        e.reader->OnReceivingFirstPacket(2599168056LL, 40289);
        CHECK(ThrowsInvalidArgument([&] { e.Feed({ { 2599168056LL, 40289, false, Hex(bad) } }); }));
    }
}

TEST(RtpPcmReader_packets) {
    const Bytes frame1 = { 1, 2, 3, 4 }, frame2 = { 5, 6, 7, 8 };
    const Packet p1{ 2599168056LL, 40289, false, frame1 }, p2{ 2599169592LL, 40290, false, frame2 };
    struct Case { RtpPayloadFormat format; std::int64_t secondUs; };
    for (const Case& c : { Case{ AudioFormat(kMimeAudioRaw, 2, 48'000, RtpPayloadFormat::RTP_MEDIA_PCM_L8, kEncodingPcm8Bit), 32000 },
                           Case{ AudioFormat(kMimeAudioRaw, 1, 60'000, RtpPayloadFormat::RTP_MEDIA_PCM_L16, kEncodingPcm16BitBigEndian), 25600 },
                           Case{ AudioFormat(kMimeAudioAlaw, 2, 16'000, RtpPayloadFormat::RTP_MEDIA_PCMA), 96000 },
                           Case{ AudioFormat(kMimeAudioMlaw, 2, 24'000, RtpPayloadFormat::RTP_MEDIA_PCMU), 64000 } }) {
        Reader r(c.format);
        r.reader->OnReceivingFirstPacket(p1.timestamp, p1.sequenceNumber);
        r.Feed({ p1, p2 });
        CHECK(r.out.samples.size() == 2 && r.Sample(0, frame1, 0) && r.Sample(1, frame2, c.secondUs));
    }
}

TEST(RtpMp4aReader_fragments) {
    const RtpPayloadFormat latm = AudioFormat(stream::MimeTypes::AUDIO_AAC, 1, 44'100, RtpPayloadFormat::RTP_MEDIA_MPEG4_LATM_AUDIO);
    const Packet f1a{ 2599168056LL, 40289, false, Hex("020102") }, f1b{ 2599168056LL, 40290, true, Hex("03030405") };
    const Packet f2a{ 2599168344LL, 40291, false, Hex("020607") }, f2b{ 2599168344LL, 40292, true, Hex("020809") };
    Reader all(latm);
    all.reader->OnReceivingFirstPacket(f1a.timestamp, f1a.sequenceNumber);
    all.Feed({ f1a, f1b, f2a, f2b });
    CHECK(all.out.samples.size() == 2 && all.Sample(0, Hex("0102030405"), 0) && all.Sample(1, Hex("06070809"), 6530));
    Reader noFirst(latm);
    noFirst.reader->OnReceivingFirstPacket(f1a.timestamp, f1a.sequenceNumber);
    noFirst.Feed({ f1b, f2a, f2b });
    CHECK(noFirst.out.samples.size() == 2 && noFirst.Sample(0, Hex("030405"), 0) && noFirst.Sample(1, Hex("06070809"), 6530));
    Reader noBoundary(latm);
    noBoundary.reader->OnReceivingFirstPacket(f1a.timestamp, f1a.sequenceNumber);
    noBoundary.Feed({ f1a, f2a, f2b });
    CHECK(noBoundary.out.samples.size() == 2 && noBoundary.Sample(0, Hex("0102"), 0) && noBoundary.Sample(1, Hex("06070809"), 6530));
}

TEST(RtpOpusReader_headersAndFrames) {
    const RtpPayloadFormat opus = AudioFormat(stream::MimeTypes::AUDIO_OPUS, 6, 48'000, RtpPayloadFormat::RTP_MEDIA_OPUS);
    const Packet head{ 2599168056LL, 40289, false, Hex("4F707573486561640102000000000000000000") };
    const Packet tags{ 2599168056LL, 40290, false, Hex("4F707573546167730000000000000000000000") };
    const Packet frame1{ 2599168056LL, 40292, false, Hex("010203") }, frame2{ 2599169592LL, 40293, false, Hex("04050607") };
    Reader r(opus);
    r.reader->OnReceivingFirstPacket(head.timestamp, head.sequenceNumber);
    r.Feed({ head, tags, frame1, frame2 });
    CHECK(r.out.samples.size() == 2 && r.Sample(0, Hex("010203"), 0) && r.Sample(1, Hex("04050607"), 32000));
    CHECK(r.out.formats.size() == 2 && r.out.formats[1].initializationData.size() == 3 && r.out.formats[1].initializationData[0] == head.payload);
    auto throws = [&](std::initializer_list<Packet> packets) {
        Reader e(opus);
        e.reader->OnReceivingFirstPacket(head.timestamp, head.sequenceNumber);
        return ThrowsInvalidArgument([&] { e.Feed(packets); });
    };
    CHECK(throws({ { 2599168056LL, 40289, false, Hex("4F727573486561640102000000000000000000") } }));   // not OpusHead
    CHECK(throws({ { 2599168056LL, 40289, false, Hex("4F707573486561640102") } }));                     // too short
    CHECK(throws({ { 2599168056LL, 40289, false, Hex("4f707573486561640202000000000000000000") } }));   // version 2
    CHECK(throws({ head, { 2599168056LL, 40290, false, Hex("4F70757354676773") } }));                   // not OpusTags
    CHECK(throws({ head, frame1 }));
    CHECK(throws({ tags }));
    CHECK(throws({ frame1 }));
}

// --- RtpVp8ReaderTest, RtpVp9ReaderTest, RtpH263ReaderTest -----------------------------------------
namespace {
    const Bytes kPartition1 = Hex("000102030405060708090A0B0C0D0E"), kPartition2 = Hex("0D0C0B0A09080706050403020100");
    const Bytes kP1F1(kPartition1.begin(), kPartition1.begin() + 11), kP1F2(kPartition1.begin() + 11, kPartition1.end());
    const Bytes kP2F1(kPartition2.begin(), kPartition2.begin() + 10), kP2F2(kPartition2.begin() + 10, kPartition2.end());
    constexpr std::int64_t kP1Ts = 2599168056LL, kP2Ts = 2599168344LL;
    // The four VPx packets in order, with their descriptors.
    struct Vpx {
        Packet p1f1, p1f2, p2f1, p2f2;
    };
    void CheckVpx(const RtpPayloadFormat& format, const Vpx& v) {
        const std::int64_t p2Us = Scaled(kP2Ts - kP1Ts, 90'000);
        Reader all(format);
        all.reader->OnReceivingFirstPacket(kP1Ts, 40289);
        all.Feed({ v.p1f1, v.p1f2, v.p2f1, v.p2f2 });
        CHECK(all.out.samples.size() == 2 && all.Sample(0, kPartition1, 0) && all.Sample(1, kPartition2, p2Us));
        Reader noFirst(format);
        noFirst.reader->OnReceivingFirstPacket(kP1Ts, 40289);
        noFirst.Feed({ v.p1f2, v.p2f1, v.p2f2 });
        CHECK(noFirst.out.samples.size() == 1 && noFirst.Sample(0, kPartition2, p2Us));
        Reader noBoundary(format);
        noBoundary.reader->OnReceivingFirstPacket(kP1Ts, 40289);
        noBoundary.Feed({ v.p1f1, v.p2f1, v.p2f2 });
        CHECK(noBoundary.out.samples.size() == 2 && noBoundary.Sample(0, kP1F1, 0) && noBoundary.Sample(1, kPartition2, p2Us));
        Reader outOfOrder(format);
        outOfOrder.reader->OnReceivingFirstPacket(kP1Ts, 40289);
        outOfOrder.Feed({ v.p1f1, v.p2f1, v.p1f2, v.p2f2 });
        CHECK(outOfOrder.out.samples.size() == 2 && outOfOrder.Sample(0, kP1F1, 0) && outOfOrder.Sample(1, kPartition2, p2Us));
    }
}

TEST(RtpVp8Reader_partitions) {
    const RtpPayloadFormat vp8 = VideoFormat(stream::MimeTypes::VIDEO_VP8, 96, RtpPayloadFormat::RTP_MEDIA_VP8, {}, -1, -1);
    CheckVpx(vp8, { { kP1Ts, 40289, false, Cat({ Hex("10"), kP1F1 }) }, { kP1Ts, 40290, true, Cat({ Hex("00"), kP1F2 }) },
                    { kP2Ts, 40291, false, Cat({ Hex("10"), kP2F1 }) }, { kP2Ts, 40292, true, Cat({ Hex("80"), Hex("D6AA953961"), kP2F2 }) } });
}

TEST(RtpVp9Reader_partitions) {
    const RtpPayloadFormat vp9 = VideoFormat(stream::MimeTypes::VIDEO_VP9, 96, RtpPayloadFormat::RTP_MEDIA_VP9, {}, -1, -1);
    CheckVpx(vp9, { { kP1Ts, 40289, false, Cat({ Hex("08"), kP1F1 }) }, { kP1Ts, 40290, true, Cat({ Hex("00"), kP1F2 }) },
                    { kP2Ts, 40291, false, Cat({ Hex("08"), kP2F1 }) }, { kP2Ts, 40292, true, Cat({ Hex("00"), kP2F2 }) } });
}

TEST(RtpH263Reader_frames) {
    const RtpPayloadFormat h263 = VideoFormat(kMimeVideoH263, 96, RtpPayloadFormat::RTP_MEDIA_H263_1998, {}, 352, 288);
    const Bytes f1a = Hex("80020c0419b7b7d9591f03023e0c37"), f1b = Hex("03140e0e77d5e83021a0c3");
    const Bytes f2a = Hex("800a0e023fffffffffffffffff"), f2b = Hex("830df80c501839dfccdbdbecac");
    const Packet p1a{ kP1Ts, 40289, false, Cat({ Hex("0400"), f1a }) }, p1b{ kP1Ts, 40290, true, Cat({ Hex("0000"), f1b }) };
    const Packet p2a{ kP2Ts, 40291, false, Cat({ Hex("0400"), f2a }) }, p2b{ kP2Ts, 40292, true, Cat({ Hex("0000"), f2b }) };
    const Bytes frame1 = Cat({ Hex("0000"), f1a, f1b }), frame2 = Cat({ Hex("0000"), f2a, f2b });
    const std::int64_t p2Us = Scaled(kP2Ts - kP1Ts, 90'000);
    Reader all(h263);
    all.reader->OnReceivingFirstPacket(kP1Ts, 40289);
    all.Feed({ p1a, p1b, p2a, p2b });
    CHECK(all.out.samples.size() == 2 && all.Sample(0, frame1, 0) && all.Sample(1, frame2, p2Us));
    Reader noFirst(h263);
    noFirst.reader->OnReceivingFirstPacket(kP1Ts, 40289);
    noFirst.Feed({ p1b, p2a, p2b });
    CHECK(noFirst.out.samples.size() == 1 && noFirst.Sample(0, frame2, p2Us));
    for (bool outOfOrder : { false, true }) {
        Reader r(h263);
        r.reader->OnReceivingFirstPacket(kP1Ts, 40289);
        if (outOfOrder) r.Feed({ p1a, p2a, p1b, p2b });
        else r.Feed({ p1a, p2a, p2b });
        CHECK(r.out.samples.size() == 2 && r.Sample(0, Cat({ Hex("0000"), f1a }), 0) && r.Sample(1, frame2, p2Us));
    }
}
