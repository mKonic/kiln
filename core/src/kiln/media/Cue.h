#pragma once

// Subtitles (media3's common text package and the extractor's SubtitleParser): a Cue is a piece
// of text to show over the video, placed in the viewport by line and position; CuesWithTiming is
// what a parser reads (cues and when they show); CueGroup is what shows at a moment.
//
// Not ported: bitmap cues (Cue.bitmap, bitmapHeight), which only the image subtitle formats
// (PGS, DVB, VobSub) produce, and those formats aren't ported.

#include "kiln/media/MediaItem.h"
#include "kiln/text/AnnotatedString.h"
#include <kiln/Function.h>

#include <cfloat>
#include <climits>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kiln::media {

    struct Cue {
        // DIMEN_UNSET / TYPE_UNSET.
        static constexpr float kDimenUnset = -FLT_MAX;
        static constexpr int kTypeUnset = INT_MIN;
        // AnchorType: which part of the cue line and position place.
        static constexpr int kAnchorTypeStart = 0, kAnchorTypeMiddle = 1, kAnchorTypeEnd = 2;
        // LineType: line as a fraction of the viewport's height, or a number of lines (negative
        // from the bottom).
        static constexpr int kLineTypeFraction = 0, kLineTypeNumber = 1;
        // TextSizeType: textSize as a fraction of the viewport's height (with or without its
        // padding), or in pixels.
        static constexpr int kTextSizeTypeFractional = 0, kTextSizeTypeFractionalIgnorePadding = 1, kTextSizeTypeAbsolute = 2;
        // VerticalType: vertical text, lines right to left or left to right.
        static constexpr int kVerticalTypeRl = 1, kVerticalTypeLr = 2;
        // Layout.Alignment of the text within the cue box.
        enum class Alignment { Normal, Center, Opposite };

        text::AnnotatedString text;
        std::optional<Alignment> textAlignment;     // unset: centred
        std::optional<Alignment> multiRowAlignment;
        float line = kDimenUnset;
        int lineType = kTypeUnset;
        int lineAnchor = kTypeUnset;
        float position = kDimenUnset;
        int positionAnchor = kTypeUnset;
        float size = kDimenUnset;                   // the box's width (height when vertical), a fraction
        bool windowColorSet = false;
        std::uint32_t windowColor = 0xFF000000;     // ARGB, Color.BLACK
        int textSizeType = kTypeUnset;
        float textSize = kDimenUnset;
        int verticalType = kTypeUnset;
        float shearDegrees = 0;
        int zIndex = 0;
        bool operator==(const Cue&) const = default;
    };

    // What shows at presentationTimeUs (CueGroup).
    struct CueGroup {
        std::vector<Cue> cues;
        std::int64_t presentationTimeUs = 0;
        bool operator==(const CueGroup&) const = default;
    };

    // CuesWithTiming: cues and their time; durationUs kTimeUnset: until the next (endTimeUs too).
    struct CuesWithTiming {
        std::vector<Cue> cues;
        std::int64_t startTimeUs = 0;
        std::int64_t durationUs = 0;
        std::int64_t endTimeUs = 0;
        CuesWithTiming() = default;
        CuesWithTiming(std::vector<Cue> c, std::int64_t start, std::int64_t duration);
        bool operator==(const CuesWithTiming&) const = default;
    };

    // SubtitleParser.OutputOptions: everything, or only cues ending at or after startTimeUs
    // (optionally followed by the ones before it).
    struct SubtitleOutputOptions {
        std::int64_t startTimeUs = kTimeUnset;
        bool outputAllCues = false;
        static SubtitleOutputOptions AllCues() { return { kTimeUnset, false }; }
        static SubtitleOutputOptions OnlyCuesAfter(std::int64_t us) { return { us, false }; }
        static SubtitleOutputOptions CuesAfterThenRemainingCuesBefore(std::int64_t us) { return { us, true }; }
    };

    // Format.CueReplacementBehavior: a new CuesWithTiming joins the ones still showing (merge) or
    // replaces them.
    enum class CueReplacementBehavior { Merge, Replace };

    class SubtitleParser {
    public:
        virtual ~SubtitleParser() = default;
        virtual CueReplacementBehavior GetCueReplacementBehavior() const = 0;
        virtual void Parse(std::span<const std::uint8_t> data, const SubtitleOutputOptions& options,
                           const kiln::Function<void(CuesWithTiming)>& output) = 0;
        // The whole file, as a list.
        std::vector<CuesWithTiming> ParseAll(std::span<const std::uint8_t> data);
    };

    // MergingCuesResolver: the CuesWithTiming showing at a time, all of them (ordered by start
    // time, longer first), and when that changes. For parsers whose behaviour is Merge.
    class MergingCuesResolver {
    public:
        // Whether they show at currentPositionUs.
        bool AddCues(CuesWithTiming cues, std::int64_t currentPositionUs);
        std::vector<Cue> GetCuesAtTimeUs(std::int64_t timeUs) const;
        void DiscardCuesBeforeTimeUs(std::int64_t timeUs);
        std::int64_t GetPreviousCueChangeTimeUs(std::int64_t timeUs) const;   // kTimeUnset before the first
        std::int64_t GetNextCueChangeTimeUs(std::int64_t timeUs) const;       // kTimeEndOfSource after the last
        void Clear() { m_List.clear(); }
        bool Empty() const { return m_List.empty(); }

    private:
        std::vector<CuesWithTiming> m_List;   // by start time
    };

    // Text lines as ParsableByteArray.readLine reads them: a byte order mark chooses UTF-8 or
    // UTF-16 (UTF-8 without one); lines end at \n, \r or \r\n.
    class SubtitleLineReader {
    public:
        explicit SubtitleLineReader(std::span<const std::uint8_t> data);
        // false at the end.
        bool ReadLine(std::string& line);
        // The next line without taking it (peek), false at the end.
        bool PeekLine(std::string& line) const;
        // Where it reads in Text() (UTF-8), to come back to or cut a block out.
        std::size_t Position() const { return m_Pos; }
        void SetPosition(std::size_t position) { m_Pos = position; }
        const std::string& Text() const { return m_Text; }

    private:
        std::string m_Text;   // UTF-8
        std::size_t m_Pos = 0;
    };

    // Java's String.trim: without the characters up to the space at both ends.
    std::string_view JavaTrim(std::string_view s);

    namespace detail {
        std::u16string Utf8To16(std::string_view s);
        std::string Utf16To8(std::u16string_view s);
    }

}
