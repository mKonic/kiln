#pragma once

// Text field state (spec 04 req. 1): UTF-16 text (the unit Android's IME speaks), a selection
// and an optional composing region, observable as one value. Edit commands follow Compose's
// EditCommand.kt semantics, which mirror Android's InputConnection contract.

#include "kiln/runtime/State.h"

#include <optional>
#include <string>
#include <string_view>

namespace kiln::foundation {

    struct TextRange {
        int start = 0, end = 0;
        int Min() const { return start < end ? start : end; }
        int Max() const { return start < end ? end : start; }
        bool Collapsed() const { return start == end; }
        bool operator==(const TextRange&) const = default;
    };

    struct TextFieldValue {
        std::u16string text;
        TextRange selection;
        std::optional<TextRange> composition;
        bool operator==(const TextFieldValue&) const = default;
    };

    std::u16string ToUtf16(std::string_view utf8);
    std::string ToUtf8(std::u16string_view utf16);

    class TextFieldState {
    public:
        explicit TextFieldState(std::string_view initialUtf8 = {});

        const TextFieldValue& Value() const { return m_Value.Get(); }
        const TextFieldValue& Peek() const { return m_Value.Peek(); }
        std::string Text() const { return ToUtf8(m_Value.Get().text); }
        void SetText(std::string_view utf8);   // programmatic: cursor to the end, composition cleared

        // InputConnection commands (EditCommand.kt).
        void CommitText(std::u16string_view text, int newCursorPosition);
        void SetComposingText(std::u16string_view text, int newCursorPosition);
        void SetComposingRegion(int start, int end);
        void FinishComposingText();
        void DeleteSurroundingText(int before, int after);
        void DeleteSurroundingTextInCodePoints(int before, int after);
        void SetSelection(int start, int end);

        // Keyboard commands.
        void Backspace();        // the selection, else the code point (pair) before the cursor
        void DeleteForward();
        void MoveCursor(int delta, bool extend);   // by code points
        void InsertFromKeyboard(std::u16string_view text) { CommitText(text, 1); }

        // Incremented on every change (lets the platform session detect external edits).
        std::uint64_t Version() const { return m_Version; }

    private:
        void Set(TextFieldValue v);
        void Replace(int start, int end, std::u16string_view text);
        rt::State<TextFieldValue> m_Value;
        std::uint64_t m_Version = 0;
    };

    // rememberTextFieldState: the text and selection survive re-creation (TextFieldState.Saver).
    TextFieldState& RememberTextFieldState(std::string_view initialText = {});

}
