#pragma once

// TextFieldSelectionState (Compose's text/input/internal/selection): a text field's cursor and
// focus rectangles; taps, long-press and mouse selection, double and triple taps; the cursor and
// selection handles and their drags; the floating text toolbar (shown for the selection or the
// cursor while the content is visible); and the clipboard commands.

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/foundation/text/input/TextLayoutState.h"

#include <cmath>
#include <functional>
#include <memory>

namespace kiln::ui { class Owner; }

namespace kiln::foundation {

    class ReceiveContentConfiguration;
    class PlatformSelectionBehaviors;
    class TextContextMenuToolbarHandler;
    class TextContextMenuBuilderScope;

    enum class TextToolbarState : std::uint8_t { None, Cursor, Selection };
    enum class TextFieldHandle : std::uint8_t { Cursor, SelectionStart, SelectionEnd };

    // TextFieldHandleState: whether a handle shows, where (in the text layout node), its line
    // height and run direction.
    struct TextFieldHandleState {
        bool visible = false;
        Offset position{ NAN, NAN };
        float lineHeight = 0;
        text::ResolvedTextDirection direction = text::ResolvedTextDirection::Ltr;
        bool handlesCrossed = false;
        bool operator==(const TextFieldHandleState& o) const {
            auto same = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
            return visible == o.visible && same(position.x, o.position.x) && same(position.y, o.position.y) && lineHeight == o.lineHeight &&
                   direction == o.direction && handlesCrossed == o.handlesCrossed;
        }
    };

    class TextFieldSelectionState {
    public:
        enum class InputType : std::uint8_t { None, Touch, Mouse };

        TextFieldSelectionState(TransformedTextFieldState& state, TextLayoutState& layout) : textFieldState(state), textLayoutState(layout) {}
        ~TextFieldSelectionState();

        TransformedTextFieldState& textFieldState;
        TextLayoutState& textLayoutState;
        ui::Owner* owner = nullptr;   // clipboard, haptics and the text toolbar
        float density = 1;

        bool isWindowAndTextFieldFocused = false;
        bool enabled = true, readOnly = false, isPassword = false;
        bool Editable() const { return enabled && !readOnly; }

        // Whether the last pointer was a finger (not a mouse or touchpad), and observable.
        bool IsDirectTouchInteraction() const { return m_DirectTouch.Get(); }
        void SetDirectTouchInteraction(bool v);
        bool ShowCursorHandle() const { return m_ShowCursorHandle.Get(); }
        void SetShowCursorHandle(bool v) { if (m_ShowCursorHandle.Peek() != v) m_ShowCursorHandle.Set(v); }
        std::optional<TextFieldHandle> DraggingHandle() const { return m_DraggingHandle.Get(); }
        InputType DirectDragGestureInitiator() const { return m_DragInitiator.Get(); }
        std::shared_ptr<MutableInteractionSource> interactionSource;
        // The receiveContent modifiers around the field (pastes go through them first).
        std::function<ReceiveContentConfiguration*()> receiveContentConfiguration;
        // Smart selection (null: none), and the toolbar handler the core node keeps
        // (ToolbarRequester).
        PlatformSelectionBehaviors* platformSelectionBehaviors = nullptr;
        TextContextMenuToolbarHandler* toolbarHandler = nullptr;
        // requestAutofillAction (null: no Autofill item).
        std::function<void()> requestAutofill;

        void Update(ui::Owner* owner, float density, bool enabled, bool readOnly, bool isPassword);

        // In the text layout node.
        Rect GetCursorRect() const;
        Rect GetCursorRectInLayout(const text::TextLayoutResult& layout, int offset) const;
        // In the decorator (the whole field when not focused: UnsetFocusRect).
        std::optional<Rect> GetFocusRect() const;
        TextFieldHandleState GetCursorHandleState(bool includePosition) const;
        TextFieldHandleState GetSelectionHandleState(bool isStartHandle, bool includePosition) const;
        // handleDragPosition: the dragged handle's position in the text layout node.
        Offset HandleDragPosition() const;

        // placeCursorAtNearestOffset: `offset` in the text layout node; false when nothing changed.
        bool PlaceCursorAtNearestOffset(Offset offset);

        void UpdateTextToolbarState(TextToolbarState s);
        TextToolbarState ToolbarState() const { return m_ToolbarState.Get(); }

        bool IsCutAllowed() const;
        bool IsCopyAllowed() const;
        bool IsPasteAllowed() const { return Editable(); }
        // ClipboardPasteState: what the clipboard held when the toolbar last showed.
        void UpdateClipboardEntry();
        bool CanShowPasteMenuItem() const;
        bool CanShowAutofillMenuItem() const;
        void Autofill() { if (requestAutofill) requestAutofill(); }
        bool CanShowLookUpMenuItem() const;
        void LookUp();
        // Whether the toolbar is up (TextContextMenuToolbarHandlerNode's onShow / onHide).
        bool TextToolbarShown() const { return m_TextToolbarShown.Get(); }
        void SetTextToolbarShown(bool v) { if (m_TextToolbarShown.Peek() != v) m_TextToolbarShown.Set(v); }
        // addBasicTextFieldTextContextMenuComponents' builder.
        void AddTextContextMenuComponents(TextContextMenuBuilderScope& scope);
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
        // textFieldSelectionGestures: long-press and mouse selection, double and triple taps.
        rt::Task<> TextFieldSelectionGestures(PointerInputScope& scope, std::function<void()> requestFocus);
        // cursorHandleGestures / selectionHandleGestures, one detector each (the handle chains a
        // pointer input per detector, with DetectDirectTouchInteraction).
        rt::Task<> CursorHandleDragGestures(PointerInputScope& scope);
        rt::Task<> CursorHandleTapGestures(PointerInputScope& scope);
        rt::Task<> SelectionHandlePressGestures(PointerInputScope& scope, bool isStartHandle);
        rt::Task<> SelectionHandleDragGestures(PointerInputScope& scope, bool isStartHandle);

        // startToolbarAndHandlesVisibilityObserver (while focused): a text change hides the
        // cursor handle and the toolbar; the toolbar follows the visible content bounds.
        void StartToolbarAndHandlesVisibilityObserver();
        void StopToolbarAndHandlesVisibilityObserver();
        // Something the toolbar's bounds read changed (the text, a layout, a move).
        void OnContentChanged();
        // The toolbar's content bounds in the root (null: the toolbar hides).
        std::optional<Rect> DerivedVisibleContentBounds() const;

        // Internal (the gesture observers).
        void UpdateHandleDragging(TextFieldHandle handle, Offset position);
        void ClearHandleDragging();
        void MarkStartContentVisibleOffset();
        void SetDirectDragGestureInitiator(InputType t) { if (m_DragInitiator.Peek() != t) { m_DragInitiator.Set(t); ScheduleToolbarUpdate(); } }
        TextRange UpdateSelection(const TextFieldCharSequence& text, int startOffset, int endOffset, bool isStartHandle,
                                  selection::Adjustment adjustment, bool allowPreviousSelectionCollapsed, bool isStartOfSelection,
                                  std::optional<int> hapticFeedback);
        void MaybeSuggestSelectionRange();
        void ResetPreviousRawDragOffset() { m_PreviousRawDragOffset = -1; }
        Offset GetHandlePosition(bool isStartHandle) const;
        void PerformHaptic(int constant) const;

    private:
        Rect CalculateCursorRect(const text::TextLayoutResult& layout, const TextFieldCharSequence& visual) const;
        Rect CalculateSelectionRect(const text::TextLayoutResult& layout, const TextFieldCharSequence& visual) const;
        TextRange GetTextFieldSelection(int rawStart, int rawEnd, std::optional<TextRange> previous, bool isStartHandle, selection::Adjustment a);
        Rect GetContentRect() const;
        Offset CurrentTextLayoutPositionInWindow() const;
        bool IsCursorHandleInVisibleBounds() const;
        void ScheduleToolbarUpdate();
        void ShowTextToolbar();
        void HideTextToolbar();

        rt::State<bool> m_DirectTouch{ true };
        rt::State<bool> m_ShowCursorHandle{ false };
        rt::State<TextToolbarState> m_ToolbarState{ TextToolbarState::None };
        rt::State<std::optional<TextFieldHandle>> m_DraggingHandle{ std::nullopt };
        rt::State<InputType> m_DragInitiator{ InputType::None };
        rt::State<Offset> m_RawHandleDragPosition{ Offset{ NAN, NAN } };
        Offset m_StartTextLayoutPositionInWindow{ NAN, NAN };
        std::uint64_t m_Press = 0;
        std::optional<selection::SelectionLayout> m_PreviousSelectionLayout;
        int m_PreviousRawDragOffset = -1;
        mutable const text::TextLayoutResult* m_CursorCacheLayout = nullptr;
        mutable int m_CursorCacheOffset = -1;
        mutable Rect m_CursorCacheValue;
        // The visibility observer.
        bool m_Observing = false;
        bool m_UpdatePosted = false;
        std::optional<std::u16string> m_ObservedText;
        rt::State<bool> m_TextToolbarShown{ false };
        rt::State<bool> m_PasteHasText{ false }, m_PasteHasClip{ false };
        rt::Job m_SuggestJob;
        std::shared_ptr<char> m_Alive = std::make_shared<char>(0);
    };

    // getAdjustedCoordinates: a line's bottom is the next line's top; one pixel up hits the line.
    inline Offset GetAdjustedCoordinates(Offset p) { return { p.x, p.y - 1 }; }

}
