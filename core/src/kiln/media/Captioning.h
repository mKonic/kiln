#pragma once

// The system's caption settings (Android's CaptioningManager): whether captions are on, in
// which language, and how they look. Subtitles follow them as media3 does: the language picks
// the text track (DefaultTrackSelector) and the style and size draw it (SubtitleView's user
// defaults).

#include <kiln/Function.h>

#include <cstdint>
#include <optional>
#include <string>

namespace kiln::media {

    // CaptionStyleCompat: colours (ARGB), the edge drawn around the glyphs, and a typeface.
    struct CaptionStyle {
        static constexpr int kEdgeTypeNone = 0, kEdgeTypeOutline = 1, kEdgeTypeDropShadow = 2, kEdgeTypeRaised = 3, kEdgeTypeDepressed = 4;
        std::uint32_t foregroundColor = 0xFFFFFFFF;
        std::uint32_t backgroundColor = 0xFF000000;
        std::uint32_t windowColor = 0x00000000;
        int edgeType = kEdgeTypeNone;
        std::uint32_t edgeColor = 0xFFFFFFFF;
        std::string typeface;   // a family name; empty: the default
        bool operator==(const CaptionStyle&) const = default;
    };

    struct CaptioningHost {
        kiln::Function<bool()> isEnabled;
        kiln::Function<std::optional<std::string>()> localeTag;   // the captions' language, when set
        kiln::Function<CaptionStyle()> userStyle;                 // the user's, missing parts the default's
        kiln::Function<float()> fontScale;                        // 1 normal
    };
    void SetCaptioningHost(CaptioningHost host);

    // getPreferredLanguageFromCaptioningManager: the captions' language while they're on.
    std::optional<std::string> CaptioningPreferredLanguage();
    // The user's caption style and text scale while captions are on (else CaptionStyleCompat.DEFAULT
    // and 1).
    CaptionStyle UserCaptionStyle();
    float CaptioningFontScale();

}
