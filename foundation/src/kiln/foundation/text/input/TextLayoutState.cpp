#include "kiln/foundation/text/input/TextLayoutState.h"

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    Offset CoerceIn(Offset o, const Rect& r) {
        return { o.x < r.left ? r.left : (o.x > r.right ? r.right : o.x), o.y < r.top ? r.top : (o.y > r.bottom ? r.bottom : o.y) };
    }

    void TextLayoutState::UpdateNonMeasureInputs(TransformedTextFieldState& state, const text::TextStyle& style, bool singleLine,
                                                 bool softWrap, bool isKeyboardTypePhone) {
        m_Inputs = NonMeasureInputs{ &state, style, singleLine, softWrap, isKeyboardTypePhone };
    }

    std::shared_ptr<const text::TextLayoutResult> TextLayoutState::LayoutWithNewMeasureInputs(TextBackend* backend, const ui::Density& density,
                                                                                              ui::LayoutDirection direction, int fontGeneration,
                                                                                              const ui::Constraints& constraints) {
        const NonMeasureInputs& in = *m_Inputs;
        const TextFieldCharSequence visual = in.state->VisualText();
        // Styled text (TextStyleBuffer) is not ported yet: the composing spans only.
        const std::vector<PlacedSpan>& annotations = visual.composingAnnotations;
        Cache& c = m_Cache;
        if (!(c.result && c.visualText == visual.text && c.annotations == annotations && c.composition == visual.composition &&
              c.singleLine == in.singleLine && c.softWrap == in.softWrap && c.direction == direction && c.density == density &&
              c.fontGeneration == fontGeneration && c.constraints.minWidth == constraints.minWidth &&
              c.constraints.maxWidth == constraints.maxWidth && c.constraints.minHeight == constraints.minHeight &&
              c.constraints.maxHeight == constraints.maxHeight && c.style == in.style)) {
            // computeLayout: a phone number field lays out in its locale's digit direction
            // (left to right for every locale's digits: European, Arabic-Indic and extended
            // Arabic-Indic digits are all weak left-to-right or Arabic numbers).
            text::TextStyle style = in.style;
            if (in.isKeyboardTypePhone) style.paragraph.textDirection = text::TextDirection::Ltr;
            text::TextLayoutInput li;
            std::vector<text::Range<text::Annotation>> spans;
            spans.reserve(annotations.size());
            for (const PlacedSpan& a : annotations) spans.push_back({ a.item, a.start, a.end, a.tag });
            li.text = text::AnnotatedString(ToUtf8(visual.text), std::move(spans));
            li.style = style;
            li.softWrap = in.softWrap;
            li.maxLines = in.singleLine ? 1 : 0x7fffffff;
            li.density = density;
            li.layoutDirection = direction;
            li.constraints = constraints;
            c.result = std::make_shared<const text::TextLayoutResult>(text::LayoutText(backend, std::move(li)));
            c.visualText = visual.text;
            c.annotations = annotations;
            c.composition = visual.composition;
            c.style = in.style;
            c.singleLine = in.singleLine;
            c.softWrap = in.softWrap;
            c.direction = direction;
            c.density = density;
            c.fontGeneration = fontGeneration;
            c.constraints = constraints;
        }
        if (m_Result.Peek() != c.result) m_Result.Set(c.result);
        if (onTextLayout) onTextLayout(*c.result);
        return c.result;
    }

    namespace {
        bool Attached(const std::optional<selection::Coordinates>& c) { return c && c->IsAttached(); }
    }

    Rect TextLayoutState::VisibleTextLayoutRectInDecorator() const {
        if (!Attached(textLayoutNodeCoordinates) || !Attached(decoratorNodeCoordinates)) return {};
        const Rect v = selection::VisibleBounds(*textLayoutNodeCoordinates);
        const Offset tl = selection::LocalPositionOf(*decoratorNodeCoordinates, *textLayoutNodeCoordinates, { v.left, v.top });
        const Offset br = selection::LocalPositionOf(*decoratorNodeCoordinates, *textLayoutNodeCoordinates, { v.right, v.bottom });
        return { std::min(tl.x, br.x), std::min(tl.y, br.y), std::max(tl.x, br.x), std::max(tl.y, br.y) };
    }

    int TextLayoutState::GetOffsetForPosition(Offset position, bool coerceInVisibleBounds) const {
        const auto layout = LayoutResult();
        if (!layout) return -1;
        const Offset coerced = coerceInVisibleBounds ? CoercedInVisibleBoundsOfInputText(position) : position;
        return layout->OffsetForPosition(FromDecorationToTextLayout(coerced));
    }

    bool TextLayoutState::IsPositionOnText(Offset offset) const {
        const auto layout = LayoutResult();
        if (!layout) return false;
        const Offset relative = FromDecorationToTextLayout(CoercedInVisibleBoundsOfInputText(offset));
        const int line = layout->LineForVerticalPosition(relative.y);
        return relative.x >= layout->LineLeft(line) && relative.x <= layout->LineRight(line);
    }

    Offset TextLayoutState::CoercedInVisibleBoundsOfInputText(Offset offset) const {
        Rect visible;
        if (textLayoutNodeCoordinates && Attached(textLayoutNodeCoordinates)) visible = VisibleTextLayoutRectInDecorator();
        return CoerceIn(offset, visible);
    }

    Offset TextLayoutState::FromTextLayoutToCore(Offset offset) const {
        if (!Attached(textLayoutNodeCoordinates) || !Attached(coreNodeCoordinates)) return offset;
        return selection::LocalPositionOf(*coreNodeCoordinates, *textLayoutNodeCoordinates, offset);
    }

    Offset TextLayoutState::FromDecorationToTextLayout(Offset offset) const {
        if (!Attached(textLayoutNodeCoordinates) || !Attached(decoratorNodeCoordinates)) return offset;
        return selection::LocalPositionOf(*textLayoutNodeCoordinates, *decoratorNodeCoordinates, offset);
    }

    Offset TextLayoutState::FromWindowToDecoration(Offset offset) const {
        if (!Attached(decoratorNodeCoordinates)) return offset;
        return ui::LayoutNode::UnmapPoint(decoratorNodeCoordinates->ToRoot(), offset).value_or(offset);
    }

    Rect TextLayoutState::FromTextLayoutToDecoration(Rect rect) const {
        if (!Attached(textLayoutNodeCoordinates) || !Attached(decoratorNodeCoordinates)) return rect;
        // localBoundingBoxOf(textLayoutNode, clipBounds = false).topLeft
        const Offset tl = selection::LocalPositionOf(*decoratorNodeCoordinates, *textLayoutNodeCoordinates, {});
        return { rect.left + tl.x, rect.top + tl.y, rect.right + tl.x, rect.bottom + tl.y };
    }

}
