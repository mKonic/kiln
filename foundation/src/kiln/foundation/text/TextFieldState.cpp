#include "kiln/foundation/text/TextFieldState.h"

#include <algorithm>

namespace kiln::foundation {

    std::u16string ToUtf16(std::string_view s) {
        std::u16string out;
        for (std::size_t i = 0; i < s.size();) {
            const unsigned char c = s[i];
            char32_t cp;
            int len;
            if (c < 0x80) { cp = c; len = 1; }
            else if (c < 0xE0) { cp = c & 0x1F; len = 2; }
            else if (c < 0xF0) { cp = c & 0x0F; len = 3; }
            else { cp = c & 0x07; len = 4; }
            for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
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

    namespace {
        bool IsHigh(char16_t c) { return c >= 0xD800 && c < 0xDC00; }
        bool IsLow(char16_t c) { return c >= 0xDC00 && c < 0xE000; }
    }

    TextFieldState::TextFieldState(std::string_view initial) {
        TextFieldValue v;
        v.text = ToUtf16(initial);
        v.selection = { int(v.text.size()), int(v.text.size()) };
        m_Value.Set(std::move(v));
    }

    void TextFieldState::Set(TextFieldValue v) {
        const int n = int(v.text.size());
        v.selection = { std::clamp(v.selection.start, 0, n), std::clamp(v.selection.end, 0, n) };
        if (v.composition) {
            TextRange c{ std::clamp(v.composition->Min(), 0, n), std::clamp(v.composition->Max(), 0, n) };
            if (c.Collapsed()) v.composition.reset(); else v.composition = c;
        }
        if (v == m_Value.Peek()) return;
        ++m_Version;
        m_Value.Set(std::move(v));
    }

    void TextFieldState::SetText(std::string_view utf8) {
        TextFieldValue v;
        v.text = ToUtf16(utf8);
        v.selection = { int(v.text.size()), int(v.text.size()) };
        Set(std::move(v));
    }

    // Replacing [start, end) moves the selection and composition like EditingBuffer.replace.
    void TextFieldState::Replace(int start, int end, std::u16string_view text) {
        TextFieldValue v = m_Value.Peek();
        v.text.replace(std::size_t(start), std::size_t(end - start), text);
        const int delta = int(text.size()) - (end - start);
        auto shift = [&](int p) { return p >= end ? p + delta : p > start ? start : p; };
        v.selection = { shift(v.selection.start), shift(v.selection.end) };
        if (v.composition) v.composition = TextRange{ shift(v.composition->start), shift(v.composition->end) };
        m_Value.Set(std::move(v));
    }

    void TextFieldState::CommitText(std::u16string_view text, int newCursor) {
        TextFieldValue v = m_Value.Peek();
        // CommitTextCommand: replace the composition if any, else the selection.
        int start, end;
        if (v.composition) { start = v.composition->Min(); end = v.composition->Max(); }
        else { start = v.selection.Min(); end = v.selection.Max(); }
        v.text.replace(std::size_t(start), std::size_t(end - start), text);
        v.composition.reset();
        // newCursorPosition > 0: relative to the end of the inserted text (1 = right after it);
        // <= 0: relative to its start.
        const int cursor = newCursor > 0 ? start + int(text.size()) + newCursor - 1 : start + newCursor;
        const int c = std::clamp(cursor, 0, int(v.text.size()));
        v.selection = { c, c };
        Set(std::move(v));
    }

    void TextFieldState::SetComposingText(std::u16string_view text, int newCursor) {
        TextFieldValue v = m_Value.Peek();
        int start, end;
        if (v.composition) { start = v.composition->Min(); end = v.composition->Max(); }
        else { start = v.selection.Min(); end = v.selection.Max(); }
        v.text.replace(std::size_t(start), std::size_t(end - start), text);
        if (text.empty()) v.composition.reset();
        else v.composition = TextRange{ start, start + int(text.size()) };
        const int cursor = newCursor > 0 ? start + int(text.size()) + newCursor - 1 : start + newCursor;
        const int c = std::clamp(cursor, 0, int(v.text.size()));
        v.selection = { c, c };
        Set(std::move(v));
    }

    void TextFieldState::SetComposingRegion(int start, int end) {
        TextFieldValue v = m_Value.Peek();
        const int n = int(v.text.size());
        start = std::clamp(start, 0, n);
        end = std::clamp(end, 0, n);
        if (start == end) v.composition.reset();
        else v.composition = TextRange{ std::min(start, end), std::max(start, end) };
        Set(std::move(v));
    }

    void TextFieldState::FinishComposingText() {
        TextFieldValue v = m_Value.Peek();
        v.composition.reset();
        Set(std::move(v));
    }

    void TextFieldState::DeleteSurroundingText(int before, int after) {
        // DeleteSurroundingTextCommand: after the selection first, then before; never splits a
        // surrogate pair at either boundary.
        const TextFieldValue& v = m_Value.Peek();
        const int n = int(v.text.size());
        int afterEnd = std::min(n, v.selection.Max() + after);
        if (afterEnd < n && afterEnd > 0 && IsLow(v.text[afterEnd]) && IsHigh(v.text[afterEnd - 1])) ++afterEnd;
        const int selMax = v.selection.Max();
        Replace(selMax, afterEnd, {});
        const TextFieldValue& w = m_Value.Peek();
        int beforeStart = std::max(0, w.selection.Min() - before);
        if (beforeStart > 0 && IsLow(w.text[beforeStart]) && IsHigh(w.text[beforeStart - 1])) --beforeStart;
        const int selMin = w.selection.Min();
        Replace(beforeStart, selMin, {});
        TextFieldValue done = m_Value.Peek();
        ++m_Version;
        m_Value.Set(std::move(done));
    }

    void TextFieldState::DeleteSurroundingTextInCodePoints(int before, int after) {
        const TextFieldValue& v = m_Value.Peek();
        int b = 0, i = v.selection.Min();
        for (int k = 0; k < before && i > 0; ++k) { --i; ++b; if (i > 0 && IsLow(v.text[i]) && IsHigh(v.text[i - 1])) { --i; ++b; } }
        int a = 0, j = v.selection.Max();
        const int n = int(v.text.size());
        for (int k = 0; k < after && j < n; ++k) { ++a; if (IsHigh(v.text[j]) && j + 1 < n && IsLow(v.text[j + 1])) { ++j; ++a; } ++j; }
        DeleteSurroundingText(b, a);
    }

    void TextFieldState::SetSelection(int start, int end) {
        TextFieldValue v = m_Value.Peek();
        const int n = int(v.text.size());
        v.selection = { std::clamp(start, 0, n), std::clamp(end, 0, n) };
        Set(std::move(v));
    }

    void TextFieldState::Backspace() {
        const TextFieldValue& v = m_Value.Peek();
        if (!v.selection.Collapsed()) { CommitText({}, 1); return; }
        int c = v.selection.start;
        if (c == 0) return;
        int start = c - 1;
        if (start > 0 && IsLow(v.text[start]) && IsHigh(v.text[start - 1])) --start;
        TextFieldValue w = v;
        w.composition.reset();
        w.text.erase(std::size_t(start), std::size_t(c - start));
        w.selection = { start, start };
        Set(std::move(w));
    }

    void TextFieldState::DeleteForward() {
        const TextFieldValue& v = m_Value.Peek();
        if (!v.selection.Collapsed()) { CommitText({}, 1); return; }
        const int c = v.selection.start, n = int(v.text.size());
        if (c >= n) return;
        int end = c + 1;
        if (end < n && IsHigh(v.text[c]) && IsLow(v.text[end])) ++end;
        TextFieldValue w = v;
        w.composition.reset();
        w.text.erase(std::size_t(c), std::size_t(end - c));
        Set(std::move(w));
    }

    void TextFieldState::MoveCursor(int delta, bool extend) {
        const TextFieldValue& v = m_Value.Peek();
        const int n = int(v.text.size());
        int c = v.selection.end;
        if (!extend && !v.selection.Collapsed()) c = delta < 0 ? v.selection.Min() : v.selection.Max();
        else {
            for (int k = 0; k < std::abs(delta); ++k) {
                if (delta < 0 && c > 0) { --c; if (c > 0 && IsLow(v.text[c]) && IsHigh(v.text[c - 1])) --c; }
                if (delta > 0 && c < n) { ++c; if (c < n && IsLow(v.text[c]) && IsHigh(v.text[c - 1])) ++c; }
            }
        }
        TextFieldValue w = v;
        w.composition.reset();
        w.selection = extend ? TextRange{ v.selection.start, c } : TextRange{ c, c };
        Set(std::move(w));
    }

}
