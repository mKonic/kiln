// media3's SubripParserTest, over its sample files (media/subrip).
#include "Check.h"

#include "kiln/media/Subrip.h"

#include <string_view>

using namespace kiln::media;

namespace {
    // media/subrip/typical
    constexpr std::string_view kTypical = R"srt(1
00:00:00,000 --> 00:00:01,234
This is the first subtitle.

2
00:00:02,345 --> 00:00:03,456
This is the second subtitle.
Second subtitle with second line.

3
02:00:04,567 --> 02:00:08,901
This is the third subtitle.
)srt";
    // media/subrip/typical_with_tags
    constexpr std::string_view kTypicalWithTags = R"srt(1
00:00:00,000 --> 00:00:01,234
This is {\an1} the first subtitle.

2
00:00:02,345 --> 00:00:03,456
This is the second subtitle.
Second {\ an 2} subtitle with second line.

3
00:00:04,567 --> 00:00:08,901
This {\an2} is the third {\ tag} subtitle.

4
00:00:09,567 --> 00:00:12,901
This { \an2} is not a valid tag due to the space after the opening bracket.

5
00:00:013,567 --> 00:00:14,901
This {\an2} is the fifth subtitle with multiple {\xyz} valid {\qwe} tags.

6
00:00:015,567 --> 00:00:15,901
This {\an1} is a lines.

7
00:00:016,567 --> 00:00:16,901
This {\an2} is a line.

8
00:00:017,567 --> 00:00:17,901
This {\an3} is a line.

9
00:00:018,567 --> 00:00:18,901
This {\an4} is a line.

10
00:00:019,567 --> 00:00:19,901
This {\an5} is a line.

11
00:00:020,567 --> 00:00:20,901
This {\an6} is a line.

12
00:00:021,567 --> 00:00:22,901
This {\an7} is a line.

13
00:00:023,567 --> 00:00:23,901
This {\an8} is a line.

14
00:00:024,567 --> 00:00:24,901
This {\an9} is a line.
)srt";
    // media/subrip/typical_bad_timestamps
    constexpr std::string_view kTypicalBadTimestamps = R"srt(1
00:00:00,000 --> 00:00:01,234
This is the first subtitle.

2
00:00:02,345 --> 00:00:03,4560
This should be discarded (too many decimal places).

3
02:00:04,567 --> 02:00:08,90
This should be discarded (too few decimal places).
)srt";
    // media/subrip/typical_no_hours_and_millis
    constexpr std::string_view kTypicalNoHoursAndMillis = R"srt(1
00:00,000 --> 00:01,234
This is the first subtitle.

2
00:00:02 --> 00:00:03
This is the second subtitle.
Second subtitle with second line.

3
02:00:04,567 --> 02:00:08,901
This is the third subtitle.
)srt";
    // media/subrip/typical_negative_timestamps
    constexpr std::string_view kTypicalNegativeTimestamps = R"srt(1
-0:00:04,567 --> -0:00:03,456
This is the first subtitle.

2
-00:00:02,345 --> 00:00:01,234
This is the second subtitle.
Second subtitle with second line.

3
02:00:04,567 --> 02:00:08,901
This is the third subtitle.
)srt";
    // media/subrip/typical_missing_timecode
    constexpr std::string_view kTypicalMissingTimecode = R"srt(1
00:00:00,000 --> 00:00:01,234
This is the first subtitle.

2
This is the second subtitle.
Second subtitle with second line.

3
02:00:04,567 --> 02:00:08,901
This is the third subtitle.

4
             --> 02:00:10,901
This is the fourth subtitle.

5
02:00:12,901 -->
This is the fifth subtitle.
)srt";
    // media/subrip/typical_missing_sequence
    constexpr std::string_view kTypicalMissingSequence = R"srt(1
00:00:00,000 --> 00:00:01,234
This is the first subtitle.

00:00:02,345 --> 00:00:03,456
This is the second subtitle.
Second subtitle with second line.

3
02:00:04,567 --> 02:00:08,901
This is the third subtitle.
)srt";
    // media/subrip/typical_unexpected_end
    constexpr std::string_view kTypicalUnexpectedEnd = R"srt(1
00:00:00,000 --> 00:00:01,234
This is the first subtitle.

2
00:00:02,345 --> 00:00:03,456
This is the second subtitle.
Second subtitle with second line.

3
)srt";
    // media/subrip/typical_extra_blank_line
    constexpr std::string_view kTypicalExtraBlankLine = R"srt(1
00:00:00,000 --> 00:00:01,234
This is the first subtitle.


2
00:00:02,345 --> 00:00:03,456
This is the second subtitle.
Second subtitle with second line.

3
02:00:04,567 --> 02:00:08,901
This is the third subtitle.
)srt";

    std::span<const std::uint8_t> Bytes(std::string_view s) { return { reinterpret_cast<const std::uint8_t*>(s.data()), s.size() }; }
    std::vector<std::uint8_t> Utf16(std::string_view ascii, bool bigEndian) {
        std::vector<std::uint8_t> out = bigEndian ? std::vector<std::uint8_t>{ 0xFE, 0xFF } : std::vector<std::uint8_t>{ 0xFF, 0xFE };
        for (char c : ascii) {
            if (bigEndian) { out.push_back(0); out.push_back(std::uint8_t(c)); }
            else { out.push_back(std::uint8_t(c)); out.push_back(0); }
        }
        return out;
    }
    std::vector<CuesWithTiming> ParseAll(std::span<const std::uint8_t> data) { return SubripParser().ParseAll(data); }
    const std::string& Text(const CuesWithTiming& c) { return c.cues.at(0).text.Text(); }

    bool TypicalCue1(const CuesWithTiming& c) {
        return c.startTimeUs == 0 && Text(c) == "This is the first subtitle." && c.durationUs == 1234000 && c.endTimeUs == 1234000;
    }
    bool TypicalCue2(const CuesWithTiming& c) {
        return c.startTimeUs == 2345000 && Text(c) == "This is the second subtitle.\nSecond subtitle with second line."
               && c.durationUs == 3456000 - 2345000 && c.endTimeUs == 3456000;
    }
    bool TypicalCue3(const CuesWithTiming& c) {
        const std::int64_t start = (((2LL * 60 * 60) + 4) * 1000 + 567) * 1000, end = (((2LL * 60 * 60) + 8) * 1000 + 901) * 1000;
        return c.startTimeUs == start && Text(c) == "This is the third subtitle." && c.durationUs == end - start && c.endTimeUs == end;
    }
    bool Typical(const std::vector<CuesWithTiming>& all) {
        return all.size() == 3 && TypicalCue1(all[0]) && TypicalCue2(all[1]) && TypicalCue3(all[2]);
    }
    bool AlignmentCue(const CuesWithTiming& c, int lineAnchor, int positionAnchor) {
        const Cue& cue = c.cues.at(0);
        return cue.lineType == Cue::kLineTypeFraction && cue.lineAnchor == lineAnchor
               && cue.line == SubripParser::FractionalPositionForAnchorType(lineAnchor) && cue.positionAnchor == positionAnchor
               && cue.position == SubripParser::FractionalPositionForAnchorType(positionAnchor);
    }
}

TEST(SubripParsesAsMedia3) {
    CHECK(SubripParser().GetCueReplacementBehavior() == CueReplacementBehavior::Merge);
    CHECK(ParseAll({}).empty());   // parseEmpty
    CHECK(Typical(ParseAll(Bytes(kTypical))));
    // parseTypicalAtOffsetAndRestrictedLength: the first cue skipped, the third cut short.
    {
        const auto b = Bytes(kTypical);
        const auto all = ParseAll(b.subspan(10, b.size() - 15));
        CHECK(all.size() == 2 && TypicalCue2(all[0]) && Text(all[1]) == "This is the third subti");
    }
    // onlyCuesAfter / cuesAfterThenRemainingCuesBefore, halfway through the second cue.
    {
        std::vector<CuesWithTiming> cues;
        SubripParser().Parse(Bytes(kTypical), SubtitleOutputOptions::OnlyCuesAfter(3'000'000), [&](CuesWithTiming c) { cues.push_back(c); });
        CHECK(cues.size() == 2 && TypicalCue2(cues[0]) && TypicalCue3(cues[1]));
        cues.clear();
        SubripParser().Parse(Bytes(kTypical), SubtitleOutputOptions::CuesAfterThenRemainingCuesBefore(3'000'000), [&](CuesWithTiming c) { cues.push_back(c); });
        CHECK(cues.size() == 3 && TypicalCue2(cues[0]) && TypicalCue3(cues[1]) && TypicalCue1(cues[2]));
    }
    // Byte order marks: UTF-8, UTF-16 little and big endian.
    {
        std::string bom = "\xEF\xBB\xBF";
        bom += kTypical;
        CHECK(Typical(ParseAll(Bytes(bom))));
        CHECK(Typical(ParseAll(Utf16(kTypical, false))));
        CHECK(Typical(ParseAll(Utf16(kTypical, true))));
    }
    CHECK(Typical(ParseAll(Bytes(kTypicalExtraBlankLine))));
    {
        const auto all = ParseAll(Bytes(kTypicalMissingTimecode));
        CHECK(all.size() == 2 && TypicalCue1(all[0]) && TypicalCue3(all[1]));
    }
    {
        const auto all = ParseAll(Bytes(kTypicalMissingSequence));
        CHECK(all.size() == 2 && TypicalCue1(all[0]) && TypicalCue3(all[1]));
    }
    {
        const auto all = ParseAll(Bytes(kTypicalNegativeTimestamps));
        CHECK(all.size() == 1 && TypicalCue3(all[0]));
    }
    {
        const auto all = ParseAll(Bytes(kTypicalUnexpectedEnd));
        CHECK(all.size() == 2 && TypicalCue1(all[0]) && TypicalCue2(all[1]));
    }
    {
        const auto all = ParseAll(Bytes(kTypicalNoHoursAndMillis));
        CHECK(all.size() == 3 && TypicalCue1(all[0]) && TypicalCue3(all[2]));
        CHECK(all[1].startTimeUs == 2'000'000 && all[1].durationUs == 1'000'000 && all[1].endTimeUs == 3'000'000);
    }
    {
        const auto all = ParseAll(Bytes(kTypicalBadTimestamps));
        CHECK(all.size() == 1 && TypicalCue1(all[0]));
    }
}

TEST(SubripReadsTagsAndAlignment) {
    // parseCueWithTag
    const auto all = ParseAll(Bytes(kTypicalWithTags));
    CHECK(all.size() == 14);
    if (all.size() != 14) return;
    CHECK(Text(all[0]) == "This is the first subtitle.");
    CHECK(Text(all[1]) == "This is the second subtitle.\nSecond subtitle with second line.");
    CHECK(Text(all[2]) == "This is the third subtitle.");
    CHECK(Text(all[3]) == "This { \\an2} is not a valid tag due to the space after the opening bracket.");
    CHECK(Text(all[4]) == "This is the fifth subtitle with multiple valid tags.");
    CHECK(AlignmentCue(all[5], Cue::kAnchorTypeEnd, Cue::kAnchorTypeStart));      // {\an1}
    CHECK(AlignmentCue(all[6], Cue::kAnchorTypeEnd, Cue::kAnchorTypeMiddle));     // {\an2}
    CHECK(AlignmentCue(all[7], Cue::kAnchorTypeEnd, Cue::kAnchorTypeEnd));        // {\an3}
    CHECK(AlignmentCue(all[8], Cue::kAnchorTypeMiddle, Cue::kAnchorTypeStart));   // {\an4}
    CHECK(AlignmentCue(all[9], Cue::kAnchorTypeMiddle, Cue::kAnchorTypeMiddle));  // {\an5}
    CHECK(AlignmentCue(all[10], Cue::kAnchorTypeMiddle, Cue::kAnchorTypeEnd));    // {\an6}
    CHECK(AlignmentCue(all[11], Cue::kAnchorTypeStart, Cue::kAnchorTypeStart));   // {\an7}
    CHECK(AlignmentCue(all[12], Cue::kAnchorTypeStart, Cue::kAnchorTypeMiddle));  // {\an8}
    CHECK(AlignmentCue(all[13], Cue::kAnchorTypeStart, Cue::kAnchorTypeEnd));     // {\an9}
    // HTML styling comes through as spans.
    const auto styled = ParseAll(Bytes("1\n00:00:01,000 --> 00:00:02,000\n<b>Bold</b> and <i>italic</i>\n"));
    CHECK(styled.size() == 1 && Text(styled[0]) == "Bold and italic" && styled[0].cues[0].text.SpanStyles().size() == 2);
}
