// Ported from media3 exoplayer-rtsp: RtpPacketTest, RtpPacketReorderingQueueTest.
#include "Check.h"

#include "kiln/media/codec/CodecUtil.h"
#include "kiln/media/rtsp/Rtp.h"

using namespace kiln::media;
using namespace kiln::media::rtsp;
using Bytes = std::vector<std::uint8_t>;

namespace {
    const Bytes kRtpData = codec::BytesFromHexString("80e0568f0349c5c0d76ef1a6019fb174427f00006c10c4008962e33ceb5f1fde8ee2d0d9b169651024c83b24c3a0f274ea327e2440ae0d3e2ed194beaa2c91edaa5d1e1df7ce30d1ca3726804d2db37765cf3d174338459623bc627c15c687045390a8d702f623a8dbe49e5c7896dbd7105daecb02ce30c0eee324c0c21ed820a0e67344c7a6e10859");
    const Bytes kRtpDataWithLargeTimestamp = codec::BytesFromHexString("80e07232dbdce1faf5fe62a4419a246c43bffea99600000300000300000300000300000300000300000300000300000300000300000300000300000300000300000300000300000300002ce0");
    const Bytes kRtpDataWithHeaderExtension = codec::BytesFromHexString(
        "9060f04a7735940035ff277300a20003a94f000062150100cbca01007c85b841bc439048000834f1a6943c00040bf038ee4de07acb6dc67cb44716d9b61800600f1041214b121de2af09a8063ff2d88fedf7f565eafb9c44412a8e5a247d0ac76a6a8566a6ff593f9711114b6c625ca1363950ae8524a37c75c509a806833fd4bbeb6dda6db697aef12d709a80910e522bb3e2e793eb3c37995c4429448f2ba8b16bcb825ca11c3dffb3ff50ba8c5a3e5ffaff978b7e1350037d7dce4ddc906dbfff50ba8069ace7a9df442fffde2afc26a004b076dd611ebedb8bf15fd9596fc47e03e1008a32013d454401f8590ea42b6a67a3cf4da90aa006aca053283cf09c0e42c000444519045f3a0002c4c0e5e0f81e8316c5b16cbf3737c462bd5f87cc66b3e508a10128ac18d5656c78a6e293f10e0252c1819c2040fc5b16fe222296c4da247284ae892a16de65db7236a9bab718da108d05f09bf85d22bebb3ff11ff178b8da7c52c52fc4428b07ae1f1a9e0e3cc963136d542b2d698b6e84d572fbfbffcf1eafaf5af5e3d4092e33443b7b7ff09a8008ae5d661597bdbcfeb54dbd944c711014a2161cd76dc63b5087d087befe7ffb97e5484d5025f4e5fda36ffdcb500975c917ac1357fffea6484d401565b4a77bba2ff34135b711004583df39c7a16669f2840edca371fec8f575fff5f8f5049f3b1fff7d709a8338aaffeff1ad4981350cb5bffbfef09a87f043fff5f426a1132f7f5ffd09a821786b3fff7d426a057b80dfff6fe84d430c37fe9a7baed09a800769f54dd97df7dbbd3dc9e23e23158888250743e4da9d997974501d0879f2fe581b62b3f7711825831ca84b9421d3e4fa4d0874bc9ef895b771190dac47e55003a076252c032ccac0d4583c67078e3f101a946e260152754c800402a134002756425e7bb77f97e2305b1094e1c2c067a62284172a506a260011df686ed758e28c461702e001778034a407a1b16cd4ff11105bb0003784c552d810f6e5c5ce79e03e84381fd1008d9e471b8bb0cb2e20c70a34a8ab3fff1100a945d03f257e3cc499e2be9ae9072000f94896b5ca506e1c577fb28fc442b2503e0bce41bc1abacb5e05d6320455c3f3505681ec1c1a888a3240aab928b10001004f2c10782ac03e640ba8691007cc559a827a3cbfe221e600288540a914651e4be0e87c007b983f8cc550080038df00d6c8001818aa92448153c01ef80fc3b362084bf97f111059618ed4580e84505d02d6242f5d8c8e03f9e0705036e1400025385840c64c3a16da22241291e7e6fc9f1111ef1e738583a11ef2b553f1e62380ce79c845ac2901008910430ea3c00040078592b101bb030b2e33d97f111ac5000811454c01a83a00f138d0c0022d06cc09476727596154030742a4141bc3415c9264021789002af000f9e1b8bfcbf88852be0e4e2381400a8d266a1e00e1a9f10f02c1610c6780f29f05440792d0210684000a79f0625601b88c16e958f5eb6583012a944252f8ca00ba920f9fc7ad1500025061e018ca88000b8d7bfc132625ae297f8885641a000100c052c006be1d8000801faf80014b19614e521f0e87c9f21b000170d4c410c0372c007a4320d40600315906e156fda684435100f47855e2c05c73a5985d4009135a0e927d09e1dead7fffb752c63af06890b07fb9531297e0d0af0e03b1fb7c9d3f20a02fb1d7bca101c2c05064c0b78b2cb0ff0ba800dec69a27563d71bbb570bb95510da8fad5695a1814378ab8b7e135001be5e47055c6ba97975bbd75be5f4c826a2ee52bffdbdb6e96750516c6ab5077a0f5035b219ffbfa8f50997537f7aecbe223426e66b5426b057565b5f25bc7a98c6f047ffd7c26a037df97eaffedc4629dccd6a8448d0afa8b742146e4fbf09a80eeeecb2aed7ff7e221ab99a3aaf312d83fcaa857e25ece7f9950ba847b6574eafb7fffbf16f09a879268b7fff951d04d4007eabcf048f7cdedd046c754d447f11fd47a8011ba4b0ed3fdfe69");
    constexpr std::size_t kExtensionLength = 16;
    Bytes From(const Bytes& b, std::size_t at) { return Bytes(b.begin() + std::ptrdiff_t(at), b.end()); }
    RtpPacket Parse(const Bytes& b) { return *RtpPacket::Parse(b.data(), int(b.size())); }
}

TEST(RtpPacket_parse) {
    RtpPacket p = Parse(kRtpData);
    CHECK(p.version == RtpPacket::RTP_VERSION && !p.padding && !p.extension && p.csrcCount == 0 && p.csrc.empty() && p.marker);
    CHECK(p.payloadType == 96 && p.sequenceNumber == 22159 && p.timestamp == 55166400 && p.ssrc == int(0xD76EF1A6u));
    CHECK(p.payloadData == From(kRtpData, RtpPacket::MIN_HEADER_SIZE));
    RtpPacket large = Parse(kRtpDataWithLargeTimestamp);
    CHECK(large.marker && large.sequenceNumber == 29234 && large.timestamp == 3688686074LL && large.ssrc == int(0xf5fe62a4u));
    CHECK(large.payloadData == From(kRtpDataWithLargeTimestamp, RtpPacket::MIN_HEADER_SIZE));
    // The header extension is skipped, not kept.
    RtpPacket ext = Parse(kRtpDataWithHeaderExtension);
    CHECK(!ext.extension && !ext.marker && ext.payloadType == 96 && ext.sequenceNumber == 61514 && ext.timestamp == 2000000000 && ext.ssrc == 0x35ff2773);
    CHECK(ext.payloadData == From(kRtpDataWithHeaderExtension, RtpPacket::MIN_HEADER_SIZE + kExtensionLength));
}

TEST(RtpPacket_writeToBuffer) {
    const int n = int(kRtpData.size());
    RtpPacket p = Parse(kRtpData);
    Bytes buffer(std::size_t(n), 0);
    CHECK(p.WriteToBuffer(buffer.data(), n, 0, n) == n && buffer == kRtpData);
    Bytes half(std::size_t(n / 2), 0);
    CHECK(p.WriteToBuffer(half.data(), n / 2, 0, n) == -1);
    CHECK(p.WriteToBuffer(buffer.data(), n, 0, n / 2) == -1);
    CHECK(p.WriteToBuffer(buffer.data(), n, n - 1, n) == -1);
}

TEST(RtpPacket_build) {
    RtpPacket built;
    built.marker = true;
    built.payloadType = 96;
    built.SetSequenceNumber(22159);
    built.timestamp = 55166400;
    built.ssrc = int(0xD76EF1A6u);
    built.payloadData = From(kRtpData, RtpPacket::MIN_HEADER_SIZE);
    CHECK(Parse(kRtpData) == built);
    RtpPacket large;
    large.marker = true;
    large.payloadType = 96;
    large.SetSequenceNumber(29234);
    large.timestamp = 3688686074LL;
    large.ssrc = int(0xf5fe62a4u);
    large.payloadData = From(kRtpDataWithLargeTimestamp, RtpPacket::MIN_HEADER_SIZE);
    const int size = RtpPacket::MIN_HEADER_SIZE + int(large.payloadData.size());
    Bytes bytes(std::size_t(size), 0);
    large.WriteToBuffer(bytes.data(), size, 0, size);
    CHECK(bytes == kRtpDataWithLargeTimestamp);
}

TEST(RtpPacket_sequenceNumbers) {
    CHECK(RtpPacket::GetNextSequenceNumber(65534) == 65535 && RtpPacket::GetNextSequenceNumber(65535) == 0 && RtpPacket::GetNextSequenceNumber(0) == 1);
    CHECK(RtpPacket::GetPreviousSequenceNumber(1) == 0 && RtpPacket::GetPreviousSequenceNumber(0) == 65535 && RtpPacket::GetPreviousSequenceNumber(65535) == 65534);
    bool symmetric = true;
    for (int i = 0; i < RtpPacket::MAX_SEQUENCE_NUMBER; ++i)
        symmetric = symmetric && RtpPacket::GetPreviousSequenceNumber(RtpPacket::GetNextSequenceNumber(i)) == i &&
                    RtpPacket::GetNextSequenceNumber(RtpPacket::GetPreviousSequenceNumber(i)) == i;
    CHECK(symmetric);
}

namespace {
    RtpPacket MakePacket(int sequenceNumber) {
        RtpPacket p;
        p.SetSequenceNumber(sequenceNumber);
        return p;
    }
    bool Is(const std::optional<RtpPacket>& p, int sequenceNumber) { return p && p->sequenceNumber == sequenceNumber; }
}

TEST(RtpPacketReorderingQueue_pollAndReset) {
    RtpPacketReorderingQueue q;
    CHECK(!q.Poll(0));
    q.Offer(MakePacket(1), 0);
    CHECK(Is(q.Poll(0), 1));
    RtpPacketReorderingQueue inOrder;
    for (int i = 1; i <= 3; ++i) inOrder.Offer(MakePacket(i), 0);
    CHECK(Is(inOrder.Poll(0), 1) && Is(inOrder.Poll(0), 2) && Is(inOrder.Poll(0), 3));
    RtpPacketReorderingQueue r;
    r.Offer(MakePacket(1), 0);
    r.Offer(MakePacket(2), 0);
    r.Reset();
    CHECK(!r.Poll(0));
}

TEST(RtpPacketReorderingQueue_reorder) {
    // Arriving 1, 4, 5, 2, 3, 6: the late ones in time.
    RtpPacketReorderingQueue q;
    q.Offer(MakePacket(1), 1);
    q.Offer(MakePacket(4), 2);
    CHECK(Is(q.Poll(0), 1));
    q.Offer(MakePacket(5), 3);
    CHECK(!q.Poll(1));
    q.Offer(MakePacket(2), 4);
    CHECK(Is(q.Poll(2), 2));
    q.Offer(MakePacket(3), 5);
    CHECK(Is(q.Poll(3), 3));
    q.Offer(MakePacket(6), 6);
    CHECK(Is(q.Poll(4), 4) && Is(q.Poll(5), 5) && Is(q.Poll(6), 6));
    // Arriving 1, 3, 4, 2: 2 after 3 was taken is dropped.
    RtpPacketReorderingQueue late;
    late.Offer(MakePacket(1), 1);
    late.Offer(MakePacket(3), 2);
    CHECK(Is(late.Poll(1), 1));
    late.Offer(MakePacket(4), 3);
    CHECK(Is(late.Poll(2), 3));
    late.Offer(MakePacket(2), 4);
    CHECK(Is(late.Poll(3), 4));
}

TEST(RtpPacketReorderingQueue_jumpsAndWrapAround) {
    RtpPacketReorderingQueue forward;
    forward.Offer(MakePacket(1), 1);
    forward.Offer(MakePacket(10 + RtpPacketReorderingQueue::MAX_SEQUENCE_LEAP_ALLOWED), 2);
    CHECK(Is(forward.Poll(0), 10 + RtpPacketReorderingQueue::MAX_SEQUENCE_LEAP_ALLOWED));
    RtpPacketReorderingQueue backward;
    const int jump = RtpPacket::MAX_SEQUENCE_NUMBER - RtpPacketReorderingQueue::MAX_SEQUENCE_LEAP_ALLOWED - 10;
    backward.Offer(MakePacket(1), 1);
    backward.Offer(MakePacket(jump), 2);
    CHECK(Is(backward.Poll(0), jump));
    RtpPacketReorderingQueue wrap;
    for (int i = 0; i < 4; ++i) wrap.Offer(MakePacket(65000 + i), 1 + i);
    CHECK(Is(wrap.Poll(0), 65000));
    wrap.Offer(MakePacket(2), 5);
    wrap.Offer(MakePacket(3), 6);
    wrap.Offer(MakePacket(65004), 7);
    CHECK(Is(wrap.Poll(0), 65001) && Is(wrap.Poll(0), 65002) && Is(wrap.Poll(0), 65003) && Is(wrap.Poll(0), 65004));
    CHECK(Is(wrap.Poll(6), 2) && Is(wrap.Poll(0), 3));
}
