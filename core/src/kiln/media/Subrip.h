#pragma once

// SubRip (.srt) subtitles: media3's SubripParser. Each cue is an index line, a timing line
// ("01:02:03,456 --> 01:02:04,567", hours and milliseconds optional) and text lines with HTML
// tags (b, i, u, font color ...; read as Html.fromHtml does). An SSA alignment tag ({\an1} to
// {\an9}) places the cue; other {\...} tags are dropped. Invalid cues are skipped.

#include "kiln/media/Cue.h"

namespace kiln::media {

    class SubripParser final : public SubtitleParser {
    public:
        static constexpr CueReplacementBehavior kCueReplacementBehavior = CueReplacementBehavior::Merge;
        CueReplacementBehavior GetCueReplacementBehavior() const override { return kCueReplacementBehavior; }
        void Parse(std::span<const std::uint8_t> data, const SubtitleOutputOptions& options,
                   const kiln::Function<void(CuesWithTiming)>& output) override;
        // Where an anchor puts the cue (0.08, 0.5 or 0.92 of the viewport).
        static float FractionalPositionForAnchorType(int anchorType);
    };

}
