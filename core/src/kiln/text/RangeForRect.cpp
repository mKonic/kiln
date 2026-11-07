// getRangeForRect (Compose's TextLayoutGetRangeForRectExtensions, the platform-independent path
// of TextLayout.getRangeForRect, and MultiParagraph.getRangeForRect) with its segment finders
// (WordSegmentFinder over the backend's word breaks, GraphemeClusterSegmentFinder over its
// character breaks).

#include "kiln/text/Paragraph.h"

#include <algorithm>

namespace kiln::text {

    namespace inclusion {
        TextInclusionStrategy AnyOverlap() {
            return [](const ui::Rect& t, const ui::Rect& r) {
                return t.right > r.left && r.right > t.left && t.bottom > r.top && r.bottom > t.top;   // Rect.overlaps
            };
        }
        TextInclusionStrategy ContainsAll() {
            return [](const ui::Rect& t, const ui::Rect& r) {
                const bool empty = r.left >= r.right || r.top >= r.bottom;
                return !empty && t.left >= r.left && t.right <= r.right && t.top >= r.top && t.bottom <= r.bottom;
            };
        }
        TextInclusionStrategy ContainsCenter() {
            return [](const ui::Rect& t, const ui::Rect& r) {
                const float cx = (t.left + t.right) / 2, cy = (t.top + t.bottom) / 2;
                return cx >= r.left && cx < r.right && cy >= r.top && cy < r.bottom;   // Rect.contains
            };
        }
    }

    namespace {
        constexpr int kDone = -1;

        bool IsWhitespace(char16_t c) {
            // Character.isWhitespace
            return c == u' ' || c == u'\t' || c == u'\n' || c == 0x0B || c == 0x0C || c == u'\r' || (c >= 0x1C && c <= 0x1F) ||
                   c == 0x1680 || (c >= 0x2000 && c <= 0x2006) || (c >= 0x2008 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
                   c == 0x205F || c == 0x3000;
        }

        class SegmentFinder {
        public:
            virtual ~SegmentFinder() = default;
            virtual int PreviousStartBoundary(int offset) const = 0;
            virtual int PreviousEndBoundary(int offset) const = 0;
            virtual int NextStartBoundary(int offset) const = 0;
            virtual int NextEndBoundary(int offset) const = 0;
        };

        // WordSegmentFinder: word iterator boundaries that do not start (or end) on whitespace.
        class WordSegmentFinder final : public SegmentFinder {
        public:
            WordSegmentFinder(std::u16string_view text, TextBackend* backend) : m_Text(text), m_Backend(backend) {}
            int PreviousStartBoundary(int offset) const override {
                int b = offset;
                do {
                    b = Prev(b);
                    if (b == kDone) return kDone;
                } while (IsWhitespace(m_Text[std::size_t(b)]));
                return b;
            }
            int PreviousEndBoundary(int offset) const override {
                int b = offset;
                do {
                    b = Prev(b);
                    if (b == kDone || b == 0) return kDone;
                } while (IsWhitespace(m_Text[std::size_t(b - 1)]));
                return b;
            }
            int NextStartBoundary(int offset) const override {
                int b = offset;
                do {
                    b = Next(b);
                    if (b == kDone || b == int(m_Text.size())) return kDone;
                } while (IsWhitespace(m_Text[std::size_t(b)]));
                return b;
            }
            int NextEndBoundary(int offset) const override {
                int b = offset;
                do {
                    b = Next(b);
                    if (b == kDone) return kDone;
                } while (IsWhitespace(m_Text[std::size_t(b - 1)]));
                return b;
            }
        private:
            // WordIterator.nextBoundary / prevBoundary (BreakIterator following / preceding).
            int Next(int offset) const {
                return m_Backend ? m_Backend->WordBreak(m_Text, offset, true) : TextBackend::SimpleWordBreak(m_Text, offset, true);
            }
            int Prev(int offset) const {
                return m_Backend ? m_Backend->WordBreak(m_Text, offset, false) : TextBackend::SimpleWordBreak(m_Text, offset, false);
            }
            std::u16string_view m_Text;
            TextBackend* m_Backend;
        };

        class GraphemeClusterSegmentFinder final : public SegmentFinder {
        public:
            GraphemeClusterSegmentFinder(std::u16string_view text, TextBackend* backend) : m_Text(text), m_Backend(backend) {}
            int PreviousStartBoundary(int offset) const override { return Previous(offset); }
            int PreviousEndBoundary(int offset) const override {
                const int p = Previous(offset);
                if (p == kDone) return kDone;
                return Previous(p) == kDone ? kDone : p;
            }
            int NextStartBoundary(int offset) const override {
                const int n = Next(offset);
                if (n == kDone) return kDone;
                return Next(n) == kDone ? kDone : n;
            }
            int NextEndBoundary(int offset) const override { return Next(offset); }
        private:
            int Previous(int offset) const {
                const int r = m_Backend ? m_Backend->CharacterBreak(m_Text, offset, false) : TextBackend::SurrogateCharacterBreak(m_Text, offset, false);
                return r < 0 ? kDone : r;
            }
            int Next(int offset) const {
                const int r = m_Backend ? m_Backend->CharacterBreak(m_Text, offset, true) : TextBackend::SurrogateCharacterBreak(m_Text, offset, true);
                return r < 0 ? kDone : r;
            }
            std::u16string_view m_Text;
            TextBackend* m_Backend;
        };

        struct BidiRun { int start, end; bool rtl; };

        struct LineContext {
            int lineStart;
            float lineTop, lineBottom;
            const std::vector<float>& bounds;   // left, right per character of the line
            float Left(int offset) const { return bounds[std::size_t(2 * (offset - lineStart))]; }
            float Right(int offset) const { return bounds[std::size_t(2 * (offset - lineStart) + 1)]; }
        };

        bool HorizontalOverlap(const ui::Rect& r, float left, float right) { return right >= r.left && left <= r.right; }

        int StartOffsetForRectWithinRun(const BidiRun& run, const ui::Rect& rect, const LineContext& l, float runLeft, float runRight,
                                        const SegmentFinder& finder, const TextInclusionStrategy& inclusion) {
            if (!HorizontalOverlap(rect, runLeft, runRight)) return -1;
            int firstChar;
            if ((!run.rtl && rect.left <= runLeft) || (run.rtl && rect.right >= runRight)) {
                firstChar = run.start;
            } else {
                int low = run.start, high = run.end;
                while (high - low > 1) {
                    const int guess = (high + low) / 2;
                    const float position = l.Left(guess);
                    if ((!run.rtl && position > rect.left) || (run.rtl && position < rect.right)) high = guess;
                    else low = guess;
                }
                firstChar = run.rtl ? high : low;
            }
            int segmentEnd = finder.NextEndBoundary(firstChar);
            if (segmentEnd == kDone) return -1;
            int segmentStart = finder.PreviousStartBoundary(segmentEnd);
            if (segmentStart >= run.end) return -1;
            segmentStart = std::max(segmentStart, run.start);
            segmentEnd = std::min(segmentEnd, run.end);
            ui::Rect bounds{ 0, l.lineTop, 0, l.lineBottom };
            for (;;) {
                bounds.left = run.rtl ? l.Left(segmentEnd - 1) : l.Left(segmentStart);
                bounds.right = run.rtl ? l.Right(segmentStart) : l.Right(segmentEnd - 1);
                if (inclusion(bounds, rect)) return segmentStart;
                segmentStart = finder.NextStartBoundary(segmentStart);
                if (segmentStart == kDone || segmentStart >= run.end) return -1;
                segmentEnd = std::min(finder.NextEndBoundary(segmentStart), run.end);
            }
        }

        int EndOffsetForRectWithinRun(const BidiRun& run, const ui::Rect& rect, const LineContext& l, float runLeft, float runRight,
                                      const SegmentFinder& finder, const TextInclusionStrategy& inclusion) {
            if (!HorizontalOverlap(rect, runLeft, runRight)) return -1;
            int lastChar;
            if ((!run.rtl && rect.right >= runRight) || (run.rtl && rect.left <= runLeft)) {
                lastChar = run.end - 1;
            } else {
                int low = run.start, high = run.end;
                while (high - low > 1) {
                    const int guess = (high + low) / 2;
                    const float position = l.Left(guess);
                    if ((!run.rtl && position > rect.right) || (run.rtl && position < rect.left)) high = guess;
                    else low = guess;
                }
                lastChar = run.rtl ? high : low;
            }
            int segmentStart = finder.PreviousStartBoundary(lastChar + 1);
            if (segmentStart == kDone) return -1;
            int segmentEnd = finder.NextEndBoundary(segmentStart);
            if (segmentEnd <= run.start) return -1;
            segmentStart = std::max(segmentStart, run.start);
            segmentEnd = std::min(segmentEnd, run.end);
            ui::Rect bounds{ 0, l.lineTop, 0, l.lineBottom };
            for (;;) {
                bounds.left = run.rtl ? l.Left(segmentEnd - 1) : l.Left(segmentStart);
                bounds.right = run.rtl ? l.Right(segmentStart) : l.Right(segmentEnd - 1);
                if (inclusion(bounds, rect)) return segmentEnd;
                segmentEnd = finder.PreviousEndBoundary(segmentEnd);
                if (segmentEnd == kDone || segmentEnd <= run.start) return -1;
                segmentStart = std::max(finder.PreviousStartBoundary(segmentEnd), run.start);
            }
        }
    }

    std::pair<int, int> Paragraph::RangeForRect(const ui::Rect& rect, TextGranularity granularity, const TextInclusionStrategy& inclusion) const {
        const auto& lines = m_Data.lines;
        if (lines.empty() || !m_Backend || !m_Data.handle) return { 0, 0 };
        const std::u16string text = [&] {
            std::u16string out;
            for (std::size_t i = 0; i < m_Request.text.size();) {
                // UTF-8 to UTF-16
                const unsigned char c = static_cast<unsigned char>(m_Request.text[i]);
                char32_t cp;
                int n;
                if (c < 0x80) { cp = c; n = 1; }
                else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
                else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
                else { cp = c & 0x07; n = 4; }
                for (int k = 1; k < n && i + std::size_t(k) < m_Request.text.size(); ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(m_Request.text[i + std::size_t(k)]) & 0x3F);
                i += std::size_t(n);
                if (cp >= 0x10000) { cp -= 0x10000; out.push_back(char16_t(0xD800 + (cp >> 10))); out.push_back(char16_t(0xDC00 + (cp & 0x3FF))); }
                else out.push_back(char16_t(cp));
            }
            return out;
        }();
        const WordSegmentFinder words(text, m_Backend);
        const GraphemeClusterSegmentFinder graphemes(text, m_Backend);
        const SegmentFinder& finder = granularity == TextGranularity::Word ? static_cast<const SegmentFinder&>(words) : graphemes;

        // getStartOrEndOffsetForRectWithinLine
        auto withinLine = [&](int lineIndex, bool getStart) -> int {
            const LineData& line = lines[std::size_t(lineIndex)];
            const int lineStart = line.start, lineEnd = std::min(line.end, m_Length);
            if (lineStart == lineEnd) return -1;
            // fillLineHorizontalBounds: each character's left and right.
            std::vector<float> bounds(std::size_t(2 * (lineEnd - lineStart)));
            for (int o = lineStart; o < lineEnd; ++o) {
                const RectF b = BoundingBox(o);
                bounds[std::size_t(2 * (o - lineStart))] = b.x;
                bounds[std::size_t(2 * (o - lineStart) + 1)] = b.x + b.w;
            }
            // getLineBidiRuns: logical runs of one direction.
            std::vector<BidiRun> runs;
            for (int o = lineStart; o < lineEnd; ++o) {
                const bool rtl = IsRtlCharAt(o);
                if (runs.empty() || runs.back().rtl != rtl) runs.push_back({ o, o + 1, rtl });
                else runs.back().end = o + 1;
            }
            const LineContext l{ lineStart, line.top, line.bottom, bounds };
            for (std::size_t k = 0; k < runs.size(); ++k) {
                const BidiRun& run = runs[getStart ? k : runs.size() - 1 - k];
                const float runLeft = run.rtl ? l.Left(run.end - 1) : l.Left(run.start);
                const float runRight = run.rtl ? l.Right(run.start) : l.Right(run.end - 1);
                const int r = getStart ? StartOffsetForRectWithinRun(run, rect, l, runLeft, runRight, finder, inclusion)
                                       : EndOffsetForRectWithinRun(run, rect, l, runLeft, runRight, finder, inclusion);
                if (r >= 0) return r;
            }
            return -1;
        };

        const int lineCount = int(lines.size());
        // Layout.getLineForVertical takes an int.
        int startLine = LineForVertical(float(int(rect.top)));
        if (rect.top > lines[std::size_t(startLine)].bottom) {
            if (++startLine >= lineCount) return { 0, 0 };
        }
        int endLine = LineForVertical(float(int(rect.bottom)));
        if (endLine == 0 && rect.bottom < lines[0].top) return { 0, 0 };
        int start = withinLine(startLine, true);
        while (start == -1 && startLine < endLine) start = withinLine(++startLine, true);
        if (start == -1) return { 0, 0 };
        int end = withinLine(endLine, false);
        while (end == -1 && startLine < endLine) end = withinLine(--endLine, false);
        if (end == -1) return { 0, 0 };
        start = finder.PreviousStartBoundary(start + 1);
        end = finder.NextEndBoundary(end - 1);
        return { start, end };
    }

    std::pair<int, int> MultiParagraph::RangeForRect(const ui::Rect& rect, TextGranularity granularity, const TextInclusionStrategy& inclusion) const {
        if (m_Infos.empty()) return { 0, 0 };
        auto findByY = [&](float y) {
            if (y <= 0) return std::size_t(0);
            if (y >= m_Height) return m_Infos.size() - 1;
            for (std::size_t i = 0; i < m_Infos.size(); ++i) if (y < m_Infos[i].bottom) return i;
            return m_Infos.size() - 1;
        };
        auto rangeIn = [&](std::size_t i) -> std::pair<int, int> {
            const ParagraphInfo& info = m_Infos[i];
            const ui::Rect local{ rect.left, rect.top - info.top, rect.right, rect.bottom - info.top };
            const auto r = info.paragraph->RangeForRect(local, granularity, inclusion);
            if (r.first == 0 && r.second == 0) return { 0, 0 };
            return { r.first + info.startIndex, r.second + info.startIndex };
        };
        auto zero = [](const std::pair<int, int>& r) { return r.first == 0 && r.second == 0; };
        std::size_t first = findByY(rect.top);
        // The first paragraph holds the whole rect.
        if (m_Infos[first].bottom >= rect.bottom || first == m_Infos.size() - 1) return rangeIn(first);
        std::size_t last = findByY(rect.bottom);
        std::pair<int, int> startRange{ 0, 0 };
        while (zero(startRange) && first <= last) startRange = rangeIn(first++);
        if (zero(startRange)) return { 0, 0 };
        std::pair<int, int> endRange{ 0, 0 };
        while (zero(endRange) && first <= last) {
            endRange = rangeIn(last);
            if (last == 0) break;
            --last;
        }
        if (zero(endRange)) return startRange;
        return { startRange.first, endRange.second };
    }

}
