#pragma once

// Track selection (media3's TrackSelectionParameters and DefaultTrackSelector's choices): which
// of an item's tracks play, from the app's preferences and the media's own flags.

#include "kiln/media/MediaItem.h"
#include "kiln/media/stream/DrmInitData.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kiln::media {

    // C.SELECTION_FLAG_*.
    inline constexpr int kSelectionFlagDefault = 1, kSelectionFlagForced = 2, kSelectionFlagAutoselect = 4;
    // C.ROLE_FLAG_*.
    inline constexpr int kRoleFlagMain = 1, kRoleFlagAlternate = 1 << 1, kRoleFlagSupplementary = 1 << 2, kRoleFlagCommentary = 1 << 3,
                         kRoleFlagDub = 1 << 4, kRoleFlagEmergency = 1 << 5, kRoleFlagCaption = 1 << 6, kRoleFlagSubtitle = 1 << 7,
                         kRoleFlagSign = 1 << 8, kRoleFlagDescribesVideo = 1 << 9, kRoleFlagDescribesMusicAndSound = 1 << 10,
                         kRoleFlagEnhancedDialogIntelligibility = 1 << 11, kRoleFlagTranscribesDialog = 1 << 12,
                         kRoleFlagEasyToRead = 1 << 13, kRoleFlagTrickPlay = 1 << 14, kRoleFlagAuxiliary = 1 << 15;
    // C.TRACK_TYPE_*.
    enum class TrackType { None = -2, Unknown = -1, Default = 0, Audio = 1, Video = 2, Text = 3, Image = 4, Metadata = 5 };

    // Label: a track's name in a language (Format.labels).
    struct Label {
        std::string language;   // "" when unspecified
        std::string value;
        bool operator==(const Label&) const = default;
    };

    // Format: what a track holds (-1: unknown).
    struct Format {
        std::string id, label, language, sampleMimeType;
        int selectionFlags = 0, roleFlags = 0;
        int bitrate = -1, channelCount = -1, sampleRate = -1;   // audio
        int width = -1, height = -1;                            // video
        float frameRate = -1;
        // Adaptive streams (HLS, DASH): the RFC 6381 codecs, the container's MIME type, the
        // declared average and peak bitrates (bitrate is the peak when known, else the average,
        // as media3's Format.bitrate), the CEA-608/708 channel, and the manifest's score.
        std::string codecs, containerMimeType;
        int averageBitrate = -1, peakBitrate = -1;
        int accessibilityChannel = -1;
        float selectionPriority = -1;
        // ColorInfo (C.COLOR_SPACE_* / COLOR_RANGE_* / COLOR_TRANSFER_*), -1 when unknown.
        int colorSpace = -1, colorRange = -1, colorTransfer = -1;
        // A DASH manifest's names for the track, image tiles, and protection schemes.
        std::vector<Label> labels;
        int tileCountHorizontal = -1, tileCountVertical = -1;
        std::optional<stream::DrmInitData> drmInitData;
        // Codec initialization data (SmoothStreaming's CodecPrivateData: H.264 SPS and PPS, an
        // AAC AudioSpecificConfig).
        std::vector<std::vector<std::uint8_t>> initializationData;
        // The pixel aspect ratio, and raw audio's C.ENCODING_PCM_* (-1: not raw).
        float pixelWidthHeightRatio = 1;
        int pcmEncoding = -1;
        void SetBitrates(int average, int peak) {
            averageBitrate = average;
            peakBitrate = peak;
            bitrate = peak != -1 ? peak : average;
        }
        bool operator==(const Format&) const = default;
    };

    // Tracks: the current item's tracks in groups (a progressive file's tracks are a group each;
    // each side-loaded subtitle is one), with which can play and which do (getCurrentTracks).
    struct Tracks {
        struct Group {
            std::string id;                   // "m<n>": the media's track n; "s<n>": subtitleConfigurations[n]
            TrackType type = TrackType::Unknown;
            std::vector<Format> formats;
            std::vector<bool> trackSupported, trackSelected;
            bool IsSelected() const;
            bool IsSupported(bool allowExceedsCapabilities = false) const;
            bool operator==(const Group&) const = default;
        };
        std::vector<Group> groups;
        bool ContainsType(TrackType type) const;
        bool IsTypeSelected(TrackType type) const;
        bool IsTypeSupported(TrackType type, bool allowExceedsCapabilities = false) const;
        bool operator==(const Tracks&) const = default;
    };

    // TrackSelectionOverride: these tracks of a group, whatever the preferences say (no tracks:
    // nothing of its type).
    struct TrackSelectionOverride {
        TrackType type = TrackType::Unknown;
        std::string groupId;
        std::vector<int> trackIndices;
        bool operator==(const TrackSelectionOverride&) const = default;
    };

    struct TrackSelectionParameters {
        // Video: the largest and smallest size, frame rate and bitrate wanted, the viewport it
        // shows in (tracks far larger than it would show are left out; by default the physical
        // display's size), and the languages, labels, roles and formats to prefer.
        int maxVideoWidth = 0x7fffffff, maxVideoHeight = 0x7fffffff, maxVideoFrameRate = 0x7fffffff, maxVideoBitrate = 0x7fffffff;
        int minVideoWidth = 0, minVideoHeight = 0, minVideoFrameRate = 0, minVideoBitrate = 0;
        int viewportWidth = 0x7fffffff, viewportHeight = 0x7fffffff;
        bool viewportOrientationMayChange = true;
        bool isViewportSizeLimitedByPhysicalDisplaySize = true;
        std::vector<std::string> preferredVideoMimeTypes, preferredVideoLanguages, preferredVideoLabels;
        int preferredVideoRoleFlags = 0;
        bool forceHighestSupportedBitrate = false;
        // DefaultTrackSelector.Parameters: the closest track when none meets the constraints, or a
        // track the decoders can't fully handle when none can.
        bool exceedVideoConstraintsIfNecessary = true;
        bool exceedRendererCapabilitiesIfNecessary = true;
        bool allowVideoMixedMimeTypeAdaptiveness = false;   // adapt between video formats of different types
        // Audio: the languages (in order of preference), roles, labels and formats to prefer;
        // without a match, the system's languages.
        std::vector<std::string> preferredAudioLanguages;
        int preferredAudioRoleFlags = 0;
        std::vector<std::string> preferredAudioLabels;
        std::vector<std::string> preferredAudioMimeTypes;
        int maxAudioChannelCount = 0x7fffffff, maxAudioBitrate = 0x7fffffff;
        bool exceedAudioConstraintsIfNecessary = true;
        bool forceLowestBitrate = false;
        // Text: the languages (in order of preference; normalized), roles and labels to prefer.
        std::vector<std::string> preferredTextLanguages;
        int preferredTextRoleFlags = 0;
        std::vector<std::string> preferredTextLabels;
        bool selectUndeterminedTextLanguage = false;   // a track without a language matches
        int ignoredTextSelectionFlags = 0;             // treated as unset on text tracks
        bool selectTextByDefault = false;              // some text track even without a match
        // With the system's captions turned on, their language and caption roles instead.
        bool usePreferredTextLanguagesAndRoleFlagsFromCaptioningManager = true;
        std::set<TrackType> disabledTrackTypes;        // nothing of these types plays
        // At most one per group; one for a type replaces the preferences for that type.
        std::vector<TrackSelectionOverride> overrides;

        // Builder conveniences: addOverride (replacing one for the group), setOverrideForType
        // (the only one for its type), clearOverridesOfType, setTrackTypeDisabled.
        TrackSelectionParameters& AddOverride(TrackSelectionOverride o);
        TrackSelectionParameters& SetOverrideForType(TrackSelectionOverride o);
        TrackSelectionParameters& ClearOverridesOfType(TrackType type);
        TrackSelectionParameters& SetTrackTypeDisabled(TrackType type, bool disabled);
        // setMaxVideoSizeSd (1279 x 719), setMaxVideoSize, clearVideoSizeConstraints,
        // setMinVideoSize, setViewportSize (turning off the physical display's), clearViewportSizeConstraints,
        // setViewportSizeToPhysicalDisplaySize.
        TrackSelectionParameters& SetMaxVideoSizeSd() { return SetMaxVideoSize(1279, 719); }
        TrackSelectionParameters& SetMaxVideoSize(int width, int height);
        TrackSelectionParameters& ClearVideoSizeConstraints() { return SetMaxVideoSize(0x7fffffff, 0x7fffffff); }
        TrackSelectionParameters& SetMinVideoSize(int width, int height);
        TrackSelectionParameters& SetViewportSize(int width, int height, bool orientationMayChange);
        TrackSelectionParameters& ClearViewportSizeConstraints() { return SetViewportSize(0x7fffffff, 0x7fffffff, true); }
        TrackSelectionParameters& SetViewportSizeToPhysicalDisplaySize(bool orientationMayChange);
        bool operator==(const TrackSelectionParameters&) const = default;
    };

    // TrackSelectionUtil.getMaxVideoSizeInViewport: the largest size a video of this shape shows at
    // in the viewport (turned with it when the orientation may change).
    std::pair<int, int> MaxVideoSizeInViewport(bool orientationMayChange, int viewportWidth, int viewportHeight, int videoWidth, int videoHeight);

    // DefaultTrackSelector.selectVideoTrack over VideoTrackInfo: of the video groups, the best
    // selection (adaptive tracks of one group, or a fixed track), by the parameters' constraints and
    // preferences, then quality. support[g][i]: 2 a decoder handles the track, 1 it takes the type
    // but not these properties, 0 none; kDecoderHardwareAccelerated added when that decoder is
    // (missing: 2). displaySize: the physical display's mode, when known.
    inline constexpr int kDecoderHardwareAccelerated = 8;
    struct VideoSelection {
        int group = -1;
        std::vector<int> tracks;
        bool operator==(const VideoSelection&) const = default;
    };
    VideoSelection SelectVideoTracks(const std::vector<std::vector<Format>>& groups, const std::vector<std::vector<int>>& support,
                                     const TrackSelectionParameters& parameters, const std::optional<std::string>& selectedAudioLanguage,
                                     std::optional<std::pair<int, int>> displaySize);

    // Util.normalizeLanguageCode: lower case with '-', ISO 639-2 codes as ISO 639-1 ("eng" is
    // "en"), macrolanguages explicit ("nb" is "no-nob"), legacy tags replaced.
    std::string NormalizeLanguageCode(std::string_view language);
    // DefaultTrackSelector.getFormatLanguageScore: 4 the same, 3 one a prefix of the other, 2 the
    // same main language, 1 an undetermined track language when allowed, else 0.
    int FormatLanguageScore(const std::string& formatLanguage, const std::optional<std::string>& language, bool allowUndeterminedFormatLanguage);

    // The audio track DefaultTrackSelector picks (AudioTrackInfo) among a progressive item's
    // audio tracks, or -1. systemLanguages: Util.getSystemLanguageCodes, most preferred first.
    int SelectAudioTrack(const std::vector<Format>& tracks, const std::vector<bool>& supported, const TrackSelectionParameters& parameters,
                         const std::vector<std::string>& systemLanguages);

    // The selection for a type from the overrides: the group and track overridden (track -1:
    // nothing of the type), nullopt without an override for the type.
    struct OverrideChoice { std::string groupId; int track = -1; };
    std::optional<OverrideChoice> OverrideForType(const TrackSelectionParameters& parameters, TrackType type);

    // The text track DefaultTrackSelector picks (TextTrackInfo) among an item's subtitles, or -1.
    // supported: whether each can be read; captioningLanguage: the system captions' language when
    // they're on (and the parameters use them); selectedAudioLanguage: the playing audio's.
    int SelectTextTrack(const std::vector<MediaItem::SubtitleConfiguration>& tracks, const std::vector<bool>& supported,
                        const TrackSelectionParameters& parameters, const std::optional<std::string>& captioningLanguage,
                        const std::optional<std::string>& selectedAudioLanguage);

}
