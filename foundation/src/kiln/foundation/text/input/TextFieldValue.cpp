#include "kiln/foundation/text/input/TextFieldValue.h"
#include <kiln/Function.h>

#include "kiln/TextBackend.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace kiln::foundation {

    TextFieldValue::TextFieldValue(text::AnnotatedString t, TextRange selection, std::optional<TextRange> composition)
        : m_Text(std::move(t)), m_Selection(selection.CoerceIn(0, m_Text.Length())) {
        if (composition) m_Composition = composition->CoerceIn(0, m_Text.Length());
    }

    namespace {
        // addExactOrElse / subtractExactOrElse: saturating.
        int AddSat(int a, int b, int fallback) { const long long r = (long long)a + b; return r > std::numeric_limits<int>::max() ? fallback : int(r); }
        int SubSat(int a, int b, int fallback) { const long long r = (long long)a - b; return r < std::numeric_limits<int>::min() ? fallback : int(r); }
    }

    text::AnnotatedString GetTextBeforeSelection(const TextFieldValue& v, int maxChars) {
        const int min = v.Selection().Min();
        return v.AnnotatedString().SubSequence(std::max(0, SubSat(min, maxChars, 0)), min);
    }
    text::AnnotatedString GetTextAfterSelection(const TextFieldValue& v, int maxChars) {
        const int max = v.Selection().Max(), len = v.AnnotatedString().Length();
        return v.AnnotatedString().SubSequence(max, std::min(AddSat(max, maxChars, len), len));
    }
    text::AnnotatedString GetSelectedText(const TextFieldValue& v) { return v.AnnotatedString().SubSequence(v.Selection().Min(), v.Selection().Max()); }
    std::string Substring(const text::AnnotatedString& t, TextRange r) { return t.SubSequence(r.Min(), r.Max()).Text(); }

    // --- EditingBuffer ---------------------------------------------------------------------

    namespace {
        void CheckRange(int start, int end, int length, bool allowEmpty = true) {
            if (start < 0 || start > length) throw std::out_of_range("start offset is outside of text region");
            if (end < 0 || end > length) throw std::out_of_range("end offset is outside of text region");
            if (allowEmpty ? start > end : start >= end) throw std::invalid_argument("Do not set reversed range");
        }
        // updateRangeAfterDelete.
        TextRange AfterDelete(TextRange target, TextRange deleted) {
            int tMin = target.Min(), tMax = target.Max();
            const bool intersects = deleted.Min() < tMax && tMin < deleted.Max();
            if (intersects) {
                if (deleted.Contains(target)) { tMin = deleted.Min(); tMax = tMin; }
                else if (target.Contains(deleted)) tMax -= deleted.Length();
                else if (deleted.Min() <= tMin && tMin < deleted.Max()) { tMin = deleted.Min(); tMax -= deleted.Length(); }
                else tMax = deleted.Min();
            } else if (tMax > deleted.Min()) {
                tMin -= deleted.Length();
                tMax -= deleted.Length();
            }
            return { tMin, tMax };
        }
    }

    EditingBuffer::EditingBuffer(const text::AnnotatedString& t, TextRange selection, const TextBackend* backend)
        : m_Text(ToUtf16(t.Text())), m_SelStart(selection.Min()), m_SelEnd(selection.Max()), m_Backend(backend) {
        CheckRange(m_SelStart, m_SelEnd, Length());
    }

    void EditingBuffer::Replace(int start, int end, std::u16string_view t) {
        CheckRange(start, end, Length());
        m_Text.replace(std::size_t(start), std::size_t(end - start), t);
        m_SelStart = m_SelEnd = start + int(t.size());
        m_CompStart = m_CompEnd = kNowhere;
    }

    void EditingBuffer::Delete(int start, int end) {
        const TextRange deleted(start, end);
        m_Text.erase(std::size_t(deleted.Min()), std::size_t(deleted.Length()));
        const TextRange sel = AfterDelete({ m_SelStart, m_SelEnd }, deleted);
        m_SelStart = sel.Min();
        m_SelEnd = sel.Max();
        if (HasComposition()) {
            const TextRange comp = AfterDelete({ m_CompStart, m_CompEnd }, deleted);
            if (comp.Collapsed()) CommitComposition();
            else { m_CompStart = comp.Min(); m_CompEnd = comp.Max(); }
        }
    }

    void EditingBuffer::SetSelection(int start, int end) {
        CheckRange(start, end, Length());
        m_SelStart = start;
        m_SelEnd = end;
    }

    void EditingBuffer::SetComposition(int start, int end) {
        CheckRange(start, end, Length(), false);
        m_CompStart = start;
        m_CompEnd = end;
    }

    void EditingBuffer::CancelComposition() {
        Replace(m_CompStart, m_CompEnd, u"");
        m_CompStart = m_CompEnd = kNowhere;
    }

    void EditingBuffer::CommitComposition() { m_CompStart = m_CompEnd = kNowhere; }

    std::string EditingBuffer::ToString() const { return ToUtf8(m_Text); }

    int EditingBuffer::PrecedingBreak(int index) const {
        return m_Backend ? const_cast<TextBackend*>(m_Backend)->CharacterBreak(m_Text, index, false) : TextBackend::SurrogateCharacterBreak(m_Text, index, false);
    }
    int EditingBuffer::FollowingBreak(int index) const {
        return m_Backend ? const_cast<TextBackend*>(m_Backend)->CharacterBreak(m_Text, index, true) : TextBackend::SurrogateCharacterBreak(m_Text, index, true);
    }

    // --- commands --------------------------------------------------------------------------

    namespace {
        void PlaceCursor(EditingBuffer& b, int newCursorPosition, int textLength) {
            const int cursor = b.Cursor();
            const int inBuffer = newCursorPosition > 0 ? cursor + newCursorPosition - 1 : cursor + newCursorPosition - textLength;
            b.SetCursor(std::clamp(inBuffer, 0, b.Length()));
        }
        bool SurrogatePair(char16_t high, char16_t low) { return high >= 0xD800 && high < 0xDC00 && low >= 0xDC00 && low < 0xE000; }
    }

    void CommitTextCommand::ApplyTo(EditingBuffer& b) const {
        const std::u16string t = ToUtf16(text);
        if (b.HasComposition()) b.Replace(b.CompositionStart(), b.CompositionEnd(), t);
        else b.Replace(b.SelectionStart(), b.SelectionEnd(), t);
        PlaceCursor(b, newCursorPosition, int(t.size()));
    }

    void SetComposingRegionCommand::ApplyTo(EditingBuffer& b) const {
        if (b.HasComposition()) b.CommitComposition();
        const int s = std::clamp(start, 0, b.Length()), e = std::clamp(end, 0, b.Length());
        if (s < e) b.SetComposition(s, e);
        else if (s > e) b.SetComposition(e, s);
    }

    void SetComposingTextCommand::ApplyTo(EditingBuffer& b) const {
        const std::u16string t = ToUtf16(text);
        const int at = b.HasComposition() ? b.CompositionStart() : b.SelectionStart();
        if (b.HasComposition()) b.Replace(b.CompositionStart(), b.CompositionEnd(), t);
        else b.Replace(b.SelectionStart(), b.SelectionEnd(), t);
        if (!t.empty()) b.SetComposition(at, at + int(t.size()));
        PlaceCursor(b, newCursorPosition, int(t.size()));
    }

    void DeleteSurroundingTextCommand::ApplyTo(EditingBuffer& b) const {
        const int end = AddSat(b.SelectionEnd(), lengthAfterCursor, b.Length());
        b.Delete(b.SelectionEnd(), std::min(end, b.Length()));
        const int start = SubSat(b.SelectionStart(), lengthBeforeCursor, 0);
        b.Delete(std::max(0, start), b.SelectionStart());
    }

    void DeleteSurroundingTextInCodePointsCommand::ApplyTo(EditingBuffer& b) const {
        int before = 0;
        for (int i = 0; i < lengthBeforeCursor; ++i) {
            ++before;
            if (b.SelectionStart() > before) {
                if (SurrogatePair(b[b.SelectionStart() - before - 1], b[b.SelectionStart() - before])) ++before;
            } else {
                before = b.SelectionStart();
                break;
            }
        }
        int after = 0;
        for (int i = 0; i < lengthAfterCursor; ++i) {
            ++after;
            if (b.SelectionEnd() + after < b.Length()) {
                if (SurrogatePair(b[b.SelectionEnd() + after - 1], b[b.SelectionEnd() + after])) ++after;
            } else {
                after = b.Length() - b.SelectionEnd();
                break;
            }
        }
        b.Delete(b.SelectionEnd(), b.SelectionEnd() + after);
        b.Delete(b.SelectionStart() - before, b.SelectionStart());
    }

    void SetSelectionCommand::ApplyTo(EditingBuffer& b) const {
        const int s = std::clamp(start, 0, b.Length()), e = std::clamp(end, 0, b.Length());
        if (s < e) b.SetSelection(s, e);
        else b.SetSelection(e, s);
    }

    void BackspaceCommand::ApplyTo(EditingBuffer& b) const {
        if (b.HasComposition()) { b.Delete(b.CompositionStart(), b.CompositionEnd()); return; }
        if (b.Cursor() == -1) {
            const int s = b.SelectionStart(), e = b.SelectionEnd();
            b.SetCursor(s);
            b.Delete(s, e);
            return;
        }
        if (b.Cursor() == 0) return;
        const int prev = b.PrecedingBreak(b.Cursor());
        b.Delete(prev, b.Cursor());
    }

    void MoveCursorCommand::ApplyTo(EditingBuffer& b) const {
        if (b.Cursor() == -1) b.SetCursor(b.SelectionStart());
        int cursor = b.SelectionStart();
        if (amount > 0) {
            for (int i = 0; i < amount; ++i) { const int n = b.FollowingBreak(cursor); if (n == -1) break; cursor = n; }
        } else {
            for (int i = 0; i < -amount; ++i) { const int p = b.PrecedingBreak(cursor); if (p == -1) break; cursor = p; }
        }
        b.SetCursor(cursor);
    }

    // --- EditProcessor ---------------------------------------------------------------------

    EditProcessor::EditProcessor(const TextBackend* backend)
        : m_State(text::AnnotatedString(), TextRange(0), std::nullopt), m_Buffer(std::make_unique<EditingBuffer>(text::AnnotatedString(), TextRange(0), backend)),
          m_Backend(backend) {}

    void EditProcessor::Reset(const TextFieldValue& value, const kiln::Function<void(const TextFieldValue&, const TextFieldValue&)>& onUpdate) {
        bool textChanged = false, selectionChanged = false;
        const bool compositionChanged = value.Composition() != m_Buffer->Composition();
        if (m_State.Text() != value.Text()) {
            m_Buffer = std::make_unique<EditingBuffer>(value.AnnotatedString(), value.Selection(), m_Backend);
            textChanged = true;
        } else if (!(m_State.Selection() == value.Selection())) {
            m_Buffer->SetSelection(value.Selection().Min(), value.Selection().Max());
            selectionChanged = true;
        }
        if (!value.Composition()) m_Buffer->CommitComposition();
        else if (!value.Composition()->Collapsed()) m_Buffer->SetComposition(value.Composition()->Min(), value.Composition()->Max());
        TextFieldValue next = value;
        if (textChanged || (!selectionChanged && compositionChanged)) {
            m_Buffer->CommitComposition();
            next = value.CopyWithComposition(std::nullopt);
        }
        const TextFieldValue old = m_State;
        m_State = next;
        if (onUpdate) onUpdate(old, next);
    }

    TextFieldValue EditProcessor::Apply(const std::vector<EditCommandPtr>& commands) {
        for (const auto& c : commands) if (c) c->ApplyTo(*m_Buffer);
        TextRange selection = m_Buffer->Selection();
        if (m_State.Selection().Reversed()) selection = { selection.Max(), selection.Min() };
        m_State = TextFieldValue(m_Buffer->ToString(), selection, m_Buffer->Composition());
        return m_State;
    }

}
