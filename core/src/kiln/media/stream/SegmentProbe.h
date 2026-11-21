#pragma once

// What the segment loader reads from segment bytes itself: the PTS Android's MPEG-TS extractor
// counts its timestamps from, a packed audio segment's ID3 PRIV timestamp (HlsMediaChunk), the
// ID3 tags to drop before such a segment reaches the platform's ADTS / MP3 extractor, and an MP4
// segment index (FragmentedMp4Extractor.parseSidx, ChunkIndex).

#include "kiln/media/stream/StreamUtil.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kiln::media::stream {

    // The 33-bit PTS ATSParser (without TS_TIMESTAMPS_ARE_ABSOLUTE) makes its timestamps relative
    // to: the first PES with a PTS, among the program's streams that have a queue, to complete
    // (a PES completes when the next one of its stream starts). nullopt when the bytes hold none.
    std::optional<std::int64_t> FirstTsTimestampBase(std::span<const std::uint8_t> ts);

    // "com.apple.streaming.transportStreamTimestamp": the 33-bit PTS of a packed audio segment's
    // first sample, from the ID3 tag at its start (kTimeUnset when absent).
    inline constexpr const char* PRIV_TIMESTAMP_FRAME_OWNER = "com.apple.streaming.transportStreamTimestamp";
    std::int64_t PeekId3PrivTimestamp(std::span<const std::uint8_t> data);
    // The bytes of the ID3v2 tags at the start of the data (0 when there are none).
    std::size_t Id3TagsLength(std::span<const std::uint8_t> data);

    // ChunkIndex: an MP4 file's fragments, from its sidx box.
    struct ChunkIndex {
        std::vector<int> sizes;
        std::vector<std::int64_t> offsets, durationsUs, timesUs;
        int Length() const { return int(sizes.size()); }
        std::int64_t DurationUs() const { return sizes.empty() ? 0 : durationsUs.back() + timesUs.back(); }
        // The chunk containing timeUs (the first for a time before it, the last after the end).
        int GetChunkIndex(std::int64_t timeUs) const;
    };
    // The first sidx box among the top-level boxes of the data, which begins at inputPosition in
    // its file. nullopt when there is none; throws ParserException for an indirect reference.
    std::optional<ChunkIndex> ParseSidx(std::span<const std::uint8_t> data, std::int64_t inputPosition);

    // AES-128-CBC decryption with PKCS#7 padding removed (Aes128DataSource's
    // "AES/CBC/PKCS7Padding"). Throws ParserException for a key or IV that is not 16 bytes, data
    // that is not whole blocks, or bad padding.
    std::vector<std::uint8_t> Aes128CbcDecrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv, std::span<const std::uint8_t> data);
    // A segment's IV string (hex, with or without "0x"; the parser puts the media sequence number
    // there when the key has none) as 16 bytes (HlsMediaChunk.getEncryptionIvArray).
    std::vector<std::uint8_t> HlsEncryptionIv(const std::string& iv);

}
