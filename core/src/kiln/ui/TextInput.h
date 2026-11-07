#pragma once

// Platform text input (spec 02 req. 12, 04 req. 4-6): a focused text field starts one session;
// the host turns it into EditorInfo + InputConnection. IME calls arrive synchronously on the main
// thread and edit the field directly, so reads after writes see the new state.

#include <cstdint>
#include <functional>
#include <string>

namespace kiln::ui {

    enum class KeyboardType : std::uint8_t { Text, Ascii, Number, Phone, Uri, Email, Password, NumberPassword, Decimal, PasswordVisible };
    enum class ImeAction : std::uint8_t { Default, None, Go, Search, Send, Previous, Next, Done };
    enum class Capitalization : std::uint8_t { None, Characters, Words, Sentences };

    struct ImeOptions {
        KeyboardType keyboardType = KeyboardType::Text;
        ImeAction imeAction = ImeAction::Default;
        Capitalization capitalization = Capitalization::None;
        bool autoCorrect = true;
        bool singleLine = false;
    };

    struct TextInputSnapshot {
        std::u16string text;
        int selStart = 0, selEnd = 0;
        int compStart = -1, compEnd = -1;
    };

    // Implemented by a text field while it holds the session.
    class TextInputSession {
    public:
        virtual ~TextInputSession() = default;
        virtual ImeOptions Options() const = 0;
        virtual TextInputSnapshot Snapshot() const = 0;
        virtual void CommitText(std::u16string_view text, int newCursorPosition) = 0;
        virtual void SetComposingText(std::u16string_view text, int newCursorPosition) = 0;
        virtual void SetComposingRegion(int start, int end) = 0;
        virtual void FinishComposingText() = 0;
        virtual void DeleteSurroundingText(int before, int after) = 0;
        virtual void DeleteSurroundingTextInCodePoints(int before, int after) = 0;
        virtual void SetSelection(int start, int end) = 0;
        virtual void PerformImeAction(ImeAction action) = 0;
        // Caret rectangle in window pixels (CursorAnchorInfo insertion marker): x, top, baseline, bottom.
        struct Caret { float x = 0, top = 0, baseline = 0, bottom = 0; bool valid = false; };
        virtual Caret CaretRect() const { return {}; }
    };

    // Host side (the Android view / dev window).
    struct TextInputHost {
        std::function<void(const ImeOptions&, const TextInputSnapshot&)> start;   // restartInput + show keyboard
        std::function<void()> stop;                                               // hide keyboard + restartInput
        std::function<void(const TextInputSnapshot&)> updateSelection;           // InputMethodManager.updateSelection
        std::function<void()> restartInput;                                       // content changed under a composition
        std::function<void()> showKeyboard;                                       // SoftwareKeyboardController.show
        std::function<void()> hideKeyboard;                                       // SoftwareKeyboardController.hide
    };

}
