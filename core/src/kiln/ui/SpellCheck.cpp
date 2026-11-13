#include "kiln/ui/SpellCheck.h"

#include <algorithm>

namespace kiln::ui {

    namespace {
        // \w of the regular expression Flutter searches with.
        bool IsWord(char16_t c) {
            return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'_';
        }
        // The first `word` in `text` with a word boundary on both sides (\bword\b), or -1.
        int IndexOfWord(std::u16string_view text, std::u16string_view word) {
            if (word.empty()) return -1;
            const auto boundary = [&](std::size_t i) {
                const bool before = i > 0 && IsWord(text[i - 1]);
                const bool after = i < text.size() && IsWord(text[i]);
                return before != after;
            };
            for (std::size_t p = text.find(word); p != std::u16string_view::npos; p = text.find(word, p + 1))
                if (boundary(p) && boundary(p + word.size())) return int(p);
            return -1;
        }
    }

    std::vector<SpellCheckSpan> CorrectSpellCheckResults(std::u16string_view newText, std::u16string_view resultsText,
                                                         const std::vector<SpellCheckSpan>& results) {
        std::vector<SpellCheckSpan> corrected;
        // The spans are in order: each is searched for after the one found before it.
        int searchStart = 0;
        for (const SpellCheckSpan& span : results) {
            if (span.start < 0 || span.end < span.start || std::size_t(span.end) > resultsText.size()) continue;
            const std::u16string_view word = resultsText.substr(std::size_t(span.start), std::size_t(span.end - span.start));
            const int found = IndexOfWord(newText.substr(std::size_t(searchStart)), word);
            if (found < 0) continue;   // the word was edited: its result is gone
            const int start = searchStart + found, end = start + (span.end - span.start);
            corrected.push_back({ start, end, span.suggestions });
            searchStart = std::min(end + 1, int(newText.size()));
        }
        return corrected;
    }

}
