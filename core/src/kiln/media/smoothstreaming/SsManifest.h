#pragma once

// media3 exoplayer-smoothstreaming's manifest: SsManifest and SsManifestParser.

#include "kiln/media/TrackSelection.h"
#include "kiln/media/stream/DrmInitData.h"
#include "kiln/media/stream/StreamUtil.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media::ss {

    using stream::kTimeUnset;

    // SsManifest: a SmoothStreaming client manifest.
    struct SsManifest {
        // TrackEncryptionBox as SmoothStreaming's protection builds it (8-byte IVs, the header's
        // key id).
        struct TrackEncryptionBox {
            bool isEncrypted = true;
            int perSampleIvSize = 8;
            std::vector<std::uint8_t> keyId;
            bool operator==(const TrackEncryptionBox&) const = default;
        };
        struct ProtectionElement {
            stream::Uuid uuid{};
            std::vector<std::uint8_t> data;   // a pssh box
            std::vector<TrackEncryptionBox> trackEncryptionBoxes;
            bool operator==(const ProtectionElement&) const = default;
        };
        // A StreamIndex: one type's tracks (QualityLevels) and its chunks.
        struct StreamElement {
            TrackType type = TrackType::Unknown;
            std::string subType;
            std::int64_t timescale = 0;
            std::string name;
            int maxWidth = -1, maxHeight = -1, displayWidth = -1, displayHeight = -1;
            std::string language;
            std::vector<Format> formats;
            int chunkCount = 0;

            StreamElement() = default;
            StreamElement(std::string baseUri, std::string chunkTemplate, TrackType type, std::string subType, std::int64_t timescale, std::string name,
                          int maxWidth, int maxHeight, int displayWidth, int displayHeight, std::string language, std::vector<Format> formats,
                          std::vector<std::int64_t> chunkStartTimes, std::int64_t lastChunkDuration);
            StreamElement Copy(std::vector<Format> formats) const;
            // The chunk containing timeUs (the first when before it).
            int GetChunkIndex(std::int64_t timeUs) const;
            std::int64_t GetStartTimeUs(int chunkIndex) const { return m_ChunkStartTimesUs.at(std::size_t(chunkIndex)); }
            std::int64_t GetChunkDurationUs(int chunkIndex) const;
            // The chunk's URL: the template with {bitrate} and {start time} filled in.
            std::string BuildRequestUri(int track, int chunkIndex) const;

        private:
            std::string m_BaseUri, m_ChunkTemplate;
            std::vector<std::int64_t> m_ChunkStartTimes, m_ChunkStartTimesUs;
            std::int64_t m_LastChunkDurationUs = 0;
        };

        static constexpr int kUnsetLookahead = -1;
        int majorVersion = 0, minorVersion = 0;
        int lookAheadCount = kUnsetLookahead;
        bool isLive = false;
        std::optional<ProtectionElement> protectionElement;
        std::vector<StreamElement> streamElements;
        std::int64_t durationUs = kTimeUnset, dvrWindowLengthUs = kTimeUnset;

        SsManifest() = default;
        // Durations in the timescale's units (0: unset).
        SsManifest(int majorVersion, int minorVersion, std::int64_t timescale, std::int64_t duration, std::int64_t dvrWindowLength, int lookAheadCount,
                   bool isLive, std::optional<ProtectionElement> protectionElement, std::vector<StreamElement> streamElements);
        // FilterableManifest.copy: only the streams the keys name (group: stream element, stream:
        // format), in key order.
        SsManifest Copy(std::vector<stream::StreamKey> streamKeys) const;
    };

    // SsManifestParser. Throws stream::ParserException (MissingFieldException's message:
    // "Missing required field: <name>").
    class SsManifestParser {
    public:
        SsManifest Parse(const std::string& uri, const std::string& content) const;
    };

}
