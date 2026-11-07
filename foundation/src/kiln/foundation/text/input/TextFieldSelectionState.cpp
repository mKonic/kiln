#include "kiln/foundation/text/input/TextFieldSelectionState.h"

#include "kiln/foundation/content/ReceiveContent.h"
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
        if (!en) UpdateTextToolbarState(TextToolbarState::None);
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
        showCursorHandle = false;
        UpdateTextToolbarState(TextToolbarState::None);
    }

    void TextFieldSelectionState::Dispose() {
        if (owner) owner->HideTextToolbar();
    }

    rt::Task<> TextFieldSelectionState::DetectDirectTouchInteraction(PointerInputScope& scope) {
        for (;;) {
            const auto e = co_await scope.AwaitPointerEvent(PointerEventPass::Initial);
            if (e) SetDirectTouchInteraction(!IsMouseOrTouchPad(*e));
        }
    }

    rt::Task<> TextFieldSelectionState::DetectTextFieldTapGestures(PointerInputScope& scope, std::function<void()> requestFocus,
                                                                   std::function<void()> showKeyboard) {
        TapCallbacks cb;
        cb.onTap = [this, requestFocus, showKeyboard](Offset offset) {
            requestFocus();
            if (enabled && isWindowAndTextFieldFocused) {
                if (!readOnly) {
                    showKeyboard();
                    if (textFieldState.VisualText().Length() > 0) showCursorHandle = true;
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

}
