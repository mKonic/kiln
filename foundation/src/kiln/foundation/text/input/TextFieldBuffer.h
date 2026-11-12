#pragma once

// The editable text model of BasicTextField (spec 04 req. 1-3; Compose's text/input package):
// TextRange, the immutable TextFieldCharSequence a state holds, the mutable TextFieldBuffer edits
// go through (with the ChangeTracker that records what changed), and the OffsetMappingCalculator
// that maps offsets across output and codepoint transformations. Text is UTF-16, the unit
// Android's IME and Compose's offsets use.

#include "kiln/foundation/text/input/TextStyleBuffer.h"
#include "kiln/text/AnnotatedString.h"

#include <memory>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln::foundation {

    // TextRange: start may be after end (a reversed selection, dragged backwards).
    struct TextRange {
        int start = 0, end = 0;
        constexpr TextRange() = default;
        constexpr TextRange(int s, int e) : start(s), end(e) {}
        constexpr explicit TextRange(int at) : start(at), end(at) {}
        int Min() const { return start < end ? start : end; }
        int Max() const { return start < end ? end : start; }
        bool Collapsed() const { return start == end; }
        bool Reversed() const { return start > end; }
        int Length() const { return Max() - Min(); }
        bool Contains(const TextRange& o) const { return Min() <= o.Min() && o.Max() <= Max(); }
        TextRange CoerceIn(int lo, int hi) const;
        bool operator==(const TextRange&) const = default;
    };

    std::u16string ToUtf16(std::string_view utf8);
    std::string ToUtf8(std::u16string_view utf16);

    // Handwriting previews (TextHighlightType).
    enum class TextHighlightType : std::uint8_t { HandwritingSelectPreview, HandwritingDeletePreview };

    using PlacedSpan = text::Range<text::SpanStyle>;
    using PlacedAnnotation = text::Range<text::Annotation>;

    // TextFieldCharSequence: text, selection and composition (both clamped into the text), a
    // handwriting highlight, the IME's composing spans and the text's styles.
    struct TextFieldCharSequence {
        std::u16string text;
        TextRange selection;
        std::optional<TextRange> composition;
        std::optional<std::pair<TextHighlightType, TextRange>> highlight;
        std::vector<PlacedSpan> composingAnnotations;
        std::shared_ptr<const TextFieldTextStyles> textFieldTextStyles;

        TextFieldCharSequence() = default;
        TextFieldCharSequence(std::u16string t, TextRange sel, std::optional<TextRange> comp = std::nullopt,
                              std::optional<std::pair<TextHighlightType, TextRange>> hl = std::nullopt,
                              std::vector<PlacedSpan> composing = {}, std::shared_ptr<const TextFieldTextStyles> styles = nullptr);
        int Length() const { return int(text.size()); }
        bool ContentEquals(std::u16string_view other) const { return text == other; }
        bool ShouldShowSelection() const { return !highlight; }
        std::u16string_view TextBeforeSelection(int maxChars) const;
        std::u16string_view TextAfterSelection(int maxChars) const;
        std::u16string_view SelectedText() const;
        bool operator==(const TextFieldCharSequence& o) const;
    };

    // ChangeList: the ranges changed by an edit, in the text after it (Range) and before it
    // (OriginalRange), merged where they touch, in order.
    class ChangeList {
    public:
        virtual ~ChangeList() = default;
        virtual int ChangeCount() const = 0;
        virtual TextRange Range(int index) const = 0;
        virtual TextRange OriginalRange(int index) const = 0;
    };

    // forEachChange: each change in order, as (range now, range before). Changes made in the block
    // to text after the current one are visited too.
    template<typename F> void ForEachChange(const ChangeList& changes, F&& block) {
        for (int i = 0; i < changes.ChangeCount(); ++i) block(changes.Range(i), changes.OriginalRange(i));
    }
    // forEachChangeReversed: last to first; changes made in the block after the current one are
    // not visited.
    template<typename F> void ForEachChangeReversed(const ChangeList& changes, F&& block) {
        for (int i = changes.ChangeCount() - 1; i >= 0; --i) block(changes.Range(i), changes.OriginalRange(i));
    }

    class ChangeTracker final : public ChangeList {
    public:
        ChangeTracker() = default;
        ChangeTracker(const ChangeTracker&) = default;
        ChangeTracker& operator=(const ChangeTracker&) = default;
        void TrackChange(int preStart, int preEnd, int postLength, bool isFromHardwareSource = false);
        void ClearChanges() { m_Changes.clear(); }
        int ChangeCount() const override { return int(m_Changes.size()); }
        TextRange Range(int i) const override { return { m_Changes[std::size_t(i)].preStart, m_Changes[std::size_t(i)].preEnd }; }
        TextRange OriginalRange(int i) const override { return { m_Changes[std::size_t(i)].originalStart, m_Changes[std::size_t(i)].originalEnd }; }
        bool IsFromHardwareSource(int i) const { return m_Changes[std::size_t(i)].isFromHardwareSource; }
    private:
        struct Change { int preStart, preEnd, originalStart, originalEnd; bool isFromHardwareSource; };
        void AppendNewChange(Change* merged, int preMin, int preMax, int postDelta, bool hardware, std::vector<Change>& temp);
        std::vector<Change> m_Changes;
    };

    // OffsetMappingCalculator: the edits a transformation made, to map offsets between the source
    // and the transformed text. A position inside a replaced range maps to the whole range.
    class OffsetMappingCalculator {
    public:
        void RecordEditOperation(int sourceStart, int sourceEnd, int newLength);
        TextRange MapFromSource(int offset) const { return Map(offset, true); }
        TextRange MapFromDest(int offset) const { return Map(offset, false); }
    private:
        TextRange Map(int offset, bool fromSource) const;
        struct Op { int offset, srcLen, destLen; };
        std::vector<Op> m_Ops;
    };

    // adjustTextRange: where a range ends up after [replaceStart, replaceEnd) becomes
    // insertedLength characters.
    TextRange AdjustTextRange(TextRange original, int replaceStart, int replaceEnd, int insertedLength);

    // TextFieldBuffer: an edit in progress. Every replacement is tracked, moves the selection
    // with the text and ends the composition.
    class TextFieldBuffer {
    public:
        explicit TextFieldBuffer(TextFieldCharSequence initial, const ChangeTracker* initialChanges = nullptr,
                                 std::optional<TextFieldCharSequence> original = std::nullopt,
                                 OffsetMappingCalculator* offsetMapping = nullptr);
        // A copy has its own styles (its tracked ranges are not the source's).
        TextFieldBuffer(const TextFieldBuffer& o);
        TextFieldBuffer& operator=(const TextFieldBuffer& o);
        TextFieldBuffer(TextFieldBuffer&&) noexcept = default;
        TextFieldBuffer& operator=(TextFieldBuffer&&) noexcept = default;

        int Length() const { return int(m_Text.size()); }
        const std::u16string& AsCharSequence() const { return m_Text; }
        std::string ToString() const { return ToUtf8(m_Text); }
        char16_t CharAt(int index) const { return m_Text[std::size_t(index)]; }
        const std::u16string& OriginalText() const { return m_Original.text; }
        TextRange OriginalSelection() const { return m_Original.selection; }
        const TextFieldCharSequence& OriginalValue() const { return m_Original; }
        const ChangeList& Changes() const { return m_Changes; }
        ChangeTracker& Tracker() { return m_Changes; }

        TextRange Selection() const { return m_Selection; }
        void SetSelection(TextRange r);   // must be inside the text; clears the highlight
        bool HasSelection() const { return !m_Selection.Collapsed(); }
        void SetSelectionCoerced(int start, int end);
        void SetSelectionCoerced(int at) { SetSelectionCoerced(at, at); }
        void PlaceCursorBeforeCharAt(int index);
        void PlaceCursorAfterCharAt(int index);
        void PlaceCursorAtEnd() { PlaceCursorBeforeCharAt(Length()); }
        void SelectAll() { SetSelection({ 0, Length() }); }

        void Replace(int start, int end, std::u16string_view text, bool isFromHardwareSource = false);
        void Replace(int start, int end, std::string_view utf8) { Replace(start, end, ToUtf16(utf8)); }
        void Insert(int index, std::u16string_view text) { Replace(index, index, text); }
        void Delete(int start, int end) { Replace(start, end, std::u16string_view{}); }
        void Append(std::u16string_view text);
        // setTextIfChanged: replaces only what differs (the common prefix and suffix stay).
        void SetTextIfChanged(std::u16string_view text);
        void RevertAllChanges();

        // Composition, highlight and the IME's suggestion flag.
        std::optional<TextRange> Composition() const { return m_Composition; }
        bool HasComposition() const { return m_Composition.has_value(); }
        void CommitComposition() { SetCompositionRange(std::nullopt); }
        void SetComposition(int start, int end, const std::vector<PlacedSpan>* annotations = nullptr);
        const std::vector<PlacedSpan>& ComposingAnnotations() const { return m_ComposingAnnotations; }
        std::optional<std::pair<TextHighlightType, TextRange>> Highlight() const { return m_Highlight; }
        void SetHighlight(TextHighlightType type, int start, int end);
        void ClearHighlight() { m_Highlight.reset(); }
        bool suggestionSelected = false;

        // Styles over the text (TextStyleBuffer): added ranges follow the edits. addStyle without a
        // policy expands at the end; the TrackedRange variants can be read and changed later in
        // this buffer.
        void AddStyle(text::SpanStyle style, int start, int end);
        void AddStyle(text::ParagraphStyle style, int start, int end);
        TrackedRange<text::SpanStyle> AddStyle(text::SpanStyle style, TextRange range, ExpandPolicy policy);
        TrackedRange<text::ParagraphStyle> AddStyle(text::ParagraphStyle style, TextRange range, ExpandPolicy policy);
        std::vector<TrackedRange<text::SpanStyle>> GetSpanStyles(TextRange range) const;
        std::vector<TrackedRange<text::ParagraphStyle>> GetParagraphStyles(TextRange range) const;
        template<typename T> bool RemoveStyle(const TrackedRange<T>& r) { return m_Styles && m_Styles->RemoveStyle(r.creatorId, r.handle); }
        template<typename T> bool IsValid(const TrackedRange<T>& r) const { return m_Styles && m_Styles->IsValid(r.creatorId, r.handle); }
        template<typename T> TextRange GetTextRange(const TrackedRange<T>& r) const {
            if (!IsValid(r)) return {};
            const auto [s, e] = m_Styles->GetRange(r.handle);
            return { s, e };
        }
        template<typename T> void SetTextRange(const TrackedRange<T>& r, TextRange range) {
            RequireValidStyleRange(range);
            if (range.Collapsed()) throw std::invalid_argument("TrackedRange's textRange cannot be collapsed");
            if (IsValid(r)) m_Styles->SetRange(r.handle, range.start, range.end);
        }
        template<typename T> T GetStyle(const TrackedRange<T>& r) const {
            if (!IsValid(r)) return T{};
            const text::Annotation* a = m_Styles->GetItem(r.handle);
            const T* t = a ? std::get_if<T>(a) : nullptr;
            return t ? *t : T{};
        }
        template<typename T> void SetStyle(const TrackedRange<T>& r, T style) { if (IsValid(r)) m_Styles->SetItem(r.handle, text::Annotation(std::move(style))); }
        template<typename T> ExpandPolicy GetExpandPolicy(const TrackedRange<T>& r) const { return IsValid(r) ? m_Styles->GetExpandPolicy(r.handle) : ExpandPolicy::InsideOnly(); }
        template<typename T> void SetExpandPolicy(const TrackedRange<T>& r, ExpandPolicy p) { if (IsValid(r)) m_Styles->SetExpandPolicy(r.handle, p); }
        const TextStyleBuffer* StyleBuffer() const { return m_Styles.get(); }
        // getTextFieldTextStyles: a read-only copy (null without styles).
        std::shared_ptr<const TextFieldTextStyles> GetTextFieldTextStyles() const;

        TextFieldCharSequence ToTextFieldCharSequence() const;
        TextFieldCharSequence ToTextFieldCharSequence(TextRange selection, std::optional<TextRange> composition) const;

    private:
        void OnTextWillChange(int start, int end, int newLength, bool hardware);
        void SetCompositionRange(std::optional<TextRange> r);
        std::u16string m_Text;
        TextFieldCharSequence m_Original;
        ChangeTracker m_Changes;
        OffsetMappingCalculator* m_OffsetMapping = nullptr;
        TextRange m_Selection;
        std::optional<TextRange> m_Composition;
        std::vector<PlacedSpan> m_ComposingAnnotations;
        std::optional<std::pair<TextHighlightType, TextRange>> m_Highlight;
        std::unique_ptr<TextStyleBuffer> m_Styles;
        TextStyleBuffer& RequireStyleBuffer();
        void RequireValidStyleRange(TextRange range) const;
        template<typename T> TrackedRange<T> AddAnnotationToBuffer(T style, TextRange range, ExpandPolicy policy);
    };

    // findCommonPrefixAndSuffix: the differing middle of a and b, or false when equal.
    bool FindCommonPrefixAndSuffix(std::u16string_view a, std::u16string_view b, int& aStart, int& aEnd, int& bStart, int& bEnd);

}
