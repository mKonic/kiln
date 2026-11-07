#include "kiln/foundation/text/input/TextFieldKeys.h"

#include "kiln/TextBackend.h"
#include "kiln/text/Paragraph.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        // Android key codes.
        enum : int {
            kA = 29, kC = 31, kH = 36, kV = 50, kX = 52, kY = 53, kZ = 54, kTab = 61, kEnter = 66, kDel = 67,
            kBackslash = 73, kPageUp = 92, kPageDown = 93, kForwardDel = 112, kMoveHome = 122, kMoveEnd = 123,
            kInsert = 124, kNumpadEnter = 160, kUp = 19, kDown = 20, kLeft = 21, kRight = 22, kCenter = 23,
            kCut = 277, kCopy = 278, kPaste = 279,
        };
        // KeyModifiers: exactly these modifiers held.
        enum Mods : int { None = 0, Alt = 1, Ctrl = 2, Meta = 4, Shift = 8 };
        int ModsOf(const ui::KeyEvent& e) {
            return (e.Alt() ? Alt : 0) | (e.Ctrl() ? Ctrl : 0) | (e.Meta() ? Meta : 0) | (e.Shift() ? Shift : 0);
        }

        // commonKeyMapping(Ctrl).
        std::optional<KeyCommand> Common(const ui::KeyEvent& e) {
            const int m = ModsOf(e), k = e.keyCode;
            if (m == (Ctrl | Shift)) return k == kZ ? std::optional(KeyCommand::Redo) : std::nullopt;
            if (m == Ctrl) {
                switch (k) {
                    case kC: case kInsert: return KeyCommand::Copy;
                    case kV: return KeyCommand::Paste;
                    case kX: return KeyCommand::Cut;
                    case kA: return KeyCommand::SelectAll;
                    case kY: return KeyCommand::Redo;
                    case kZ: return KeyCommand::Undo;
                    default: return std::nullopt;
                }
            }
            if (m == Shift) {
                switch (k) {
                    case kLeft: return KeyCommand::SelectLeftChar;
                    case kRight: return KeyCommand::SelectRightChar;
                    case kUp: return KeyCommand::SelectUp;
                    case kDown: return KeyCommand::SelectDown;
                    case kPageUp: return KeyCommand::SelectPageUp;
                    case kPageDown: return KeyCommand::SelectPageDown;
                    case kMoveHome: return KeyCommand::SelectLineStart;
                    case kMoveEnd: return KeyCommand::SelectLineEnd;
                    case kInsert: return KeyCommand::Paste;
                    default: return std::nullopt;
                }
            }
            if (m == None) {
                switch (k) {
                    case kLeft: return KeyCommand::LeftChar;
                    case kRight: return KeyCommand::RightChar;
                    case kUp: return KeyCommand::Up;
                    case kDown: return KeyCommand::Down;
                    case kCenter: return KeyCommand::Center;
                    case kPageUp: return KeyCommand::PageUp;
                    case kPageDown: return KeyCommand::PageDown;
                    case kMoveHome: return KeyCommand::LineStart;
                    case kMoveEnd: return KeyCommand::LineEnd;
                    case kEnter: case kNumpadEnter: return KeyCommand::NewLine;
                    case kDel: return KeyCommand::DeletePrevChar;
                    case kForwardDel: return KeyCommand::DeleteNextChar;
                    case kPaste: return KeyCommand::Paste;
                    case kCut: return KeyCommand::Cut;
                    case kCopy: return KeyCommand::Copy;
                    case kTab: return KeyCommand::Tab;
                    default: return std::nullopt;
                }
            }
            return std::nullopt;
        }

        // defaultKeyMapping.
        std::optional<KeyCommand> Default(const ui::KeyEvent& e) {
            const int m = ModsOf(e), k = e.keyCode;
            if (k == kDel) {
                if (m == None || m == Shift || m == (Shift | Meta)) return KeyCommand::DeletePrevChar;
                if (m == Ctrl || m == (Ctrl | Shift)) return KeyCommand::DeletePrevWord;
            } else if (k == kEnter || k == kNumpadEnter) {
                if (m == None || m == Shift || m == Ctrl || m == (Ctrl | Shift)) return KeyCommand::NewLine;
            }
            std::optional<KeyCommand> r;
            if (m == (Ctrl | Shift)) {
                switch (k) {
                    case kLeft: r = KeyCommand::SelectLeftWord; break;
                    case kRight: r = KeyCommand::SelectRightWord; break;
                    case kUp: r = KeyCommand::SelectPrevParagraph; break;
                    case kDown: r = KeyCommand::SelectNextParagraph; break;
                    default: break;
                }
            } else if (m == Ctrl) {
                switch (k) {
                    case kLeft: r = KeyCommand::LeftWord; break;
                    case kRight: r = KeyCommand::RightWord; break;
                    case kUp: r = KeyCommand::PrevParagraph; break;
                    case kDown: r = KeyCommand::NextParagraph; break;
                    case kH: r = KeyCommand::DeletePrevChar; break;
                    case kForwardDel: r = KeyCommand::DeleteNextWord; break;
                    case kBackslash: r = KeyCommand::Deselect; break;
                    default: break;
                }
            } else if (m == Shift) {
                if (k == kMoveHome) r = KeyCommand::SelectLineStart;
                else if (k == kMoveEnd) r = KeyCommand::SelectLineEnd;
            } else if (m == Alt) {
                if (k == kForwardDel) r = KeyCommand::DeleteToLineEnd;
            }
            return r ? r : Common(e);
        }
    }

    bool EditsText(KeyCommand c) {
        switch (c) {
            case KeyCommand::Paste: case KeyCommand::Cut: case KeyCommand::DeletePrevChar: case KeyCommand::DeleteNextChar:
            case KeyCommand::DeletePrevWord: case KeyCommand::DeleteNextWord: case KeyCommand::DeleteFromLineStart:
            case KeyCommand::DeleteToLineEnd: case KeyCommand::NewLine: case KeyCommand::Tab: case KeyCommand::Undo:
            case KeyCommand::Redo: case KeyCommand::CharacterPalette:
                return true;
            default:
                return false;
        }
    }

    std::optional<KeyCommand> MapKey(const ui::KeyEvent& e) {
        const int m = ModsOf(e), k = e.keyCode;
        if (m == (Alt | Shift)) {
            switch (k) {
                case kLeft: return KeyCommand::SelectLineLeft;
                case kRight: return KeyCommand::SelectLineRight;
                case kUp: return KeyCommand::SelectHome;
                case kDown: return KeyCommand::SelectEnd;
                default: break;
            }
        } else if (m == Alt) {
            switch (k) {
                case kLeft: return KeyCommand::LineLeft;
                case kRight: return KeyCommand::LineRight;
                case kUp: return KeyCommand::Home;
                case kDown: return KeyCommand::End;
                case kDel: return KeyCommand::DeleteFromLineStart;
                default: break;
            }
        }
        return Default(e);
    }

    std::optional<char32_t> DeadKeyCombiner::Consume(std::uint32_t codePoint, const ui::Owner* owner) {
        if (codePoint & kCombiningAccent) { deadKey = int(codePoint & kAccentMask); return std::nullopt; }
        if (deadKey) {
            const int accent = *deadKey;
            deadKey.reset();
            const int combined = owner ? owner->DeadChar(accent, int(codePoint)) : 0;
            return combined != 0 ? char32_t(combined) : char32_t(codePoint);
        }
        return char32_t(codePoint);
    }

    bool IsTypedEvent(const ui::KeyEvent& e) {
        if (e.type != ui::KeyEventType::KeyDown) return false;
        const std::uint32_t u = e.unicode;
        if (u & DeadKeyCombiner::kCombiningAccent) return true;
        return !(u <= 0x1F || (u >= 0x7F && u <= 0x9F));
    }

    // --- string helpers -----------------------------------------------------------------------

    int FindCodePointOrEmojiStartBefore(std::u16string_view t, int index, int ifNotFound) {
        if (index <= 0) return ifNotFound;
        int i = index - 1;
        if (i > 0 && t[std::size_t(i)] >= 0xDC00 && t[std::size_t(i)] < 0xE000 && t[std::size_t(i) - 1] >= 0xD800 && t[std::size_t(i) - 1] < 0xDC00) --i;
        return i;
    }
    int FindParagraphStart(std::u16string_view t, int index) {
        for (int i = std::min(index, int(t.size())); i >= 1; --i) if (t[std::size_t(i) - 1] == u'\n') return i;
        return 0;
    }
    int FindParagraphEnd(std::u16string_view t, int index) {
        for (int i = std::max(index, 0); i < int(t.size()); ++i) if (t[std::size_t(i)] == u'\n') return i;
        return int(t.size());
    }

    std::pair<int, std::optional<WedgeAffinity>> CalculateNextCursorPositionAndWedgeAffinity(int proposed, int cursor,
                                                                                             const TransformedTextFieldState& state) {
        if (proposed == -1) return { cursor, std::nullopt };
        const bool forward = proposed > cursor;
        TextRange untransformed, retransformed;
        switch (state.IndexTransformation(proposed, &untransformed, &retransformed)) {
            case IndexTransformationType::Untransformed: return { proposed, forward ? WedgeAffinity::Start : WedgeAffinity::End };
            case IndexTransformationType::Deletion: return { proposed, std::nullopt };
            case IndexTransformationType::Replacement:
                return forward ? std::pair{ retransformed.end, std::optional(WedgeAffinity::Start) }
                               : std::pair{ retransformed.start, std::optional(WedgeAffinity::End) };
            case IndexTransformationType::Insertion:
                if (forward)
                    return proposed == retransformed.start ? std::pair{ proposed, std::optional(WedgeAffinity::Start) }
                                                           : std::pair{ retransformed.end, std::optional(WedgeAffinity::End) };
                return proposed == retransformed.end ? std::pair{ proposed, std::optional(WedgeAffinity::End) }
                                                     : std::pair{ retransformed.start, std::optional(WedgeAffinity::Start) };
        }
        return { proposed, std::nullopt };
    }

    // --- SelectionMovementDeletionContext -----------------------------------------------------

    SelectionMovementDeletionContext::SelectionMovementDeletionContext(TransformedTextFieldState& state,
                                                                       std::shared_ptr<const text::TextLayoutResult> layout,
                                                                       bool soft, bool hardware, float visibleHeight,
                                                                       TextFieldPreparedSelectionState& prepared, ui::Owner* owner)
        : initialValue(state.VisualText()), initialWedgeAffinity(state.selectionWedgeAffinity), selection(initialValue.selection),
          m_State(state), m_Layout(std::move(layout)), m_FromSoftKeyboard(soft), m_FromHardwareSource(hardware),
          m_VisibleHeight(visibleHeight), m_Prepared(prepared), m_Owner(owner), m_Text(initialValue.text) {}

    template <class F>
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::ApplyIfNotEmpty(bool resetCachedX, F block) {
        if (resetCachedX) m_Prepared.ResetCachedX();
        if (!m_Text.empty()) block();
        return *this;
    }

    template <class F>
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorTo(bool resetCachedX, F proposed) {
        return ApplyIfNotEmpty(resetCachedX, [&] {
            const int oldCursor = selection.end;
            const auto [next, affinity] = CalculateNextCursorPositionAndWedgeAffinity(proposed(), oldCursor, m_State);
            if (next != oldCursor || !selection.Collapsed()) selection = TextRange(next);
            if (affinity) wedgeAffinity = affinity;
        });
    }

    int SelectionMovementDeletionContext::PrecedingBreak(int index) const {
        TextBackend* b = m_Owner ? m_Owner->Text() : nullptr;
        return b ? b->CharacterBreak(m_Text, index, false) : TextBackend::SurrogateCharacterBreak(m_Text, index, false);
    }
    int SelectionMovementDeletionContext::FollowingBreak(int index) const {
        TextBackend* b = m_Owner ? m_Owner->Text() : nullptr;
        return b ? b->CharacterBreak(m_Text, index, true) : TextBackend::SurrogateCharacterBreak(m_Text, index, true);
    }

    bool SelectionMovementDeletionContext::IsLtr() const {
        if (!m_Layout) return true;
        return m_Layout->ParagraphDirection(selection.end) == text::ResolvedTextDirection::Ltr;
    }

    SelectionMovementDeletionContext& SelectionMovementDeletionContext::SelectAll() {
        return ApplyIfNotEmpty(true, [&] { selection = TextRange(0, int(m_Text.size())); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::Deselect() {
        return ApplyIfNotEmpty(true, [&] { selection = TextRange(selection.end); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorLeftByChar() { return IsLtr() ? MoveCursorPrevByChar() : MoveCursorNextByChar(); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorRightByChar() { return IsLtr() ? MoveCursorNextByChar() : MoveCursorPrevByChar(); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::CollapseLeftOr(const std::function<void(SelectionMovementDeletionContext&)>& or_) {
        return ApplyIfNotEmpty(true, [&] {
            if (selection.Collapsed()) or_(*this);
            else selection = IsLtr() ? TextRange(selection.Min()) : TextRange(selection.Max());
        });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::CollapseRightOr(const std::function<void(SelectionMovementDeletionContext&)>& or_) {
        return ApplyIfNotEmpty(true, [&] {
            if (selection.Collapsed()) or_(*this);
            else selection = IsLtr() ? TextRange(selection.Max()) : TextRange(selection.Min());
        });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorPrevByCodePointOrEmoji() {
        return MoveCursorTo(true, [&] { return FindCodePointOrEmojiStartBefore(m_Text, selection.end, -1); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorPrevByChar() { return MoveCursorTo(true, [&] { return PrecedingBreak(selection.end); }); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorNextByChar() { return MoveCursorTo(true, [&] { return FollowingBreak(selection.end); }); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorToHome() { return MoveCursorTo(true, [] { return 0; }); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorToEnd() { return MoveCursorTo(true, [&] { return int(m_Text.size()); }); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorLeftByWord() { return IsLtr() ? MoveCursorPrevByWord() : MoveCursorNextByWord(); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorRightByWord() { return IsLtr() ? MoveCursorNextByWord() : MoveCursorPrevByWord(); }

    int SelectionMovementDeletionContext::NextWordOffset(int offset) const {
        const int length = initialValue.Length();
        while (offset < length) {
            const auto word = m_Layout->WordBoundary(std::min(offset, int(m_Text.size()) - 1));
            if (word.second <= offset) { ++offset; continue; }
            return word.second;
        }
        return length;
    }
    int SelectionMovementDeletionContext::PrevWordOffset(int offset) const {
        while (offset > 0) {
            const auto word = m_Layout->WordBoundary(std::min(offset, int(m_Text.size()) - 1));
            if (word.first >= offset) { --offset; continue; }
            return word.first;
        }
        return 0;
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorNextByWord() {
        return MoveCursorTo(true, [&] { return m_Layout ? NextWordOffset(selection.end) : int(m_Text.size()); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorPrevByWord() {
        return MoveCursorTo(true, [&] { return m_Layout ? PrevWordOffset(selection.end) : 0; });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorPrevByParagraph() {
        return MoveCursorTo(true, [&] {
            int start = FindParagraphStart(m_Text, selection.Min());
            if (start == selection.Min() && start != 0) start = FindParagraphStart(m_Text, start - 1);
            return start;
        });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorNextByParagraph() {
        return MoveCursorTo(true, [&] {
            int end = FindParagraphEnd(m_Text, selection.Max());
            if (end == selection.Max() && end != int(m_Text.size())) end = FindParagraphEnd(m_Text, end + 1);
            return end;
        });
    }

    int SelectionMovementDeletionContext::JumpByLinesOffset(int lines) {
        const int current = selection.end;
        if (std::isnan(m_Prepared.cachedX)) m_Prepared.cachedX = m_Layout->Cursor(current).x;
        const int target = m_Layout->LineForOffset(current) + lines;
        if (target < 0) return INT_MIN;
        if (target >= m_Layout->LineCount()) return INT_MAX;
        const float y = m_Layout->LineBottom(target) - 1;
        const float x = m_Prepared.cachedX;
        if ((IsLtr() && x >= m_Layout->LineRight(target)) || (!IsLtr() && x <= m_Layout->LineLeft(target))) return m_Layout->LineEnd(target, true);
        return m_Layout->OffsetForPosition({ x, y });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorUpByLine() {
        const int target = m_Layout ? JumpByLinesOffset(-1) : INT_MIN;
        return MoveCursorTo(target == INT_MIN, [&] { return std::max(target, 0); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorDownByLine() {
        const int target = m_Layout ? JumpByLinesOffset(1) : INT_MAX;
        return MoveCursorTo(target == INT_MAX, [&] { return std::min(target, int(m_Text.size())); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorToLineLeftSide() { return IsLtr() ? MoveCursorToLineStart() : MoveCursorToLineEnd(); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorToLineRightSide() { return IsLtr() ? MoveCursorToLineEnd() : MoveCursorToLineStart(); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorToLineStart() {
        return MoveCursorTo(true, [&] { return m_Layout ? m_Layout->LineStart(m_Layout->LineForOffset(selection.Min())) : 0; });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorToLineEnd() {
        return MoveCursorTo(true, [&] { return m_Layout ? m_Layout->LineEnd(m_Layout->LineForOffset(selection.Max()), true) : int(m_Text.size()); });
    }
    int SelectionMovementDeletionContext::JumpByPagesOffset(int pages) const {
        const int current = initialValue.selection.end;
        if (!m_Layout || std::isnan(m_VisibleHeight)) return current;
        const auto c = m_Layout->Cursor(current);
        const float top = c.top + m_VisibleHeight * float(pages), bottom = c.bottom + m_VisibleHeight * float(pages);
        const int topLine = m_Layout->LineForVerticalPosition(top);
        const float separator = m_Layout->LineBottom(topLine);
        return std::abs(top - separator) > std::abs(bottom - separator) ? m_Layout->OffsetForPosition({ c.x, top })
                                                                       : m_Layout->OffsetForPosition({ c.x, bottom });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorUpByPage() { return MoveCursorTo(false, [&] { return JumpByPagesOffset(-1); }); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::MoveCursorDownByPage() { return MoveCursorTo(false, [&] { return JumpByPagesOffset(1); }); }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::SelectMovement() {
        return ApplyIfNotEmpty(false, [&] { selection = TextRange(initialValue.selection.start, selection.end); });
    }
    SelectionMovementDeletionContext& SelectionMovementDeletionContext::DeleteMovement() {
        return ApplyIfNotEmpty(false, [&] {
            if (!initialValue.selection.Collapsed()) {
                m_State.DeleteSelectedText(m_FromHardwareSource);
            } else {
                m_State.ReplaceText(u"", TextRange(initialValue.selection.start, selection.end), TextFieldEditUndoBehavior::MergeIfPossible,
                                    !m_FromSoftKeyboard, m_FromHardwareSource);
            }
            selection = m_State.VisualText().selection;
            wedgeAffinity = WedgeAffinity::Start;
        });
    }

    // --- TextFieldKeyEventHandler -------------------------------------------------------------

    bool TextFieldKeyEventHandler::OnPreKeyEvent(const ui::KeyEvent& e, TransformedTextFieldState& state, const Callbacks& cb) {
        // cancelsTextSelection: Back released with a selection.
        const TextRange sel = state.VisualText().selection;
        if (!sel.Collapsed() && e.keyCode == ui::keys::Back && e.type == ui::KeyEventType::KeyUp) {
            if (cb.deselect) cb.deselect();
            return true;
        }
        return false;
    }

    bool TextFieldKeyEventHandler::OnKeyEvent(const ui::KeyEvent& e, TransformedTextFieldState& state,
                                              std::shared_ptr<const text::TextLayoutResult> layout, float visibleHeight,
                                              bool editable, bool singleLine, ui::Owner* owner, const Callbacks& cb) {
        // AndroidTextFieldKeyEventHandler: a keyboard key ends the direct touch interaction.
        if (e.type == ui::KeyEventType::KeyDown && e.fromKeyboardSource && (!e.fromSoftKeyboard || !IsTypedEvent(e)))
            if (cb.endDirectTouchInteraction) cb.endDirectTouchInteraction();
        if (e.type == ui::KeyEventType::KeyUp) {
            if (m_ConsumedDownKeys.erase(e.keyCode)) return true;
            return false;
        }
        if (e.type == ui::KeyEventType::Unknown && !IsTypedEvent(e)) return false;
        const bool consumed = ProcessKeyDown(e, state, std::move(layout), visibleHeight, editable, singleLine, owner, cb);
        if (consumed) m_ConsumedDownKeys.insert(e.keyCode);
        return consumed;
    }

    bool TextFieldKeyEventHandler::ProcessKeyDown(const ui::KeyEvent& e, TransformedTextFieldState& state,
                                                  std::shared_ptr<const text::TextLayoutResult> layout, float visibleHeight,
                                                  bool editable, bool singleLine, ui::Owner* owner, const Callbacks& cb) {
        if (IsTypedEvent(e)) {
            if (const auto cp = m_DeadKeys.Consume(e.unicode, owner)) {
                std::u16string text;
                char32_t c = *cp;
                if (c >= 0x10000) { c -= 0x10000; text.push_back(char16_t(0xD800 + (c >> 10))); text.push_back(char16_t(0xDC00 + (c & 0x3FF))); }
                else text.push_back(char16_t(c));
                if (!editable) return false;
                state.ReplaceSelectedText(text, true, TextFieldEditUndoBehavior::MergeIfPossible, !e.fromSoftKeyboard, e.fromHardwareSource);
                m_Prepared.ResetCachedX();
                return true;
            }
            // A dead key: wait for the next character (Compose returns false and the IME, if
            // any, sees it; with a hardware keyboard the accent is consumed here).
            if (e.unicode & DeadKeyCombiner::kCombiningAccent) return true;
        }
        const auto command = MapKey(e);
        if (!command || (EditsText(*command) && !editable)) return false;
        SelectionMovementDeletionContext ctx(state, layout, e.fromSoftKeyboard, e.fromHardwareSource, visibleHeight, m_Prepared, owner);
        bool consumed = true;
        using K = KeyCommand;
        auto prevChar = [](SelectionMovementDeletionContext& c) { c.MoveCursorLeftByChar(); };
        auto nextChar = [](SelectionMovementDeletionContext& c) { c.MoveCursorRightByChar(); };
        switch (*command) {
            case K::Copy: case K::Paste: case K::Cut: if (cb.clipboard) cb.clipboard(*command, e.fromHardwareSource); break;
            case K::LeftChar: ctx.CollapseLeftOr(prevChar); break;
            case K::RightChar: ctx.CollapseRightOr(nextChar); break;
            case K::LeftWord: ctx.MoveCursorLeftByWord(); break;
            case K::RightWord: ctx.MoveCursorRightByWord(); break;
            case K::PrevParagraph: ctx.MoveCursorPrevByParagraph(); break;
            case K::NextParagraph: ctx.MoveCursorNextByParagraph(); break;
            case K::Up: ctx.MoveCursorUpByLine(); break;
            case K::Down: ctx.MoveCursorDownByLine(); break;
            case K::PageUp: ctx.MoveCursorUpByPage(); break;
            case K::PageDown: ctx.MoveCursorDownByPage(); break;
            case K::LineStart: ctx.MoveCursorToLineStart(); break;
            case K::LineEnd: ctx.MoveCursorToLineEnd(); break;
            case K::LineLeft: ctx.MoveCursorToLineLeftSide(); break;
            case K::LineRight: ctx.MoveCursorToLineRightSide(); break;
            case K::Home: ctx.MoveCursorToHome(); break;
            case K::End: ctx.MoveCursorToEnd(); break;
            case K::DeletePrevChar: ctx.MoveCursorPrevByCodePointOrEmoji().DeleteMovement(); break;
            case K::DeleteNextChar: ctx.MoveCursorNextByChar().DeleteMovement(); break;
            case K::DeletePrevWord: ctx.MoveCursorPrevByWord().DeleteMovement(); break;
            case K::DeleteNextWord: ctx.MoveCursorNextByWord().DeleteMovement(); break;
            case K::DeleteFromLineStart: ctx.MoveCursorToLineStart().DeleteMovement(); break;
            case K::DeleteToLineEnd: ctx.MoveCursorToLineEnd().DeleteMovement(); break;
            case K::NewLine:
                if (!singleLine) state.ReplaceSelectedText(u"\n", true, TextFieldEditUndoBehavior::MergeIfPossible, !e.fromSoftKeyboard, e.fromHardwareSource);
                else consumed = cb.onSubmit ? cb.onSubmit() : false;
                break;
            case K::Tab:
                if (!singleLine) state.ReplaceSelectedText(u"\t", true, TextFieldEditUndoBehavior::MergeIfPossible, !e.fromSoftKeyboard, e.fromHardwareSource);
                else consumed = false;   // to the focus system
                break;
            case K::SelectAll: ctx.SelectAll(); break;
            case K::SelectLeftChar: ctx.MoveCursorLeftByChar().SelectMovement(); break;
            case K::SelectRightChar: ctx.MoveCursorRightByChar().SelectMovement(); break;
            case K::SelectLeftWord: ctx.MoveCursorLeftByWord().SelectMovement(); break;
            case K::SelectRightWord: ctx.MoveCursorRightByWord().SelectMovement(); break;
            case K::SelectPrevParagraph: ctx.MoveCursorPrevByParagraph().SelectMovement(); break;
            case K::SelectNextParagraph: ctx.MoveCursorNextByParagraph().SelectMovement(); break;
            case K::SelectLineStart: ctx.MoveCursorToLineStart().SelectMovement(); break;
            case K::SelectLineEnd: ctx.MoveCursorToLineEnd().SelectMovement(); break;
            case K::SelectLineLeft: ctx.MoveCursorToLineLeftSide().SelectMovement(); break;
            case K::SelectLineRight: ctx.MoveCursorToLineRightSide().SelectMovement(); break;
            case K::SelectUp: ctx.MoveCursorUpByLine().SelectMovement(); break;
            case K::SelectDown: ctx.MoveCursorDownByLine().SelectMovement(); break;
            case K::SelectPageUp: ctx.MoveCursorUpByPage().SelectMovement(); break;
            case K::SelectPageDown: ctx.MoveCursorDownByPage().SelectMovement(); break;
            case K::SelectHome: ctx.MoveCursorToHome().SelectMovement(); break;
            case K::SelectEnd: ctx.MoveCursorToEnd().SelectMovement(); break;
            case K::Deselect: ctx.Deselect(); break;
            case K::Undo: state.Undo(); break;
            case K::Redo: state.Redo(); break;
            case K::CharacterPalette: break;   // no such thing on Android
            case K::Center: if (cb.showKeyboard) cb.showKeyboard(); break;
        }
        if (*command == K::Up || *command == K::Down || *command == K::LeftChar || *command == K::RightChar)
            consumed = ctx.initialValue.selection != ctx.selection;
        if (ctx.selection != ctx.initialValue.selection) state.SelectCharsIn(ctx.selection);
        if (ctx.wedgeAffinity) {
            if (state.UntransformedText().selection.Collapsed()) state.selectionWedgeAffinity = { *ctx.wedgeAffinity, *ctx.wedgeAffinity };
            else state.selectionWedgeAffinity = { ctx.initialWedgeAffinity.startAffinity, *ctx.wedgeAffinity };
        }
        return consumed;
    }

}
