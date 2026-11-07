#pragma once

// A monospace text backend for tests.

#include "kiln/TextBackend.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kiln::test {
    // Monospace stand-in: each UTF-16 unit is half the run's size wide, a line is 1.25 sizes
    // tall (or the line height), wraps anywhere at the width and at '\n'.
    class FakeText final : public TextBackend {
    public:
        int layouts = 0;
        ParagraphData Layout(const ParagraphRequest& r) override {
            ++layouts;
            ParagraphData d;
            const std::u16string text = ToU16(r.text);
            const float width = r.widthPx > 0 ? r.widthPx : INFINITY;
            const float lineH = !std::isnan(r.lineHeightPx) ? r.lineHeightPx : r.base.sizePx * 1.25f;
            int start = 0;
            float x = 0;
            auto push = [&](int end, int next) {
                LineData l;
                l.top = float(d.lines.size()) * lineH;
                l.bottom = l.top + lineH;
                l.baseline = l.top + lineH * 0.8f;
                l.start = start;
                l.end = next;
                l.visibleEnd = end;
                l.right = x;
                d.width = std::max(d.width, x);
                d.lines.push_back(l);
                start = next;
                x = 0;
            };
            for (int i = 0; i < int(text.size()); ++i) {
                if (text[std::size_t(i)] == u'\n') { push(i, i + 1); continue; }
                const float w = Advance(r, i);
                if (x + w > width && i > start) push(i, i);
                x += w;
            }
            push(int(text.size()), int(text.size()));
            // Placeholders: above the baseline, at their first character.
            for (const auto& ph : r.placeholders) {
                std::optional<RectF> rect;
                for (const auto& l : d.lines) {
                    if (ph.start < l.start || ph.start >= std::max(l.end, l.start + 1)) continue;
                    float x = 0;
                    for (int k = l.start; k < ph.start; ++k) x += Advance(r, k);
                    const float h = ph.heightEm ? ph.height * r.base.sizePx : ph.height;
                    rect = RectF{ x, l.baseline - h, ph.widthEm ? ph.width * r.base.sizePx : ph.width, h };
                    break;
                }
                d.placeholderRects.push_back(rect);
            }
            if (int(d.lines.size()) > r.maxLines) {
                d.lines.resize(std::size_t(r.maxLines));
                d.didExceedMaxLines = true;
            }
            d.lineCount = int(d.lines.size());
            d.height = d.lines.back().bottom;
            d.firstBaseline = d.lines.front().baseline;
            d.lastBaseline = d.lines.back().baseline;
            d.handle = ++m_Next;
            m_Laid[d.handle] = { r.base.sizePx * 0.5f, d.lines };
            m_Texts[d.handle] = text;
            return d;
        }
        ParagraphIntrinsicWidths Intrinsics(const ParagraphRequest& r) override {
            const std::u16string text = ToU16(r.text);
            float line = 0, word = 0, maxLine = 0, maxWord = 0;
            for (int i = 0; i < int(text.size()); ++i) {
                const char16_t c = text[std::size_t(i)];
                if (c == u'\n') { line = 0; word = 0; continue; }
                const float w = Advance(r, i);
                line += w;
                word = c == u' ' ? 0 : word + w;
                maxLine = std::max(maxLine, line);
                maxWord = std::max(maxWord, word);
            }
            return { maxWord, maxLine };
        }
        void SyncAtlas(AtlasSink&) override {}
        float GetHorizontalPosition(std::int64_t h, int o, bool) override {
            const auto& [cw, lines] = Laid(h);
            const LineData& l = LineOf(lines, o);
            return float(o - l.start) * cw;
        }
        CursorRect GetCursorRect(std::int64_t h, int o) override {
            const auto& [cw, lines] = Laid(h);
            const LineData& l = LineOf(lines, o);
            return { float(o - l.start) * cw, l.top, l.bottom };
        }
        int GetOffsetForPosition(std::int64_t h, float x, float y) override {
            const auto& [cw, lines] = Laid(h);
            const LineData* l = &lines.back();
            for (const auto& line : lines) if (y < line.bottom) { l = &line; break; }
            return std::clamp(l->start + int(std::lround(x / cw)), l->start, l->visibleEnd);
        }
        RectF GetBoundingBox(std::int64_t h, int o) override {
            const auto& [cw, lines] = Laid(h);
            const LineData& l = LineOf(lines, o);
            return { float(o - l.start) * cw, l.top, cw, l.bottom - l.top };
        }
        std::vector<RectF> GetSelectionRects(std::int64_t h, int start, int end) override {
            std::vector<RectF> out;
            const auto& [cw, lines] = m_Laid[h];
            for (const auto& l : lines) {
                const int s = std::max(start, l.start), e = std::min(end, l.visibleEnd);
                if (s < e) out.push_back({ float(s - l.start) * cw, l.top, float(e - s) * cw, l.bottom - l.top });
            }
            return out;
        }
        // Words are runs of letters and digits; between words the boundary is empty.
        std::pair<int, int> GetWordBoundary(std::int64_t h, int o) override {
            const std::u16string& t = m_Texts[h];
            auto word = [&](int i) { return i >= 0 && i < int(t.size()) && std::isalnum(int(t[std::size_t(i)])); };
            if (!word(o) && !word(o - 1)) return { o, o };
            int a = o, b = o;
            while (word(a - 1)) --a;
            while (word(b)) ++b;
            return { a, b };
        }
        bool IsRtlCharAt(std::int64_t, int) override { return false; }
        void Release(std::int64_t h) override { ++released; m_Laid.erase(h); m_Texts.erase(h); }
        int released = 0;

    private:
        static std::u16string ToU16(const std::string& s) {
            std::u16string out;
            for (unsigned char c : s) out.push_back(c);   // ASCII only in these tests
            return out;
        }
        static float Advance(const ParagraphRequest& r, int i) {
            for (const auto& ph : r.placeholders)
                if (i >= ph.start && i < ph.end) return i == ph.start ? (ph.widthEm ? ph.width * r.base.sizePx : ph.width) : 0;
            for (const auto& run : r.runs)
                if (i >= run.start && i < run.end) return run.sizePx * 0.5f;
            return r.base.sizePx * 0.5f;
        }
        const std::pair<float, std::vector<LineData>>& Laid(std::int64_t h) { return m_Laid[h]; }
        static const LineData& LineOf(const std::vector<LineData>& lines, int o) {
            for (const auto& l : lines) if (o < l.end) return l;
            return lines.back();
        }
        std::int64_t m_Next = 0;
        std::map<std::int64_t, std::pair<float, std::vector<LineData>>> m_Laid;
        std::map<std::int64_t, std::u16string> m_Texts;
    };
}
