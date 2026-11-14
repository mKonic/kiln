#pragma once

// Subtitles drawn over a video: media3's SubtitleView with its canvas output (CanvasSubtitleOutput,
// SubtitlePainter). Each cue is laid out in the element by its line and position (or at the
// bottom), in the caption style: the user's system caption settings unless the app gives one.
//
//   Box(FillMax(Modifier{}, Axis::Horizontal)) {
//       VideoSurface(...)   // shows the player's subtitles itself
//   }
//   Subtitles(MatchParentSize(Modifier{}), player);   // or over anything else
//
// Vertical cues are drawn horizontally, repositioned as media3's canvas output does.

#include "kiln/foundation/Compose.h"
#include "kiln/media/Captioning.h"
#include "kiln/media/Player.h"

#include <optional>

namespace kiln::foundation {

    struct SubtitleViewOptions {
        // SubtitleView's DEFAULT_TEXT_SIZE_FRACTION and DEFAULT_BOTTOM_PADDING_FRACTION.
        static constexpr float kDefaultTextSizeFraction = 0.0533f, kDefaultBottomPaddingFraction = 0.08f;
        std::optional<media::CaptionStyle> style;   // unset: the user's (setUserDefaultStyle)
        // The text's height as a fraction of the element's; unset: the default times the user's
        // caption text scale (setUserDefaultTextSize).
        std::optional<float> fractionalTextSize;
        bool applyEmbeddedStyles = true;      // the cues' own colours, styles and window colour
        bool applyEmbeddedFontSizes = true;   // the cues' own sizes
        float bottomPaddingFraction = kDefaultBottomPaddingFraction;
    };

    void Subtitles(const Modifier& modifier, const media::CueGroup& cues, SubtitleViewOptions options = {});
    // The player's current cues.
    void Subtitles(const Modifier& modifier, media::Player& player, SubtitleViewOptions options = {});

}
