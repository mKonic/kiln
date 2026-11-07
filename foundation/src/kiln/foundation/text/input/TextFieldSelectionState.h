#pragma once

// TextFieldSelectionState: a text field's cursor and focus rectangles, tap to place the cursor,
// the clipboard commands (copy, cut, paste, select all) and the text toolbar state. Selection
// gestures, handles and the magnifier follow Compose in the same class (PLAN M8).

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/text/input/TextLayoutState.h"

#include <functional>
#include <memory>

namespace kiln::ui { class Owner; }

namespace kiln::foundation {

    enum class TextToolbarState : std::uint8_t { None, Cursor, Selection };

    class TextFieldSelectionState {
    public:
        TextFieldSelectionState(TransformedTextFieldState& state, TextLayoutState& layout) : textFieldState(state), textLayoutState(layout) {}

        TransformedTextFieldState& textFieldState;
        TextLayoutState& textLayoutState;
        ui::Owner* owner = nullptr;   // clipboard, haptics and the text toolbar
        float density = 1;

        bool isWindowAndTextFieldFocused = false;
        bool enabled = true, readOnly = false, isPassword = false;
        bool Editable() const { return enabled && !readOnly; }

        // Whether the last pointer was a finger (not a mouse or touchpad), and observable.
        bool IsDirectTouchInteraction() const { return m_DirectTouch.Get(); }
        void SetDirectTouchInteraction(bool v) { if (m_DirectTouch.Peek() != v) m_DirectTouch.Set(v); }
        bool showCursorHandle = false;
        std::shared_ptr<MutableInteractionSource> interactionSource;

        void Update(ui::Owner* owner, float density, bool enabled, bool readOnly, bool isPassword);

        // In the text layout node.
        Rect GetCursorRect() const;
        Rect GetCursorRectInLayout(const text::TextLayoutResult& layout, int offset) const;
        // In the decorator (the whole field when not focused: UnsetFocusRect).
        std::optional<Rect> GetFocusRect() const;

        // placeCursorAtNearestOffset: `offset` in the text layout node; false when nothing changed.
        bool PlaceCursorAtNearestOffset(Offset offset);

        void UpdateTextToolbarState(TextToolbarState s) { m_ToolbarState.Set(s); }
        TextToolbarState ToolbarState() const { return m_ToolbarState.Get(); }

        bool IsCutAllowed() const;
        bool IsCopyAllowed() const;
        bool IsPasteAllowed() const { return Editable(); }
        void Cut();
        void Copy(bool cancelSelection = true);
        void Paste(bool isFromHardwareSource = false);
        bool CanShowSelectAllMenuItem() const;
        void SelectAll() { textFieldState.SelectAll(); }
        void Deselect();
        void Dispose();

        // detectTextFieldTapGestures (detectTapAndPress): a tap focuses, shows the keyboard and
        // places the cursor; presses go to the interaction source.
        rt::Task<> DetectTextFieldTapGestures(PointerInputScope& scope, std::function<void()> requestFocus, std::function<void()> showKeyboard);
        // detectDirectTouchInteraction: each event's pointer type, seen first.
        rt::Task<> DetectDirectTouchInteraction(PointerInputScope& scope);

    private:
        Rect CalculateCursorRect(const text::TextLayoutResult& layout, const TextFieldCharSequence& visual) const;
        Rect CalculateSelectionRect(const text::TextLayoutResult& layout, const TextFieldCharSequence& visual) const;

        rt::State<bool> m_DirectTouch{ true };
        rt::State<TextToolbarState> m_ToolbarState{ TextToolbarState::None };
        std::uint64_t m_Press = 0;
        mutable const text::TextLayoutResult* m_CursorCacheLayout = nullptr;
        mutable int m_CursorCacheOffset = -1;
        mutable Rect m_CursorCacheValue;
    };

}
