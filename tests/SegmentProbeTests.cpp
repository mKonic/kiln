// The segment probes the stream loader uses: the PTS base of Android's TS extractor
// (ATSParser), HlsMediaChunk's ID3 PRIV timestamp, FragmentedMp4Extractor.parseSidx, and
// AES-128-CBC (the vector is openssl's: `openssl enc -aes-128-cbc` with PKCS#7 padding).
#include "Check.h"

#include "kiln/media/stream/SegmentProbe.h"

#include <cstring>
#include <string>
#include <vector>

using namespace kiln::media::stream;
using Bytes = std::vector<std::uint8_t>;

namespace {
    // One 188-byte TS packet carrying `payload` (stuffed with an adaptation field).
    Bytes TsPacket(int pid, bool unitStart, int cc, const Bytes& payload) {
        Bytes p(188, 0xff);
        p[0] = 0x47;
        p[1] = std::uint8_t((unitStart ? 0x40 : 0) | ((pid >> 8) & 0x1f));
        p[2] = std::uint8_t(pid & 0xff);
        const std::size_t stuffing = 184 - payload.size();
        if (stuffing == 0) {
            p[3] = std::uint8_t(0x10 | (cc & 0x0f));
            std::memcpy(&p[4], payload.data(), payload.size());
        } else {
            p[3] = std::uint8_t(0x30 | (cc & 0x0f));
            p[4] = std::uint8_t(stuffing - 1);
            if (stuffing > 1) p[5] = 0;
            std::memcpy(&p[4 + stuffing], payload.data(), payload.size());
        }
        return p;
    }
    Bytes Pat() { return { 0x00, 0x00, 0xb0, 13, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1, 0x00, 0, 0, 0, 0 }; }
    Bytes Pmt() {
        return { 0x00, 0x02, 0xb0, 23, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x01, 0xf0, 0x00,
                 0x1b, 0xe1, 0x01, 0xf0, 0x00,     // H.264 on 0x101
                 0x0f, 0xe1, 0x02, 0xf0, 0x00,     // ADTS on 0x102
                 0, 0, 0, 0 };
    }
    Bytes Pes(int streamId, std::int64_t pts) {
        return { 0x00, 0x00, 0x01, std::uint8_t(streamId), 0x00, 0x00, 0x80, 0x80, 0x05,
                 std::uint8_t(0x21 | ((pts >> 29) & 0x0e)), std::uint8_t((pts >> 22) & 0xff), std::uint8_t(((pts >> 14) & 0xfe) | 1),
                 std::uint8_t((pts >> 7) & 0xff), std::uint8_t(((pts << 1) & 0xfe) | 1), 0xaa, 0xbb };
    }
    void Add(Bytes& out, const Bytes& packet) { out.insert(out.end(), packet.begin(), packet.end()); }
}

TEST(SegmentProbe_firstTsTimestampBase_isTheFirstPesToComplete) {
    Bytes ts;
    Add(ts, TsPacket(0, true, 0, Pat()));
    Add(ts, TsPacket(0x100, true, 0, Pmt()));
    Add(ts, TsPacket(0x101, true, 0, Pes(0xe0, 1000)));   // video starts first
    Add(ts, TsPacket(0x102, true, 0, Pes(0xc0, 900)));
    Add(ts, TsPacket(0x101, false, 1, Bytes(184, 0x11)));
    Add(ts, TsPacket(0x102, true, 1, Pes(0xc0, 2000)));   // ...but audio's first PES completes first
    Add(ts, TsPacket(0x101, true, 2, Pes(0xe0, 4000)));
    CHECK(FirstTsTimestampBase(ts) == std::optional<std::int64_t>(900));
}

TEST(SegmentProbe_firstTsTimestampBase_videoCompletingFirst) {
    Bytes ts;
    Add(ts, TsPacket(0, true, 0, Pat()));
    Add(ts, TsPacket(0x100, true, 0, Pmt()));
    Add(ts, TsPacket(0x101, true, 0, Pes(0xe0, 0x1ffffff00LL)));   // near the 33-bit wrap
    Add(ts, TsPacket(0x102, true, 0, Pes(0xc0, 900)));
    Add(ts, TsPacket(0x101, true, 1, Pes(0xe0, 3000)));
    CHECK(FirstTsTimestampBase(ts) == std::optional<std::int64_t>(0x1ffffff00LL));
}

TEST(SegmentProbe_firstTsTimestampBase_ignoresStreamsBeforeThePmtAndContinuityBreaks) {
    Bytes ts;
    Add(ts, TsPacket(0, true, 0, Pat()));
    Add(ts, TsPacket(0x101, true, 0, Pes(0xe0, 100)));   // before the PMT: no stream yet
    Add(ts, TsPacket(0x100, true, 0, Pmt()));
    Add(ts, TsPacket(0x101, true, 1, Pes(0xe0, 200)));
    Add(ts, TsPacket(0x101, false, 5, Bytes(184, 0)));   // a continuity break drops the PES
    Add(ts, TsPacket(0x101, true, 6, Pes(0xe0, 300)));
    Add(ts, TsPacket(0x101, true, 7, Pes(0xe0, 400)));
    CHECK(FirstTsTimestampBase(ts) == std::optional<std::int64_t>(300));
    CHECK(!FirstTsTimestampBase(Bytes(188 * 3, 0)).has_value());
}

TEST(SegmentProbe_peekId3PrivTimestamp) {
    const std::string owner = PRIV_TIMESTAMP_FRAME_OWNER;
    const std::size_t frameSize = owner.size() + 1 + 8;
    Bytes tag = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, std::uint8_t(10 + frameSize) };
    Bytes frame = { 'P', 'R', 'I', 'V', 0, 0, 0, std::uint8_t(frameSize), 0, 0 };
    tag.insert(tag.end(), frame.begin(), frame.end());
    tag.insert(tag.end(), owner.begin(), owner.end());
    tag.push_back(0);
    // The top 31 bits are set too: only the 33 bits count.
    for (std::uint8_t b : { 0xff, 0xff, 0xff, 0xfe, 0x00, 0x01, 0x5f, 0x90 }) tag.push_back(b);
    Bytes segment = tag;
    segment.push_back(0xff);   // the ADTS frame that follows
    segment.push_back(0xf1);
    CHECK_EQ(PeekId3PrivTimestamp(segment), std::int64_t(0x000015f90LL));
    CHECK_EQ(Id3TagsLength(segment), tag.size());
    CHECK_EQ(PeekId3PrivTimestamp(Bytes{ 0xff, 0xf1, 0, 0, 0, 0, 0, 0, 0, 0 }), kTimeUnset);
    CHECK_EQ(Id3TagsLength(Bytes{ 0xff, 0xf1 }), std::size_t(0));
}

TEST(SegmentProbe_parseSidx) {
    Bytes box = { 0, 0, 0, 56, 's', 'i', 'd', 'x', 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0x03, 0xe8,   // timescale 1000
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,                                            // ept 0, first offset 0, 2 refs
                  0, 0, 0, 100, 0, 0, 0x07, 0xd0, 0x90, 0, 0, 0,                                 // 100 bytes, 2000
                  0, 0, 0, 200, 0, 0, 0x0b, 0xb8, 0x90, 0, 0, 0 };                               // 200 bytes, 3000
    Bytes data = { 0, 0, 0, 8, 'f', 'r', 'e', 'e' };
    data.insert(data.end(), box.begin(), box.end());
    std::optional<ChunkIndex> index = ParseSidx(data, 500);
    CHECK(index.has_value());
    CHECK_EQ(index->Length(), 2);
    CHECK_EQ(index->offsets[0], std::int64_t(564));   // after the box: 500 + 8 + 56
    CHECK_EQ(index->offsets[1], std::int64_t(664));
    CHECK_EQ(index->sizes[1], 200);
    CHECK_EQ(index->timesUs[1], std::int64_t(2'000'000));
    CHECK_EQ(index->durationsUs[1], std::int64_t(3'000'000));
    CHECK_EQ(index->DurationUs(), std::int64_t(5'000'000));
    CHECK_EQ(index->GetChunkIndex(-1), 0);
    CHECK_EQ(index->GetChunkIndex(2'000'000), 1);
    CHECK_EQ(index->GetChunkIndex(9'000'000), 1);
    CHECK(!ParseSidx(Bytes{ 0, 0, 0, 8, 'm', 'o', 'o', 'f' }, 0).has_value());
    box[32] = 0x80;   // an indirect reference
    CHECK_THROWS(ParseSidx(box, 0));
}

TEST(SegmentProbe_aes128CbcDecrypt) {
    const Bytes key = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
    const Bytes iv = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    const Bytes cipher = { 0x0b, 0xd3, 0xb0, 0x37, 0x0b, 0xd8, 0xec, 0x6a, 0x41, 0x93, 0xcf, 0xeb, 0x80, 0x2b, 0x13, 0x93, 0xd8, 0xda, 0xfa, 0x58, 0x5a, 0x8b,
                           0x04, 0xb1, 0x11, 0x91, 0xb6, 0x94, 0x54, 0x9e, 0x8f, 0xfa, 0x66, 0xa7, 0x6b, 0x89, 0x6a, 0xf8, 0xe4, 0x10, 0x8b, 0xcf, 0xd6, 0x94,
                           0x89, 0x42, 0x96, 0x9c, 0xc0, 0x3e, 0x1e, 0x67, 0x8f, 0xf8, 0x6d, 0xcc, 0xd6, 0x7a, 0xea, 0x4a, 0x8f, 0x5e, 0xd3, 0xbf };
    const Bytes plain = Aes128CbcDecrypt(key, iv, cipher);
    CHECK_EQ(std::string(plain.begin(), plain.end()), std::string("kiln adaptive streaming: AES-128 segment decryption check!"));
    CHECK_THROWS(Aes128CbcDecrypt(key, iv, Bytes(15, 0)));
    CHECK_THROWS(Aes128CbcDecrypt(Bytes(8, 0), iv, cipher));
    Bytes tampered = cipher;
    tampered.back() ^= 0x55;   // the padding no longer checks out
    CHECK_THROWS(Aes128CbcDecrypt(key, iv, tampered));
}

TEST(SegmentProbe_hlsEncryptionIv) {
    CHECK(HlsEncryptionIv("0x1A") == Bytes({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1a }));
    CHECK(HlsEncryptionIv("102") == Bytes({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02 }));
    // Longer than 16 bytes: the low 16 (BigInteger.toByteArray's tail).
    CHECK(HlsEncryptionIv("0xff000102030405060708090a0b0c0d0e0f") == Bytes({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }));
    CHECK_THROWS(HlsEncryptionIv("0xzz"));
}
