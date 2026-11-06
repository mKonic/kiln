#include "kiln/text/AnnotatedString.h"

#include <algorithm>

namespace kiln::text {

    // --- UTF helpers -------------------------------------------------------------------------

    namespace {
        // Bytes in the UTF-8 sequence starting with `lead`, and its UTF-16 units.
        int SeqLength(unsigned char lead) { return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4; }
        int Units(int seq) { return seq == 4 ? 2 : 1; }
    }

    int Utf16Length(std::string_view s) {
        int n = 0;
        for (std::size_t i = 0; i < s.size();) {
            const int len = SeqLength(static_cast<unsigned char>(s[i]));
            n += Units(len);
            i += std::size_t(len);
        }
        return n;
    }

    std::size_t Utf16ToUtf8Offset(std::string_view s, int offset16) {
        int n = 0;
        std::size_t i = 0;
        while (i < s.size() && n < offset16) {
            const int len = SeqLength(static_cast<unsigned char>(s[i]));
            n += Units(len);
            i += std::size_t(len);
        }
        return std::min(i, s.size());
    }

    int Utf8ToUtf16Offset(std::string_view s, std::size_t offset8) {
        int n = 0;
        for (std::size_t i = 0; i < s.size() && i < offset8;) {
            const int len = SeqLength(static_cast<unsigned char>(s[i]));
            n += Units(len);
            i += std::size_t(len);
        }
        return n;
    }

    // --- AnnotatedString ---------------------------------------------------------------------

    AnnotatedString::AnnotatedString(std::string text, std::vector<Range<Annotation>> annotations)
        : m_Text(std::move(text)), m_Length(Utf16Length(m_Text)), m_Annotations(std::move(annotations)) {
        for (auto& a : m_Annotations) {
            a.start = std::clamp(a.start, 0, m_Length);
            a.end = std::clamp(a.end, a.start, m_Length);
        }
    }

    namespace {
        bool Intersects(int s1, int e1, int s2, int e2) {
            // Compose's intersect: ranges overlap, or a collapsed range touches the other.
            return std::max(s1, s2) < std::min(e1, e2) || ((s1 == e1 || s2 == e2) && (s1 == s2 || (s1 <= s2 && s2 <= e1) || (s2 <= s1 && s1 <= e2)));
        }
        template<typename T>
        std::vector<Range<T>> Collect(const std::vector<Range<Annotation>>& all, int start, int end, bool all_ranges, std::string_view tag = {}, bool byTag = false) {
            std::vector<Range<T>> out;
            for (const auto& a : all) {
                const T* item = std::get_if<T>(&a.item);
                if (!item) continue;
                if (byTag && a.tag != tag) continue;
                if (!all_ranges && !Intersects(start, end, a.start, a.end)) continue;
                out.push_back({ *item, a.start, a.end, a.tag });
            }
            return out;
        }
    }

    std::vector<Range<SpanStyle>> AnnotatedString::SpanStyles() const { return Collect<SpanStyle>(m_Annotations, 0, 0, true); }
    std::vector<Range<ParagraphStyle>> AnnotatedString::ParagraphStyles() const { return Collect<ParagraphStyle>(m_Annotations, 0, 0, true); }

    std::vector<Range<std::string>> AnnotatedString::GetStringAnnotations(int start, int end) const {
        std::vector<Range<std::string>> out;
        for (const auto& r : Collect<StringAnnotation>(m_Annotations, start, end, false)) out.push_back({ r.item.value, r.start, r.end, r.tag });
        return out;
    }
    std::vector<Range<std::string>> AnnotatedString::GetStringAnnotations(std::string_view tag, int start, int end) const {
        std::vector<Range<std::string>> out;
        for (const auto& r : Collect<StringAnnotation>(m_Annotations, start, end, false, tag, true)) out.push_back({ r.item.value, r.start, r.end, r.tag });
        return out;
    }
    std::vector<Range<LinkAnnotation>> AnnotatedString::GetLinkAnnotations(int start, int end) const {
        return Collect<LinkAnnotation>(m_Annotations, start, end, false);
    }
    std::vector<Range<TtsAnnotation>> AnnotatedString::GetTtsAnnotations(int start, int end) const {
        return Collect<TtsAnnotation>(m_Annotations, start, end, false);
    }

    AnnotatedString AnnotatedString::SubSequence(int start, int end) const {
        start = std::clamp(start, 0, m_Length);
        end = std::clamp(end, start, m_Length);
        const std::size_t b0 = Utf16ToUtf8Offset(m_Text, start), b1 = Utf16ToUtf8Offset(m_Text, end);
        std::vector<Range<Annotation>> local;
        for (const auto& a : m_Annotations) {
            if (!Intersects(start, end, a.start, a.end)) continue;
            local.push_back({ a.item, std::clamp(a.start, start, end) - start, std::clamp(a.end, start, end) - start, a.tag });
        }
        return AnnotatedString(m_Text.substr(b0, b1 - b0), std::move(local));
    }

    AnnotatedString AnnotatedString::operator+(const AnnotatedString& o) const {
        AnnotatedStringBuilder b(*this);
        b.Append(o);
        return b.ToAnnotatedString();
    }

    std::vector<Range<ParagraphStyle>> AnnotatedString::NormalizedParagraphStyles(const ParagraphStyle& defaultStyle) const {
        auto sorted = ParagraphStyles();
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        std::vector<Range<ParagraphStyle>> result;
        int lastAdded = 0;
        std::vector<Range<ParagraphStyle>> stack;
        for (const auto& it : sorted) {
            Range<ParagraphStyle> current{ defaultStyle.Merge(it.item), it.start, it.end, {} };
            while (lastAdded < current.start && !stack.empty()) {
                const auto last = stack.back();
                if (current.start < last.end) {
                    result.push_back({ last.item, lastAdded, current.start, {} });
                    lastAdded = current.start;
                } else {
                    result.push_back({ last.item, lastAdded, last.end, {} });
                    lastAdded = last.end;
                    while (!stack.empty() && lastAdded == stack.back().end) stack.pop_back();
                }
            }
            if (lastAdded < current.start) {
                result.push_back({ defaultStyle, lastAdded, current.start, {} });
                lastAdded = current.start;
            }
            if (!stack.empty()) {
                const auto last = stack.back();
                if (last.start == current.start && last.end == current.end) {
                    stack.pop_back();
                    stack.push_back({ last.item.Merge(current.item), current.start, current.end, {} });
                } else if (last.start == last.end) {
                    result.push_back({ last.item, last.start, last.end, {} });   // zero length
                    stack.pop_back();
                    stack.push_back({ current.item, current.start, current.end, {} });
                } else if (last.end < current.end) {
                    // Partially overlapping paragraphs are not allowed; keep the outer one.
                    continue;
                } else {
                    stack.push_back({ last.item.Merge(current.item), current.start, current.end, {} });
                }
            } else {
                stack.push_back(current);
            }
        }
        while (lastAdded <= m_Length && !stack.empty()) {
            const auto last = stack.back();
            result.push_back({ last.item, lastAdded, last.end, {} });
            lastAdded = last.end;
            while (!stack.empty() && lastAdded == stack.back().end) stack.pop_back();
        }
        if (lastAdded < m_Length) result.push_back({ defaultStyle, lastAdded, m_Length, {} });
        // An empty text still gets one (empty) paragraph.
        if (result.empty()) result.push_back({ defaultStyle, 0, 0, {} });
        return result;
    }

    // --- Builder -----------------------------------------------------------------------------

    AnnotatedStringBuilder& AnnotatedStringBuilder::Append(std::string_view text) {
        m_Text.append(text);
        m_Length += Utf16Length(text);
        return *this;
    }

    AnnotatedStringBuilder& AnnotatedStringBuilder::Append(const AnnotatedString& text) {
        const int start = m_Length;
        m_Text.append(text.Text());
        m_Length += text.Length();
        for (const auto& a : text.Annotations()) m_Annotations.push_back({ a.item, a.start + start, a.end + start, a.tag });
        return *this;
    }

    void AnnotatedStringBuilder::Pop() {
        if (m_Stack.empty()) return;   // nothing to pop
        m_Annotations[std::size_t(m_Stack.back())].end = m_Length;
        m_Stack.pop_back();
    }

    void AnnotatedStringBuilder::Pop(int index) {
        while (int(m_Stack.size()) > index) Pop();
    }

    AnnotatedString AnnotatedStringBuilder::ToAnnotatedString() const {
        std::vector<Range<Annotation>> out = m_Annotations;
        for (auto& a : out) if (a.end < 0) a.end = m_Length;   // still open: to the end
        return AnnotatedString(m_Text, std::move(out));
    }

}
