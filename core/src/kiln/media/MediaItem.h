#pragma once

// What a player plays and describes: media3's MediaItem, MediaMetadata, PlaybackParameters,
// PlaybackException and VideoSize, and ExoPlayer's DefaultShuffleOrder.

#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace kiln::media {

    // C.TIME_UNSET / C.TIME_END_OF_SOURCE / C.INDEX_UNSET.
    inline constexpr std::int64_t kTimeUnset = std::numeric_limits<std::int64_t>::min() + 1;
    inline constexpr std::int64_t kTimeEndOfSource = std::numeric_limits<std::int64_t>::min();
    inline constexpr int kIndexUnset = -1;

    // MediaMetadata.MEDIA_TYPE_*.
    enum class MediaType {
        Mixed = 0, Music, AudioBookChapter, PodcastEpisode, RadioStation, News, Video, Trailer, Movie, TvShow,
        Album, Artist, Genre, Playlist, Year, AudioBook, Podcast, TvChannel, TvSeries, TvSeason,
        FolderMixed, FolderAlbums, FolderArtists, FolderGenres, FolderPlaylists, FolderYears, FolderAudioBooks,
        FolderPodcasts, FolderTvChannels, FolderTvSeries, FolderTvShows, FolderRadioStations, FolderNews,
        FolderVideos, FolderTrailers, FolderMovies,
    };

    // Rating and its kinds (HeartRating, ThumbRating, StarRating, PercentageRating): unrated when
    // isRated is false.
    struct Rating {
        enum class Kind { Heart, Thumb, Star, Percentage };
        Kind kind = Kind::Heart;
        bool isRated = false;
        bool liked = false;    // Heart: hearted; Thumb: thumbs up
        int maxStars = 5;      // Star: 1 .. maxStars
        float value = 0;       // Star: the stars; Percentage: 0 .. 100
        bool operator==(const Rating&) const = default;
    };

    struct MediaMetadata {
        std::string title, artist, albumTitle, albumArtist, displayTitle, subtitle, description;
        std::optional<std::int64_t> durationMs;
        std::optional<Rating> userRating, overallRating;
        // asset:///, file:///, an absolute path, content:// or http(s)://; empty: none.
        std::string artworkUri;
        std::vector<std::uint8_t> artworkData;   // an encoded image
        std::optional<int> artworkDataType;      // PICTURE_TYPE_* (3: front cover)
        std::optional<int> trackNumber, totalTrackCount;
        std::optional<bool> isBrowsable, isPlayable;
        std::optional<int> recordingYear, recordingMonth, recordingDay;
        std::optional<int> releaseYear, releaseMonth, releaseDay;
        std::string writer, composer, conductor, author;
        std::string discSubtitle;
        std::optional<int> discNumber, totalDiscCount;
        std::string genre, compilation, station;
        std::optional<MediaType> mediaType;
        bool operator==(const MediaMetadata&) const = default;

        // Builder.populate: every field other sets (a non-empty string, a set optional) replaces
        // this one's; artwork (URI and data together) when other has either.
        MediaMetadata& Populate(const MediaMetadata& other);
        // Builder.maybeSetArtworkData: a front cover replaces any artwork; other pictures only
        // when there is none or it is not a front cover.
        MediaMetadata& MaybeSetArtworkData(std::vector<std::uint8_t> data, int pictureType);
    };

    struct MediaItem {
        // Identifies the item (MediaItem.DEFAULT_MEDIA_ID: empty).
        std::string mediaId;
        std::string uri;
        std::string mimeType;   // a hint, for a URI without an extension (application/x-mpegURL ...)
        MediaMetadata mediaMetadata;
        // Plays only part of the media.
        struct ClippingConfiguration {
            std::int64_t startPositionMs = 0;
            std::int64_t endPositionMs = kTimeEndOfSource;   // to the end
            bool relativeToLiveWindow = false;
            bool relativeToDefaultPosition = false;
            bool startsAtKeyFrame = false;   // start at the key frame before the start (no decoding from it)
            bool operator==(const ClippingConfiguration&) const = default;
        } clippingConfiguration;
        // How the app found it (MediaItem.RequestMetadata), handed to sessions.
        struct RequestMetadata {
            std::string mediaUri, searchQuery;
            bool operator==(const RequestMetadata&) const = default;
        } requestMetadata;
        // Side-loaded subtitles (MediaItem.SubtitleConfiguration).
        struct SubtitleConfiguration {
            std::string uri, mimeType, language, label, id;
            int selectionFlags = 0, roleFlags = 0;   // C.SELECTION_FLAG_* / C.ROLE_FLAG_*
            bool operator==(const SubtitleConfiguration&) const = default;
        };
        std::vector<SubtitleConfiguration> subtitleConfigurations;
        bool operator==(const MediaItem&) const = default;

        static MediaItem FromUri(std::string uri) {
            MediaItem m;
            m.uri = std::move(uri);
            return m;
        }
    };

    // PlaybackParameters: speed and pitch, each more than 0 (1 is normal).
    struct PlaybackParameters {
        float speed = 1.0f;
        float pitch = 1.0f;
        PlaybackParameters WithSpeed(float s) const { return { s, pitch }; }
        bool operator==(const PlaybackParameters&) const = default;
    };

    // PlaybackException: why playback failed, as an ERROR_CODE_*.
    struct PlaybackException {
        enum Code {
            Unspecified = 1000, RemoteError = 1001, BehindLiveWindow = 1002, Timeout = 1003, FailedRuntimeCheck = 1004,
            IoUnspecified = 2000, IoNetworkConnectionFailed = 2001, IoNetworkConnectionTimeout = 2002,
            IoInvalidHttpContentType = 2003, IoBadHttpStatus = 2004, IoFileNotFound = 2005, IoNoPermission = 2006,
            IoCleartextNotPermitted = 2007, IoReadPositionOutOfRange = 2008,
            ParsingContainerMalformed = 3001, ParsingManifestMalformed = 3002, ParsingContainerUnsupported = 3003,
            ParsingManifestUnsupported = 3004,
            DecoderInitFailed = 4001, DecoderQueryFailed = 4002, DecodingFailed = 4003,
            DecodingFormatExceedsCapabilities = 4004, DecodingFormatUnsupported = 4005, DecodingResourcesReclaimed = 4006,
            AudioTrackInitFailed = 5001, AudioTrackWriteFailed = 5002, AudioTrackOffloadWriteFailed = 5003,
            AudioTrackOffloadInitFailed = 5004,
            DrmUnspecified = 6000, DrmSchemeUnsupported = 6001, DrmProvisioningFailed = 6002, DrmContentError = 6003,
            DrmLicenseAcquisitionFailed = 6004, DrmDisallowedOperation = 6005, DrmSystemError = 6006,
            DrmDeviceRevoked = 6007, DrmLicenseExpired = 6008,
            VideoFrameProcessorInitFailed = 7000, VideoFrameProcessingFailed = 7001,
        };
        int errorCode = Unspecified;
        std::string message;
        std::int64_t timestampMs = 0;   // SystemClock.elapsedRealtime when it happened
        bool operator==(const PlaybackException&) const = default;
        // getErrorCodeName: "ERROR_CODE_IO_FILE_NOT_FOUND"; custom codes (1000000 and up)
        // "custom error code"; others "invalid error code".
        static std::string ErrorCodeName(int code);
    };

    // VideoSize: the picture's size in pixels, as decoded.
    struct VideoSize {
        int width = 0, height = 0;
        float pixelWidthHeightRatio = 1.0f;   // a pixel's width over its height
        bool operator==(const VideoSize&) const = default;
    };

    // ShuffleOrder.DefaultShuffleOrder: a random permutation of the playlist, kept in step as
    // items are added and removed.
    class ShuffleOrder {
    public:
        explicit ShuffleOrder(int length = 0, std::uint64_t seed = std::random_device{}());
        // A given order (cloned from shuffledIndices), as DefaultShuffleOrder(int[], long).
        ShuffleOrder(std::vector<int> shuffledIndices, std::uint64_t seed);

        int Length() const { return int(m_Shuffled.size()); }
        int NextIndex(int index) const;       // kIndexUnset after the last
        int PreviousIndex(int index) const;   // kIndexUnset before the first
        int LastIndex() const;
        int FirstIndex() const;
        ShuffleOrder CloneAndInsert(int insertionIndex, int insertionCount);
        ShuffleOrder CloneAndRemove(int indexFrom, int indexToExclusive);
        ShuffleOrder CloneAndClear();
        const std::vector<int>& Shuffled() const { return m_Shuffled; }

    private:
        ShuffleOrder(std::vector<int> shuffled, std::mt19937_64 random);
        std::vector<int> m_Shuffled, m_IndexInShuffled;
        std::mt19937_64 m_Random;
    };

}
