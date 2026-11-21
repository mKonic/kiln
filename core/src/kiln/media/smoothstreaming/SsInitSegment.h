#pragma once

// SmoothStreaming fragments come without a movie box: media3 gives FragmentedMp4Extractor a
// Track built from the manifest (DefaultSsChunkSource). The platform's MP4 extractor reads
// tracks only from a moov, so one is written from the same values, and the fragments' track ids
// are set to it.

#include "kiln/media/TrackSelection.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace kiln::media::ss {

    // ftyp + moov (one trak, mvex/trex) for a QualityLevel's format: H.264 (avcC from its SPS
    // and PPS, 4-byte NAL lengths), AAC (esds), AC-3 (dac3), E-AC-3 (dec3), Opus (dOps), DTS.
    // Nothing for a format no sample entry can describe.
    std::optional<std::vector<std::uint8_t>> BuildInitSegment(const Format& format, TrackType type, std::int64_t timescale, std::uint32_t trackId);
    // Sets the track id of every top-level moof's tfhd boxes.
    void SetFragmentTrackIds(std::vector<std::uint8_t>& bytes, std::uint32_t trackId);

}
