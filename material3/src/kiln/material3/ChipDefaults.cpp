#include "kiln/material3/Chip.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/AssistChipTokens.h"
#include "kiln/material3/tokens/FilterChipTokens.h"
#include "kiln/material3/tokens/InputChipTokens.h"
#include "kiln/material3/tokens/SuggestionChipTokens.h"
#include "kiln/material3/tokens/ChipsTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Take(Color color, Color fallback) { return color.Specified() ? color : fallback; }
        struct ChipArrangement final : ArrangementPolicy {
            Dp leading, trailing;
            ChipArrangement(Dp l, Dp t) : leading(l), trailing(t) {}
            Dp Spacing() const override { return Dp((leading.value + trailing.value) / 2); }
            void Arrange(const ui::Density& d, int total, std::span<const int> sizes, ui::LayoutDirection dir, std::span<int> positions) const override {
                const int first = d.RoundToPx(leading), last = d.RoundToPx(trailing);
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    const int x = i == 1 ? sizes[0] + (sizes[0] > 0 ? first : last) : i == 2 ? total - sizes[i] : 0;
                    positions[i] = dir == ui::LayoutDirection::Ltr ? x : total - x - sizes[i];
                }
            }
            bool Equals(const ArrangementPolicy& other) const override {
                const auto* o = dynamic_cast<const ChipArrangement*>(&other);
                return o && leading == o->leading && trailing == o->trailing;
            }
        };
        Arrangement Arrange(Dp leading, Dp trailing) { return Arrangement::Custom(std::make_shared<ChipArrangement>(leading, trailing)); }
        Arrangement Compact(bool leading, bool trailing) { return Arrange(Dp(leading ? 4 : 8), Dp(trailing ? 4 : 8)); }
        ChipColors ActionColors(bool assist, bool elevated) {
            const auto label = Value(assist ? AssistChipTokens::LabelTextColor : SuggestionChipTokens::LabelTextColor);
            const auto icon = Value(assist ? AssistChipTokens::IconColor : SuggestionChipTokens::LeadingIconColor);
            const auto disabledLabel = Value(assist ? AssistChipTokens::DisabledLabelTextColor : SuggestionChipTokens::DisabledLabelTextColor)
                .WithAlpha(assist ? AssistChipTokens::DisabledLabelTextOpacity : SuggestionChipTokens::DisabledLabelTextOpacity);
            const auto disabledIcon = Value(assist || elevated ? AssistChipTokens::DisabledIconColor : SuggestionChipTokens::DisabledLeadingIconColor)
                .WithAlpha(assist || elevated ? AssistChipTokens::DisabledIconOpacity : SuggestionChipTokens::DisabledLeadingIconOpacity);
            const auto container = elevated ? Value(assist ? AssistChipTokens::ElevatedContainerColor : SuggestionChipTokens::ElevatedContainerColor) : Color::Transparent();
            const auto disabledContainer = elevated ? Value(assist ? AssistChipTokens::ElevatedDisabledContainerColor : SuggestionChipTokens::ElevatedDisabledContainerColor)
                .WithAlpha(AssistChipTokens::ElevatedDisabledContainerOpacity) : Color::Transparent();
            return { container, label, icon, assist ? icon : Color::Unspecified(), disabledContainer, disabledLabel, disabledIcon, assist ? disabledIcon : Color::Unspecified() };
        }
        SelectableChipColors FilterColors(bool elevated) {
            return { elevated ? Value(FilterChipTokens::ElevatedUnselectedContainerColor) : Color::Transparent(), Value(FilterChipTokens::UnselectedLabelTextColor),
                     Value(FilterChipTokens::UnselectedLeadingIconColor), Value(FilterChipTokens::UnselectedTrailingIconColor),
                     elevated ? Value(FilterChipTokens::ElevatedDisabledContainerColor).WithAlpha(FilterChipTokens::ElevatedDisabledContainerOpacity) : Color::Transparent(),
                     Value(FilterChipTokens::DisabledLabelTextColor).WithAlpha(FilterChipTokens::DisabledLabelTextOpacity),
                     Value(FilterChipTokens::DisabledLeadingIconColor).WithAlpha(FilterChipTokens::DisabledLeadingIconOpacity),
                     Value(FilterChipTokens::DisabledTrailingIconColor).WithAlpha(elevated ? FilterChipTokens::DisabledLeadingIconOpacity : FilterChipTokens::DisabledTrailingIconOpacity),
                     Value(elevated ? FilterChipTokens::ElevatedSelectedContainerColor : FilterChipTokens::FlatSelectedContainerColor),
                     elevated ? Value(FilterChipTokens::ElevatedDisabledContainerColor).WithAlpha(FilterChipTokens::ElevatedDisabledContainerOpacity) :
                                Value(FilterChipTokens::FlatDisabledSelectedContainerColor).WithAlpha(FilterChipTokens::FlatDisabledSelectedContainerOpacity),
                     Value(FilterChipTokens::SelectedLabelTextColor), Value(FilterChipTokens::SelectedLeadingIconColor), Value(FilterChipTokens::SelectedTrailingIconColor) };
        }
    }

    ChipColors ChipColors::Copy(const ChipColors& o) const {
        return { Take(o.containerColor, containerColor), Take(o.labelColor, labelColor), Take(o.leadingIconContentColor, leadingIconContentColor), Take(o.trailingIconContentColor, trailingIconContentColor),
                 Take(o.disabledContainerColor, disabledContainerColor), Take(o.disabledLabelColor, disabledLabelColor), Take(o.disabledLeadingIconContentColor, disabledLeadingIconContentColor), Take(o.disabledTrailingIconContentColor, disabledTrailingIconContentColor) };
    }
    SelectableChipColors SelectableChipColors::Copy(const SelectableChipColors& o) const {
        return { Take(o.containerColor, containerColor), Take(o.labelColor, labelColor), Take(o.leadingIconColor, leadingIconColor), Take(o.trailingIconColor, trailingIconColor),
                 Take(o.disabledContainerColor, disabledContainerColor), Take(o.disabledLabelColor, disabledLabelColor), Take(o.disabledLeadingIconColor, disabledLeadingIconColor), Take(o.disabledTrailingIconColor, disabledTrailingIconColor),
                 Take(o.selectedContainerColor, selectedContainerColor), Take(o.disabledSelectedContainerColor, disabledSelectedContainerColor), Take(o.selectedLabelColor, selectedLabelColor), Take(o.selectedLeadingIconColor, selectedLeadingIconColor), Take(o.selectedTrailingIconColor, selectedTrailingIconColor) };
    }
    Color SelectableChipColors::ContainerColor(bool enabled, bool selected) const { return enabled ? (selected ? selectedContainerColor : containerColor) : (selected ? disabledSelectedContainerColor : disabledContainerColor); }
    Color SelectableChipColors::LabelColor(bool enabled, bool selected) const { return !enabled ? disabledLabelColor : selected ? selectedLabelColor : labelColor; }
    Color SelectableChipColors::LeadingIconContentColor(bool enabled, bool selected) const { return !enabled ? disabledLeadingIconColor : selected ? selectedLeadingIconColor : leadingIconColor; }
    Color SelectableChipColors::TrailingIconContentColor(bool enabled, bool selected) const { return !enabled ? disabledTrailingIconColor : selected ? selectedTrailingIconColor : trailingIconColor; }

    ChipShapes ChipShapes::Copy(ShapePtr s, ShapePtr selected, ShapePtr pressed) const { return { s ? s : shape, selected ? selected : selectedShape, pressed ? pressed : (pressedShape ? pressedShape : selectedShape) }; }
    bool ChipShapes::operator==(const ChipShapes& o) const {
        auto equal = [](const ShapePtr& a, const ShapePtr& b) { return a == b || (a && b && a->Equals(*b)); };
        return equal(shape, o.shape) && equal(selectedShape, o.selectedShape) && equal(pressedShape ? pressedShape : selectedShape, o.pressedShape ? o.pressedShape : o.selectedShape);
    }
    namespace AssistChipDefaults {
        const PaddingValues ContentPadding{ Dp(8), Dp(0), Dp(8), Dp(0) };
        Arrangement horizontalArrangement(Dp spacing) { return Arrange(spacing, spacing); }
        ShapePtr shape() { return Value(AssistChipTokens::ContainerShape); }
        ChipColors assistChipColors() { return ActionColors(true, false); }
        ChipColors assistChipColors(const ChipColors& o) { return assistChipColors().Copy(o); }
        ChipColors elevatedAssistChipColors() { return ActionColors(true, true); }
        ChipColors elevatedAssistChipColors(const ChipColors& o) { return elevatedAssistChipColors().Copy(o); }
        ChipElevation assistChipElevation() { return { AssistChipTokens::FlatContainerElevation, AssistChipTokens::FlatContainerElevation, AssistChipTokens::FlatContainerElevation, AssistChipTokens::FlatContainerElevation, AssistChipTokens::DraggedContainerElevation, AssistChipTokens::FlatContainerElevation }; }
        ChipElevation elevatedAssistChipElevation() { return { AssistChipTokens::ElevatedContainerElevation, AssistChipTokens::ElevatedPressedContainerElevation, AssistChipTokens::ElevatedFocusContainerElevation, AssistChipTokens::ElevatedHoverContainerElevation, AssistChipTokens::DraggedContainerElevation, AssistChipTokens::ElevatedDisabledContainerElevation }; }
        BorderStroke assistChipBorder(bool enabled) { return { AssistChipTokens::FlatOutlineWidth, enabled ? Value(AssistChipTokens::FlatOutlineColor) : Value(AssistChipTokens::FlatDisabledOutlineColor).WithAlpha(AssistChipTokens::FlatDisabledOutlineOpacity) }; }
    }
    namespace SuggestionChipDefaults {
        const PaddingValues ContentPadding{ Dp(8), Dp(0), Dp(8), Dp(0) };
        Arrangement horizontalArrangement(Dp spacing) { return Arrange(spacing, spacing); }
        ShapePtr shape() { return Value(SuggestionChipTokens::ContainerShape); }
        ChipColors suggestionChipColors() { return ActionColors(false, false); }
        ChipColors suggestionChipColors(const ChipColors& o) { return suggestionChipColors().Copy(o); }
        ChipColors elevatedSuggestionChipColors() { return ActionColors(false, true); }
        ChipColors elevatedSuggestionChipColors(const ChipColors& o) { return elevatedSuggestionChipColors().Copy(o); }
        ChipElevation suggestionChipElevation() { return { SuggestionChipTokens::FlatContainerElevation, SuggestionChipTokens::FlatContainerElevation, SuggestionChipTokens::FlatContainerElevation, SuggestionChipTokens::FlatContainerElevation, SuggestionChipTokens::DraggedContainerElevation, SuggestionChipTokens::FlatContainerElevation }; }
        ChipElevation elevatedSuggestionChipElevation() { return { SuggestionChipTokens::ElevatedContainerElevation, SuggestionChipTokens::ElevatedPressedContainerElevation, SuggestionChipTokens::ElevatedFocusContainerElevation, SuggestionChipTokens::ElevatedHoverContainerElevation, SuggestionChipTokens::DraggedContainerElevation, SuggestionChipTokens::ElevatedDisabledContainerElevation }; }
        BorderStroke suggestionChipBorder(bool enabled) { return { SuggestionChipTokens::FlatOutlineWidth, enabled ? Value(SuggestionChipTokens::FlatOutlineColor) : Value(SuggestionChipTokens::FlatDisabledOutlineColor).WithAlpha(SuggestionChipTokens::FlatDisabledOutlineOpacity) }; }
    }
    namespace FilterChipDefaults {
        const PaddingValues ContentPadding{ Dp(8), Dp(0), Dp(8), Dp(0) };
        Arrangement horizontalArrangement(Dp spacing) { return Arrange(spacing, spacing); }
        Arrangement horizontalArrangement(bool leading, bool trailing) { return Compact(leading, trailing); }
        ShapePtr shape() { return Value(FilterChipTokens::ContainerShape); }
        ChipShapes shapes(ShapePtr s, ShapePtr selected, ShapePtr pressed) { return ChipShapes{ Value(ChipsTokens::UnselectedShape), Value(ChipsTokens::SelectedShape), Value(ChipsTokens::PressedShape) }.Copy(s, selected, pressed); }
        SelectableChipColors filterChipColors() { return FilterColors(false); }
        SelectableChipColors filterChipColors(const SelectableChipColors& o) { return filterChipColors().Copy(o); }
        SelectableChipColors tonalFilterChipColors() { auto colors = filterChipColors(); colors.leadingIconColor = Value(ChipsTokens::UnselectedLeadingIconColor); return colors; }
        SelectableChipColors elevatedFilterChipColors() { return FilterColors(true); }
        SelectableChipColors elevatedFilterChipColors(const SelectableChipColors& o) { return elevatedFilterChipColors().Copy(o); }
        SelectableChipColors tonalElevatedFilterChipColors() { auto colors = elevatedFilterChipColors(); colors.leadingIconColor = Value(ChipsTokens::UnselectedLeadingIconColor); return colors; }
        ChipElevation filterChipElevation() { return { FilterChipTokens::FlatContainerElevation, FilterChipTokens::FlatSelectedPressedContainerElevation, FilterChipTokens::FlatSelectedFocusContainerElevation, FilterChipTokens::FlatSelectedHoverContainerElevation, FilterChipTokens::DraggedContainerElevation, FilterChipTokens::FlatContainerElevation }; }
        ChipElevation elevatedFilterChipElevation() { return { FilterChipTokens::ElevatedContainerElevation, FilterChipTokens::ElevatedPressedContainerElevation, FilterChipTokens::ElevatedFocusContainerElevation, FilterChipTokens::ElevatedHoverContainerElevation, FilterChipTokens::DraggedContainerElevation, FilterChipTokens::ElevatedDisabledContainerElevation }; }
        BorderStroke filterChipBorder(bool enabled, bool selected) {
            const auto color = selected ? Color::Transparent() : enabled ? Value(FilterChipTokens::FlatUnselectedOutlineColor) : Value(FilterChipTokens::FlatDisabledUnselectedOutlineColor).WithAlpha(FilterChipTokens::FlatDisabledUnselectedOutlineOpacity);
            return { selected ? FilterChipTokens::FlatSelectedOutlineWidth : FilterChipTokens::FlatUnselectedOutlineWidth, color };
        }
    }
    namespace InputChipDefaults {
        Arrangement horizontalArrangement(Dp spacing) { return Arrange(spacing, spacing); }
        Arrangement horizontalArrangement(bool avatar, bool leading, bool trailing) { return Compact(avatar || leading, trailing); }
        PaddingValues contentPadding(bool avatar, bool leading, bool trailing) { return { Dp(avatar || !leading ? 4 : 8), Dp(0), Dp(trailing ? 8 : 4), Dp(0) }; }
        ShapePtr shape() { return Value(InputChipTokens::ContainerShape); }
        ChipShapes shapes(ShapePtr s, ShapePtr selected, ShapePtr pressed) { return FilterChipDefaults::shapes(s, selected, pressed); }
        SelectableChipColors inputChipColors() {
            return { Color::Transparent(), Value(InputChipTokens::UnselectedLabelTextColor), Value(InputChipTokens::UnselectedLeadingIconColor), Value(InputChipTokens::UnselectedTrailingIconColor),
                     Color::Transparent(), Value(InputChipTokens::DisabledLabelTextColor).WithAlpha(InputChipTokens::DisabledLabelTextOpacity),
                     Value(InputChipTokens::DisabledLeadingIconColor).WithAlpha(InputChipTokens::DisabledLeadingIconOpacity), Value(InputChipTokens::DisabledTrailingIconColor).WithAlpha(InputChipTokens::DisabledTrailingIconOpacity),
                     Value(InputChipTokens::SelectedContainerColor), Value(InputChipTokens::DisabledSelectedContainerColor).WithAlpha(InputChipTokens::DisabledSelectedContainerOpacity),
                     Value(InputChipTokens::SelectedLabelTextColor), Value(InputChipTokens::SelectedLeadingIconColor), Value(InputChipTokens::SelectedTrailingIconColor) };
        }
        SelectableChipColors inputChipColors(const SelectableChipColors& o) { return inputChipColors().Copy(o); }
        SelectableChipColors tonalInputChipColors() { auto colors = inputChipColors(); colors.leadingIconColor = Value(ChipsTokens::UnselectedLeadingIconColor); return colors; }
        ChipElevation inputChipElevation() { return { InputChipTokens::ContainerElevation, InputChipTokens::ContainerElevation, InputChipTokens::ContainerElevation, InputChipTokens::ContainerElevation, InputChipTokens::DraggedContainerElevation, InputChipTokens::ContainerElevation }; }
        BorderStroke inputChipBorder(bool enabled, bool selected) {
            const auto color = selected ? Color::Transparent() : enabled ? Value(InputChipTokens::UnselectedOutlineColor) : Value(InputChipTokens::DisabledUnselectedOutlineColor).WithAlpha(InputChipTokens::DisabledUnselectedOutlineOpacity);
            return { selected ? InputChipTokens::SelectedOutlineWidth : InputChipTokens::UnselectedOutlineWidth, color };
        }
    }
}
