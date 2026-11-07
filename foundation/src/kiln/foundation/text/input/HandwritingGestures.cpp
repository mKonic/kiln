#include "kiln/foundation/text/input/HandwritingGestures.h"

#include "kiln/TextBackend.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>

namespace kiln::foundation {

    using G = ui::HandwritingGesture;

    namespace {
        constexpr char32_t kLineFeed = '\n', kNbsp = 0xA0;

        int CharType(TextBackend* b, char32_t c) { return b ? b->CharacterType(c) : TextBackend::SimpleCharacterType(c); }
        bool IsNewline(TextBackend* b, char32_t c) {
            const int t = CharType(b, c);
            return t == 14 || t == 13 || c == kLineFeed;   // PARAGRAPH_SEPARATOR, LINE_SEPARATOR
        }
        // Character.isWhitespace, plus the no-break space.
        bool IsWhitespace(TextBackend* b, char32_t c) {
            if (c == kNbsp) return true;
            if (c == '\t' || c == '\n' || c == 0x0B || c == 0x0C || c == '\r' || (c >= 0x1C && c <= 0x1F)) return true;
            const int t = CharType(b, c);
            return (t == 12 || t == 13 || t == 14) && c != 0x2007 && c != 0x202F;
        }
        bool IsWhitespaceExceptNewline(TextBackend* b, char32_t c) { return IsWhitespace(b, c) && !IsNewline(b, c); }
        bool IsPunctuation(TextBackend* b, char32_t c) {
            const int t = CharType(b, c);
            return t == 23 || t == 20 || t == 22 || t == 30 || t == 29 || t == 24 || t == 21;
        }
        char32_t CodePointAt(std::u16string_view t, int i) {
            const char16_t c = t[std::size_t(i)];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < int(t.size()) && t[std::size_t(i + 1)] >= 0xDC00 && t[std::size_t(i + 1)] < 0xE000)
                return 0x10000 + ((char32_t(c) - 0xD800) << 10) + (t[std::size_t(i + 1)] - 0xDC00);
            return c;
        }
        char32_t CodePointBefore(std::u16string_view t, int i) {
            const char16_t c = t[std::size_t(i - 1)];
            if (c >= 0xDC00 && c < 0xE000 && i - 2 >= 0 && t[std::size_t(i - 2)] >= 0xD800 && t[std::size_t(i - 2)] < 0xDC00)
                return 0x10000 + ((char32_t(t[std::size_t(i - 2)]) - 0xD800) << 10) + (c - 0xDC00);
            return c;
        }
        int CharCount(char32_t c) { return c >= 0x10000 ? 2 : 1; }

        // adjustHandwritingDeleteGestureRange: a deleted word takes the whitespace on one side.
        TextRange AdjustDeleteRange(TextRange r, std::u16string_view text, TextBackend* b) {
            int start = r.Min(), end = r.Max();
            char32_t before = start > 0 ? CodePointBefore(text, start) : kLineFeed;
            char32_t atEnd = end < int(text.size()) ? CodePointAt(text, end) : kLineFeed;
            if (IsWhitespaceExceptNewline(b, before) && (IsWhitespace(b, atEnd) || IsPunctuation(b, atEnd))) {
                do {
                    start -= CharCount(before);
                    if (start == 0) break;
                    before = CodePointBefore(text, start);
                } while (IsWhitespaceExceptNewline(b, before));
                return TextRange(start, end);
            }
            if (IsWhitespaceExceptNewline(b, atEnd) && (IsWhitespace(b, before) || IsPunctuation(b, before))) {
                do {
                    end += CharCount(atEnd);
                    if (end == int(text.size())) break;
                    atEnd = CodePointAt(text, end);
                } while (IsWhitespaceExceptNewline(b, atEnd));
                return TextRange(start, end);
            }
            return r;
        }

        TextRange RangeOfWhitespaces(std::u16string_view text, int offset, TextBackend* b) {
            int start = offset, end = offset;
            while (start > 0) {
                const char32_t c = CodePointBefore(text, start);
                if (!IsWhitespace(b, c)) break;
                start -= CharCount(c);
            }
            while (end < int(text.size())) {
                const char32_t c = CodePointAt(text, end);
                if (!IsWhitespace(b, c)) break;
                end += CharCount(c);
            }
            return TextRange(start, end);
        }

        // screenToLocal: root pixels into the text layout node.
        std::optional<Offset> ToLocal(const TextLayoutState& l, Offset p) {
            const auto& c = l.textLayoutNodeCoordinates;
            if (!c || !c->IsAttached()) return std::nullopt;
            return ui::LayoutNode::UnmapPoint(c->ToRoot(), p);
        }
        std::optional<Rect> ToLocal(const TextLayoutState& l, const Rect& r) {
            const auto origin = ToLocal(l, Offset{ 0, 0 });
            if (!origin) return std::nullopt;
            return Rect{ r.left + origin->x, r.top + origin->y, r.right + origin->x, r.bottom + origin->y };
        }

        text::TextGranularity Granularity(const G& g) {
            return g.granularity == G::Granularity::Word ? text::TextGranularity::Word : text::TextGranularity::Character;
        }

        TextRange RangeForScreenRect(const TextLayoutState& l, const Rect& r, text::TextGranularity granularity) {
            const auto layout = l.PeekLayoutResult();
            const auto local = ToLocal(l, r);
            if (!layout || !local) return TextRange(0);
            const auto range = layout->RangeForRect(*local, granularity, text::inclusion::ContainsCenter());
            return TextRange(range.first, range.second);
        }
        TextRange RangeForScreenRects(const TextLayoutState& l, const Rect& a, const Rect& b, text::TextGranularity granularity) {
            const TextRange start = RangeForScreenRect(l, a, granularity);
            if (start.Collapsed()) return TextRange(0);
            const TextRange end = RangeForScreenRect(l, b, granularity);
            if (end.Collapsed()) return TextRange(0);
            return TextRange(std::min(start.Min(), end.Min()), std::max(start.Max(), end.Max()));   // enclosure
        }

        // getLineForHandwritingGesture: the line under the point within the line margin, or -1.
        int LineForGesture(const text::TextLayoutResult& layout, Offset p, float margin) {
            const int line = layout.LineForVerticalPosition(p.y);
            if (p.y < layout.LineTop(line) - margin || p.y > layout.LineBottom(line) + margin) return -1;
            if (p.x < -margin || p.x > layout.Paragraphs().Width() + margin) return -1;
            return line;
        }
        int OffsetForGesture(const TextLayoutState& l, Offset screenPoint, float margin) {
            const auto layout = l.PeekLayoutResult();
            const auto p = ToLocal(l, screenPoint);
            if (!layout || !p) return -1;
            const int line = LineForGesture(*layout, *p, margin);
            if (line == -1) return -1;
            return layout->OffsetForPosition({ p->x, (layout->LineTop(line) + layout->LineBottom(line)) / 2 });
        }
        bool IsBidiBoundary(const text::TextLayoutResult& l, int offset) {
            const int line = l.LineForOffset(offset);
            if (offset == l.LineStart(line) || offset == l.LineEnd(line)) return l.ParagraphDirection(offset) != l.BidiRunDirection(offset);
            return l.BidiRunDirection(offset) != l.BidiRunDirection(offset - 1);
        }

        int Fallback(const HandwritingGestureContext& c, const G& g) {
            c.state.EditUntransformedTextAsUser([](TextFieldBuffer& b) { b.ClearHighlight(); });
            if (!g.fallbackText) return G::kResultFailed;
            c.state.ReplaceSelectedText(*g.fallbackText, true);
            return G::kResultFallback;
        }
        void PerformDeletion(const HandwritingGestureContext& c, TextRange range, bool adjust) {
            const TextRange r = adjust ? AdjustDeleteRange(range, c.state.VisualText().text, c.backend) : range;
            c.state.ReplaceText(u"", r);
        }
        void HighlightRange(TransformedTextFieldState& s, TextRange r, TextHighlightType type) {
            if (r.Collapsed()) s.EditUntransformedTextAsUser([](TextFieldBuffer& b) { b.ClearHighlight(); });
            else s.HighlightCharsIn(type, r);
        }
    }

    int PerformHandwritingGesture(const HandwritingGestureContext& c, const G& g) {
        switch (g.type) {
            case G::Type::Select:
            case G::Type::SelectRange: {
                const TextRange r = g.type == G::Type::Select ? RangeForScreenRect(c.layout, g.area, Granularity(g))
                                                              : RangeForScreenRects(c.layout, g.area, g.endArea, Granularity(g));
                if (r.Collapsed()) return Fallback(c, g);
                c.state.SelectCharsIn(r);
                if (c.updateSelectionState) c.updateSelectionState();
                return G::kResultSuccess;
            }
            case G::Type::Delete:
            case G::Type::DeleteRange: {
                const text::TextGranularity granularity = Granularity(g);
                const TextRange r = g.type == G::Type::Delete ? RangeForScreenRect(c.layout, g.area, granularity)
                                                              : RangeForScreenRects(c.layout, g.area, g.endArea, granularity);
                if (r.Collapsed()) return Fallback(c, g);
                PerformDeletion(c, r, granularity == text::TextGranularity::Word);
                return G::kResultSuccess;
            }
            case G::Type::JoinOrSplit: {
                if (c.state.IsOutputTransformed()) return G::kResultFailed;
                const int offset = OffsetForGesture(c.layout, g.point, c.lineMargin);
                const auto layout = c.layout.PeekLayoutResult();
                if (offset == -1 || (layout && IsBidiBoundary(*layout, offset))) return Fallback(c, g);
                const TextRange r = RangeOfWhitespaces(c.state.VisualText().text, offset, c.backend);
                if (r.Collapsed()) c.state.ReplaceText(u" ", r);
                else PerformDeletion(c, r, false);
                return G::kResultSuccess;
            }
            case G::Type::Insert: {
                const int offset = OffsetForGesture(c.layout, g.point, c.lineMargin);
                if (offset == -1) return Fallback(c, g);
                c.state.ReplaceText(g.textToInsert, TextRange(offset));
                return G::kResultSuccess;
            }
            case G::Type::RemoveSpace: {
                // getRangeForRemoveSpaceGesture: the characters the line between the points crosses.
                const auto layout = c.layout.PeekLayoutResult();
                const auto start = ToLocal(c.layout, g.point), end = ToLocal(c.layout, g.endPoint);
                TextRange range(0);
                if (layout && start && end) {
                    const int startLine = LineForGesture(*layout, *start, c.lineMargin), endLine = LineForGesture(*layout, *end, c.lineMargin);
                    int line = -1;
                    if (startLine == -1) line = endLine;
                    else line = endLine == -1 ? startLine : std::min(startLine, endLine);
                    if (line != -1) {
                        const float center = (layout->LineTop(line) + layout->LineBottom(line)) / 2;
                        const Rect rect{ std::min(start->x, end->x), center - 0.1f, std::max(start->x, end->x), center + 0.1f };
                        const auto r = layout->RangeForRect(rect, text::TextGranularity::Character, text::inclusion::AnyOverlap());
                        range = TextRange(r.first, r.second);
                    }
                }
                if (range.Collapsed()) return Fallback(c, g);
                const std::u16string text = c.state.VisualText().text.substr(std::size_t(range.Min()), std::size_t(range.Length()));
                // replace(Regex("\\s+"), ""), remembering the first match's start and the last's end.
                std::u16string newText;
                int firstMatchStart = -1, lastMatchEnd = -1;
                for (int i = 0; i < int(text.size());) {
                    const char32_t cp = CodePointAt(text, i);
                    const int n = CharCount(cp);
                    // Java regex \s: space, tab, line feed, vertical tab, form feed, carriage return
                    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == 0x0B || cp == '\f' || cp == '\r') {
                        if (firstMatchStart == -1) firstMatchStart = i;
                        lastMatchEnd = i + n;
                    } else {
                        newText.append(text.substr(std::size_t(i), std::size_t(n)));
                    }
                    i += n;
                }
                if (firstMatchStart == -1 || lastMatchEnd == -1) return Fallback(c, g);
                const TextRange finalRange(range.Min() + firstMatchStart, range.Min() + lastMatchEnd);
                const int keepTail = range.Length() - lastMatchEnd;
                const std::u16string finalText = newText.substr(std::size_t(firstMatchStart), newText.size() - std::size_t(firstMatchStart) - std::size_t(keepTail));
                c.state.ReplaceText(finalText, finalRange);
                return G::kResultSuccess;
            }
        }
        return G::kResultUnsupported;
    }

    bool PreviewHandwritingGesture(const HandwritingGestureContext& c, const G& g) {
        switch (g.type) {
            case G::Type::Select:
                HighlightRange(c.state, RangeForScreenRect(c.layout, g.area, Granularity(g)), TextHighlightType::HandwritingSelectPreview);
                return true;
            case G::Type::Delete:
                HighlightRange(c.state, RangeForScreenRect(c.layout, g.area, Granularity(g)), TextHighlightType::HandwritingDeletePreview);
                return true;
            case G::Type::SelectRange:
                HighlightRange(c.state, RangeForScreenRects(c.layout, g.area, g.endArea, Granularity(g)), TextHighlightType::HandwritingSelectPreview);
                return true;
            case G::Type::DeleteRange:
                HighlightRange(c.state, RangeForScreenRects(c.layout, g.area, g.endArea, Granularity(g)), TextHighlightType::HandwritingDeletePreview);
                return true;
            default:
                return false;
        }
    }

    void CancelHandwritingPreview(TransformedTextFieldState& state) {
        state.EditUntransformedTextAsUser([](TextFieldBuffer& b) { b.ClearHighlight(); });
    }

}
