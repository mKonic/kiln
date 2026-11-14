#pragma once

// Track selection (media3's TrackSelectionParameters and DefaultTrackSelector's choices): which
// of an item's tracks play, from the app's preferences and the media's own flags.

#include "kiln/media/MediaItem.h"

#include <optional>
#include <set>
#include <string>
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

    struct TrackSelectionParameters {
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
        bool operator==(const TrackSelectionParameters&) const = default;
    };

    // Util.normalizeLanguageCode: lower case with '-', ISO 639-2 codes as ISO 639-1 ("eng" is
    // "en"), macrolanguages explicit ("nb" is "no-nob"), legacy tags replaced.
    std::string NormalizeLanguageCode(std::string_view language);
    // DefaultTrackSelector.getFormatLanguageScore: 4 the same, 3 one a prefix of the other, 2 the
    // same main language, 1 an undetermined track language when allowed, else 0.
    int FormatLanguageScore(const std::string& formatLanguage, const std::optional<std::string>& language, bool allowUndeterminedFormatLanguage);

    // The text track DefaultTrackSelector picks (TextTrackInfo) among an item's subtitles, or -1.
    // supported: whether each can be read; captioningLanguage: the system captions' language when
    // they're on (and the parameters use them); selectedAudioLanguage: the playing audio's.
    int SelectTextTrack(const std::vector<MediaItem::SubtitleConfiguration>& tracks, const std::vector<bool>& supported,
                        const TrackSelectionParameters& parameters, const std::optional<std::string>& captioningLanguage,
                        const std::optional<std::string>& selectedAudioLanguage);

}
