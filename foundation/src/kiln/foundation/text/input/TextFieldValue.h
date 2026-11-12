#pragma once

// The value-based text input model (ui-text input/TextFieldValue.kt, EditingBuffer.kt,
// EditCommand.kt, EditProcessor.kt): a text with its selection and composition, and the IME's
// edit commands applied to a buffer of it. Offsets are UTF-16 units, as everywhere in text.

#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/text/AnnotatedString.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln { class TextBackend; }

namespace kiln::foundation {

    // TextFieldValue: selection and composition are clamped to the text.
    class TextFieldValue {
    public:
        TextFieldValue() = default;
        TextFieldValue(text::AnnotatedString annotatedString, TextRange selection = {}, std::optional<TextRange> composition = std::nullopt);
        TextFieldValue(std::string text, TextRange selection = {}, std::optional<TextRange> composition = std::nullopt)
            : TextFieldValue(text::AnnotatedString(std::move(text)), selection, composition) {}
        TextFieldValue(const char* text, TextRange selection = {}, std::optional<TextRange> composition = std::nullopt)
            : TextFieldValue(std::string(text), selection, composition) {}
        const text::AnnotatedString& AnnotatedString() const { return m_Text; }
        const std::string& Text() const { return m_Text.Text(); }
        TextRange Selection() const { return m_Selection; }
        const std::optional<TextRange>& Composition() const { return m_Composition; }
        TextFieldValue Copy(text::AnnotatedString text) const { return TextFieldValue(std::move(text), m_Selection, m_Composition); }
        TextFieldValue Copy(std::string text) const { return TextFieldValue(std::move(text), m_Selection, m_Composition); }
        TextFieldValue CopyWithSelection(TextRange selection) const { return TextFieldValue(m_Text, selection, m_Composition); }
        TextFieldValue CopyWithComposition(std::optional<TextRange> composition) const { return TextFieldValue(m_Text, m_Selection, composition); }
        bool operator==(const TextFieldValue&) const = default;

    private:
        text::AnnotatedString m_Text;
        TextRange m_Selection;
        std::optional<TextRange> m_Composition;
    };

    // getTextBeforeSelection / getTextAfterSelection: up to maxChars next to the selection.
    text::AnnotatedString GetTextBeforeSelection(const TextFieldValue& value, int maxChars);
    text::AnnotatedString GetTextAfterSelection(const TextFieldValue& value, int maxChars);
    text::AnnotatedString GetSelectedText(const TextFieldValue& value);
    // CharSequence.substring(range).
    std::string Substring(const text::AnnotatedString& text, TextRange range);

    // EditingBuffer: the text being edited with its selection and composition.
    class EditingBuffer {
    public:
        static constexpr int kNowhere = -1;
        EditingBuffer(const text::AnnotatedString& text, TextRange selection, const TextBackend* backend = nullptr);
        int SelectionStart() const { return m_SelStart; }
        int SelectionEnd() const { return m_SelEnd; }
        int CompositionStart() const { return m_CompStart; }
        int CompositionEnd() const { return m_CompEnd; }
        bool HasComposition() const { return m_CompStart != kNowhere; }
        std::optional<TextRange> Composition() const { return HasComposition() ? std::optional<TextRange>(TextRange(m_CompStart, m_CompEnd)) : std::nullopt; }
        TextRange Selection() const { return { m_SelStart, m_SelEnd }; }
        // cursor: the collapsed selection, or -1.
        int Cursor() const { return m_SelStart == m_SelEnd ? m_SelEnd : -1; }
        void SetCursor(int cursor) { SetSelection(cursor, cursor); }
        char16_t operator[](int index) const { return m_Text[std::size_t(index)]; }
        int Length() const { return int(m_Text.size()); }
        void Replace(int start, int end, std::u16string_view text);
        void Delete(int start, int end);
        void SetSelection(int start, int end);
        void SetComposition(int start, int end);
        void CancelComposition();
        void CommitComposition();
        const std::u16string& Utf16() const { return m_Text; }
        std::string ToString() const;
        // The grapheme boundary before / after `index` (-1: none).
        int PrecedingBreak(int index) const;
        int FollowingBreak(int index) const;

    private:
        std::u16string m_Text;
        int m_SelStart = 0, m_SelEnd = 0, m_CompStart = kNowhere, m_CompEnd = kNowhere;
        const TextBackend* m_Backend = nullptr;
    };

    // EditCommand: one IME edit.
    class EditCommand {
    public:
        virtual ~EditCommand() = default;
        virtual void ApplyTo(EditingBuffer& buffer) const = 0;
    };
    using EditCommandPtr = std::shared_ptr<const EditCommand>;

    class CommitTextCommand final : public EditCommand {
    public:
        CommitTextCommand(std::string text, int newCursorPosition) : text(std::move(text)), newCursorPosition(newCursorPosition) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        std::string text;
        int newCursorPosition;
    };
    class SetComposingRegionCommand final : public EditCommand {
    public:
        SetComposingRegionCommand(int start, int end) : start(start), end(end) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        int start, end;
    };
    class SetComposingTextCommand final : public EditCommand {
    public:
        SetComposingTextCommand(std::string text, int newCursorPosition) : text(std::move(text)), newCursorPosition(newCursorPosition) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        std::string text;
        int newCursorPosition;
    };
    class DeleteSurroundingTextCommand final : public EditCommand {
    public:
        DeleteSurroundingTextCommand(int before, int after) : lengthBeforeCursor(std::max(0, before)), lengthAfterCursor(std::max(0, after)) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        int lengthBeforeCursor, lengthAfterCursor;
    };
    class DeleteSurroundingTextInCodePointsCommand final : public EditCommand {
    public:
        DeleteSurroundingTextInCodePointsCommand(int before, int after) : lengthBeforeCursor(std::max(0, before)), lengthAfterCursor(std::max(0, after)) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        int lengthBeforeCursor, lengthAfterCursor;
    };
    class SetSelectionCommand final : public EditCommand {
    public:
        SetSelectionCommand(int start, int end) : start(start), end(end) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        int start, end;
    };
    class FinishComposingTextCommand final : public EditCommand {
    public:
        void ApplyTo(EditingBuffer& buffer) const override { buffer.CommitComposition(); }
    };
    class BackspaceCommand final : public EditCommand {
    public:
        void ApplyTo(EditingBuffer& buffer) const override;
    };
    class MoveCursorCommand final : public EditCommand {
    public:
        explicit MoveCursorCommand(int amount) : amount(amount) {}
        void ApplyTo(EditingBuffer& buffer) const override;
        int amount;
    };
    class DeleteAllCommand final : public EditCommand {
    public:
        void ApplyTo(EditingBuffer& buffer) const override { buffer.Replace(0, buffer.Length(), u""); }
    };

    // EditProcessor: keeps the field's value and applies batches of IME commands to it.
    class EditProcessor {
    public:
        explicit EditProcessor(const TextBackend* backend = nullptr);
        // reset: a new value from the app; onUpdate (TextInputSession.updateState) hears old and new.
        void Reset(const TextFieldValue& value, const std::function<void(const TextFieldValue&, const TextFieldValue&)>& onUpdate = {});
        TextFieldValue Apply(const std::vector<EditCommandPtr>& commands);
        const TextFieldValue& ToTextFieldValue() const { return m_State; }

    private:
        TextFieldValue m_State;
        std::unique_ptr<EditingBuffer> m_Buffer;
        const TextBackend* m_Backend;
    };

}
