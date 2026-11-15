#include "kiln/media/MediaItem.h"

#include <algorithm>
#include <utility>

namespace kiln::media {

    MediaMetadata& MediaMetadata::Populate(const MediaMetadata& o) {
        auto text = [](std::string& to, const std::string& from) { if (!from.empty()) to = from; };
        auto opt = [](auto& to, const auto& from) { if (from) to = from; };
        text(title, o.title);
        text(artist, o.artist);
        text(albumTitle, o.albumTitle);
        text(albumArtist, o.albumArtist);
        text(displayTitle, o.displayTitle);
        text(subtitle, o.subtitle);
        text(description, o.description);
        opt(durationMs, o.durationMs);
        opt(userRating, o.userRating);
        opt(overallRating, o.overallRating);
        if (!o.artworkUri.empty() || !o.artworkData.empty()) {
            artworkUri = o.artworkUri;
            artworkData = o.artworkData;
            artworkDataType = o.artworkDataType;
        }
        opt(trackNumber, o.trackNumber);
        opt(totalTrackCount, o.totalTrackCount);
        opt(isBrowsable, o.isBrowsable);
        opt(isPlayable, o.isPlayable);
        opt(recordingYear, o.recordingYear);
        opt(recordingMonth, o.recordingMonth);
        opt(recordingDay, o.recordingDay);
        opt(releaseYear, o.releaseYear);
        opt(releaseMonth, o.releaseMonth);
        opt(releaseDay, o.releaseDay);
        text(writer, o.writer);
        text(composer, o.composer);
        text(conductor, o.conductor);
        text(author, o.author);
        text(discSubtitle, o.discSubtitle);
        opt(discNumber, o.discNumber);
        opt(totalDiscCount, o.totalDiscCount);
        text(genre, o.genre);
        text(compilation, o.compilation);
        text(station, o.station);
        opt(mediaType, o.mediaType);
        return *this;
    }

    MediaMetadata& MediaMetadata::MaybeSetArtworkData(std::vector<std::uint8_t> data, int pictureType) {
        constexpr int kFrontCover = 3;   // PICTURE_TYPE_FRONT_COVER
        if (artworkData.empty() || pictureType == kFrontCover || artworkDataType != kFrontCover) {
            artworkData = std::move(data);
            artworkDataType = pictureType;
        }
        return *this;
    }

    std::string PlaybackException::ErrorCodeName(int code) {
        switch (code) {
#define KILN_CODE(name, value) case value: return "ERROR_CODE_" name;
            KILN_CODE("UNSPECIFIED", 1000) KILN_CODE("REMOTE_ERROR", 1001) KILN_CODE("BEHIND_LIVE_WINDOW", 1002)
            KILN_CODE("TIMEOUT", 1003) KILN_CODE("FAILED_RUNTIME_CHECK", 1004) KILN_CODE("IO_UNSPECIFIED", 2000)
            KILN_CODE("IO_NETWORK_CONNECTION_FAILED", 2001) KILN_CODE("IO_NETWORK_CONNECTION_TIMEOUT", 2002)
            KILN_CODE("IO_INVALID_HTTP_CONTENT_TYPE", 2003) KILN_CODE("IO_BAD_HTTP_STATUS", 2004)
            KILN_CODE("IO_FILE_NOT_FOUND", 2005) KILN_CODE("IO_NO_PERMISSION", 2006)
            KILN_CODE("IO_CLEARTEXT_NOT_PERMITTED", 2007) KILN_CODE("IO_READ_POSITION_OUT_OF_RANGE", 2008)
            KILN_CODE("PARSING_CONTAINER_MALFORMED", 3001) KILN_CODE("PARSING_MANIFEST_MALFORMED", 3002)
            KILN_CODE("PARSING_CONTAINER_UNSUPPORTED", 3003) KILN_CODE("PARSING_MANIFEST_UNSUPPORTED", 3004)
            KILN_CODE("DECODER_INIT_FAILED", 4001) KILN_CODE("DECODER_QUERY_FAILED", 4002) KILN_CODE("DECODING_FAILED", 4003)
            KILN_CODE("DECODING_FORMAT_EXCEEDS_CAPABILITIES", 4004) KILN_CODE("DECODING_FORMAT_UNSUPPORTED", 4005)
            KILN_CODE("DECODING_RESOURCES_RECLAIMED", 4006) KILN_CODE("AUDIO_TRACK_INIT_FAILED", 5001)
            KILN_CODE("AUDIO_TRACK_WRITE_FAILED", 5002) KILN_CODE("AUDIO_TRACK_OFFLOAD_WRITE_FAILED", 5003)
            KILN_CODE("AUDIO_TRACK_OFFLOAD_INIT_FAILED", 5004) KILN_CODE("DRM_UNSPECIFIED", 6000)
            KILN_CODE("DRM_SCHEME_UNSUPPORTED", 6001) KILN_CODE("DRM_PROVISIONING_FAILED", 6002)
            KILN_CODE("DRM_CONTENT_ERROR", 6003) KILN_CODE("DRM_LICENSE_ACQUISITION_FAILED", 6004)
            KILN_CODE("DRM_DISALLOWED_OPERATION", 6005) KILN_CODE("DRM_SYSTEM_ERROR", 6006)
            KILN_CODE("DRM_DEVICE_REVOKED", 6007) KILN_CODE("DRM_LICENSE_EXPIRED", 6008)
            KILN_CODE("VIDEO_FRAME_PROCESSOR_INIT_FAILED", 7000) KILN_CODE("VIDEO_FRAME_PROCESSING_FAILED", 7001)
#undef KILN_CODE
            default: return code >= 1000000 ? "custom error code" : "invalid error code";
        }
    }

    // --- DefaultShuffleOrder --------------------------------------------------------------------

    namespace {
        // java.util.Random.nextInt(bound) is not reproduced; any uniform choice keeps the order's
        // behaviour (a uniformly random permutation, insertions at uniform points).
        int NextInt(std::mt19937_64& r, int bound) { return int(std::uniform_int_distribution<int>(0, bound - 1)(r)); }

        std::vector<int> CreateShuffledList(int length, std::mt19937_64& random) {
            std::vector<int> shuffled(std::size_t(std::max(length, 0)));
            for (int i = 0; i < length; ++i) {
                const int swap = NextInt(random, i + 1);
                shuffled[std::size_t(i)] = shuffled[std::size_t(swap)];
                shuffled[std::size_t(swap)] = i;
            }
            return shuffled;
        }
    }

    ShuffleOrder::ShuffleOrder(std::vector<int> shuffled, std::mt19937_64 random)
        : m_Shuffled(std::move(shuffled)), m_IndexInShuffled(m_Shuffled.size()), m_Random(random) {
        for (std::size_t i = 0; i < m_Shuffled.size(); ++i) m_IndexInShuffled[std::size_t(m_Shuffled[i])] = int(i);
    }

    ShuffleOrder::ShuffleOrder(int length, std::uint64_t seed) : ShuffleOrder(std::vector<int>{}, std::mt19937_64(seed)) {
        std::vector<int> shuffled = CreateShuffledList(length, m_Random);
        *this = ShuffleOrder(std::move(shuffled), m_Random);
    }

    ShuffleOrder::ShuffleOrder(std::vector<int> shuffledIndices, std::uint64_t seed)
        : ShuffleOrder(std::move(shuffledIndices), std::mt19937_64(seed)) {}

    int ShuffleOrder::NextIndex(int index) const {
        const int s = m_IndexInShuffled[std::size_t(index)] + 1;
        return s < Length() ? m_Shuffled[std::size_t(s)] : kIndexUnset;
    }
    int ShuffleOrder::PreviousIndex(int index) const {
        const int s = m_IndexInShuffled[std::size_t(index)] - 1;
        return s >= 0 ? m_Shuffled[std::size_t(s)] : kIndexUnset;
    }
    int ShuffleOrder::LastIndex() const { return m_Shuffled.empty() ? kIndexUnset : m_Shuffled.back(); }
    int ShuffleOrder::FirstIndex() const { return m_Shuffled.empty() ? kIndexUnset : m_Shuffled.front(); }

    ShuffleOrder ShuffleOrder::CloneAndInsert(int insertionIndex, int insertionCount) {
        std::vector<int> points(static_cast<std::size_t>(insertionCount)), values(static_cast<std::size_t>(insertionCount));
        for (int i = 0; i < insertionCount; ++i) {
            points[std::size_t(i)] = NextInt(m_Random, Length() + 1);
            const int swap = NextInt(m_Random, i + 1);
            values[std::size_t(i)] = values[std::size_t(swap)];
            values[std::size_t(swap)] = i + insertionIndex;
        }
        std::sort(points.begin(), points.end());
        std::vector<int> next(static_cast<std::size_t>(Length() + insertionCount));
        int inOld = 0, inInsertion = 0;
        for (std::size_t i = 0; i < next.size(); ++i) {
            if (inInsertion < insertionCount && inOld == points[std::size_t(inInsertion)]) {
                next[i] = values[std::size_t(inInsertion++)];
            } else {
                next[i] = m_Shuffled[std::size_t(inOld++)];
                if (next[i] >= insertionIndex) next[i] += insertionCount;
            }
        }
        return ShuffleOrder(std::move(next), std::mt19937_64(m_Random()));
    }

    ShuffleOrder ShuffleOrder::CloneAndRemove(int from, int toExclusive) {
        const int count = toExclusive - from;
        std::vector<int> next;
        next.reserve(std::size_t(std::max(Length() - count, 0)));
        for (int v : m_Shuffled) {
            if (v >= from && v < toExclusive) continue;
            next.push_back(v >= from ? v - count : v);
        }
        return ShuffleOrder(std::move(next), std::mt19937_64(m_Random()));
    }

    ShuffleOrder ShuffleOrder::CloneAndClear() { return ShuffleOrder(std::vector<int>{}, std::mt19937_64(m_Random())); }

}
