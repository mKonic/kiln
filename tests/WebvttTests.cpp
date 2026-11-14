// media3's WebvttParserTest (over its sample files, media/webvtt), with pieces of
// WebvttCssParserTest, WebvttCueParserTest and ColorParserTest.
#include "Check.h"

#include "kiln/media/Webvtt.h"

#include <cmath>
#include <string_view>

using namespace kiln::media;
using kiln::text::AnnotatedString;
using kiln::text::SpanStyle;

namespace {
    // media/webvtt/empty
    constexpr std::string_view kEmpty =
        "\n"
        "";
    // media/webvtt/typical
    constexpr std::string_view kTypical =
        "WEBVTT # This comment is allowed\n"
        "\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "00:02.345 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "";
    // media/webvtt/typical_with_bad_timestamps
    constexpr std::string_view kTypicalWithBadTimestamps =
        "WEBVTT # This comment is allowed\n"
        "\n"
        "# First timestamp is missing the 1/1000ths component, but parse anyway.\n"
        "00:00 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "02.345 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "\n"
        "0.0.0 --> 00:05.678\n"
        "This should be discarded (too many dots).\n"
        "\n"
        "00:06.789 --> not-a-timestamp\n"
        "This should be discarded (not a timestamp).\n"
        "\n"
        "00:07.789 --> 00:08.7890\n"
        "This should be discarded (too many decimal places).\n"
        "\n"
        "00:09.789 --> 00:10.78\n"
        "This should be discarded (too few decimal places).\n"
        "";
    // media/webvtt/typical_with_comments
    constexpr std::string_view kTypicalWithComments =
        "WEBVTT\n"
        "\n"
        "NOTE\n"
        "This is a comment block\n"
        "with multiple lines\n"
        "\n"
        "1\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "NOTE Single line comment with a space\n"
        "\n"
        "NOTE\011Single line comment with a tab\n"
        "\n"
        "2\n"
        "00:02.345 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "\n"
        "NOTE\n"
        "File ending with a comment\n"
        "";
    // media/webvtt/typical_with_identifiers
    constexpr std::string_view kTypicalWithIdentifiers =
        "WEBVTT\n"
        "\n"
        "1\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "2\n"
        "00:02.345 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "";
    // media/webvtt/with_bad_cue_header
    constexpr std::string_view kWithBadCueHeader =
        "WEBVTT # This comment is allowed\n"
        "\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "00:02.badbadbadbadbadbad --> 00:03.456\n"
        "This is the second subtitle.\n"
        "\n"
        "00:04.000 --> 00:05.000\n"
        "This is the third subtitle.\n"
        "";
    // media/webvtt/with_bom
    constexpr std::string_view kWithBom =
        "\357\273\277WEBVTT # This comment is allowed\n"
        "\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "00:02.345 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "";
    // media/webvtt/with_consecutive_cues
    constexpr std::string_view kWithConsecutiveCues =
        "WEBVTT\n"
        "\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "00:01.234 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "";
    // media/webvtt/with_css_complex_selectors
    constexpr std::string_view kWithCssComplexSelectors =
        "WEBVTT\n"
        "\n"
        "STYLE\n"
        "::cue(#id ){text-decoration:underline;}\n"
        "\n"
        "STYLE\n"
        "::cue(#id.class1.class2 ){ color: violet;}\n"
        "\n"
        "STYLE\n"
        "::cue(lang){font-family:Courier}\n"
        "\n"
        "STYLE\n"
        "::cue(.class.another ){font-weight: bold;}\n"
        "\n"
        "STYLE\n"
        "::cue(v.class[voice=\"Strider Trancos\"] ){ font-weight:bold; }\n"
        "\n"
        "STYLE\n"
        "::cue(v#anId.class1.class2[voice=\"Robert\"] ){ font-style:italic; }\n"
        "\n"
        "id\n"
        "00:00.000 --> 00:01.001\n"
        "This should be underlined and <lang.class1.class2>courier and violet.\n"
        "\n"
        "\303\255d\n"
        "00:02.000 --> 00:02.001\n"
        "This <lang.class1.class2>should be just courier.\n"
        "\n"
        "_id\n"
        "00:02.500 --> 00:02.501\n"
        "This <lang.class.another>should be courier and bold.\n"
        "\n"
        "00:04.000 --> 00:04.001\n"
        "This <v Strider Trancos>shouldn't be bold.</v>\n"
        "This <v.class.clazz Strider Trancos>should be bold.\n"
        "\n"
        "anId\n"
        "00:05.000 --> 00:05.001\n"
        "This is <v.class1.class3.class2 Pipo>specific</v>\n"
        "<v.class1.class3.class2 Robert>But this is more italic</v>\n"
        "";
    // media/webvtt/with_css_styles
    constexpr std::string_view kWithCssStyles =
        "WEBVTT\n"
        "\n"
        "STYLE\n"
        "::cue {\n"
        "  background-color: green;\n"
        "  color: papayawhip;\n"
        "}\n"
        "/* Style blocks cannot use blank lines nor \"dash dash greater than\" */\n"
        "\n"
        "NOTE comment blocks can be used between style blocks.\n"
        "\n"
        "STYLE\n"
        "::cue(#id2) {\n"
        "  color: peachpuff;\n"
        "}\n"
        "::cue(v[voice=\"LaGord\"]) { background-color: lime }\n"
        "\n"
        "STYLE\n"
        "::cue(v[voice=\"The Frog\"]) { font-weight: bold }\n"
        "\n"
        "STYLE\n"
        "::cue(v){text-decoration:underline}\n"
        "\n"
        "id1\n"
        "00:00.000 --> 00:01.234\n"
        "This is the first subtitle.\n"
        "\n"
        "id2\n"
        "00:02.345 --> 00:03.456\n"
        "This is the second subtitle.\n"
        "\n"
        "00:20.000 --> 00:21.000\n"
        "This is a <v.clazz Mary>reference by element</v>\n"
        "\n"
        "00:25.000 --> 00:28.000\n"
        "<v  LaGord>You are an idiot</v>\n"
        "<v  The Frog>You don't have the guts</v>\n"
        "";
    // media/webvtt/with_css_text_combine_upright
    constexpr std::string_view kWithCssTextCombineUpright =
        "WEBVTT\n"
        "\n"
        "NOTE https://developer.mozilla.org/en-US/docs/Web/CSS/text-combine-upright\n"
        "NOTE The `digits` values are ignored in CssParser and all assumed to be `all`\n"
        "\n"
        "STYLE\n"
        "::cue(.tcu-all) {\n"
        "  text-combine-upright: all;\n"
        "}\n"
        "::cue(.tcu-digits) {\n"
        "  text-combine-upright: digits 4;\n"
        "}\n"
        "\n"
        "00:00:00.000 --> 00:00:01.000 vertical:rl\n"
        "Combine <c.tcu-all>all</c> test\n"
        "\n"
        "00:03.000 --> 00:04.000 vertical:rl\n"
        "Combine <c.tcu-digits>0004</c> digits\n"
        "";
    // media/webvtt/with_font_size
    constexpr std::string_view kWithFontSize =
        "WEBVTT\n"
        "\n"
        "STYLE\n"
        "::cue(.unit-em) {\n"
        "  font-size: 4.4em;\n"
        "}\n"
        "\n"
        "STYLE\n"
        "::cue(.unit-px) {\n"
        "  font-size: 2px;\n"
        "}\n"
        "\n"
        "STYLE\n"
        "::cue(.unit-percent) {\n"
        "  font-size: 3.5%\n"
        "}\n"
        "\n"
        "STYLE\n"
        "::cue(.case-insensitivity) {\n"
        "  font-size: 2Px;\n"
        "}\n"
        "\n"
        "STYLE\n"
        "::cue(.bad-unit) {\n"
        "  font-size: 4.4ef;\n"
        "}\n"
        "\n"
        "STYLE\n"
        "::cue(.bad-value) {\n"
        "  font-size: 3.5.5%\n"
        "}\n"
        "\n"
        "00:00.000 --> 00:02.000\n"
        "<c.unit-em>Sentence with font-size set to 4.4em.</c>\n"
        "\n"
        "00:02.100 --> 00:02.400\n"
        "<c.bad-unit>Sentence with bad font-size unit.</c>\n"
        "\n"
        "00:02.500 --> 00:04.000\n"
        "<c.unit-px>Absolute font-size expressed in px unit!</c>\n"
        "\n"
        "00:04.500 --> 00:06.000\n"
        "<c.unit-percent>Relative font-size expressed in % unit!</c>\n"
        "\n"
        "00:06.100 --> 00:06.400\n"
        "<c.bad-value>Sentence with bad font-size value.</c>\n"
        "\n"
        "00:06.500 --> 00:08.000\n"
        "<c.case-insensitivity>Upper and lower case letters in font-size unit.</c>\n"
        "";
    // media/webvtt/with_overlapping_timestamps
    constexpr std::string_view kWithOverlappingTimestamps =
        "WEBVTT\n"
        "\n"
        "00:00:00.000 --> 00:00:03.000\n"
        "Displayed at the bottom for 3 seconds.\n"
        "\n"
        "00:01.000 --> 00:02.000\n"
        "Appears directly above for 1 second.\n"
        "\n"
        "00:04.000 --> 00:06.000\n"
        "Displayed at the bottom for 2 seconds.\n"
        "\n"
        "00:05.000 --> 00:07.000\n"
        "Appears directly above the previous cue, then replaces it after 1 second.\n"
        "";
    // media/webvtt/with_positioning
    constexpr std::string_view kWithPositioning =
        "WEBVTT\n"
        "\n"
        "NOTE Position with percentage and position alignment\n"
        "\n"
        "00:00:00.000 --> 00:00:01.234 position:60%,end align:start size:35%\n"
        "This is the first subtitle.\n"
        "\n"
        "NOTE Wrong position provided. It should be provided as\n"
        "a percentage value\n"
        "\n"
        "00:02.345 --> 00:03.456 position:10 align:end\n"
        "This is the second subtitle.\n"
        "\n"
        "NOTE Line as percentage and line alignment\n"
        "\n"
        "00:04.000 --> 00:05.000 line:45%,end align:middle\n"
        "This is the third subtitle.\n"
        "\n"
        "NOTE Line as absolute negative number and without line alignment.\n"
        "\n"
        "00:06.000 --> 00:07.000 line:-10 align:middle\n"
        "This is the fourth subtitle.\n"
        "\n"
        "NOTE The position and positioning alignment should be inherited from align.\n"
        "\n"
        "00:08.000 --> 00:09.000 align:right\n"
        "This is the fifth subtitle.\n"
        "\n"
        "NOTE In newer drafts, align:middle has been replaced by align:center\n"
        "\n"
        "00:10.000 --> 00:11.000 align:center\n"
        "This is the sixth subtitle.\n"
        "\n"
        "NOTE In older drafts position alignment could be start,middle,end\n"
        "\n"
        "00:12.000 --> 00:13.000 position:20%,start\n"
        "This is the seventh subtitle.\n"
        "\n"
        "NOTE In the released spec position alignment can be line-left,center,line-right\n"
        "\n"
        "00:14.000 --> 00:15.000 position:70%,line-right\n"
        "This is the eighth subtitle.\n"
        "";
    // media/webvtt/with_rubies
    constexpr std::string_view kWithRubies =
        "WEBVTT\n"
        "\n"
        "STYLE\n"
        "::cue(.under) {\n"
        "  ruby-position: under;\n"
        "}\n"
        "\n"
        "STYLE\n"
        "::cue(.over) {\n"
        "  ruby-position: over;\n"
        "}\n"
        "\n"
        "00:00:01.000 --> 00:00:02.000\n"
        "Some <ruby.over>text with over-ruby<rt>over</rt></ruby>.\n"
        "\n"
        "00:00:03.000 --> 00:00:04.000\n"
        "Some <ruby.under>text with under-ruby<rt>under</rt></ruby> and <ruby>over-ruby (default)<rt>over</rt></ruby>.\n"
        "\n"
        "NOTE Many individual rubies in a single <ruby> tag\n"
        "\n"
        "00:00:05.000 --> 00:00:06.000\n"
        "<ruby.under>base1<rt.over>over1</rt>base2<rt>under2</rt>base3<rt.under>under3</rt></ruby>.\n"
        "\n"
        "00:00:07.000 --> 00:00:08.000\n"
        "Some <ruby>text with no ruby text</ruby>.\n"
        "";
    // media/webvtt/with_tags
    constexpr std::string_view kWithTags =
        "WEBVTT\n"
        "\n"
        "00:00.000 --> 00:01.234\n"
        "This is the <i>first</i> subtitle.\n"
        "\n"
        "00:02.345 --> 00:03.456\n"
        "This is the <b><i>second</b></i> subtitle.\n"
        "\n"
        "00:04.000 --> 00:05.000\n"
        "This is the <c.red.caps>third</c> subtitle.\n"
        "\n"
        "00:06.000 --> 00:07.000\n"
        "This is the &lt;fourth&gt; &amp;subtitle.\n"
        "";
    // media/webvtt/with_vertical
    constexpr std::string_view kWithVertical =
        "WEBVTT\n"
        "\n"
        "NOTE vertical spec: https://www.w3.org/TR/webvtt1/#webvtt-alignment-cue-setting\n"
        "\n"
        "00:00:00.000 --> 00:00:01.234 vertical:rl\n"
        "Vertical right-to-left (e.g. Japanese)\n"
        "\n"
        "00:02.345 --> 00:03.456 vertical:lr\n"
        "Vertical left-to-right (e.g. Mongolian)\n"
        "\n"
        "00:04.000 --> 00:05.000\n"
        "No vertical setting (i.e. horizontal)\n"
        "";
    // media/webvtt/with_wide_unicode_chars_and_cr_endings
    constexpr std::string_view kWithWideUnicodeCharsAndCrEndings =
        "WEBVTT\r\r00:00.000 --> 00:01.234\r\360\237\230\233\r\360\237\230\233\r";
    // media/webvtt/with_x-timestamp-map_header
    constexpr std::string_view kWithXTimestampMapHeader =
        "WEBVTT\n"
        "X-TIMESTAMP-MAP=LOCAL:1:11:16.443,MPEGTS:384879885\n"
        "\n"
        "NOTE\n"
        "X-TIMESTAMP-MAP is defined in RFC 8216 section 3.5 (HLS spec).\n"
        "The cue times below are deliberately greater than 2^33 in 90kHz PTS clock,\n"
        "to test wraparound logic.\n"
        "\n"
        "266:18:35.679 --> 266:18:37.305\n"
        "  Cue text one\n"
        "\n"
        "266:18:37.433 --> 266:18:38.276\n"
        "  Cue text two\n"
        "";

    std::span<const std::uint8_t> Bytes(std::string_view s) { return { reinterpret_cast<const std::uint8_t*>(s.data()), s.size() }; }
    std::vector<CuesWithTiming> Parse(std::string_view file, SubtitleOutputOptions options = SubtitleOutputOptions::AllCues()) {
        std::vector<CuesWithTiming> all;
        WebvttParser().Parse(Bytes(file), options, [&](CuesWithTiming c) { all.push_back(std::move(c)); });
        return all;
    }
    const std::string& Text(const Cue& c) { return c.text.Text(); }
    const Cue& Only(const CuesWithTiming& c) { return c.cues.at(0); }
    bool Timing(const CuesWithTiming& c, std::int64_t start, std::int64_t end) {
        return c.startTimeUs == start && c.durationUs == end - start && c.endTimeUs == end;
    }
    int Len(std::string_view s) { return kiln::text::Utf16Length(s); }

    // SpannedSubject: a span exactly over [start, end) that passes `match`.
    template<typename F>
    bool HasStyle(const AnnotatedString& t, int start, int end, F&& match) {
        for (const auto& r : t.SpanStyles())
            if (r.start == start && r.end == end && match(r.item)) return true;
        return false;
    }
    template<typename F>
    bool HasNoStyleOverlapping(const AnnotatedString& t, int start, int end, F&& match) {
        for (const auto& r : t.SpanStyles())
            if (r.start < end && r.end > start && match(r.item)) return false;
        return true;
    }
    bool HasAnnotation(const AnnotatedString& t, std::string_view tag, int start, int end, std::string_view value) {
        for (const auto& r : t.GetStringAnnotations(tag, 0, t.Length()))
            if (r.start == start && r.end == end && r.item == value) return true;
        return false;
    }
    bool HasRuby(const AnnotatedString& t, int start, int end, std::string_view text, std::string_view position) {
        return HasAnnotation(t, kRubyAnnotation, start, end, text) && HasAnnotation(t, kRubyPositionAnnotation, start, end, position);
    }
    auto Bold = [](const SpanStyle& s) { return s.fontWeight == kiln::text::FontWeight::Bold(); };
    auto Italic = [](const SpanStyle& s) { return s.fontStyle == kiln::text::FontStyle::Italic; };
    auto AnyStyle = [](const SpanStyle& s) { return s.fontWeight.has_value() || s.fontStyle.has_value(); };
    auto Underline = [](const SpanStyle& s) { return s.textDecoration == kiln::text::TextDecoration::Underline; };
    auto Fg = [](std::uint32_t c) { return [c](const SpanStyle& s) { return s.color == kiln::ui::Color(c); }; };
    auto AnyFg = [](const SpanStyle& s) { return s.color != kiln::ui::Color::Unspecified(); };
    auto Bg = [](std::uint32_t c) { return [c](const SpanStyle& s) { return s.background == kiln::ui::Color(c); }; };
    auto Family = [](std::string name) {
        return [name](const SpanStyle& s) {
            return s.fontFamily && s.fontFamily->kind == kiln::text::FontFamily::Kind::Fonts && s.fontFamily->fonts.size() == 1
                   && s.fontFamily->fonts[0] == kiln::text::Font::Device(name);
        };
    };
    auto Em = [](float v) { return [v](const SpanStyle& s) { return s.fontSize.IsEm() && std::fabs(s.fontSize.value - v) < 1e-6f; }; };
    auto Sp = [](float v) { return [v](const SpanStyle& s) { return s.fontSize.IsSp() && s.fontSize.value == v; }; };
}

TEST(WebvttParsesTheTypicalFiles) {
    CHECK(WebvttParser().GetCueReplacementBehavior() == CueReplacementBehavior::Merge);
    // parseEmpty: not WebVTT.
    bool threw = false;
    try { Parse(kEmpty); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    auto typical = [](const std::vector<CuesWithTiming>& all) {
        return all.size() == 2 && Timing(all[0], 0, 1'234'000) && Text(Only(all[0])) == "This is the first subtitle."
               && Timing(all[1], 2'345'000, 3'456'000) && Text(Only(all[1])) == "This is the second subtitle.";
    };
    CHECK(typical(Parse(kTypical)));
    CHECK(typical(Parse(kWithBom)));
    CHECK(typical(Parse(kTypicalWithBadTimestamps)));
    CHECK(typical(Parse(kTypicalWithIdentifiers)));
    CHECK(typical(Parse(kTypicalWithComments)));
    {
        const auto after = Parse(kTypical, SubtitleOutputOptions::OnlyCuesAfter(2'000'000));
        CHECK(after.size() == 1 && Timing(after[0], 2'345'000, 3'456'000) && Text(Only(after[0])) == "This is the second subtitle.");
        const auto thenBefore = Parse(kTypical, SubtitleOutputOptions::CuesAfterThenRemainingCuesBefore(2'000'000));
        CHECK(thenBefore.size() == 2 && Timing(thenBefore[0], 2'345'000, 3'456'000) && Timing(thenBefore[1], 0, 1'234'000)
              && Text(Only(thenBefore[1])) == "This is the first subtitle.");
    }
    {
        // parseWithTags
        const auto all = Parse(kWithTags);
        CHECK(all.size() == 4);
        if (all.size() == 4) {
            CHECK(Timing(all[0], 0, 1'234'000) && Text(Only(all[0])) == "This is the first subtitle.");
            CHECK(Timing(all[1], 2'345'000, 3'456'000) && Text(Only(all[1])) == "This is the second subtitle.");
            CHECK(Timing(all[2], 4'000'000, 5'000'000) && Text(Only(all[2])) == "This is the third subtitle.");
            CHECK(Timing(all[3], 6'000'000, 7'000'000) && Text(Only(all[3])) == "This is the <fourth> &subtitle.");
        }
    }
    {
        // parseWithConsecutiveTimestamps
        const auto all = Parse(kWithConsecutiveCues);
        CHECK(all.size() == 2 && Timing(all[0], 0, 1'234'000) && Timing(all[1], 1'234'000, 3'456'000)
              && Text(Only(all[1])) == "This is the second subtitle.");
    }
    {
        // parseWithBadCueHeader
        const auto all = Parse(kWithBadCueHeader);
        CHECK(all.size() == 2 && Timing(all[0], 0, 1'234'000) && Timing(all[1], 4'000'000, 5'000'000)
              && Text(Only(all[1])) == "This is the third subtitle.");
    }
    {
        // parseWithWideUnicodeCharsAndCrLineEndings
        const auto all = Parse(kWithWideUnicodeCharsAndCrEndings);
        CHECK(all.size() == 1 && Text(Only(all[0])) == "\xF0\x9F\x98\x9B\n\xF0\x9F\x98\x9B");
    }
}

TEST(WebvttPlacesCuesAsMedia3) {
    // parseWithPositioning
    const auto all = Parse(kWithPositioning);
    CHECK(all.size() == 8);
    if (all.size() != 8) return;
    const Cue& c1 = Only(all[0]);
    CHECK(Timing(all[0], 0, 1'234'000) && Text(c1) == "This is the first subtitle.");
    CHECK(c1.position == 0.6f && c1.positionAnchor == Cue::kAnchorTypeEnd && c1.textAlignment == Cue::Alignment::Normal && c1.size == 0.35f);
    CHECK(c1.line == -1.0f && c1.lineType == Cue::kLineTypeNumber && c1.verticalType == Cue::kTypeUnset);
    const Cue& c2 = Only(all[1]);
    CHECK(Text(c2) == "This is the second subtitle." && c2.position == 0.5f && c2.textAlignment == Cue::Alignment::Opposite);
    const Cue& c3 = Only(all[2]);
    CHECK(c3.line == 0.45f && c3.lineType == Cue::kLineTypeFraction && c3.lineAnchor == Cue::kAnchorTypeEnd
          && c3.textAlignment == Cue::Alignment::Center && c3.positionAnchor == Cue::kAnchorTypeMiddle);
    const Cue& c4 = Only(all[3]);
    CHECK(c4.line == -10.0f && c4.lineAnchor == Cue::kAnchorTypeStart && c4.textAlignment == Cue::Alignment::Center
          && c4.position == 0.5f && c4.positionAnchor == Cue::kAnchorTypeMiddle);
    const Cue& c5 = Only(all[4]);
    CHECK(c5.textAlignment == Cue::Alignment::Opposite && c5.position == 1.0f && c5.positionAnchor == Cue::kAnchorTypeEnd);
    const Cue& c6 = Only(all[5]);
    CHECK(c6.textAlignment == Cue::Alignment::Center && c6.position == 0.5f && c6.positionAnchor == Cue::kAnchorTypeMiddle);
    CHECK(Only(all[6]).positionAnchor == Cue::kAnchorTypeStart && Text(Only(all[6])) == "This is the seventh subtitle.");
    CHECK(Only(all[7]).positionAnchor == Cue::kAnchorTypeEnd && Timing(all[7], 14'000'000, 15'000'000));

    // parseWithOverlappingTimestamps: cues without a line stack up from the bottom.
    const auto o = Parse(kWithOverlappingTimestamps);
    CHECK(o.size() == 6);
    if (o.size() == 6) {
        CHECK(Timing(o[0], 0, 1'000'000) && Text(Only(o[0])) == "Displayed at the bottom for 3 seconds." && Only(o[0]).line == -1.0f);
        CHECK(Timing(o[1], 1'000'000, 2'000'000) && o[1].cues.size() == 2 && o[1].cues[0].line == -1.0f && o[1].cues[1].line == -2.0f
              && Text(o[1].cues[1]) == "Appears directly above for 1 second." && o[1].cues[1].lineType == Cue::kLineTypeNumber);
        CHECK(Timing(o[2], 2'000'000, 3'000'000) && Only(o[2]).line == -1.0f);
        CHECK(Timing(o[3], 4'000'000, 5'000'000) && Text(Only(o[3])) == "Displayed at the bottom for 2 seconds.");
        CHECK(Timing(o[4], 5'000'000, 6'000'000) && o[4].cues.size() == 2 && o[4].cues[1].line == -2.0f);
        CHECK(Timing(o[5], 6'000'000, 7'000'000) && Text(Only(o[5])) == "Appears directly above the previous cue, then replaces it after 1 second."
              && Only(o[5]).line == -1.0f);
    }

    // parseWithVertical
    const auto v = Parse(kWithVertical);
    CHECK(v.size() == 3 && Only(v[0]).verticalType == Cue::kVerticalTypeRl && Only(v[1]).verticalType == Cue::kVerticalTypeLr
          && Only(v[2]).verticalType == Cue::kTypeUnset && Text(Only(v[0])) == "Vertical right-to-left (e.g. Japanese)");
}

TEST(WebvttStylesCueText) {
    // parseWithRubies
    {
        const auto all = Parse(kWithRubies);
        CHECK(all.size() == 4);
        if (all.size() == 4) {
            const AnnotatedString& t1 = Only(all[0]).text;
            CHECK(t1.Text() == "Some text with over-ruby." && HasRuby(t1, Len("Some "), Len("Some text with over-ruby"), "over", "over"));
            const AnnotatedString& t2 = Only(all[1]).text;
            CHECK(t2.Text() == "Some text with under-ruby and over-ruby (default).");
            CHECK(HasRuby(t2, Len("Some "), Len("Some text with under-ruby"), "under", "under"));
            CHECK(HasRuby(t2, Len("Some text with under-ruby and "), Len("Some text with under-ruby and over-ruby (default)"), "over", "over"));
            const AnnotatedString& t3 = Only(all[2]).text;
            CHECK(t3.Text() == "base1base2base3.");
            CHECK(HasRuby(t3, 0, Len("base1"), "over1", "over") && HasRuby(t3, Len("base1"), Len("base1base2"), "under2", "under")
                  && HasRuby(t3, Len("base1base2"), Len("base1base2base3"), "under3", "under"));
            const AnnotatedString& t4 = Only(all[3]).text;
            CHECK(t4.Text() == "Some text with no ruby text." && t4.Annotations().empty());
        }
    }
    // parseWithCssFontSizeStyle
    {
        const auto all = Parse(kWithFontSize);
        CHECK(all.size() == 6);
        if (all.size() == 6) {
            const auto& t1 = Only(all[0]).text;
            CHECK(Timing(all[0], 0, 2'000'000) && HasStyle(t1, 0, t1.Length(), Em(4.4f)));
            CHECK(Timing(all[1], 2'100'000, 2'400'000) && Only(all[1]).text.Annotations().empty());
            const auto& t3 = Only(all[2]).text;
            CHECK(t3.Text() == "Absolute font-size expressed in px unit!" && HasStyle(t3, 0, t3.Length(), Sp(2)));
            const auto& t4 = Only(all[3]).text;
            CHECK(HasStyle(t4, 0, t4.Length(), Em(0.035f)));
            CHECK(Text(Only(all[4])) == "Sentence with bad font-size value." && Only(all[4]).text.Annotations().empty());
            const auto& t6 = Only(all[5]).text;
            CHECK(HasStyle(t6, 0, t6.Length(), Sp(2)));
        }
    }
    // webvttWithCssStyle
    {
        const auto all = Parse(kWithCssStyles);
        CHECK(all.size() >= 4);
        if (all.size() >= 4) {
            const auto& t1 = Only(all[0]).text;
            CHECK(t1.Text() == "This is the first subtitle." && HasStyle(t1, 0, t1.Length(), Fg(ParseCssColor("papayawhip")))
                  && HasStyle(t1, 0, t1.Length(), Bg(ParseCssColor("green"))));
            const auto& t2 = Only(all[1]).text;
            CHECK(HasStyle(t2, 0, t2.Length(), Fg(ParseCssColor("peachpuff"))));
            const auto& t3 = Only(all[2]).text;
            CHECK(t3.Text() == "This is a reference by element" && HasStyle(t3, Len("This is a "), t3.Length(), Underline));
            const auto& t4 = Only(all[3]).text;
            CHECK(t4.Text() == "You are an idiot\nYou don't have the guts");
            CHECK(HasStyle(t4, 0, Len("You are an idiot"), Bg(ParseCssColor("lime"))));
            CHECK(HasStyle(t4, Len("You are an idiot\n"), t4.Length(), Bold));
        }
    }
    // withComplexCssSelectors
    {
        const auto all = Parse(kWithCssComplexSelectors);
        CHECK(all.size() >= 5);
        if (all.size() >= 5) {
            const auto& t1 = Only(all[0]).text;
            CHECK(HasStyle(t1, 0, t1.Length(), Underline));
            CHECK(HasStyle(t1, Len("This should be underlined and "), t1.Length(), Fg(ParseCssColor("violet"))));
            CHECK(HasStyle(t1, Len("This should be underlined and "), t1.Length(), Family("courier")));
            const auto& t2 = Only(all[1]).text;
            CHECK(HasStyle(t2, Len("This "), t2.Length(), Family("courier")) && HasNoStyleOverlapping(t2, Len("This "), t2.Length(), AnyFg));
            const auto& t3 = Only(all[2]).text;
            CHECK(HasStyle(t3, Len("This "), t3.Length(), Bold) && HasStyle(t3, Len("This "), t3.Length(), Family("courier")));
            const auto& t4 = Only(all[3]).text;
            CHECK(HasNoStyleOverlapping(t4, Len("This "), Len("shouldn't be bold."), AnyStyle));
            CHECK(HasStyle(t4, Len("This shouldn't be bold.\nThis "), t4.Length(), Bold));
            const auto& t5 = Only(all[4]).text;
            CHECK(HasNoStyleOverlapping(t5, Len("This is "), Len("This is specific"), AnyStyle));
            CHECK(HasStyle(t5, Len("This is specific\n"), t5.Length(), Italic));
        }
    }
    // webvttWithCssTextCombineUpright
    {
        const auto all = Parse(kWithCssTextCombineUpright);
        CHECK(all.size() >= 2);
        if (all.size() >= 2) {
            CHECK(HasAnnotation(Only(all[0]).text, kTextCombineUprightAnnotation, Len("Combine "), Len("Combine all"), ""));
            CHECK(HasAnnotation(Only(all[1]).text, kTextCombineUprightAnnotation, Len("Combine "), Len("Combine 0004"), ""));
        }
    }
}

TEST(WebvttCueTextTagsAndSettings) {
    // WebvttCueParserTest
    const std::vector<WebvttCssStyle> none;
    auto text = [&](std::string_view markup) { return webvtt::ParseCueText(nullptr, markup, none); };
    const auto styled = text("This <u><i>is</i></u> <b>a</b> <c.red.bg_blue>test</c>");
    CHECK(styled.Text() == "This is a test");
    CHECK(HasStyle(styled, 5, 7, Italic) && HasStyle(styled, 5, 7, Underline) && HasStyle(styled, 8, 9, Bold));
    CHECK(HasStyle(styled, 10, 14, Fg(0xFFFF0000)) && HasStyle(styled, 10, 14, Bg(0xFF0000FF)));
    CHECK(text("An unclosed u tag with <u>italic</u> inside").Text() == "An unclosed u tag with italic inside");
    CHECK(text("<v.loud Mary>Hello <i>there</i></v>").Text() == "Hello there");
    CHECK(HasAnnotation(text("<v.loud Mary>Hello</v>"), kVoiceAnnotation, 0, 5, "Mary"));
    CHECK(text("&lt;html&gt;&amp;&nbsp;").Text() == "<html>& ");
    CHECK(text("&unknown; &lt").Text() == " &lt");   // an unknown entity is dropped; an unended one stays text
    CHECK(text("Text with <unknown tag> and </ ").Text() == "Text with  and ");
    CHECK(text("<b><i>overlap</b> rest</i>").Text() == "overlap rest");
    // Underline and strikethrough together show both.
    std::vector<WebvttCssStyle> strike(1);
    strike[0].targetTag = "u";
    strike[0].linethrough = 1;
    const auto both = webvtt::ParseCueText(nullptr, "<u>x</u>", strike);
    CHECK(HasStyle(both, 0, 1, [](const SpanStyle& s) { return s.textDecoration == kiln::text::TextDecoration::Both; }));
    // A bad setting is skipped; the rest still apply.
    const Cue c = webvtt::ParseCueSettingsList("line:foo position:30% align:start size:150%");
    CHECK(c.line == Cue::kDimenUnset && c.position == 0.3f && c.positionAnchor == Cue::kAnchorTypeStart
          && c.textAlignment == Cue::Alignment::Normal && std::fabs(c.size - 0.7f) < 1e-6f);
    CHECK(webvtt::NewCueForText("x").size == 1.0f && webvtt::NewCueForText("x").position == 0.5f);
    // Timestamps.
    CHECK(webvtt::ParseTimestampUs("01:02:03.004") == 3'723'004'000 && webvtt::ParseTimestampUs("02:03.004") == 123'004'000);
    bool threw = false;
    try { webvtt::ParseTimestampUs("00:00.0001"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(WebvttCssBlocksAndColors) {
    // WebvttCssParserTest
    auto styles = ParseWebvttCssBlock("::cue { color : black; background-color: PapayaWhip }\n"
                                      "::cue(#id) { text-decoration:underline; }\n"
                                      "::cue(v.x.y[voice=\"Mary\"]) { font-weight: bold; font-style: italic; font-size: 1.5em }\n"
                                      "::cue(c.a) /* a comment */ { font-family: Courier; ruby-position: under }\n");
    CHECK(styles.size() == 4);
    if (styles.size() == 4) {
        CHECK(styles[0].fontColor == 0xFF000000u && styles[0].backgroundColor == 0xFFFFEFD5u);
        CHECK(styles[0].GetSpecificityScore(nullptr, "", {}, "") == 1 && styles[0].GetSpecificityScore(nullptr, "b", {}, "") == 0);
        const std::string id = "id";
        CHECK(styles[1].targetId == "id" && styles[1].underline == 1 && styles[1].GetSpecificityScore(&id, "", {}, "") == 0x40000000);
        CHECK(styles[2].targetTag == "v" && styles[2].targetVoice == "Mary" && (styles[2].targetClasses == std::set<std::string>{ "x", "y" }));
        CHECK(styles[2].GetStyle() == WebvttCssStyle::kStyleBoldItalic && styles[2].fontSizeUnit == WebvttCssStyle::kFontSizeUnitEm
              && styles[2].fontSize == 1.5f);
        CHECK(styles[2].GetSpecificityScore(nullptr, "v", { "x", "y", "z" }, "Mary") == 2 + 4 + 8);
        CHECK(styles[2].GetSpecificityScore(nullptr, "v", { "x" }, "Mary") == 0);
        CHECK(styles[3].fontFamily == "courier" && styles[3].rubyPosition == kTextAnnotationPositionAfter);
    }
    // ColorParserTest (CSS).
    CHECK(ParseCssColor("#FF0000") == 0xFFFF0000u && ParseCssColor("#FF000080") == 0x80FF0000u);
    CHECK(ParseCssColor("rgb(0, 255, 0)") == 0xFF00FF00u && ParseCssColor("rgba(0,0,255,0.5)") == 0x7F0000FFu);
    CHECK(ParseCssColor("Lime") == 0xFF00FF00u && ParseCssColor("transparent") == 0x00000000u);
    for (const char* bad : { "#fff", "rgb(1,2)", "notacolour", "#GG0000" }) {
        bool threw = false;
        try { ParseCssColor(bad); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }
}

TEST(WebvttSubtitleSlicesAsLegacySubtitleUtil) {
    // LegacySubtitleUtilWebvttTest: "start+duration:texts" per CuesWithTiming (A first, B second).
    auto info = [](const char* text, std::int64_t start, std::int64_t end) { return WebvttCueInfo{ webvtt::NewCueForText(text), start, end }; };
    const WebvttSubtitle empty({});
    const WebvttSubtitle simple({ info("A", 1'000'000, 2'000'000), info("B", 3'000'000, 4'000'000) });
    const WebvttSubtitle consecutive({ info("A", 1'000'000, 2'000'000), info("B", 2'000'000, 4'000'000) });
    const WebvttSubtitle overlapping({ info("A", 1'000'000, 3'000'000), info("B", 2'000'000, 4'000'000) });
    auto run = [](const WebvttSubtitle& s, SubtitleOutputOptions o) {
        std::string out;
        s.ToCuesWithTiming(o, [&](CuesWithTiming c) {
            out += std::to_string(c.startTimeUs / 100'000) + "+" + std::to_string(c.durationUs / 100'000) + ":";
            for (const Cue& cue : c.cues) out += cue.text.Text();
            out += ";";
        });
        return out;
    };
    using O = SubtitleOutputOptions;
    CHECK(run(simple, O::AllCues()) == "10+10:A;30+10:B;");
    CHECK(run(consecutive, O::AllCues()) == "10+10:A;20+20:B;");
    CHECK(run(overlapping, O::AllCues()) == "10+10:A;20+10:AB;30+10:B;");
    CHECK(run(empty, O::AllCues()).empty());
    CHECK(run(simple, O::OnlyCuesAfter(2'500'000)) == "30+10:B;");
    CHECK(run(simple, O::OnlyCuesAfter(2'000'000)) == "30+10:B;");
    CHECK(run(simple, O::OnlyCuesAfter(3'000'000)) == "30+10:B;");
    CHECK(run(simple, O::OnlyCuesAfter(1'500'000)) == "15+5:A;30+10:B;");
    CHECK(run(simple, O::OnlyCuesAfter(4'000'000)).empty());
    CHECK(run(simple, O::OnlyCuesAfter(4'500'000)).empty());
    CHECK(run(consecutive, O::OnlyCuesAfter(2'000'000)) == "20+20:B;");
    CHECK(run(consecutive, O::OnlyCuesAfter(1'500'000)) == "15+5:A;20+20:B;");
    CHECK(run(overlapping, O::OnlyCuesAfter(2'500'000)) == "25+5:AB;30+10:B;");
    CHECK(run(empty, O::OnlyCuesAfter(0)).empty());
    CHECK(run(simple, O::CuesAfterThenRemainingCuesBefore(3'000'000)) == "30+10:B;10+10:A;");
    CHECK(run(simple, O::CuesAfterThenRemainingCuesBefore(1'500'000)) == "15+5:A;30+10:B;10+5:A;");
    CHECK(run(simple, O::CuesAfterThenRemainingCuesBefore(2'000'000)) == "30+10:B;10+10:A;");
    CHECK(run(simple, O::CuesAfterThenRemainingCuesBefore(4'000'000)) == "10+10:A;30+10:B;");
    CHECK(run(simple, O::CuesAfterThenRemainingCuesBefore(4'500'000)) == "10+10:A;30+10:B;");
    CHECK(run(consecutive, O::CuesAfterThenRemainingCuesBefore(2'000'000)) == "20+20:B;10+10:A;");
    CHECK(run(consecutive, O::CuesAfterThenRemainingCuesBefore(1'500'000)) == "15+5:A;20+20:B;10+5:A;");
    CHECK(run(overlapping, O::CuesAfterThenRemainingCuesBefore(2'500'000)) == "25+5:AB;30+10:B;10+10:A;20+5:AB;");
}
