#pragma once

// The platform text backend (architecture 3.5): lays out one paragraph of styled text (one
// ParagraphStyle, any number of character runs) and rasterises its glyphs into atlas pages.
// On Android it builds a Spannable for StaticLayout the way Compose's AndroidParagraph does;
// the host build has an approximate FreeType + HarfBuzz version.

#include "kiln/QuadRenderer.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiln {

    // One positioned glyph of a laid-out paragraph, already rasterised into an atlas page.
    struct GlyphQuad {
        RectF rect;          // relative to the paragraph's top-left, pixels
        RectF uv;            // normalised within the page
        std::uint32_t page = 0;
        bool color = false;  // colour glyph (emoji): draw with its own colours, not the text tint
        int run = 0;         // index into the request's runs
    };

    // The font a run asks for: a generic or named system family, or a font file already matched
    // (FontMatcher) with synthesis applied (synthesizeTypeface: weight and slant are final).
    struct TextFontRequest {
        enum class Kind : std::uint8_t { Default, SansSerif, Serif, Monospace, Cursive, SystemName, Asset, File, Downloadable };
        Kind kind = Kind::Default;
        // SystemName family, the font's path, or for Downloadable "authority\npackage\nquery\ncert,cert".
        std::string name;
        int weight = 400;
        bool italic = false;
        std::string variationSettings;      // files: "'wght' 650, 'wdth' 90"
        // What the platform fakes for a file (minikin computeFakery), which glyph drawing must
        // repeat: bold when asking 600+ from a font 200+ lighter, slant when it has none.
        bool fakeBold = false, fakeItalic = false;
        bool operator==(const TextFontRequest&) const = default;
    };

    // A character run with fully resolved, layout-affecting attributes. Offsets are UTF-16.
    struct TextRunRequest {
        int start = 0, end = 0;
        float sizePx = 14;
        TextFontRequest font;
        std::string fontFeatureSettings;
        float letterSpacingPx = NAN;        // one of these two, or neither
        float letterSpacingEm = NAN;
        float baselineShift = 0;            // multiplier of the ascent
        float scaleX = 1, skewX = 0;
        std::string localeList;             // BCP-47, comma separated
        bool underline = false, lineThrough = false;   // decoration geometry wanted
        bool operator==(const TextRunRequest& o) const;   // NaN-aware
    };

    // A placeholder for inline content (PlaceholderSpan): a box of this size in the text,
    // replacing the characters [start, end). Units: px, or em of the font size there.
    struct PlaceholderRequest {
        int start = 0, end = 0;
        float width = 0, height = 0;
        bool widthEm = false, heightEm = false;
        std::uint8_t verticalAlign = 0;   // PlaceholderSpan ALIGN_*: 0 above baseline .. 6 text centre
        bool operator==(const PlaceholderRequest&) const = default;
    };

    // One paragraph. Enums carry Compose's values (TextAlign, TextDirection, LineBreak, ...).
    struct ParagraphRequest {
        std::string text;                   // UTF-8
        TextRunRequest base;                // the paragraph's style (paint before spans)
        std::vector<TextRunRequest> runs;   // cover the text, in order
        std::vector<PlaceholderRequest> placeholders;
        float widthPx = 0;                  // <= 0 or infinite: unbounded
        float maxHeightPx = INFINITY;       // Ellipsis with several lines: fewer lines to fit
        int maxLines = 0x7fffffff;
        enum class Overflow : std::uint8_t { Clip, Ellipsis, Visible, StartEllipsis, MiddleEllipsis };
        Overflow overflow = Overflow::Clip;
        std::uint8_t textAlign = 5;         // TextAlign (resolved: never Unspecified)
        std::uint8_t textDirection = 4;     // TextDirection, resolved (Ltr, Rtl, ContentOrLtr, ContentOrRtl)
        float lineHeightPx = NAN;
        float lineHeightAlignment = -1;     // LineHeightStyle
        std::uint8_t lineHeightTrim = 0x11, lineHeightMode = 0;
        bool hasLineHeightStyle = false;    // LineHeightStyle given (else the default when needed)
        float indentFirstPx = 0, indentRestPx = 0;
        std::uint8_t breakStrategy = 1, breakStrictness = 3, wordBreak = 1;
        std::uint8_t hyphens = 1;
        bool includeFontPadding = false;
        bool operator==(const ParagraphRequest& o) const;
    };

    struct LineData {
        float top = 0, bottom = 0, baseline = 0, ascent = 0, descent = 0;   // ascent < 0
        float left = 0, right = 0;          // visible extent
        int start = 0, end = 0, visibleEnd = 0;   // UTF-16; visibleEnd drops trailing spaces
        int ellipsisStart = 0, ellipsisCount = 0; // relative to start
        bool rtl = false;                   // paragraph direction
    };

    // A piece of a run on one line: its horizontal extent (for backgrounds, decorations).
    struct RunSegment {
        int run = 0, line = 0;
        float left = 0, right = 0;
        float baselineShift = 0;            // added to the line's baseline
    };

    // Per-run font metrics, relative to the baseline (positive is down).
    struct RunMetrics {
        float underlinePosition = 0, underlineThickness = 0;
        float strikePosition = 0, strikeThickness = 0;
    };

    struct ParagraphData {
        float width = 0, height = 0;
        int lineCount = 0;
        float firstBaseline = 0, lastBaseline = 0;   // from the top (FirstBaseline / LastBaseline)
        bool didExceedMaxLines = false;
        std::vector<GlyphQuad> glyphs;
        std::vector<LineData> lines;
        std::vector<RunSegment> segments;
        std::vector<RunMetrics> runMetrics;          // one per request run
        // One per request placeholder: where it is, or none when ellipsized or cut off.
        std::vector<std::optional<RectF>> placeholderRects;
        // Backend handle for position queries (0 = none). Released with TextBackend::Release.
        std::int64_t handle = 0;
    };

    // ParagraphIntrinsics: the widest unbreakable segment, and the width with no wrapping.
    struct ParagraphIntrinsicWidths { float minWidth = 0, maxWidth = 0; };

    // A zero-width caret rectangle (TextLayoutResult.getCursorRect: the caller applies width).
    struct CursorRect { float x = 0, top = 0, bottom = 0; };

    // Receives atlas pixels from a backend. Pages are created on demand by index.
    class AtlasSink {
    public:
        virtual ~AtlasSink() = default;
        virtual void EnsurePage(std::uint32_t index, std::uint32_t width, std::uint32_t height) = 0;
        virtual void UploadPage(std::uint32_t index, const void* rgba, std::uint32_t strideBytes,
                                std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) = 0;
    };

    class TextBackend {
    public:
        virtual ~TextBackend() = default;
        virtual ParagraphData Layout(const ParagraphRequest& request) = 0;
        // Ignores width, lines and overflow.
        virtual ParagraphIntrinsicWidths Intrinsics(const ParagraphRequest& request) = 0;
        // Plain text in one style: a convenience over Layout.
        ParagraphData LayoutPlain(std::string_view utf8, float sizePx, float widthPx);
        // Pushes every atlas region rasterised since the last call.
        virtual void SyncAtlas(AtlasSink& sink) = 0;
        // Queries on a laid-out paragraph; offsets are UTF-16 code units (what IMEs use).
        // getHorizontalPosition: usePrimary picks the primary (paragraph direction) caret.
        virtual float GetHorizontalPosition(std::int64_t handle, int offset, bool usePrimaryDirection) = 0;
        virtual CursorRect GetCursorRect(std::int64_t handle, int offset) = 0;
        virtual int GetOffsetForPosition(std::int64_t handle, float x, float y) = 0;
        // getBoundingBox: the character's box (an empty box at the end of the text).
        virtual RectF GetBoundingBox(std::int64_t handle, int offset) = 0;
        // getPathForRange as rectangles, one per line and direction run.
        virtual std::vector<RectF> GetSelectionRects(std::int64_t handle, int start, int end) = 0;
        // getWordBoundary: [start, end) of the word at offset.
        virtual std::pair<int, int> GetWordBoundary(std::int64_t handle, int offset) = 0;
        // Whether the character at offset is in a right-to-left run.
        virtual bool IsRtlCharAt(std::int64_t handle, int offset) = 0;
        virtual void Release(std::int64_t handle) = 0;
        // BreakIterator.getCharacterInstance preceding / following: the grapheme boundary before
        // or after a UTF-16 offset (-1 when there is none). The default steps over surrogate pairs only.
        virtual int CharacterBreak(std::u16string_view text, int offset, bool following) { return SurrogateCharacterBreak(text, offset, following); }
        static int SurrogateCharacterBreak(std::u16string_view text, int offset, bool following);
    };

}
