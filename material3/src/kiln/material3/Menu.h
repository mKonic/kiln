#pragma once

// Menu.kt, MenuDefaults.kt and AndroidMenu.android.kt: DropdownMenu (a popup list next to its
// anchor that scales and fades in from where it meets the anchor), the expressive
// DropdownMenuPopup with DropdownMenuGroup segments, and the menu items: the classic
// DropdownMenuItem and the shaped, checkable and selectable expressive items.

#include "kiln/foundation/Border.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Popup.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/internal/Helpers.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    using internal::MenuAnchorPosition;

    struct MenuItemColors {
        ui::Color textColor, leadingIconColor, trailingIconColor, disabledTextColor, disabledLeadingIconColor, disabledTrailingIconColor;
        ui::Color containerColor = ui::Color::Transparent(), disabledContainerColor = ui::Color::Transparent();
        // Unspecified arguments keep this one's colours.
        MenuItemColors Copy(ui::Color textColor = ui::Color::Unspecified(), ui::Color containerColor = ui::Color::Unspecified(),
                            ui::Color leadingIconColor = ui::Color::Unspecified(), ui::Color trailingIconColor = ui::Color::Unspecified(),
                            ui::Color disabledTextColor = ui::Color::Unspecified(), ui::Color disabledContainerColor = ui::Color::Unspecified(),
                            ui::Color disabledLeadingIconColor = ui::Color::Unspecified(),
                            ui::Color disabledTrailingIconColor = ui::Color::Unspecified()) const;
        ui::Color TextColor(bool enabled) const { return enabled ? textColor : disabledTextColor; }
        ui::Color LeadingIconColor(bool enabled) const { return enabled ? leadingIconColor : disabledLeadingIconColor; }
        ui::Color TrailingIconColor(bool enabled) const { return enabled ? trailingIconColor : disabledTrailingIconColor; }
        ui::Color ContainerColor(bool enabled) const { return enabled ? containerColor : disabledContainerColor; }
        bool operator==(const MenuItemColors&) const = default;
    };

    struct SelectableMenuItemColors {
        ui::Color textColor, containerColor, leadingIconColor, trailingContentColor;
        ui::Color disabledTextColor, disabledContainerColor, disabledLeadingIconColor, disabledTrailingContentColor;
        ui::Color selectedTextColor, selectedContainerColor, selectedLeadingIconColor, selectedTrailingContentColor;
        // Unspecified fields of `o` keep this one's colours.
        SelectableMenuItemColors Copy(const SelectableMenuItemColors& o) const;
        ui::Color TextColor(bool enabled, bool selected) const { return !enabled ? disabledTextColor : selected ? selectedTextColor : textColor; }
        ui::Color LeadingIconColor(bool enabled, bool selected) const {
            return !enabled ? disabledLeadingIconColor : selected ? selectedLeadingIconColor : leadingIconColor;
        }
        ui::Color TrailingContentColor(bool enabled, bool selected) const {
            return !enabled ? disabledTrailingContentColor : selected ? selectedTrailingContentColor : trailingContentColor;
        }
        ui::Color ContainerColor(bool enabled, bool selected) const {
            return !enabled ? disabledContainerColor : selected ? selectedContainerColor : containerColor;
        }
        bool operator==(const SelectableMenuItemColors&) const = default;
        // Every colour Unspecified: the base for Copy arguments.
        static SelectableMenuItemColors Unspecified();
    };

    struct MenuItemShapes {
        foundation::ShapePtr shape, selectedShape;
        // Null arguments keep this one's shapes.
        MenuItemShapes Copy(foundation::ShapePtr shape, foundation::ShapePtr selectedShape) const {
            return { shape ? shape : this->shape, selectedShape ? selectedShape : this->selectedShape };
        }
        bool operator==(const MenuItemShapes&) const = default;
    };

    struct MenuGroupShapes {
        foundation::ShapePtr shape, inactiveShape;   // inactiveShape: once hovered, while not hovered
        MenuGroupShapes Copy(foundation::ShapePtr shape, foundation::ShapePtr inactiveShape) const {
            return { shape ? shape : this->shape, inactiveShape ? inactiveShape : this->inactiveShape };
        }
        bool operator==(const MenuGroupShapes&) const = default;
    };

    // DropdownMenuPopupPositionProvider: a popup position provider that knows where the menu
    // meets its anchor (the scale animation's origin).
    using DropdownMenuPopupPositionProvider = internal::DropdownMenuPositionProvider;

    namespace MenuDefaults {
        ui::PopupProperties DefaultMenuProperties();   // focusable
        extern const ui::Dp TonalElevation, ShadowElevation, LeadingIconSize, GroupSpacing;
        ui::Dp TrailingIconSize();
        foundation::ShapePtr shape();
        ui::Color containerColor();
        ui::Color groupStandardContainerColor();
        ui::Color groupVibrantContainerColor();
        foundation::ShapePtr leadingGroupShape();
        foundation::ShapePtr middleGroupShape();
        foundation::ShapePtr trailingGroupShape();
        foundation::ShapePtr leadingItemShape();
        foundation::ShapePtr middleItemShape();
        foundation::ShapePtr trailingItemShape();
        foundation::ShapePtr standaloneItemShape();
        foundation::ShapePtr selectedItemShape();
        foundation::ShapePtr standaloneGroupShape();
        foundation::ShapePtr inactiveGroupShape();
        extern const foundation::PaddingValues HorizontalDividerPadding, DropdownMenuGroupLabelHorizontalPadding;
        extern const foundation::PaddingValues DropdownMenuItemContentPadding, DropdownMenuGroupContentPadding;
        foundation::PaddingValues DropdownMenuSelectableItemContentPadding();
        foundation::PaddingValues DropdownMenuItemTrailingLabelHorizontalPadding();

        // The shapes of a group or item at `index` of `count` (leading, middle, trailing or alone).
        MenuGroupShapes groupShape(int index, int count);
        MenuItemShapes itemShape(int index, int count);
        MenuItemShapes itemShapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr selectedShape = nullptr);
        MenuGroupShapes groupShapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr inactiveShape = nullptr);

        MenuItemColors itemColors();
        MenuItemColors itemVibrantColors();
        SelectableMenuItemColors selectableItemColors();
        SelectableMenuItemColors selectableItemVibrantColors();

        // Leading icon, then the text after the spacing, trailing content at the end.
        foundation::Arrangement DropdownMenuItemHorizontalArrangement();

        void DropdownMenuGroupLabel(const ui::Modifier& modifier, std::function<void()> content,
                                    ui::Alignment contentAlignment = ui::Alignment::CenterStart(),
                                    foundation::PaddingValues padding = DropdownMenuGroupLabelHorizontalPadding);
        void DropdownMenuItemTrailingLabel(const ui::Modifier& modifier, std::function<void()> content);

        std::shared_ptr<const DropdownMenuPopupPositionProvider> rememberDropdownMenuPopupPositionProvider(
            const MenuAnchorPosition& dropdownMenuAnchorPosition, ui::DpOffset offset = {});
    }

    struct DropdownMenuOptions {
        ui::DpOffset offset{};
        foundation::ScrollState* scrollState = nullptr;               // null: remembered
        std::optional<ui::PopupProperties> properties;                // unset: DefaultMenuProperties
        foundation::ShapePtr shape;                                   // null: MenuDefaults::shape
        std::optional<ui::Color> containerColor;
        ui::Dp tonalElevation = MenuDefaults::TonalElevation;
        std::optional<ui::Dp> shadowElevation;                        // unset: MenuDefaults::ShadowElevation
        std::optional<foundation::BorderStroke> border;
    };
    // The items go in a column as wide as the widest one, scrolled when taller than the window.
    void DropdownMenu(bool expanded, std::function<void()> onDismissRequest, const ui::Modifier& modifier, const DropdownMenuOptions& options,
                      std::function<void()> content);

    // DropdownMenuPopup: the popup and its animation only; content arranges groups itself.
    void DropdownMenuPopup(bool expanded, std::function<void()> onDismissRequest, const ui::Modifier& modifier,
                           std::shared_ptr<const DropdownMenuPopupPositionProvider> popupPositionProvider, std::function<void()> content,
                           std::optional<ui::PopupProperties> properties = std::nullopt);

    struct DropdownMenuGroupOptions {
        std::optional<ui::Color> containerColor;                      // unset: groupStandardContainerColor
        ui::Dp tonalElevation = MenuDefaults::TonalElevation;
        std::optional<ui::Dp> shadowElevation;
        std::optional<foundation::BorderStroke> border;
        foundation::PaddingValues contentPadding = MenuDefaults::DropdownMenuGroupContentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void DropdownMenuGroup(const MenuGroupShapes& shapes, const ui::Modifier& modifier, const DropdownMenuGroupOptions& options,
                           std::function<void()> content);

    // The classic item: a 48 dp row with optional icons.
    struct DropdownMenuItemOptions {
        std::function<void()> leadingIcon, trailingIcon;
        bool enabled = true;
        std::optional<MenuItemColors> colors;
        foundation::PaddingValues contentPadding = MenuDefaults::DropdownMenuItemContentPadding;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void DropdownMenuItem(std::function<void()> text, std::function<void()> onClick, const ui::Modifier& modifier,
                          const DropdownMenuItemOptions& options);

    // The expressive items.
    struct MenuItemOptions {
        std::function<void()> leadingIcon;
        std::function<void()> selectedLeadingIcon;                     // checked / selected forms only
        std::function<void()> trailingContent, supportingText;
        bool enabled = true;
        std::optional<foundation::Arrangement> horizontalArrangement;  // unset: DropdownMenuItemHorizontalArrangement
        std::optional<foundation::PaddingValues> contentPadding;       // unset: DropdownMenuSelectableItemContentPadding
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    // DropdownMenuItem(onClick, text, shape, ...): a shaped item (role Button).
    void DropdownMenuItem(std::function<void()> onClick, std::function<void()> text, foundation::ShapePtr shape, const ui::Modifier& modifier,
                          const MenuItemOptions& options, std::optional<MenuItemColors> colors = std::nullopt);
    // Role Checkbox: a click flips `checked`.
    void CheckableDropdownMenuItem(bool checked, std::function<void(bool)> onCheckedChange, std::function<void()> text,
                                   const MenuItemShapes& shapes, const ui::Modifier& modifier, const MenuItemOptions& options,
                                   std::optional<SelectableMenuItemColors> colors = std::nullopt);
    // Role RadioButton.
    void SelectableDropdownMenuItem(bool selected, std::function<void()> onClick, std::function<void()> text, const MenuItemShapes& shapes,
                                    const ui::Modifier& modifier, const MenuItemOptions& options,
                                    std::optional<SelectableMenuItemColors> colors = std::nullopt);

}
