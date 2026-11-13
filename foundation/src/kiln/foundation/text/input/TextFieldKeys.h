#pragma once

// Hardware keys in a text field (TextFieldKeyEventHandler, KeyMapping, TextPreparedSelection):
// typed characters (with dead keys), and the key commands (cursor movement by character, word,
// line, paragraph and page; selection; deletion; clipboard; undo) mapped from Android's keys.

#include "kiln/foundation/text/TextFieldState.h"
#include <kiln/Function.h>
#include "kiln/ui/Focus.h"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>

namespace kiln::text { class TextLayoutResult; }
namespace kiln::ui { class Owner; }

namespace kiln::foundation {

    enum class KeyCommand : std::uint8_t {
        LeftChar, RightChar, RightWord, LeftWord, NextParagraph, PrevParagraph, LineStart, LineEnd, LineLeft, LineRight,
        Up, Down, Center, PageUp, PageDown, Home, End, Copy, Paste, Cut, DeletePrevChar, DeleteNextChar, DeletePrevWord,
        DeleteNextWord, DeleteFromLineStart, DeleteToLineEnd, SelectAll, SelectLeftChar, SelectRightChar, SelectUp,
        SelectDown, SelectPageUp, SelectPageDown, SelectHome, SelectEnd, SelectLeftWord, SelectRightWord,
        SelectNextParagraph, SelectPrevParagraph, SelectLineStart, SelectLineEnd, SelectLineLeft, SelectLineRight,
        Deselect, NewLine, Tab, Undo, Redo, CharacterPalette,
    };
    bool EditsText(KeyCommand c);
    // platformDefaultKeyMapping (Android).
    std::optional<KeyCommand> MapKey(const ui::KeyEvent& e);

    // DeadKeyCombiner (Android): a dead key's accent waits for the next typed character and
    // combines with it (KeyCharacterMap.getDeadChar), falling back to that character.
    struct DeadKeyCombiner {
        static constexpr std::uint32_t kCombiningAccent = 0x80000000u, kAccentMask = 0x7FFFFFFFu;
        std::optional<int> deadKey;
        std::optional<char32_t> Consume(std::uint32_t codePoint, const ui::Owner* owner);
    };
    // KeyEvent.isTypedEvent: a key down whose character is not an ISO control.
    bool IsTypedEvent(const ui::KeyEvent& e);

    // TextFieldPreparedSelectionState: the x kept while moving up and down lines.
    struct TextFieldPreparedSelectionState {
        float cachedX = NAN;
        void ResetCachedX() { cachedX = NAN; }
    };

    // SelectionMovementDeletionContext: a key command's movement of the cursor in the visual
    // text, then selecting or deleting what it moved over.
    class SelectionMovementDeletionContext {
    public:
        SelectionMovementDeletionContext(TransformedTextFieldState& state, std::shared_ptr<const text::TextLayoutResult> layout,
                                         bool isFromSoftKeyboard, bool isFromHardwareSource, float visibleTextLayoutHeight,
                                         TextFieldPreparedSelectionState& prepared, ui::Owner* owner);
        TextFieldCharSequence initialValue;
        SelectionWedgeAffinity initialWedgeAffinity;
        TextRange selection;
        std::optional<WedgeAffinity> wedgeAffinity;

        SelectionMovementDeletionContext& SelectAll();
        SelectionMovementDeletionContext& Deselect();
        SelectionMovementDeletionContext& MoveCursorLeftByChar();
        SelectionMovementDeletionContext& MoveCursorRightByChar();
        SelectionMovementDeletionContext& CollapseLeftOr(const kiln::Function<void(SelectionMovementDeletionContext&)>& or_);
        SelectionMovementDeletionContext& CollapseRightOr(const kiln::Function<void(SelectionMovementDeletionContext&)>& or_);
        SelectionMovementDeletionContext& MoveCursorPrevByCodePointOrEmoji();
        SelectionMovementDeletionContext& MoveCursorPrevByChar();
        SelectionMovementDeletionContext& MoveCursorNextByChar();
        SelectionMovementDeletionContext& MoveCursorToHome();
        SelectionMovementDeletionContext& MoveCursorToEnd();
        SelectionMovementDeletionContext& MoveCursorLeftByWord();
        SelectionMovementDeletionContext& MoveCursorRightByWord();
        SelectionMovementDeletionContext& MoveCursorNextByWord();
        SelectionMovementDeletionContext& MoveCursorPrevByWord();
        SelectionMovementDeletionContext& MoveCursorPrevByParagraph();
        SelectionMovementDeletionContext& MoveCursorNextByParagraph();
        SelectionMovementDeletionContext& MoveCursorUpByLine();
        SelectionMovementDeletionContext& MoveCursorDownByLine();
        SelectionMovementDeletionContext& MoveCursorToLineLeftSide();
        SelectionMovementDeletionContext& MoveCursorToLineRightSide();
        SelectionMovementDeletionContext& MoveCursorToLineStart();
        SelectionMovementDeletionContext& MoveCursorToLineEnd();
        SelectionMovementDeletionContext& MoveCursorUpByPage();
        SelectionMovementDeletionContext& MoveCursorDownByPage();
        SelectionMovementDeletionContext& SelectMovement();
        SelectionMovementDeletionContext& DeleteMovement();

    private:
        bool IsLtr() const;
        template <class F> SelectionMovementDeletionContext& MoveCursorTo(bool resetCachedX, F proposed);
        template <class F> SelectionMovementDeletionContext& ApplyIfNotEmpty(bool resetCachedX, F block);
        int PrecedingBreak(int index) const;
        int FollowingBreak(int index) const;
        int NextWordOffset(int offset) const;
        int PrevWordOffset(int offset) const;
        int JumpByLinesOffset(int lines);
        int JumpByPagesOffset(int pages) const;

        TransformedTextFieldState& m_State;
        std::shared_ptr<const text::TextLayoutResult> m_Layout;
        bool m_FromSoftKeyboard, m_FromHardwareSource;
        float m_VisibleHeight;
        TextFieldPreparedSelectionState& m_Prepared;
        ui::Owner* m_Owner;
        std::u16string m_Text;
    };

    // calculateNextCursorPositionAndWedgeAffinity: a proposed cursor moved off the inside of a
    // transformed range, and the affinity it lands with.
    std::pair<int, std::optional<WedgeAffinity>> CalculateNextCursorPositionAndWedgeAffinity(int proposed, int cursor,
                                                                                             const TransformedTextFieldState& state);

    // findCodePointOrEmojiStartBefore / findParagraphStart / findParagraphEnd.
    int FindCodePointOrEmojiStartBefore(std::u16string_view text, int index, int ifNotFound);
    int FindParagraphStart(std::u16string_view text, int index);
    int FindParagraphEnd(std::u16string_view text, int index);

    class TextFieldKeyEventHandler {
    public:
        struct Callbacks {
            kiln::Function<void(KeyCommand, bool hardware)> clipboard;   // copy / cut / paste
            kiln::Function<bool()> onSubmit;                             // single line Enter
            kiln::Function<void()> deselect;
            kiln::Function<void()> showKeyboard;
            kiln::Function<void()> endDirectTouchInteraction;            // a keyboard key leaves touch mode
        };
        bool OnPreKeyEvent(const ui::KeyEvent& e, TransformedTextFieldState& state, const Callbacks& cb);
        bool OnKeyEvent(const ui::KeyEvent& e, TransformedTextFieldState& state, std::shared_ptr<const text::TextLayoutResult> layout,
                        float visibleTextLayoutHeight, bool editable, bool singleLine, ui::Owner* owner, const Callbacks& cb);
    private:
        bool ProcessKeyDown(const ui::KeyEvent& e, TransformedTextFieldState& state, std::shared_ptr<const text::TextLayoutResult> layout,
                            float visibleTextLayoutHeight, bool editable, bool singleLine, ui::Owner* owner, const Callbacks& cb);
        TextFieldPreparedSelectionState m_Prepared;
        DeadKeyCombiner m_DeadKeys;
        std::unordered_set<int> m_ConsumedDownKeys;
    };

}
