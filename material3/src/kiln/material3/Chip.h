#pragma once

// Chip.kt: action chips, selectable chips and Expressive pressed/selected shape morphing.

#include "kiln/foundation/Border.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"

#include <optional>

namespace kiln::material3 {
    struct ChipColors {
        ui::Color containerColor = ui::Color::Unspecified(), labelColor = ui::Color::Unspecified();
        ui::Color leadingIconContentColor = ui::Color::Unspecified(), trailingIconContentColor = ui::Color::Unspecified();
        ui::Color disabledContainerColor = ui::Color::Unspecified(), disabledLabelColor = ui::Color::Unspecified();
        ui::Color disabledLeadingIconContentColor = ui::Color::Unspecified(), disabledTrailingIconContentColor = ui::Color::Unspecified();
        ChipColors Copy(const ChipColors& overrides) const;
        ui::Color ContainerColor(bool enabled) const { return enabled ? containerColor : disabledContainerColor; }
        ui::Color LabelColor(bool enabled) const { return enabled ? labelColor : disabledLabelColor; }
        ui::Color LeadingIconContentColor(bool enabled) const { return enabled ? leadingIconContentColor : disabledLeadingIconContentColor; }
        ui::Color TrailingIconContentColor(bool enabled) const { return enabled ? trailingIconContentColor : disabledTrailingIconContentColor; }
        bool operator==(const ChipColors&) const = default;
    };
    struct SelectableChipColors {
        ui::Color containerColor = ui::Color::Unspecified(), labelColor = ui::Color::Unspecified();
        ui::Color leadingIconColor = ui::Color::Unspecified(), trailingIconColor = ui::Color::Unspecified();
        ui::Color disabledContainerColor = ui::Color::Unspecified(), disabledLabelColor = ui::Color::Unspecified();
        ui::Color disabledLeadingIconColor = ui::Color::Unspecified(), disabledTrailingIconColor = ui::Color::Unspecified();
        ui::Color selectedContainerColor = ui::Color::Unspecified(), disabledSelectedContainerColor = ui::Color::Unspecified();
        ui::Color selectedLabelColor = ui::Color::Unspecified(), selectedLeadingIconColor = ui::Color::Unspecified(), selectedTrailingIconColor = ui::Color::Unspecified();
        SelectableChipColors Copy(const SelectableChipColors& overrides) const;
        ui::Color ContainerColor(bool enabled, bool selected) const;
        ui::Color LabelColor(bool enabled, bool selected) const;
        ui::Color LeadingIconContentColor(bool enabled, bool selected) const;
        ui::Color TrailingIconContentColor(bool enabled, bool selected) const;
        bool operator==(const SelectableChipColors&) const = default;
    };
    struct ChipElevation {
        ui::Dp elevation, pressedElevation, focusedElevation, hoveredElevation, draggedElevation, disabledElevation;
        bool operator==(const ChipElevation&) const = default;
    };
    using SelectableChipElevation = ChipElevation;
    ui::Dp ShadowElevation(const ChipElevation&, bool enabled, const std::shared_ptr<foundation::MutableInteractionSource>&);

    struct ChipShapes {
        foundation::ShapePtr shape, selectedShape, pressedShape;
        ChipShapes Copy(foundation::ShapePtr shape = {}, foundation::ShapePtr selectedShape = {}, foundation::ShapePtr pressedShape = {}) const;
        bool operator==(const ChipShapes&) const;
    };
    struct ChipOptions {
        bool enabled = true;
        std::function<void()> leadingIcon, trailingIcon;
        foundation::ShapePtr shape;
        std::optional<ChipColors> colors;
        std::optional<std::optional<ChipElevation>> elevation;
        std::optional<std::optional<foundation::BorderStroke>> border;
        std::optional<foundation::Arrangement> horizontalArrangement;
        std::optional<foundation::PaddingValues> contentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    struct SelectableChipOptions {
        bool enabled = true;
        std::function<void()> leadingIcon, trailingIcon, avatar;  // avatar is used only by InputChip
        foundation::ShapePtr shape;
        std::optional<ChipShapes> shapes;
        std::optional<SelectableChipColors> colors;
        std::optional<std::optional<SelectableChipElevation>> elevation;
        std::optional<std::optional<foundation::BorderStroke>> border;
        std::optional<foundation::Arrangement> horizontalArrangement;
        std::optional<foundation::PaddingValues> contentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void AssistChip(std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const ChipOptions& options = {});
    void ElevatedAssistChip(std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const ChipOptions& options = {});
    void SuggestionChip(std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const ChipOptions& options = {});
    void ElevatedSuggestionChip(std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const ChipOptions& options = {});
    void FilterChip(bool selected, std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const SelectableChipOptions& options = {});
    void ElevatedFilterChip(bool selected, std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const SelectableChipOptions& options = {});
    void InputChip(bool selected, std::function<void()> onClick, std::function<void()> label, const ui::Modifier& modifier = {}, const SelectableChipOptions& options = {});

    namespace AssistChipDefaults {
        inline constexpr ui::Dp Height{ 32 }, IconSize{ 18 }, HorizontalSpacing{ 8 };
        extern const foundation::PaddingValues ContentPadding;
        foundation::Arrangement horizontalArrangement(ui::Dp spacing = HorizontalSpacing);
        foundation::ShapePtr shape();
        ChipColors assistChipColors();
        ChipColors assistChipColors(const ChipColors& overrides);
        ChipColors elevatedAssistChipColors();
        ChipColors elevatedAssistChipColors(const ChipColors& overrides);
        ChipElevation assistChipElevation();
        ChipElevation elevatedAssistChipElevation();
        foundation::BorderStroke assistChipBorder(bool enabled = true);
    }
    namespace SuggestionChipDefaults {
        inline constexpr ui::Dp Height{ 32 }, IconSize{ 18 }, HorizontalSpacing{ 8 };
        extern const foundation::PaddingValues ContentPadding;
        foundation::Arrangement horizontalArrangement(ui::Dp spacing = HorizontalSpacing);
        foundation::ShapePtr shape();
        ChipColors suggestionChipColors();
        ChipColors suggestionChipColors(const ChipColors& overrides);
        ChipColors elevatedSuggestionChipColors();
        ChipColors elevatedSuggestionChipColors(const ChipColors& overrides);
        ChipElevation suggestionChipElevation();
        ChipElevation elevatedSuggestionChipElevation();
        foundation::BorderStroke suggestionChipBorder(bool enabled = true);
    }
    namespace FilterChipDefaults {
        inline constexpr ui::Dp Height{ 32 }, IconSize{ 18 }, HorizontalSpacing{ 8 }, CompactHorizontalSpacing{ 4 };
        extern const foundation::PaddingValues ContentPadding;
        foundation::Arrangement horizontalArrangement(ui::Dp spacing = HorizontalSpacing);
        foundation::Arrangement horizontalArrangement(bool hasLeadingIcon, bool hasTrailingIcon);
        foundation::ShapePtr shape();
        ChipShapes shapes(foundation::ShapePtr shape = {}, foundation::ShapePtr selectedShape = {}, foundation::ShapePtr pressedShape = {});
        SelectableChipColors filterChipColors();
        SelectableChipColors filterChipColors(const SelectableChipColors& overrides);
        SelectableChipColors tonalFilterChipColors();
        SelectableChipColors elevatedFilterChipColors();
        SelectableChipColors elevatedFilterChipColors(const SelectableChipColors& overrides);
        SelectableChipColors tonalElevatedFilterChipColors();
        SelectableChipElevation filterChipElevation();
        SelectableChipElevation elevatedFilterChipElevation();
        foundation::BorderStroke filterChipBorder(bool enabled, bool selected);
    }
    namespace InputChipDefaults {
        inline constexpr ui::Dp Height{ 32 }, IconSize{ 18 }, AvatarSize{ 24 }, HorizontalSpacing{ 8 }, CompactHorizontalSpacing{ 4 };
        foundation::Arrangement horizontalArrangement(ui::Dp spacing = HorizontalSpacing);
        foundation::Arrangement horizontalArrangement(bool hasAvatar, bool hasLeadingIcon, bool hasTrailingIcon);
        foundation::PaddingValues contentPadding(bool hasAvatar, bool hasLeadingIcon, bool hasTrailingIcon);
        foundation::ShapePtr shape();
        ChipShapes shapes(foundation::ShapePtr shape = {}, foundation::ShapePtr selectedShape = {}, foundation::ShapePtr pressedShape = {});
        SelectableChipColors inputChipColors();
        SelectableChipColors inputChipColors(const SelectableChipColors& overrides);
        SelectableChipColors tonalInputChipColors();
        SelectableChipElevation inputChipElevation();
        foundation::BorderStroke inputChipBorder(bool enabled, bool selected);
    }
}
