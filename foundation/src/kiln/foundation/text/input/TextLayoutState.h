#pragma once

// TextLayoutState (with TextFieldLayoutStateCache): a text field's layout of its visual text,
// cached on the text, composition, style, line mode and measure inputs; the coordinates of the
// field's text layout, core and decorator nodes; the single line field's height; and the
// bring-into-view requester the core uses to keep the cursor visible.

#include "kiln/foundation/BringIntoView.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/runtime/State.h"
#include "kiln/text/Paragraph.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::foundation {

    class TextLayoutState {
    public:
        using Coordinates = selection::Coordinates;

        std::function<void(const text::TextLayoutResult&)> onTextLayout;

        // The last layout (observable: readers recompute when a new layout replaces it).
        std::shared_ptr<const text::TextLayoutResult> LayoutResult() const { return m_Result.Get(); }
        std::shared_ptr<const text::TextLayoutResult> PeekLayoutResult() const { return m_Result.Peek(); }

        std::optional<Coordinates> textLayoutNodeCoordinates, coreNodeCoordinates, decoratorNodeCoordinates;

        // heightForSingleLineField (dp; 0 for a multi line field).
        float HeightForSingleLineField() const { return m_SingleLineHeight.Get(); }
        void SetHeightForSingleLineField(float dp) { if (m_SingleLineHeight.Peek() != dp) m_SingleLineHeight.Set(dp); }

        BringIntoViewRequester bringIntoViewRequester;

        void UpdateNonMeasureInputs(TransformedTextFieldState& state, const text::TextStyle& style, bool singleLine, bool softWrap,
                                    bool isKeyboardTypePhone);
        // layoutWithNewMeasureInputs: reads the visual text (the caller's measure observes it).
        std::shared_ptr<const text::TextLayoutResult> LayoutWithNewMeasureInputs(TextBackend* backend, const ui::Density& density,
                                                                                 ui::LayoutDirection direction, int fontGeneration,
                                                                                 const ui::Constraints& constraints);

        // getOffsetForPosition: a position in the decorator to an offset in the visual text (-1
        // without a layout), coerced into the visible text first when asked.
        int GetOffsetForPosition(Offset position, bool coerceInVisibleBounds = true) const;
        bool IsPositionOnText(Offset offset) const;
        Offset CoercedInVisibleBoundsOfInputText(Offset offset) const;
        Offset FromTextLayoutToCore(Offset offset) const;
        Offset FromDecorationToTextLayout(Offset offset) const;
        Offset FromWindowToDecoration(Offset offset) const;
        Rect FromTextLayoutToDecoration(Rect rect) const;
        // decoratorNodeCoordinates.localBoundingBoxOf(textLayoutNodeCoordinates): the text
        // layout node's visible bounds in the decorator (empty when either is detached).
        Rect VisibleTextLayoutRectInDecorator() const;

    private:
        struct NonMeasureInputs {
            TransformedTextFieldState* state = nullptr;
            text::TextStyle style;
            bool singleLine = false, softWrap = false, isKeyboardTypePhone = false;
        };
        struct Cache {
            std::u16string visualText;
            std::vector<text::Range<text::Annotation>> annotations;
            std::optional<TextRange> composition;
            text::TextStyle style;
            bool singleLine = false, softWrap = false;
            ui::Density density{ -1, -1 };
            ui::LayoutDirection direction = ui::LayoutDirection::Ltr;
            int fontGeneration = -1;
            ui::Constraints constraints;
            std::shared_ptr<const text::TextLayoutResult> result;
        };
        std::optional<NonMeasureInputs> m_Inputs;
        Cache m_Cache;
        rt::State<std::shared_ptr<const text::TextLayoutResult>> m_Result{ nullptr };
        rt::State<float> m_SingleLineHeight{ 0.0f };
    };

    // Offset.coerceIn(rect).
    Offset CoerceIn(Offset o, const Rect& r);

}
