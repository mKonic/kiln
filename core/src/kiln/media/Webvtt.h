#pragma once

// WebVTT subtitles: media3's WebvttParser, WebvttCueParser, WebvttCssParser / WebvttCssStyle,
// WebvttSubtitle and LegacySubtitleUtil.toCuesWithTiming.
//
// Cue settings (line, position, size, align, vertical), the cue text's tags (b, i, u, c with
// classes and the default colour classes, v, lang, ruby / rt), entities (&lt; &gt; &amp;
// &nbsp;), NOTE comments and STYLE blocks of ::cue rules (by id, tag, class and voice:
// color, background-color, font-family, font-weight, font-style, font-size, text-decoration:
// underline, ruby-position, text-combine-upright).
//
// The text's spans become an AnnotatedString as media3's Android spans render: StyleSpan,
// Underline/Strikethrough (both together where they overlap), Foreground/BackgroundColor,
// Typeface, AbsoluteSize (sp), RelativeSize (em). media3's own spans become string
// annotations: kRubyAnnotation (the ruby text) with kRubyPositionAnnotation ("over" or
// "under") over the base text, kVoiceAnnotation (the speaker), kTextCombineUprightAnnotation.
//
// Not ported: Mp4WebvttParser (WebVTT inside MP4 samples).
// One deliberate difference: a tag cut off at the end of the text ("</") is skipped, where
// media3's substring throws.

#include "kiln/media/Cue.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kiln::media {

    inline constexpr const char* kRubyAnnotation = "ruby";
    inline constexpr const char* kRubyPositionAnnotation = "ruby-position";
    inline constexpr const char* kVoiceAnnotation = "voice";
    inline constexpr const char* kTextCombineUprightAnnotation = "text-combine-upright";

    // TextAnnotation.Position.
    inline constexpr int kTextAnnotationPositionUnknown = -1, kTextAnnotationPositionBefore = 1, kTextAnnotationPositionAfter = 2;

    struct WebvttCssStyle {
        static constexpr int kUnspecified = -1;
        static constexpr int kStyleNormal = 0, kStyleBold = 1, kStyleItalic = 2, kStyleBoldItalic = 3;
        static constexpr int kFontSizeUnitPixel = 1, kFontSizeUnitEm = 2, kFontSizeUnitPercent = 3;

        // The selector.
        std::string targetId, targetTag, targetVoice;
        std::set<std::string> targetClasses;
        // The style (OptionalBoolean: kUnspecified, 0 off, 1 on).
        std::optional<std::string> fontFamily;   // lower case
        std::optional<std::uint32_t> fontColor, backgroundColor;   // ARGB
        int linethrough = kUnspecified, underline = kUnspecified, bold = kUnspecified, italic = kUnspecified;
        int fontSizeUnit = kUnspecified;
        float fontSize = 0;
        int rubyPosition = kTextAnnotationPositionUnknown;
        bool combineUpright = false;

        // How well the selector matches (0: not at all); the universal selector only the whole cue.
        int GetSpecificityScore(const std::string* id, const std::string& tag, const std::set<std::string>& classes,
                                const std::string& voice) const;
        int GetStyle() const;   // kUnspecified or the bold / italic bits
    };

    // WebvttCssParser.parseBlock: the ::cue rules of a STYLE block's text (to its first empty line).
    std::vector<WebvttCssStyle> ParseWebvttCssBlock(std::string_view block);

    // ColorParser.parseCssColor: #rrggbb, #rrggbbaa, rgb(), rgba() with a 0-1 alpha, or a CSS
    // colour name; throws std::invalid_argument otherwise.
    std::uint32_t ParseCssColor(std::string_view expression);

    struct WebvttCueInfo {
        Cue cue;
        std::int64_t startTimeUs = 0, endTimeUs = 0;
    };

    // WebvttCueParser's pieces (visible for tests, as in media3).
    namespace webvtt {
        // parseTimestampUs: [hh:]mm:ss.ttt; throws std::invalid_argument.
        std::int64_t ParseTimestampUs(std::string_view timestamp);
        // The cue text's markup as styled text.
        text::AnnotatedString ParseCueText(const std::string* id, std::string_view markup, const std::vector<WebvttCssStyle>& styles);
        // A cue with these settings ("line:10% align:start ...") and WebVTT's defaults.
        Cue ParseCueSettingsList(std::string_view settings);
        Cue NewCueForText(text::AnnotatedString text);
    }

    class WebvttParser final : public SubtitleParser {
    public:
        static constexpr CueReplacementBehavior kCueReplacementBehavior = CueReplacementBehavior::Merge;
        CueReplacementBehavior GetCueReplacementBehavior() const override { return kCueReplacementBehavior; }
        // Throws std::invalid_argument when the data isn't WebVTT (no WEBVTT header), a STYLE
        // block comes after a cue, or a colour can't be read.
        void Parse(std::span<const std::uint8_t> data, const SubtitleOutputOptions& options,
                   const kiln::Function<void(CuesWithTiming)>& output) override;
    };

    // WebvttSubtitle: the cues showing at a time (those without a line stacked from the bottom),
    // and the times they change.
    class WebvttSubtitle {
    public:
        explicit WebvttSubtitle(std::vector<WebvttCueInfo> cueInfos);
        int GetNextEventTimeIndex(std::int64_t timeUs) const;   // kIndexUnset past the last
        int GetEventTimeCount() const { return int(m_SortedTimes.size()); }
        std::int64_t GetEventTime(int index) const { return m_SortedTimes.at(std::size_t(index)); }
        std::vector<Cue> GetCues(std::int64_t timeUs) const;
        // LegacySubtitleUtil.toCuesWithTiming.
        void ToCuesWithTiming(const SubtitleOutputOptions& options, const kiln::Function<void(CuesWithTiming)>& output) const;

    private:
        std::vector<WebvttCueInfo> m_Infos;
        std::vector<std::int64_t> m_SortedTimes;
    };

}
