#include "kiln/foundation/text/input/TextFieldBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace kiln::foundation {

    std::u16string ToUtf16(std::string_view s) {
        std::u16string out;
        for (std::size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            char32_t cp;
            std::size_t len;
            if (c < 0x80) { cp = c; len = 1; }
            else if (c < 0xE0) { cp = c & 0x1F; len = 2; }
            else if (c < 0xF0) { cp = c & 0x0F; len = 3; }
            else { cp = c & 0x07; len = 4; }
            for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
            i += len;
            if (cp >= 0x10000) { cp -= 0x10000; out.push_back(char16_t(0xD800 + (cp >> 10))); out.push_back(char16_t(0xDC00 + (cp & 0x3FF))); }
            else out.push_back(char16_t(cp));
        }
        return out;
    }

    std::string ToUtf8(std::u16string_view s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            char32_t cp = s[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                ++i;
            }
            if (cp < 0x80) out.push_back(char(cp));
            else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3F))); }
            else if (cp < 0x10000) { out.push_back(char(0xE0 | (cp >> 12))); out.push_back(char(0x80 | ((cp >> 6) & 0x3F))); out.push_back(char(0x80 | (cp & 0x3F))); }
            else { out.push_back(char(0xF0 | (cp >> 18))); out.push_back(char(0x80 | ((cp >> 12) & 0x3F))); out.push_back(char(0x80 | ((cp >> 6) & 0x3F))); out.push_back(char(0x80 | (cp & 0x3F))); }
        }
        return out;
    }

    TextRange TextRange::CoerceIn(int lo, int hi) const {
        const int s = std::clamp(start, lo, hi), e = std::clamp(end, lo, hi);
        return { s, e };
    }

    // --- TextFieldCharSequence ----------------------------------------------------------------

    TextFieldCharSequence::TextFieldCharSequence(std::u16string t, TextRange sel, std::optional<TextRange> comp,
                                                 std::optional<std::pair<TextHighlightType, TextRange>> hl,
                                                 std::vector<PlacedSpan> composing, std::vector<PlacedAnnotation> output)
        : text(std::move(t)), composingAnnotations(std::move(composing)), outputAnnotations(std::move(output)) {
        const int n = int(text.size());
        selection = sel.CoerceIn(0, n);
        if (comp) composition = comp->CoerceIn(0, n);
        if (hl) highlight = std::pair{ hl->first, hl->second.CoerceIn(0, n) };
    }

    std::u16string_view TextFieldCharSequence::TextBeforeSelection(int maxChars) const {
        const int start = std::max(0, selection.Min() - std::max(0, maxChars));
        return std::u16string_view(text).substr(std::size_t(start), std::size_t(selection.Min() - start));
    }
    std::u16string_view TextFieldCharSequence::TextAfterSelection(int maxChars) const {
        const long long end = std::min<long long>((long long)selection.Max() + std::max(0, maxChars), Length());
        return std::u16string_view(text).substr(std::size_t(selection.Max()), std::size_t(end - selection.Max()));
    }
    std::u16string_view TextFieldCharSequence::SelectedText() const {
        return std::u16string_view(text).substr(std::size_t(selection.Min()), std::size_t(selection.Length()));
    }
    bool TextFieldCharSequence::operator==(const TextFieldCharSequence& o) const {
        return selection == o.selection && composition == o.composition && highlight == o.highlight &&
               composingAnnotations == o.composingAnnotations && text == o.text;
    }

    // --- ChangeTracker ------------------------------------------------------------------------

    void ChangeTracker::TrackChange(int preStart, int preEnd, int postLength, bool hardware) {
        if (preStart == preEnd && postLength == 0) return;
        const int preMin = std::min(preStart, preEnd), preMax = std::max(preStart, preEnd);
        bool recorded = false;
        const int postDelta = postLength - (preMax - preMin);
        std::optional<std::size_t> merged;
        std::vector<Change> temp;
        temp.reserve(m_Changes.size() + 1);
        std::vector<Change> source = m_Changes;   // merged changes are edited in place below
        Change mergedChange{};
        for (std::size_t i = 0; i < source.size(); ++i) {
            Change change = source[i];
            auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
            if (in(change.preStart, preMin, preMax) || in(change.preEnd, preMin, preMax) ||
                in(preMin, change.preStart, change.preEnd) || in(preMax, change.preStart, change.preEnd)) {
                if (!merged) {
                    merged = i;
                    mergedChange = change;
                } else {
                    mergedChange.preEnd = change.preEnd;
                    mergedChange.originalEnd = change.originalEnd;
                    mergedChange.isFromHardwareSource = mergedChange.isFromHardwareSource || change.isFromHardwareSource;
                }
                continue;
            }
            if (change.preStart > preMax && !recorded) {
                AppendNewChange(merged ? &mergedChange : nullptr, preMin, preMax, postDelta, hardware, temp);
                recorded = true;
            }
            if (recorded) {
                change.preStart += postDelta;
                change.preEnd += postDelta;
            }
            temp.push_back(change);
        }
        if (!recorded) AppendNewChange(merged ? &mergedChange : nullptr, preMin, preMax, postDelta, hardware, temp);
        m_Changes = std::move(temp);
    }

    void ChangeTracker::AppendNewChange(Change* merged, int preMin, int preMax, int postDelta, bool hardware, std::vector<Change>& temp) {
        int originalDelta = temp.empty() ? 0 : temp.back().preEnd - temp.back().originalEnd;
        Change next;
        if (!merged) {
            const int originalStart = preMin - originalDelta;
            const int originalEnd = originalStart + (preMax - preMin);
            next = { preMin, preMax + postDelta, originalStart, originalEnd, hardware };
        } else {
            next = *merged;
            next.isFromHardwareSource = next.isFromHardwareSource || hardware;
            if (next.preStart > preMin) {
                next.preStart = preMin;
                next.originalStart = preMin;
            }
            if (preMax > next.preEnd) {
                originalDelta = next.preEnd - next.originalEnd;
                next.preEnd = preMax;
                next.originalEnd = preMax - originalDelta;
            }
            next.preEnd += postDelta;
        }
        temp.push_back(next);
    }

    // --- OffsetMappingCalculator --------------------------------------------------------------

    void OffsetMappingCalculator::RecordEditOperation(int sourceStart, int sourceEnd, int newLength) {
        const int sourceMin = std::min(sourceStart, sourceEnd);
        const int sourceMax = std::max(sourceMin, sourceEnd);
        const int sourceLength = sourceMax - sourceMin;
        if (sourceLength < 2 && sourceLength == newLength) return;
        m_Ops.push_back({ sourceMin, sourceLength, newLength });
    }

    TextRange OffsetMappingCalculator::Map(int offset, bool fromSource) const {
        int start = offset, end = offset;
        auto step = [&](int at, const Op& op) -> TextRange {
            const int srcLen = fromSource ? op.srcLen : op.destLen;
            const int destLen = fromSource ? op.destLen : op.srcLen;
            if (at < op.offset) return TextRange(at);
            if (at == op.offset) return srcLen == 0 ? TextRange(op.offset, op.offset + destLen) : TextRange(op.offset);
            if (at < op.offset + srcLen) return destLen == 0 ? TextRange(op.offset) : TextRange(op.offset, op.offset + destLen);
            return TextRange(at - srcLen + destLen);
        };
        auto apply = [&](const Op& op) {
            const TextRange s = step(start, op), e = step(end, op);
            start = std::min(s.start, e.start);
            end = std::max(s.end, e.end);
        };
        if (fromSource) for (const Op& op : m_Ops) apply(op);
        else for (auto it = m_Ops.rbegin(); it != m_Ops.rend(); ++it) apply(*it);
        return { start, end };
    }

    TextRange AdjustTextRange(TextRange original, int replaceStart, int replaceEnd, int insertedLength) {
        int selStart = original.Min(), selEnd = original.Max();
        if (selEnd < replaceStart) return original;
        if (selStart <= replaceStart && replaceEnd <= selEnd) {
            const int diff = insertedLength - (replaceEnd - replaceStart);
            if (selStart == selEnd) selStart += diff;
            selEnd += diff;
        } else if (selStart > replaceStart && selEnd < replaceEnd) {
            selStart = replaceStart + insertedLength;
            selEnd = replaceStart + insertedLength;
        } else if (selStart >= replaceEnd) {
            const int diff = insertedLength - (replaceEnd - replaceStart);
            selStart += diff;
            selEnd += diff;
        } else if (replaceStart < selStart) {
            selStart = replaceStart + insertedLength;
            selEnd += insertedLength - (replaceEnd - replaceStart);
        } else {
            selEnd = replaceStart;
        }
        return { selStart, selEnd };
    }

    bool FindCommonPrefixAndSuffix(std::u16string_view a, std::u16string_view b, int& aStart, int& aEnd, int& bStart, int& bEnd) {
        aStart = 0; aEnd = int(a.size()); bStart = 0; bEnd = int(b.size());
        if (!a.empty() && !b.empty()) {
            bool prefixFound = false, suffixFound = false;
            do {
                if (!prefixFound) {
                    if (a[std::size_t(aStart)] == b[std::size_t(bStart)]) { ++aStart; ++bStart; }
                    else prefixFound = true;
                }
                if (!suffixFound) {
                    if (a[std::size_t(aEnd - 1)] == b[std::size_t(bEnd - 1)]) { --aEnd; --bEnd; }
                    else suffixFound = true;
                }
            } while (aStart < aEnd && bStart < bEnd && !(prefixFound && suffixFound));
        }
        return !(aStart >= aEnd && bStart >= bEnd);
    }

    // --- TextFieldBuffer ----------------------------------------------------------------------

    TextFieldBuffer::TextFieldBuffer(TextFieldCharSequence initial, const ChangeTracker* initialChanges,
                                     std::optional<TextFieldCharSequence> original, OffsetMappingCalculator* offsetMapping)
        : m_Text(initial.text), m_Original(original ? std::move(*original) : initial), m_OffsetMapping(offsetMapping),
          m_Selection(initial.selection), m_Composition(initial.composition),
          m_ComposingAnnotations(initial.composingAnnotations) {
        if (initialChanges) m_Changes = *initialChanges;
        canCallAddStyle = offsetMapping != nullptr;
    }

    void TextFieldBuffer::SetSelection(TextRange r) {
        if (r.Min() < 0 || r.Max() > Length()) throw std::out_of_range("selection outside the text");
        m_Selection = r;
        m_Highlight.reset();
    }

    void TextFieldBuffer::SetSelectionCoerced(int start, int end) {
        SetSelection({ std::clamp(start, 0, Length()), std::clamp(end, 0, Length()) });
    }

    void TextFieldBuffer::PlaceCursorBeforeCharAt(int index) {
        if (index < 0 || index > Length()) throw std::out_of_range("cursor outside the text");
        m_Selection = TextRange(index);
    }

    void TextFieldBuffer::PlaceCursorAfterCharAt(int index) {
        if (index < -1 || index >= Length()) throw std::out_of_range("cursor outside the text");
        m_Selection = TextRange(std::min(index + 1, Length()));
    }

    void TextFieldBuffer::SetCompositionRange(std::optional<TextRange> r) {
        if (!r || r->Collapsed()) {
            m_Composition.reset();
            m_ComposingAnnotations.clear();
        } else {
            m_Composition = r;
        }
    }

    void TextFieldBuffer::SetComposition(int start, int end, const std::vector<PlacedSpan>* annotations) {
        if (start < 0 || start > Length() || end < 0 || end > Length()) throw std::out_of_range("composition outside the text");
        if (start >= end) throw std::invalid_argument("reversed or empty composition");
        SetCompositionRange(TextRange(start, end));
        m_ComposingAnnotations.clear();
        if (annotations)
            for (PlacedSpan a : *annotations) {
                a.start += start;
                a.end += start;
                m_ComposingAnnotations.push_back(std::move(a));
            }
    }

    void TextFieldBuffer::SetHighlight(TextHighlightType type, int start, int end) {
        if (start >= end) throw std::invalid_argument("reversed or empty highlight");
        m_Highlight = std::pair{ type, TextRange(std::clamp(start, 0, Length()), std::clamp(end, 0, Length())) };
    }

    void TextFieldBuffer::OnTextWillChange(int start, int end, int newLength, bool hardware) {
        m_Changes.TrackChange(start, end, newLength, hardware);
        if (m_OffsetMapping) m_OffsetMapping->RecordEditOperation(start, end, newLength);
        m_Selection = AdjustTextRange(m_Selection, start, end, newLength);
    }

    void TextFieldBuffer::Replace(int start, int end, std::u16string_view text, bool hardware) {
        if (start > end) throw std::invalid_argument("replace start after end");
        const int s = std::clamp(start, 0, Length()), e = std::clamp(end, 0, Length());
        OnTextWillChange(s, e, int(text.size()), hardware);
        m_Text.replace(std::size_t(s), std::size_t(e - s), text);
        CommitComposition();
        ClearHighlight();
    }

    void TextFieldBuffer::Append(std::u16string_view text) {
        OnTextWillChange(Length(), Length(), int(text.size()), false);
        m_Text.append(text);
    }

    void TextFieldBuffer::SetTextIfChanged(std::u16string_view text) {
        int aStart, aEnd, bStart, bEnd;
        if (FindCommonPrefixAndSuffix(m_Text, text, aStart, aEnd, bStart, bEnd))
            Replace(aStart, aEnd, text.substr(std::size_t(bStart), std::size_t(bEnd - bStart)));
    }

    void TextFieldBuffer::RevertAllChanges() {
        Replace(0, Length(), std::u16string(m_Original.text));
        SetSelection(m_Original.selection);
        m_Changes.ClearChanges();
    }

    void TextFieldBuffer::AddStyle(text::SpanStyle style, int start, int end) {
        if (!canCallAddStyle) throw std::logic_error("styles can be added only from an OutputTransformation");
        m_OutputAnnotations.push_back({ text::Annotation(std::move(style)), start, end, {} });
    }

    void TextFieldBuffer::AddStyle(text::ParagraphStyle style, int start, int end) {
        if (!canCallAddStyle) throw std::logic_error("styles can be added only from an OutputTransformation");
        m_OutputAnnotations.push_back({ text::Annotation(std::move(style)), start, end, {} });
    }

    TextFieldCharSequence TextFieldBuffer::ToTextFieldCharSequence() const {
        return ToTextFieldCharSequence(m_Selection, m_Composition);
    }

    TextFieldCharSequence TextFieldBuffer::ToTextFieldCharSequence(TextRange selection, std::optional<TextRange> composition) const {
        return TextFieldCharSequence(m_Text, selection, composition, std::nullopt, m_ComposingAnnotations, m_OutputAnnotations);
    }

}
