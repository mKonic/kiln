#include "kiln/material3/ListItem.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/internal/PrecisionPointer.h"
#include "kiln/material3/tokens/ListTokens.h"
#include "kiln/material3/tokens/ReorderListTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;

    namespace {
        Color Choose(Color base, Color disabled, Color selectedColor, Color draggedColor, bool enabled, bool selected, bool dragged) {
            return !enabled ? disabled : dragged ? draggedColor : selected ? selectedColor : base;
        }
        Color Override(Color base, Color value) { return value.Specified() ? value : base; }
        bool SameShape(const ShapePtr& a, const ShapePtr& b) { return a == b || (a && b && a->Equals(*b)); }
    }
    ListItemColors ListItemColors::Copy(const ListItemColors& o) const {
        return {
            Override(containerColor, o.containerColor), Override(contentColor, o.contentColor),
            Override(leadingContentColor, o.leadingContentColor), Override(trailingContentColor, o.trailingContentColor),
            Override(overlineContentColor, o.overlineContentColor), Override(supportingContentColor, o.supportingContentColor),
            Override(disabledContainerColor, o.disabledContainerColor), Override(disabledContentColor, o.disabledContentColor),
            Override(disabledLeadingContentColor, o.disabledLeadingContentColor), Override(disabledTrailingContentColor, o.disabledTrailingContentColor),
            Override(disabledOverlineContentColor, o.disabledOverlineContentColor), Override(disabledSupportingContentColor, o.disabledSupportingContentColor),
            Override(selectedContainerColor, o.selectedContainerColor), Override(selectedContentColor, o.selectedContentColor),
            Override(selectedLeadingContentColor, o.selectedLeadingContentColor), Override(selectedTrailingContentColor, o.selectedTrailingContentColor),
            Override(selectedOverlineContentColor, o.selectedOverlineContentColor), Override(selectedSupportingContentColor, o.selectedSupportingContentColor),
            Override(draggedContainerColor, o.draggedContainerColor), Override(draggedContentColor, o.draggedContentColor),
            Override(draggedLeadingContentColor, o.draggedLeadingContentColor), Override(draggedTrailingContentColor, o.draggedTrailingContentColor),
            Override(draggedOverlineContentColor, o.draggedOverlineContentColor), Override(draggedSupportingContentColor, o.draggedSupportingContentColor)
        };
    }
    Color ListItemColors::ContainerColor(bool e, bool s, bool d) const { return Choose(containerColor, disabledContainerColor, selectedContainerColor, draggedContainerColor, e, s, d); }
    Color ListItemColors::ContentColor(bool e, bool s, bool d) const { return Choose(contentColor, disabledContentColor, selectedContentColor, draggedContentColor, e, s, d); }
    Color ListItemColors::LeadingContentColor(bool e, bool s, bool d) const { return Choose(leadingContentColor, disabledLeadingContentColor, selectedLeadingContentColor, draggedLeadingContentColor, e, s, d); }
    Color ListItemColors::TrailingContentColor(bool e, bool s, bool d) const { return Choose(trailingContentColor, disabledTrailingContentColor, selectedTrailingContentColor, draggedTrailingContentColor, e, s, d); }
    Color ListItemColors::OverlineContentColor(bool e, bool s, bool d) const { return Choose(overlineContentColor, disabledOverlineContentColor, selectedOverlineContentColor, draggedOverlineContentColor, e, s, d); }
    Color ListItemColors::SupportingContentColor(bool e, bool s, bool d) const { return Choose(supportingContentColor, disabledSupportingContentColor, selectedSupportingContentColor, draggedSupportingContentColor, e, s, d); }

    ListItemShapes ListItemShapes::Copy(ShapePtr base, ShapePtr selected, ShapePtr pressed, ShapePtr focused, ShapePtr hovered, ShapePtr dragged) const {
        return { base ? base : shape, selected ? selected : selectedShape, pressed ? pressed : pressedShape,
                 focused ? focused : focusedShape, hovered ? hovered : hoveredShape, dragged ? dragged : draggedShape };
    }
    ShapePtr ListItemShapes::ShapeForInteraction(bool selected, bool pressed, bool focused, bool hovered, bool dragged) const {
        return pressed ? pressedShape : dragged ? draggedShape : selected ? selectedShape : focused ? focusedShape : hovered ? hoveredShape : shape;
    }
    bool ListItemShapes::operator==(const ListItemShapes& o) const {
        return SameShape(shape, o.shape) && SameShape(selectedShape, o.selectedShape) && SameShape(pressedShape, o.pressedShape) &&
               SameShape(focusedShape, o.focusedShape) && SameShape(hoveredShape, o.hoveredShape) && SameShape(draggedShape, o.draggedShape);
    }

    namespace ListItemDefaults {
        ShapePtr shape() { return Value(ListTokens::ItemContainerShape); }
        Color containerColor() { return Value(ListTokens::ItemContainerColor); }
        Color contentColor() { return Value(ListTokens::ItemLabelTextColor); }
        PaddingValues contentPadding() {
            const bool precision = internal::ShouldUsePrecisionPointerComponentSizing();
            return { ListTokens::ItemLeadingSpace, precision ? Dp(12) : ListTokens::ItemTopSpace,
                     ListTokens::ItemTrailingSpace, precision ? Dp(12) : ListTokens::ItemBottomSpace };
        }
        ListItemVerticalAlignment verticalAlignment() {
            const auto padding = contentPadding();
            const auto density = rt::Composition::Current()->Read(ui::LocalDensity);
            const int breakpoint = density.RoundToPx((ListTokens::ItemThreeLineContainerHeight + ListTokens::ItemTwoLineContainerHeight) / 2 - padding.top - padding.bottom);
            return [breakpoint](int size, int space) { return space < breakpoint ? ui::AlignmentV::CenterVertically().Align(size, space) : 0; };
        }
        ListItemColors colors() {
            return {
                Value(ListTokens::ItemContainerColor), Value(ListTokens::ItemLabelTextColor),
                Value(ListTokens::ItemLeadingIconColor), Value(ListTokens::ItemTrailingIconColor),
                Value(ListTokens::ItemOverlineColor), Value(ListTokens::ItemSupportingTextColor),
                Value(ListTokens::ItemContainerColor), Value(ListTokens::ItemDisabledLabelTextColor).WithAlpha(ListTokens::ItemDisabledLabelTextOpacity),
                Value(ListTokens::ItemDisabledLeadingIconColor).WithAlpha(ListTokens::ItemDisabledLeadingIconOpacity),
                Value(ListTokens::ItemDisabledTrailingIconColor).WithAlpha(ListTokens::ItemDisabledTrailingIconOpacity),
                Value(ListTokens::ItemDisabledOverlineColor).WithAlpha(ListTokens::ItemDisabledOverlineOpacity),
                Value(ListTokens::ItemDisabledSupportingTextColor).WithAlpha(ListTokens::ItemDisabledSupportingTextOpacity),
                Value(ListTokens::ItemSelectedContainerColor), Value(ListTokens::ItemSelectedLabelTextColor),
                Value(ListTokens::ItemSelectedLeadingIconColor), Value(ListTokens::ItemSelectedTrailingIconColor),
                Value(ListTokens::ItemSelectedOverlineColor), Value(ListTokens::ItemSelectedSupportingTextColor),
                Value(ReorderListTokens::ItemContainerColor), Value(ReorderListTokens::ItemLabelTextColor),
                Value(ReorderListTokens::ItemLeadingIconColor), Value(ReorderListTokens::ItemTrailingIconColor),
                Value(ReorderListTokens::ItemOverlineColor), Value(ReorderListTokens::ItemSupportingTextColor)
            };
        }
        ListItemColors colors(const ListItemColors& overrides) { return colors().Copy(overrides); }
        ListItemColors segmentedColors() {
            return colors().Copy({ .containerColor = Value(ListTokens::ItemSegmentedContainerColor),
                                   .disabledContainerColor = Value(ListTokens::ItemSegmentedContainerColor) });
        }
        ListItemColors segmentedColors(const ListItemColors& overrides) { return segmentedColors().Copy(overrides); }
        ListItemShapes shapes() {
            return { Value(ListTokens::ItemContainerExpressiveShape), Value(ListTokens::ItemSelectedContainerExpressiveShape),
                     Value(ListTokens::ItemPressedContainerExpressiveShape), Value(ListTokens::ItemFocusedContainerExpressiveShape),
                     Value(ListTokens::ItemHoveredContainerExpressiveShape), Value(ReorderListTokens::ItemShape) };
        }
        ListItemShapes shapes(ShapePtr base, ShapePtr selected, ShapePtr pressed, ShapePtr focused, ShapePtr hovered, ShapePtr dragged) {
            return shapes().Copy(std::move(base), std::move(selected), std::move(pressed), std::move(focused), std::move(hovered), std::move(dragged));
        }
        ListItemShapes segmentedShapes(int index, int count) { return segmentedShapes(index, count, shapes()); }
        ListItemShapes segmentedShapes(int index, int count, const ListItemShapes& defaults) {
            const auto base = std::dynamic_pointer_cast<const CornerBasedShape>(defaults.shape);
            const auto outer = std::dynamic_pointer_cast<const CornerBasedShape>(Value(ListTokens::ContainerShape));
            if (!base || !outer || (count != 1 && index != 0 && index != count - 1)) return defaults;
            const bool top = count == 1 || index == 0, bottom = count == 1 || index == count - 1;
            return defaults.Copy(base->Copy(top ? outer->topStart : base->topStart, top ? outer->topEnd : base->topEnd,
                                            bottom ? outer->bottomEnd : base->bottomEnd, bottom ? outer->bottomStart : base->bottomStart));
        }
        ListItemElevation elevation(Dp base, Dp dragged) { return { base, dragged }; }
    }
}
