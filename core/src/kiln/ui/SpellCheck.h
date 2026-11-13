#pragma once

// Spell checking for text fields (Flutter's SpellCheckPlugin; Compose has none). The host checks a
// text with the platform's spell checker (SpellCheckerSession on Android) and reports the
// misspelled ranges with their suggestions; a field that asks for it underlines them and offers
// the suggestions in its toolbar.

#include <kiln/Function.h>

#include <string>
#include <string_view>
#include <vector>

namespace kiln::ui {

    // A misspelled range of the checked text (UTF-16 offsets) and what could replace it.
    struct SpellCheckSpan {
        int start = 0, end = 0;
        std::vector<std::u16string> suggestions;
        bool operator==(const SpellCheckSpan&) const = default;
    };

    // The results of checking `resultsText`, moved to where their words are in `newText` (the
    // text was edited while it was checked, or since): a word found again keeps its result, an
    // edited one loses it. Flutter's _correctSpellCheckResults.
    std::vector<SpellCheckSpan> CorrectSpellCheckResults(std::u16string_view newText, std::u16string_view resultsText,
                                                         const std::vector<SpellCheckSpan>& results);

    struct SpellCheckHost {
        // Checks `text`; `done` runs once, on the main thread, with the misspelled ranges (none
        // when the platform has no spell checker).
        kiln::Function<void(const std::u16string& text, kiln::Function<void(std::vector<SpellCheckSpan>)> done)> check;
    };

}
