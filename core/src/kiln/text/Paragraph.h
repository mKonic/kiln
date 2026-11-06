#pragma once

// Paragraph layout over the platform backend (ui-text Paragraph.kt, MultiParagraph.kt,
// TextLayoutResult.kt, font/FontMatcher.kt, and the Android span setup in
// AndroidParagraphHelper / SpannableExtensions): span styles flatten into resolved runs, each
// ParagraphStyle range becomes its own paragraph, and paragraphs stack into a MultiParagraph
// that answers TextLayoutResult's queries. Offsets are UTF-16.

#include "kiln/TextBackend.h"
#include "kiln/text/AnnotatedString.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kiln::text {

    enum class TextOverflow : std::uint8_t { Clip, Ellipsis, Visible, StartEllipsis, MiddleEllipsis };
    constexpr bool IsEllipsis(TextOverflow o) { return o == TextOverflow::Ellipsis || o == TextOverflow::StartEllipsis || o == TextOverflow::MiddleEllipsis; }

    enum class ResolvedTextDirection : std::uint8_t { Ltr, Rtl };

    // Placeholder (Placeholder.kt): room for inline content, sized in sp or em, aligned to the
    // line or the text.
    enum class PlaceholderVerticalAlign : std::uint8_t { AboveBaseline = 1, Top, Bottom, Center, TextTop, TextBottom, TextCenter };
    struct Placeholder {
        TextUnit width, height;
        PlaceholderVerticalAlign verticalAlign = PlaceholderVerticalAlign::AboveBaseline;
        bool operator==(const Placeholder&) const = default;
    };

    // FontMatcher.matchFont (CSS 4 font matching): the fonts of the best weight, preferring the
    // asked style.
    std::vector<Font> MatchFont(const std::vector<Font>& fonts, FontWeight weight, FontStyle style);

    // The text as consecutive runs, each the base style merged with every span style covering
    // it (in the order they were added). Adjacent equal runs join.
    std::vector<Range<SpanStyle>> FlattenSpanStyles(const AnnotatedString& text, const SpanStyle& base);

    // The backend request for one paragraph. `style` is resolved (ResolveDefaults); runStyles
    // receives the run styles, parallel to request.runs.
    struct ParagraphConstraints {
        float width = INFINITY;            // layout width (infinite: no wrapping)
        float maxHeight = INFINITY;
        int maxLines = 0x7fffffff;
        TextOverflow overflow = TextOverflow::Clip;
    };
    ParagraphRequest BuildParagraphRequest(const AnnotatedString& text, const TextStyle& style, const ui::Density& density,
                                           const ParagraphConstraints& constraints, std::vector<SpanStyle>* runStyles = nullptr,
                                           std::span<const Range<Placeholder>> placeholders = {});
    // resolveLineHeightInPx.
    float ResolveLineHeightPx(TextUnit lineHeight, float contextFontSizePx, const ui::Density& density);

    // One laid-out paragraph; releases its backend handle when destroyed.
    class Paragraph {
    public:
        Paragraph(TextBackend* backend, ParagraphRequest request, std::vector<SpanStyle> runStyles, float width);
        ~Paragraph();
        Paragraph(const Paragraph&) = delete;
        Paragraph& operator=(const Paragraph&) = delete;

        const ParagraphData& Data() const { return m_Data; }
        const ParagraphRequest& Request() const { return m_Request; }
        std::span<const SpanStyle> RunStyles() const { return m_RunStyles; }
        float Width() const { return m_Width; }
        float Height() const { return m_Data.height; }
        int LineCount() const { return m_Data.lineCount; }
        int Length() const { return m_Length; }

        int LineForOffset(int offset) const;
        int LineForVertical(float y) const;
        float HorizontalPosition(int offset, bool usePrimaryDirection) const;
        CursorRect Cursor(int offset) const;
        int OffsetForPosition(float x, float y) const;
        RectF BoundingBox(int offset) const;
        std::vector<RectF> SelectionRects(int start, int end) const;
        std::pair<int, int> WordBoundary(int offset) const;
        bool IsRtlCharAt(int offset) const;

    private:
        TextBackend* m_Backend;
        ParagraphRequest m_Request;
        std::vector<SpanStyle> m_RunStyles;
        ParagraphData m_Data;
        float m_Width;
        int m_Length;
    };

    // MultiParagraphIntrinsics: per-paragraph-style intrinsic widths.
    ParagraphIntrinsicWidths MeasureIntrinsics(TextBackend* backend, const AnnotatedString& text, const TextStyle& style,
                                               const ui::Density& density, ui::LayoutDirection direction,
                                               std::span<const Range<Placeholder>> placeholders = {});

    struct ParagraphInfo {
        std::shared_ptr<Paragraph> paragraph;
        int startIndex = 0, endIndex = 0;          // in the whole text
        int startLineIndex = 0, endLineIndex = 0;
        float top = 0, bottom = 0;
    };

    class MultiParagraph {
    public:
        // `style` unresolved; each paragraph merges its ParagraphStyle and resolves.
        MultiParagraph(TextBackend* backend, const AnnotatedString& text, const TextStyle& style, const ui::Density& density,
                       ui::LayoutDirection direction, const ParagraphConstraints& constraints,
                       std::span<const Range<Placeholder>> placeholders = {});

        const AnnotatedString& Text() const { return m_Text; }
        const std::vector<ParagraphInfo>& Paragraphs() const { return m_Infos; }
        float Width() const { return m_Width; }
        float Height() const { return m_Height; }
        int LineCount() const { return m_LineCount; }
        bool DidExceedMaxLines() const { return m_DidExceedMaxLines; }
        float FirstBaseline() const;
        float LastBaseline() const;

        int LineForOffset(int offset) const;
        int LineForVerticalPosition(float y) const;
        int LineStart(int line) const;
        // visibleEnd: excludes trailing whitespace (and an ellipsized tail is still counted).
        int LineEnd(int line, bool visibleEnd = false) const;
        bool IsLineEllipsized(int line) const;
        float LineTop(int line) const;
        float LineBottom(int line) const;
        float LineBaseline(int line) const;
        float LineLeft(int line) const;
        float LineRight(int line) const;
        float LineHeight(int line) const { return LineBottom(line) - LineTop(line); }
        float LineWidth(int line) const { return LineRight(line) - LineLeft(line); }
        float HorizontalPosition(int offset, bool usePrimaryDirection) const;
        CursorRect Cursor(int offset) const;
        RectF CursorRectWithWidth(int offset, float width) const;
        int OffsetForPosition(ui::Offset p) const;
        RectF BoundingBox(int offset) const;
        std::vector<RectF> SelectionRects(int start, int end) const;
        std::pair<int, int> WordBoundary(int offset) const;
        ResolvedTextDirection ParagraphDirection(int offset) const;
        ResolvedTextDirection BidiRunDirection(int offset) const;
        // One per placeholder given: where it is, or none (ellipsized, cut off, past maxLines).
        const std::vector<std::optional<RectF>>& PlaceholderRects() const { return m_PlaceholderRects; }

    private:
        const ParagraphInfo& InfoForOffset(int offset) const;
        const ParagraphInfo& InfoForLine(int line) const;
        const ParagraphInfo& InfoForY(float y) const;
        AnnotatedString m_Text;
        std::vector<ParagraphInfo> m_Infos;
        float m_Width = 0, m_Height = 0;
        int m_LineCount = 0;
        bool m_DidExceedMaxLines = false;
        std::vector<std::optional<RectF>> m_PlaceholderRects;
    };

    struct TextLayoutInput {
        AnnotatedString text;
        TextStyle style;
        int maxLines = 0x7fffffff;
        bool softWrap = true;
        TextOverflow overflow = TextOverflow::Clip;
        std::vector<Range<Placeholder>> placeholders;   // sorted, inside one paragraph each
        ui::Density density;
        ui::LayoutDirection layoutDirection = ui::LayoutDirection::Ltr;
        ui::Constraints constraints;
    };

    // TextLayoutResult: the paragraphs and the size they take within the constraints.
    class TextLayoutResult {
    public:
        TextLayoutResult(TextLayoutInput input, std::shared_ptr<MultiParagraph> paragraphs, ui::IntSize size)
            : m_Input(std::move(input)), m_Paragraphs(std::move(paragraphs)), m_Size(size) {}

        const TextLayoutInput& LayoutInput() const { return m_Input; }
        const MultiParagraph& Paragraphs() const { return *m_Paragraphs; }
        std::shared_ptr<MultiParagraph> SharedParagraphs() const { return m_Paragraphs; }
        ui::IntSize Size() const { return m_Size; }
        float FirstBaseline() const { return m_Paragraphs->FirstBaseline(); }
        float LastBaseline() const { return m_Paragraphs->LastBaseline(); }
        bool DidOverflowWidth() const { return float(m_Size.width) < m_Paragraphs->Width(); }
        bool DidOverflowHeight() const { return m_Paragraphs->DidExceedMaxLines() || float(m_Size.height) < m_Paragraphs->Height(); }
        bool HasVisualOverflow() const { return DidOverflowWidth() || DidOverflowHeight(); }
        int LineCount() const { return m_Paragraphs->LineCount(); }
        int LineStart(int line) const { return m_Paragraphs->LineStart(line); }
        int LineEnd(int line, bool visibleEnd = false) const { return m_Paragraphs->LineEnd(line, visibleEnd); }
        bool IsLineEllipsized(int line) const { return m_Paragraphs->IsLineEllipsized(line); }
        float LineTop(int line) const { return m_Paragraphs->LineTop(line); }
        float LineBottom(int line) const { return m_Paragraphs->LineBottom(line); }
        float LineBaseline(int line) const { return m_Paragraphs->LineBaseline(line); }
        float LineLeft(int line) const { return m_Paragraphs->LineLeft(line); }
        float LineRight(int line) const { return m_Paragraphs->LineRight(line); }
        int LineForOffset(int offset) const { return m_Paragraphs->LineForOffset(offset); }
        int LineForVerticalPosition(float y) const { return m_Paragraphs->LineForVerticalPosition(y); }
        float HorizontalPosition(int offset, bool usePrimaryDirection) const { return m_Paragraphs->HorizontalPosition(offset, usePrimaryDirection); }
        int OffsetForPosition(ui::Offset p) const { return m_Paragraphs->OffsetForPosition(p); }
        CursorRect Cursor(int offset) const { return m_Paragraphs->Cursor(offset); }
        RectF BoundingBox(int offset) const { return m_Paragraphs->BoundingBox(offset); }
        std::vector<RectF> SelectionRects(int start, int end) const { return m_Paragraphs->SelectionRects(start, end); }
        std::pair<int, int> WordBoundary(int offset) const { return m_Paragraphs->WordBoundary(offset); }
        ResolvedTextDirection ParagraphDirection(int offset) const { return m_Paragraphs->ParagraphDirection(offset); }
        ResolvedTextDirection BidiRunDirection(int offset) const { return m_Paragraphs->BidiRunDirection(offset); }
        const std::vector<std::optional<RectF>>& PlaceholderRects() const { return m_Paragraphs->PlaceholderRects(); }

    private:
        TextLayoutInput m_Input;
        std::shared_ptr<MultiParagraph> m_Paragraphs;
        ui::IntSize m_Size;
    };

    // TextDelegate / ParagraphLayoutCache.layoutText: the layout width from the constraints and
    // the intrinsic width (finalMaxWidth), maxLines overwritten to 1 for a single ellipsized line
    // (finalMaxLines), and the size constrained.
    // intrinsics: measured when empty, else reused (they only depend on text, style, density and
    // direction).
    TextLayoutResult LayoutText(TextBackend* backend, TextLayoutInput input, std::optional<ParagraphIntrinsicWidths>* intrinsics = nullptr);

}
