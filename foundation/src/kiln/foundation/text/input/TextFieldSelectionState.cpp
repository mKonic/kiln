#include "kiln/foundation/text/input/TextFieldSelectionState.h"
#include <kiln/Function.h>

#include "kiln/foundation/content/ReceiveContent.h"
#include "kiln/foundation/text/PlatformSelectionBehaviors.h"
#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        constexpr float kDefaultCursorThicknessDp = 2;

        bool ContainsInclusive(const Rect& r, Offset p) { return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom; }
        float DistanceSquaredToClosestCornerFromOutside(Offset o, const Rect& r) {
            if (ContainsInclusive(r, o)) return 0;
            float d = INFINITY;
            for (const Offset c : { Offset{ r.left, r.top }, Offset{ r.right, r.top }, Offset{ r.left, r.bottom }, Offset{ r.right, r.bottom } }) {
                const float dx = c.x - o.x, dy = c.y - o.y;
                d = std::min(d, dx * dx + dy * dy);
            }
            return d;
        }
        // findClosestRect: -1 when rect1 is closer, 1 when rect2 is, 0 when equal.
        int FindClosestRect(Offset o, const Rect& a, const Rect& b) {
            const float da = DistanceSquaredToClosestCornerFromOutside(o, a), db = DistanceSquaredToClosestCornerFromOutside(o, b);
            if (da == db) return 0;
            return da < db ? -1 : 1;
        }
        Rect CursorRectOf(const text::TextLayoutResult& l, int offset) {
            const CursorRect c = l.Cursor(offset);
            return { c.x, c.top, c.x, c.bottom };
        }
        bool IsMouseOrTouchPad(const PointerEvent& e) {
            if (e.changes.empty()) return false;
            for (const auto& c : e.changes) if (c.type != PointerType::Mouse) return false;
            return true;
        }
    }

    void TextFieldSelectionState::Update(ui::Owner* o, float d, bool en, bool ro, bool pw) {
        if (!en) HideTextToolbar();
        owner = o;
        density = d;
        enabled = en;
        readOnly = ro;
        isPassword = pw;
    }

    Rect TextFieldSelectionState::GetCursorRect() const {
        const auto layout = textLayoutState.LayoutResult();
        if (!layout) return {};
        return CalculateCursorRect(*layout, textFieldState.VisualText());
    }

    std::optional<Rect> TextFieldSelectionState::GetFocusRect() const {
        const auto layout = textLayoutState.LayoutResult();
        if (!layout) return Rect{};
        if (!isWindowAndTextFieldFocused) return std::nullopt;   // UnsetFocusRect: the whole field
        const TextFieldCharSequence value = textFieldState.VisualText();
        const Rect r = value.selection.Collapsed() ? CalculateCursorRect(*layout, value) : CalculateSelectionRect(*layout, value);
        return textLayoutState.FromTextLayoutToDecoration(r);
    }

    Rect TextFieldSelectionState::GetCursorRectInLayout(const text::TextLayoutResult& layout, int offset) const {
        if (&layout == m_CursorCacheLayout && offset == m_CursorCacheOffset) return m_CursorCacheValue;
        const Rect r = CursorRectOf(layout, offset);
        m_CursorCacheLayout = &layout;
        m_CursorCacheOffset = offset;
        m_CursorCacheValue = r;
        return r;
    }

    Rect TextFieldSelectionState::CalculateCursorRect(const text::TextLayoutResult& layout, const TextFieldCharSequence& visual) const {
        if (!visual.selection.Collapsed()) return {};
        const int offset = std::clamp(visual.selection.start, 0, layout.LayoutInput().text.Length());
        const Rect cursor = GetCursorRectInLayout(layout, offset);
        const float width = std::max(1.0f, std::floor(kDefaultCursorThicknessDp * density));
        const float centerX = layout.LayoutInput().layoutDirection == ui::LayoutDirection::Ltr ? cursor.left + width / 2 : cursor.right - width / 2;
        float x = std::max(std::min(centerX, float(layout.Size().width) - width / 2), width / 2);
        // An odd width draws on a pixel's middle to avoid blurring.
        x = int(width) % 2 == 1 ? std::floor(x) + 0.5f : std::round(x);
        return { x - width / 2, cursor.top, x + width / 2, cursor.bottom };
    }

    Rect TextFieldSelectionState::CalculateSelectionRect(const text::TextLayoutResult& layout, const TextFieldCharSequence& visual) const {
        if (visual.selection.Collapsed()) return {};
        const int length = layout.LayoutInput().text.Length();
        const int start = std::clamp(visual.selection.start, 0, length), end = std::clamp(visual.selection.end, 0, length);
        const int lineStart = layout.LineForOffset(start), lineEnd = layout.LineForOffset(end);
        if (lineStart == lineEnd) {
            const float a = layout.HorizontalPosition(start, true), b = layout.HorizontalPosition(end, true);
            return { std::min(a, b), layout.LineTop(lineStart), std::max(a, b), layout.LineBottom(lineEnd) };
        }
        // getPathForRange(...).getBounds()
        Rect bounds{ INFINITY, INFINITY, -INFINITY, -INFINITY };
        for (const RectF& r : layout.SelectionRects(std::min(start, end), std::max(start, end))) {
            bounds.left = std::min(bounds.left, r.x);
            bounds.top = std::min(bounds.top, r.y);
            bounds.right = std::max(bounds.right, r.x + r.w);
            bounds.bottom = std::max(bounds.bottom, r.y + r.h);
        }
        return bounds.left <= bounds.right ? bounds : Rect{};
    }

    bool TextFieldSelectionState::PlaceCursorAtNearestOffset(Offset offset) {
        const auto layout = textLayoutState.LayoutResult();
        if (!layout) return false;
        const int index = layout->OffsetForPosition(offset);
        if (index == -1) return false;
        std::optional<SelectionWedgeAffinity> newAffinity;
        TextRange untransformed, retransformed;
        int cursor = index;
        switch (textFieldState.IndexTransformation(index, &untransformed, &retransformed)) {
            case IndexTransformationType::Untransformed:
            case IndexTransformationType::Deletion:
                cursor = untransformed.start;
                break;
            case IndexTransformationType::Insertion: {
                const int closer = FindClosestRect(offset, CursorRectOf(*layout, retransformed.start), CursorRectOf(*layout, retransformed.end));
                const WedgeAffinity a = closer < 0 ? WedgeAffinity::Start : WedgeAffinity::End;
                newAffinity = SelectionWedgeAffinity{ a, a };
                cursor = untransformed.start;
                break;
            }
            case IndexTransformationType::Replacement: {
                const int closer = FindClosestRect(offset, CursorRectOf(*layout, retransformed.start), CursorRectOf(*layout, retransformed.end));
                cursor = closer < 0 ? untransformed.start : untransformed.end;
                break;
            }
        }
        const TextRange range(cursor);
        if (range == textFieldState.UntransformedText().selection && (!newAffinity || *newAffinity == textFieldState.selectionWedgeAffinity))
            return false;
        textFieldState.SelectUntransformedCharsIn(range);
        if (newAffinity) textFieldState.selectionWedgeAffinity = *newAffinity;
        return true;
    }

    bool TextFieldSelectionState::IsCutAllowed() const { return !textFieldState.VisualText().selection.Collapsed() && Editable() && !isPassword; }
    bool TextFieldSelectionState::IsCopyAllowed() const { return !textFieldState.VisualText().selection.Collapsed() && !isPassword; }

    void TextFieldSelectionState::Cut() {
        if (!IsCutAllowed()) return;
        const std::u16string selected(textFieldState.VisualText().SelectedText());
        textFieldState.DeleteSelectedText();
        if (owner) owner->SetClipboardText(ToUtf8(selected));
    }

    void TextFieldSelectionState::Copy(bool cancelSelection) {
        if (!IsCopyAllowed()) return;
        const std::u16string selected(textFieldState.VisualText().SelectedText());
        if (cancelSelection) textFieldState.CollapseSelectionToMax();
        if (owner) owner->SetClipboardText(ToUtf8(selected));
    }

    void TextFieldSelectionState::Paste(bool isFromHardwareSource) {
        if (!IsPasteAllowed() || !owner) return;
        ReceiveContentConfiguration* config = receiveContentConfiguration ? receiveContentConfiguration() : nullptr;
        auto clip = config ? owner->ClipboardClip() : std::nullopt;
        if (!config || !clip) {
            // pasteAsPlainText
            const auto text = owner->ClipboardText();
            if (!text) return;
            textFieldState.ReplaceSelectedText(ToUtf16(*text), false, TextFieldEditUndoBehavior::NeverMerge, true, isFromHardwareSource);
            return;
        }
        auto content = std::make_shared<TransferableContent>(std::move(clip->first), std::move(clip->second), TransferableContent::Source::Clipboard);
        const TransferableContentPtr remaining = config->Listener().OnReceive(content);
        if (!remaining) return;
        if (const auto text = remaining->ReadPlainText())
            textFieldState.ReplaceSelectedText(ToUtf16(*text), false, TextFieldEditUndoBehavior::NeverMerge, true, isFromHardwareSource);
    }

    bool TextFieldSelectionState::CanShowSelectAllMenuItem() const {
        const TextFieldCharSequence v = textFieldState.VisualText();
        return v.selection.Length() != v.Length();
    }

    void TextFieldSelectionState::Deselect() {
        if (!textFieldState.VisualText().selection.Collapsed()) textFieldState.CollapseSelectionToEnd();
        SetShowCursorHandle(false);
        UpdateTextToolbarState(TextToolbarState::None);
    }

    void TextFieldSelectionState::Dispose() {
        HideTextToolbar();
    }

    TextFieldSelectionState::~TextFieldSelectionState() { m_SuggestJob.Cancel(); }

    rt::Task<> TextFieldSelectionState::DetectDirectTouchInteraction(PointerInputScope& scope) {
        for (;;) {
            const auto e = co_await scope.AwaitPointerEvent(PointerEventPass::Initial);
            if (e) SetDirectTouchInteraction(!IsMouseOrTouchPad(*e));
        }
    }

    rt::Task<> TextFieldSelectionState::DetectTextFieldTapGestures(PointerInputScope& scope, kiln::Function<void()> requestFocus,
                                                                   kiln::Function<void()> showKeyboard) {
        TapCallbacks cb;
        cb.onTap = [this, requestFocus, showKeyboard](Offset offset) {
            requestFocus();
            if (enabled && isWindowAndTextFieldFocused) {
                if (!readOnly) {
                    showKeyboard();
                    if (textFieldState.VisualText().Length() > 0) SetShowCursorHandle(true);
                }
                // No text toolbar after a tap.
                UpdateTextToolbarState(TextToolbarState::None);
                const Offset coerced = textLayoutState.CoercedInVisibleBoundsOfInputText(offset);
                PlaceCursorAtNearestOffset(textLayoutState.FromDecorationToTextLayout(coerced));
            }
        };
        cb.onPress = [this](Offset offset) {
            if (!interactionSource) return;
            // Drop an interaction a missed stop or cancel left behind.
            if (m_Press) interactionSource->Cancel(Interaction::Kind::Press, m_Press);
            m_Press = interactionSource->Start(Interaction::Kind::Press, offset);
        };
        cb.onPressEnd = [this](bool released) {
            if (!interactionSource || !m_Press) return;
            if (released) interactionSource->End(Interaction::Kind::Press, m_Press);
            else interactionSource->Cancel(Interaction::Kind::Press, m_Press);
            m_Press = 0;
        };
        co_await DetectTapGestures(scope, std::move(cb));
    }


    // --- state ---------------------------------------------------------------------------------

    namespace {
        constexpr int kHapticLongPress = 0, kHapticTextHandleMove = 9;   // HapticFeedbackConstants
        bool IsSpecified(Offset o) { return !std::isnan(o.x) && !std::isnan(o.y); }
        // Runs `fn` when the coroutine frame holding it goes (a cancelled gesture).
        struct Finally {
            kiln::Function<void()> fn;
            ~Finally() { if (fn) fn(); }
        };
    }

    void TextFieldSelectionState::SetDirectTouchInteraction(bool v) {
        if (m_DirectTouch.Peek() == v) return;
        m_DirectTouch.Set(v);
        ScheduleToolbarUpdate();
    }

    void TextFieldSelectionState::UpdateTextToolbarState(TextToolbarState s) {
        if (m_ToolbarState.Peek() != s) m_ToolbarState.Set(s);
        ScheduleToolbarUpdate();
    }

    void TextFieldSelectionState::PerformHaptic(int constant) const { if (owner) owner->PerformHapticFeedback(constant); }

    Offset TextFieldSelectionState::CurrentTextLayoutPositionInWindow() const {
        const auto& c = textLayoutState.textLayoutNodeCoordinates;
        if (!c || !c->IsAttached()) return { NAN, NAN };
        return selection::PositionInRoot(*c, {});
    }

    Offset TextFieldSelectionState::HandleDragPosition() const {
        const Offset raw = m_RawHandleDragPosition.Get();
        if (!IsSpecified(raw)) return raw;   // nothing is dragged
        // No real handle (a long-press drag on the decorator): map out of the decorator.
        if (!IsSpecified(m_StartTextLayoutPositionInWindow)) return textLayoutState.FromDecorationToTextLayout(raw);
        // A handle: follow the text layout as it scrolls.
        const Offset current = CurrentTextLayoutPositionInWindow();
        return { raw.x + (m_StartTextLayoutPositionInWindow.x - current.x), raw.y + (m_StartTextLayoutPositionInWindow.y - current.y) };
    }

    void TextFieldSelectionState::UpdateHandleDragging(TextFieldHandle handle, Offset position) {
        if (m_DraggingHandle.Peek() != handle) m_DraggingHandle.Set(handle);
        m_RawHandleDragPosition.Set(position);
        ScheduleToolbarUpdate();
    }
    void TextFieldSelectionState::ClearHandleDragging() {
        if (m_DraggingHandle.Peek()) m_DraggingHandle.Set(std::nullopt);
        m_RawHandleDragPosition.Set({ NAN, NAN });
        m_StartTextLayoutPositionInWindow = { NAN, NAN };
        ScheduleToolbarUpdate();
    }
    void TextFieldSelectionState::MarkStartContentVisibleOffset() { m_StartTextLayoutPositionInWindow = CurrentTextLayoutPositionInWindow(); }

    // getSelectionHandleCoordinates
    Offset TextFieldSelectionState::GetHandlePosition(bool isStartHandle) const {
        const auto layout = textLayoutState.LayoutResult();
        if (!layout) return {};
        const TextRange sel = textFieldState.VisualText().selection;
        const int offset = std::clamp(isStartHandle ? sel.start : sel.end, 0, layout->LayoutInput().text.Length());
        const int line = layout->LineForOffset(offset);
        if (line >= layout->LineCount()) return { NAN, NAN };
        const bool crossed = sel.Reversed();
        const int check = (isStartHandle && !crossed) || (!isStartHandle && crossed) ? offset : std::max(offset - 1, 0);
        const bool primary = layout->BidiRunDirection(check) == layout->ParagraphDirection(offset);
        const float x = std::clamp(layout->HorizontalPosition(offset, primary), 0.0f, float(layout->Size().width));
        const float y = std::clamp(layout->LineBottom(line), 0.0f, float(layout->Size().height));
        return { x, y };
    }

    bool TextFieldSelectionState::IsCursorHandleInVisibleBounds() const {
        const auto& c = textLayoutState.textLayoutNodeCoordinates;
        if (!c || !c->IsAttached()) return false;
        const Rect r = GetCursorRect();
        const Offset p{ (r.left + r.right) / 2, r.bottom };
        const Rect v = selection::VisibleBounds(*c);
        return p.x >= v.left && p.x <= v.right && p.y >= v.top && p.y <= v.bottom;
    }

    TextFieldHandleState TextFieldSelectionState::GetCursorHandleState(bool includePosition) const {
        // Every read registers, whatever short-circuits below.
        const TextFieldCharSequence text = textFieldState.VisualText();
        const bool show = m_ShowCursorHandle.Get();
        const bool notDragged = m_DragInitiator.Get() == InputType::None;
        const auto dragging = m_DraggingHandle.Get();
        const bool visible = show && notDragged && text.selection.Collapsed() && text.ShouldShowSelection() && text.Length() > 0 &&
                             (dragging == TextFieldHandle::Cursor || IsCursorHandleInVisibleBounds());
        if (!visible) return {};
        TextFieldHandleState s;
        s.visible = true;
        if (includePosition) {
            const Rect r = GetCursorRect();
            s.position = { (r.left + r.right) / 2, r.bottom };
        }
        return s;
    }

    TextFieldHandleState TextFieldSelectionState::GetSelectionHandleState(bool isStartHandle, bool includePosition) const {
        const TextFieldHandle handle = isStartHandle ? TextFieldHandle::SelectionStart : TextFieldHandle::SelectionEnd;
        const auto layout = textLayoutState.LayoutResult();
        if (!layout) return {};
        const TextFieldCharSequence text = textFieldState.VisualText();
        const TextRange sel = text.selection;
        if (sel.Collapsed()) return {};
        const Offset position = GetHandlePosition(isStartHandle);
        const auto& coords = textLayoutState.textLayoutNodeCoordinates;
        const bool attached = coords && coords->IsAttached();
        const Rect visibleBounds = attached ? selection::VisibleBounds(*coords) : Rect{};
        const bool inBounds = attached && position.x >= visibleBounds.left && position.x <= visibleBounds.right &&
                              position.y >= visibleBounds.top && position.y <= visibleBounds.bottom;
        const bool visible = m_DragInitiator.Get() == InputType::None && (m_DraggingHandle.Get() == handle || inBounds);
        if (!visible || !text.ShouldShowSelection()) return {};
        const int directionOffset = isStartHandle ? sel.start : std::max(sel.end - 1, 0);
        TextFieldHandleState s;
        s.visible = true;
        s.direction = layout->BidiRunDirection(directionOffset);
        s.handlesCrossed = sel.Reversed();
        // A dragged handle may leave the bounds but stays coerced into them (no jitter while the
        // field scrolls).
        if (includePosition) s.position = attached ? CoerceIn(position, visibleBounds) : position;
        const int line = layout->LineForOffset(isStartHandle ? sel.start : sel.end);
        s.lineHeight = layout->LineBottom(line) - layout->LineTop(line);
        return s;
    }

    // --- selection updates (getTextFieldSelection) ---------------------------------------------

    TextRange TextFieldSelectionState::UpdateSelection(const TextFieldCharSequence& text, int startOffset, int endOffset, bool isStartHandle,
                                                       selection::Adjustment adjustment, bool allowPreviousSelectionCollapsed,
                                                       bool isStartOfSelection, std::optional<int> haptic) {
        std::optional<TextRange> previous;
        if (!isStartOfSelection && (allowPreviousSelectionCollapsed || !text.selection.Collapsed())) previous = text.selection;
        const TextRange newSelection = GetTextFieldSelection(startOffset, endOffset, previous, isStartHandle, adjustment);
        // Haptics only for a meaningful change.
        if (haptic && (newSelection.Min() != text.selection.Min() || newSelection.Max() != text.selection.Max())) PerformHaptic(*haptic);
        return newSelection;
    }

    TextRange TextFieldSelectionState::GetTextFieldSelection(int rawStart, int rawEnd, std::optional<TextRange> previous, bool isStartHandle,
                                                             selection::Adjustment adjustment) {
        const auto layout = textLayoutState.PeekLayoutResult();
        if (!layout) return TextRange(0);
        // Without a previous selection a collapsed one is allowed: Character needs no adjusting.
        if (!previous && adjustment == selection::Adjustment::Character) return TextRange(rawStart, rawEnd);
        // getTextFieldSelectionLayout: one selectable in the default slot.
        selection::SelectionLayout l;
        selection::SelectableInfo info;
        info.selectableId = 1;
        info.slot = 1;
        info.rawStartHandleOffset = rawStart;
        info.rawEndHandleOffset = rawEnd;
        info.rawPreviousHandleOffset = m_PreviousRawDragOffset;
        info.layout = layout;
        info.backend = owner ? owner->Text() : nullptr;
        l.infos.push_back(info);
        l.indexById[1] = 0;
        l.startSlot = l.endSlot = 1;
        l.isStartHandle = isStartHandle;
        if (previous) {
            selection::Selection p;
            p.start = { selection::TextDirectionForOffset(*layout, previous->start), previous->start, 1 };
            p.end = { selection::TextDirectionForOffset(*layout, previous->end), previous->end, 1 };
            p.handlesCrossed = previous->Reversed();
            l.previousSelection = p;
        }
        if (previous && !l.ShouldRecomputeSelection(m_PreviousSelectionLayout)) return *previous;
        const selection::Selection result = selection::Adjust(adjustment, l);
        m_PreviousSelectionLayout = l;
        m_PreviousRawDragOffset = isStartHandle ? rawStart : rawEnd;
        return TextRange(result.start.offset, result.end.offset);
    }

    void TextFieldSelectionState::MaybeSuggestSelectionRange() {
        if (!platformSelectionBehaviors) return;
        const TextFieldCharSequence v = textFieldState.VisualText();
        if (v.text.empty() || v.selection.Collapsed()) return;
        m_SuggestJob = rt::Launch(rt::Dispatcher::Main(), [](TextFieldSelectionState* self, TextFieldCharSequence v) -> rt::Task<> {
            const auto suggested = co_await self->platformSelectionBehaviors->SuggestSelectionForLongPressOrDoubleClick(v.text, v.selection);
            // Only while the text and selection stay what the suggestion was made for.
            const TextFieldCharSequence now = self->textFieldState.VisualText();
            if (!self->isPassword && suggested && now.text == v.text && now.selection == v.selection && *suggested != now.selection)
                self->textFieldState.SelectCharsIn(*suggested);
        }(this, v));
    }

    // --- selection gestures (TextFieldTextDragObserver, TextFieldMouseSelectionObserver) -------

    namespace {
        class TouchObserver final : public selection::TextDragObserver {
        public:
            TouchObserver(TextFieldSelectionState& s, kiln::Function<void()> requestFocus) : m_S(s), m_RequestFocus(std::move(requestFocus)) {}
            void OnStop() override { DragStop(); }
            void OnCancel() override { DragStop(); }
            void OnStart(Offset startPoint, selection::Adjustment adjustment) override {
                TextFieldSelectionState& s = m_S;
                if (!s.enabled) return;
                // On the decorator: handleDragPosition maps it.
                s.UpdateHandleDragging(m_ActingHandle, startPoint);
                s.SetShowCursorHandle(false);
                s.SetDirectDragGestureInitiator(TextFieldSelectionState::InputType::Touch);
                m_BeginPosition = startPoint;
                m_TotalDistance = {};
                s.ResetPreviousRawDragOffset();
                m_LongPressSelectionOnly = true;
                m_Adjustment = adjustment;
                if (!s.textLayoutState.PeekLayoutResult()) return;
                if (!s.textLayoutState.IsPositionOnText(startPoint)) {
                    // A long press past the text: the cursor at the line's end.
                    const int offset = s.textLayoutState.GetOffsetForPosition(startPoint);
                    s.PerformHaptic(kHapticLongPress);
                    s.textFieldState.PlaceCursorBeforeCharAt(offset);
                    s.SetShowCursorHandle(true);
                    m_LongPressSelectionOnly = false;
                    s.UpdateTextToolbarState(TextToolbarState::Cursor);
                } else {
                    const TextFieldCharSequence visual = s.textFieldState.VisualText();
                    if (visual.text.empty()) return;
                    const int offset = s.textLayoutState.GetOffsetForPosition(startPoint);
                    // A reset selection: an old one is no context for this one.
                    TextFieldCharSequence reset = visual;
                    reset.selection = TextRange(0);
                    const TextRange newSelection = s.UpdateSelection(reset, offset, offset, false, m_Adjustment, false, false, kHapticLongPress);
                    s.textFieldState.SelectCharsIn(newSelection);
                    s.UpdateTextToolbarState(TextToolbarState::Selection);
                    // Char-based selection snaps the begin offset to the first word's start.
                    m_BeginOffsetInText = newSelection.start;
                }
            }
            void OnDrag(Offset delta) override {
                TextFieldSelectionState& s = m_S;
                if (!s.enabled || !s.textLayoutState.PeekLayoutResult() || s.textFieldState.VisualText().text.empty()) return;
                m_TotalDistance = m_TotalDistance + delta;
                // The pointer relative to the text also moves when the field auto-scrolls.
                const Offset current = m_BeginPosition + m_TotalDistance;
                int startOffset, endOffset;
                selection::Adjustment adjustment;
                if (m_BeginOffsetInText < 0 && !s.textLayoutState.IsPositionOnText(current)) {
                    // Started and still in the end padding.
                    startOffset = s.textLayoutState.GetOffsetForPosition(m_BeginPosition);
                    endOffset = s.textLayoutState.GetOffsetForPosition(current);
                    adjustment = startOffset == endOffset ? selection::Adjustment::None : m_Adjustment;
                } else {
                    const auto layout = s.textLayoutState.PeekLayoutResult();
                    const int length = layout ? layout->LayoutInput().text.Length() : 0;
                    startOffset = m_BeginOffsetInText >= 0 && m_BeginOffsetInText <= length ? m_BeginOffsetInText
                                                                                            : s.textLayoutState.GetOffsetForPosition(m_BeginPosition, false);
                    endOffset = s.textLayoutState.GetOffsetForPosition(current, false);
                    // From the end padding: nothing until the selection is not collapsed.
                    if (m_BeginOffsetInText < 0 && startOffset == endOffset) return;
                    adjustment = m_Adjustment;
                    s.UpdateTextToolbarState(TextToolbarState::Selection);
                }
                const TextRange prev = s.textFieldState.VisualText().selection;
                TextRange newSelection = s.UpdateSelection(s.textFieldState.VisualText(), startOffset, endOffset, false, adjustment, false, false,
                                                           kHapticTextHandleMove);
                if (m_BeginOffsetInText == -1 && !newSelection.Collapsed()) m_BeginOffsetInText = newSelection.start;
                // No reversed selection during a long-press drag (the handles would swap late).
                if (newSelection.Reversed()) newSelection = TextRange(newSelection.end, newSelection.start);
                if (newSelection != prev) {
                    // The acting handle: the end that changed, else the way the middle moved.
                    if (newSelection.start != prev.start && newSelection.end == prev.end) m_ActingHandle = TextFieldHandle::SelectionStart;
                    else if (newSelection.start == prev.start && newSelection.end != prev.end) m_ActingHandle = TextFieldHandle::SelectionEnd;
                    else m_ActingHandle = (newSelection.start + newSelection.end) / 2.0f > (prev.start + prev.end) / 2.0f ? TextFieldHandle::SelectionEnd
                                                                                                                          : TextFieldHandle::SelectionStart;
                    m_LongPressSelectionOnly = false;
                }
                // It may reverse but never collapse while dragging.
                if (prev.Collapsed() || !newSelection.Collapsed()) s.textFieldState.SelectCharsIn(newSelection);
                s.UpdateHandleDragging(m_ActingHandle, current);
            }

        private:
            void DragStop() {
                if (!IsSpecified(m_BeginPosition)) return;
                TextFieldSelectionState& s = m_S;
                s.ClearHandleDragging();
                m_BeginOffsetInText = -1;
                m_BeginPosition = { NAN, NAN };
                m_TotalDistance = {};
                s.ResetPreviousRawDragOffset();
                m_Adjustment = selection::Adjustment::None;
                s.SetDirectDragGestureInitiator(TextFieldSelectionState::InputType::None);
                if (m_RequestFocus) m_RequestFocus();
                if (m_LongPressSelectionOnly) s.MaybeSuggestSelectionRange();
            }
            TextFieldSelectionState& m_S;
            kiln::Function<void()> m_RequestFocus;
            int m_BeginOffsetInText = -1;
            Offset m_BeginPosition{ NAN, NAN };
            Offset m_TotalDistance;
            TextFieldHandle m_ActingHandle = TextFieldHandle::SelectionEnd;   // a placeholder to start
            bool m_LongPressSelectionOnly = true;
            selection::Adjustment m_Adjustment = selection::Adjustment::None;
        };

        class MouseObserver final : public selection::MouseSelectionObserver {
        public:
            MouseObserver(TextFieldSelectionState& s, kiln::Function<void()> requestFocus) : m_S(s), m_RequestFocus(std::move(requestFocus)) {}
            bool OnStart(Offset down, selection::Adjustment adjustment, int clickCount) override {
                TextFieldSelectionState& s = m_S;
                const auto layout = s.textLayoutState.PeekLayoutResult();
                if (!s.enabled || !layout || s.textFieldState.VisualText().text.empty()) return false;
                m_DoubleOrTripleClickOnly = clickCount >= 2;
                s.SetDirectDragGestureInitiator(TextFieldSelectionState::InputType::Mouse);
                if (m_RequestFocus) m_RequestFocus();
                s.ResetPreviousRawDragOffset();
                m_BeginOffsetInText = -1;
                m_BeginPosition = down;
                m_BeginOffsetInText = Update(down, adjustment, true).start;
                return true;
            }
            bool OnDrag(Offset drag, selection::Adjustment adjustment) override {
                TextFieldSelectionState& s = m_S;
                if (!s.enabled || !s.textLayoutState.PeekLayoutResult() || s.textFieldState.VisualText().text.empty()) return false;
                const TextRange prev = s.textFieldState.VisualText().selection;
                if (Update(drag, adjustment, false) != prev) m_DoubleOrTripleClickOnly = false;
                return true;
            }
            void OnDragDone() override {
                m_S.SetDirectDragGestureInitiator(TextFieldSelectionState::InputType::None);
                if (m_DoubleOrTripleClickOnly) m_S.MaybeSuggestSelectionRange();
            }
            bool OnExtend(Offset down) override {
                TextFieldSelectionState& s = m_S;
                if (!s.enabled || !s.textLayoutState.PeekLayoutResult() || s.textFieldState.VisualText().text.empty()) return false;
                m_DoubleOrTripleClickOnly = false;
                if (m_RequestFocus) m_RequestFocus();
                Update(down, selection::Adjustment::None, false);
                return true;
            }
            bool OnExtendDrag(Offset) override { return true; }

        private:
            TextRange Update(Offset drag, selection::Adjustment adjustment, bool isStartOfSelection) {
                TextFieldSelectionState& s = m_S;
                const auto layout = s.textLayoutState.PeekLayoutResult();
                const int length = layout ? layout->LayoutInput().text.Length() : 0;
                const int startOffset = m_BeginOffsetInText >= 0 && m_BeginOffsetInText <= length
                                            ? m_BeginOffsetInText
                                            : s.textLayoutState.GetOffsetForPosition(m_BeginPosition, false);
                const int endOffset = s.textLayoutState.GetOffsetForPosition(drag, false);
                const TextRange newSelection =
                    s.UpdateSelection(s.textFieldState.VisualText(), startOffset, endOffset, false, adjustment, false, isStartOfSelection, std::nullopt);
                // From the end padding the start follows the selection once there is one (the
                // field may scroll under the begin position).
                if (m_BeginOffsetInText == -1 && !newSelection.Collapsed()) m_BeginOffsetInText = newSelection.start;
                s.textFieldState.SelectCharsIn(newSelection);
                s.UpdateTextToolbarState(TextToolbarState::Selection);
                return newSelection;
            }
            TextFieldSelectionState& m_S;
            kiln::Function<void()> m_RequestFocus;
            int m_BeginOffsetInText = -1;
            Offset m_BeginPosition{ NAN, NAN };
            bool m_DoubleOrTripleClickOnly = true;
        };
    }

    rt::Task<> TextFieldSelectionState::TextFieldSelectionGestures(PointerInputScope& scope, kiln::Function<void()> requestFocus) {
        auto mouse = std::make_shared<MouseObserver>(*this, requestFocus);
        auto touch = std::make_shared<TouchObserver>(*this, requestFocus);
        co_await selection::AwaitSelectionGestures(scope, mouse, touch);
    }

    // --- handle gestures -----------------------------------------------------------------------

    rt::Task<> TextFieldSelectionState::CursorHandleDragGestures(PointerInputScope& scope) {
        auto start = std::make_shared<Offset>(Offset{ NAN, NAN });
        auto delta = std::make_shared<Offset>(Offset{ NAN, NAN });
        auto stop = [this, start, delta] {
            if (!IsSpecified(*start)) return;
            *start = { NAN, NAN };
            *delta = { NAN, NAN };
            ClearHandleDragging();
        };
        Finally guard{ stop };   // detectDragGestures does not cancel on disposal
        DragCallbacks cb;
        cb.onDragStart = [this, start, delta](const PointerInputChange&, const PointerInputChange&, Offset) {
            const Rect r = GetCursorRect();
            *start = GetAdjustedCoordinates({ (r.left + r.right) / 2, r.bottom });
            *delta = {};
            SetDirectTouchInteraction(true);
            MarkStartContentVisibleOffset();
            UpdateHandleDragging(TextFieldHandle::Cursor, *start);
        };
        cb.onDragEnd = [stop](const PointerInputChange&) { stop(); };
        cb.onDragCancel = stop;
        cb.onDrag = [this, start, delta](const PointerInputChange& change, Offset amount) {
            *delta = *delta + amount;
            UpdateHandleDragging(TextFieldHandle::Cursor, *start + *delta);
            if (PlaceCursorAtNearestOffset(HandleDragPosition())) {
                PointerInputChange c = change;
                c.Consume();
                PerformHaptic(kHapticTextHandleMove);
            }
        };
        co_await DetectDragGestures(scope, std::nullopt, std::move(cb));
    }

    rt::Task<> TextFieldSelectionState::CursorHandleTapGestures(PointerInputScope& scope) {
        TapCallbacks cb;
        cb.onTap = [this](Offset) {
            UpdateTextToolbarState(m_ToolbarState.Peek() == TextToolbarState::Cursor ? TextToolbarState::None : TextToolbarState::Cursor);
        };
        co_await DetectTapGestures(scope, std::move(cb));
    }

    rt::Task<> TextFieldSelectionState::SelectionHandlePressGestures(PointerInputScope& scope, bool isStartHandle) {
        // detectPressDownGesture: the handle is dragged from the down on.
        const TextFieldHandle handle = isStartHandle ? TextFieldHandle::SelectionStart : TextFieldHandle::SelectionEnd;
        Finally guard{ [this] { ClearHandleDragging(); } };
        for (;;) {
            const auto down = co_await AwaitFirstDown(scope, false);
            if (!down) continue;
            MarkStartContentVisibleOffset();
            UpdateHandleDragging(handle, GetAdjustedCoordinates(GetHandlePosition(isStartHandle)));
            co_await AwaitAllPointersUp(scope);
            ClearHandleDragging();
        }
    }

    rt::Task<> TextFieldSelectionState::SelectionHandleDragGestures(PointerInputScope& scope, bool isStartHandle) {
        const TextFieldHandle handle = isStartHandle ? TextFieldHandle::SelectionStart : TextFieldHandle::SelectionEnd;
        auto begin = std::make_shared<Offset>(Offset{ NAN, NAN });
        auto total = std::make_shared<Offset>();
        auto stop = [this, begin, total] {
            if (!IsSpecified(*begin)) return;
            ClearHandleDragging();
            *begin = { NAN, NAN };
            *total = {};
            m_PreviousRawDragOffset = -1;
        };
        Finally guard{ [this, stop, handle] { if (m_DraggingHandle.Peek() == handle) stop(); } };
        DragCallbacks cb;
        cb.onDragStart = [this, begin, total, isStartHandle, handle](const PointerInputChange&, const PointerInputChange&, Offset) {
            // Where the handle's character is, in the handle's terms (markStartContentVisibleOffset
            // ran on the down).
            *begin = GetAdjustedCoordinates(GetHandlePosition(isStartHandle));
            UpdateHandleDragging(handle, *begin);
            *total = {};
            m_PreviousRawDragOffset = -1;
        };
        cb.onDragEnd = [stop](const PointerInputChange&) { stop(); };
        cb.onDragCancel = stop;
        cb.onDrag = [this, begin, total, isStartHandle, handle](const PointerInputChange&, Offset delta) {
            *total = *total + delta;
            const auto layout = textLayoutState.PeekLayoutResult();
            if (!layout) return;
            UpdateHandleDragging(handle, *begin + *total);
            const TextFieldCharSequence visual = textFieldState.VisualText();
            const int dragged = layout->OffsetForPosition(HandleDragPosition());
            const int startOffset = isStartHandle ? dragged : visual.selection.start;
            const int endOffset = isStartHandle ? visual.selection.end : dragged;
            const TextRange prev = visual.selection;
            const TextRange newSelection = UpdateSelection(visual, startOffset, endOffset, isStartHandle,
                                                           selection::Adjustment::CharacterWithWordAccelerate, false, false, kHapticTextHandleMove);
            // It may reverse but never collapse while a handle is dragged.
            if (prev.Collapsed() || !newSelection.Collapsed()) textFieldState.SelectCharsIn(newSelection);
        };
        co_await DetectDragGestures(scope, std::nullopt, std::move(cb));
    }

    // --- the toolbar and handles visibility observer -------------------------------------------

    void TextFieldSelectionState::StartToolbarAndHandlesVisibilityObserver() {
        if (m_Observing) return;
        m_Observing = true;
        m_ObservedText = textFieldState.VisualText().text;
        ScheduleToolbarUpdate();
    }

    void TextFieldSelectionState::StopToolbarAndHandlesVisibilityObserver() {
        if (!m_Observing) return;
        m_Observing = false;
        m_ObservedText.reset();
        SetShowCursorHandle(false);
        if (m_ToolbarState.Peek() != TextToolbarState::None) HideTextToolbar();
    }

    void TextFieldSelectionState::OnContentChanged() {
        if (!m_Observing) return;
        // observeTextChanges: a content change hides the cursor handle and the toolbar.
        const std::u16string& text = textFieldState.VisualText().text;
        if (m_ObservedText && *m_ObservedText != text) {
            SetShowCursorHandle(false);
            if (m_ToolbarState.Peek() != TextToolbarState::None) m_ToolbarState.Set(TextToolbarState::None);
        }
        m_ObservedText = text;
        ScheduleToolbarUpdate();
    }

    void TextFieldSelectionState::ScheduleToolbarUpdate() {
        if (!m_Observing || m_UpdatePosted) return;
        m_UpdatePosted = true;
        std::weak_ptr<char> weak = m_Alive;
        rt::Dispatcher::Main().Post([this, weak] {
            if (weak.expired()) return;
            m_UpdatePosted = false;
            if (!m_Observing) return;
            // observeTextToolbarVisibility: show or hide as bounds come and go; the toolbar reads
            // the bounds itself.
            if (DerivedVisibleContentBounds()) {
                ShowTextToolbar();
                if (toolbarHandler) toolbarHandler->InvalidateContentBounds();
            } else {
                HideTextToolbar();
            }
        });
    }

    std::optional<Rect> TextFieldSelectionState::DerivedVisibleContentBounds() const {
        const TextFieldCharSequence visual = textFieldState.VisualText();
        const bool collapsed = visual.selection.Collapsed();
        const TextToolbarState state = m_ToolbarState.Peek();
        // The toolbar is asked for this very selection state.
        const bool stateVisible = (collapsed && state == TextToolbarState::Cursor) || (!collapsed && state == TextToolbarState::Selection);
        if (!stateVisible || m_DraggingHandle.Peek() || !m_DirectTouch.Peek()) return std::nullopt;
        const auto& coords = textLayoutState.textLayoutNodeCoordinates;
        if (!coords || !coords->IsAttached()) return std::nullopt;
        const Rect v = selection::VisibleBounds(*coords);
        const Offset tl = selection::PositionInRoot(*coords, { v.left, v.top });
        const Rect visibleInRoot{ tl.x, tl.y, tl.x + v.Width(), tl.y + v.Height() };
        // A wide selection: the toolbar aligns to the visible part only.
        const Rect content = GetContentRect();
        const bool overlaps = content.right > visibleInRoot.left && visibleInRoot.right > content.left && content.bottom > visibleInRoot.top &&
                              visibleInRoot.bottom > content.top;
        if (!overlaps) return std::nullopt;
        // The handles below the visible content count as content, so the toolbar never covers
        // them (TextView's Editor and the legacy TextFieldSelectionManager add the handle height;
        // the state-based field does not, and its toolbar can land on the handles).
        Rect bounds = content.Intersect(visibleInRoot);
        bounds.bottom += 25 * density;
        return bounds;
    }

    Rect TextFieldSelectionState::GetContentRect() const {
        const auto& coords = *textLayoutState.textLayoutNodeCoordinates;
        const TextFieldCharSequence text = textFieldState.VisualText();
        if (text.selection.Collapsed()) {
            // The cursor, in the root.
            const Rect r = GetCursorRect();
            const Offset tl = selection::PositionInRoot(coords, { r.left, r.top });
            return { tl.x, tl.y, tl.x + r.Width(), tl.y + r.Height() };
        }
        const auto layout = textLayoutState.PeekLayoutResult();
        if (!layout) return {};
        const Offset start = selection::PositionInRoot(coords, GetHandlePosition(true));
        const Offset end = selection::PositionInRoot(coords, GetHandlePosition(false));
        const float startTop = selection::PositionInRoot(coords, { 0, layout->Cursor(text.selection.start).top }).y;
        const float endTop = selection::PositionInRoot(coords, { 0, layout->Cursor(text.selection.end).top }).y;
        const float left = std::min(start.x, end.x), right = std::max(start.x, end.x);
        return { left, std::min(startTop, endTop), left == right ? right + 1 : right, std::max(start.y, end.y) };
    }

    void TextFieldSelectionState::UpdateClipboardEntry() {
        bool hasClip = false, hasText = false;
        if (owner) {
            const auto meta = owner->ClipboardMetadata();
            hasClip = meta.has_value();
            hasText = hasClip && meta->HasMimeType("text/*");
        }
        m_PasteHasClip.Set(hasClip);
        m_PasteHasText.Set(hasText);
    }

    bool TextFieldSelectionState::CanShowPasteMenuItem() const {
        if (!IsPasteAllowed() || !owner) return false;
        // Without a receiver a text item is needed; with one any clip will do.
        if (m_PasteHasText.Get()) return true;
        return receiveContentConfiguration && receiveContentConfiguration() && m_PasteHasClip.Get();
    }

    bool TextFieldSelectionState::CanShowAutofillMenuItem() const {
        return Editable() && textFieldState.VisualText().selection.Collapsed();
    }

    bool TextFieldSelectionState::CanShowLookUpMenuItem() const {
        if (!IsCopyAllowed()) return false;
        const TextFieldCharSequence v = textFieldState.VisualText();
        return CanShowLookUp(owner, v.text.substr(std::size_t(v.selection.Min()), std::size_t(v.selection.Length())));
    }

    void TextFieldSelectionState::LookUp() {
        const TextFieldCharSequence v = textFieldState.VisualText();
        StartLookUpActivity(owner, v.text.substr(std::size_t(v.selection.Min()), std::size_t(v.selection.Length())));
    }

    void TextFieldSelectionState::AddTextContextMenuComponents(TextContextMenuBuilderScope& scope) {
        // textFieldItem: the action, closing unless told not to, then the toolbar state it leaves.
        auto item = [this](TextContextMenuBuilderScope& sc, TextContextMenuKey key, bool enabled, kiln::Function<void()> onClick,
                           TextToolbarState desired = TextToolbarState::None, kiln::Function<bool()> closePredicate = {}) {
            if (!enabled) return;
            sc.Item(key, {}, [this, onClick, desired, closePredicate](TextContextMenuSession& session) {
                onClick();
                if (!closePredicate || closePredicate()) session.Close();
                UpdateTextToolbarState(desired);
            });
        };
        const TextFieldCharSequence v = textFieldState.VisualText();
        AddPlatformTextContextMenuItems(scope, owner, Editable(), &v.text, v.selection, platformSelectionBehaviors, [&](TextContextMenuBuilderScope& sc) {
            sc.Separator();
            item(sc, TextContextMenuKeys::Cut, IsCutAllowed(), [this] { Cut(); });
            item(sc, TextContextMenuKeys::Copy, IsCopyAllowed(), [this] { Copy(m_TextToolbarShown.Peek()); });
            item(sc, TextContextMenuKeys::Paste, CanShowPasteMenuItem(), [this] { Paste(); });
            item(sc, TextContextMenuKeys::LookUp, CanShowLookUpMenuItem(), [this] { LookUp(); });
            item(sc, TextContextMenuKeys::SelectAll, CanShowSelectAllMenuItem(), [this] { SelectAll(); }, TextToolbarState::Selection,
                 [this] { return !m_TextToolbarShown.Peek(); });
            item(sc, TextContextMenuKeys::Autofill, CanShowAutofillMenuItem() && owner && owner->Autofill(), [this] { Autofill(); });
            sc.Separator();
        });
    }

    rt::Task<> TextFieldSelectionState::OnPreShowContextMenu() {
        UpdateClipboardEntry();
        if (!platformSelectionBehaviors) co_return;
        const TextFieldCharSequence v = textFieldState.VisualText();
        co_await platformSelectionBehaviors->OnShowContextMenu(v.text, v.selection);
    }

    void TextFieldSelectionState::ShowTextToolbar() {
        if (toolbarHandler) toolbarHandler->Show();
    }

    void TextFieldSelectionState::HideTextToolbar() {
        if (toolbarHandler) toolbarHandler->Hide();
    }
}
