#pragma once

// ExposedDropdownMenu.kt: a menu anchored to a text field (or another anchor) that shows the
// current choice. The box tracks its anchor's width and the room above and below it; the
// anchor toggles the menu on a tap, Enter or Space, and accessibility actions; the menu opens
// below the anchor (above when there is no room), as wide as the anchor.

#include "kiln/foundation/Border.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/TextFieldColors.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/Modifier.h"
#include "kiln/ui/Popup.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    // ExposedDropdownMenuAnchorType: what the anchor is.
    enum class ExposedDropdownMenuAnchorType : std::uint8_t {
        PrimaryNotEditable,   // a read-only field: tapping it toggles the menu
        PrimaryEditable,      // an editable field: the menu filters as the user types
        SecondaryEditable,    // a button (a trailing icon) beside an editable field
    };

    namespace internal { struct ExposedDropdownMenuBoxState; }

    class ExposedDropdownMenuBoxScope {
    public:
        // Modifier.menuAnchor: the menu's anchor (one or more; the primary one wins). Disabled,
        // it no longer toggles the menu.
        ui::Modifier MenuAnchor(ui::Modifier m, ExposedDropdownMenuAnchorType type, bool enabled = true) const;
        // Modifier.exposedDropdownSize: as wide as the anchor (or not), at most as tall as the
        // room beside it.
        ui::Modifier ExposedDropdownSize(ui::Modifier m, bool matchAnchorWidth = true) const;

        // Internal.
        explicit ExposedDropdownMenuBoxScope(std::shared_ptr<internal::ExposedDropdownMenuBoxState> s) : m_State(std::move(s)) {}
        ExposedDropdownMenuAnchorType GetAnchorType() const;
        bool AlwaysFocusable() const;

    private:
        std::shared_ptr<internal::ExposedDropdownMenuBoxState> m_State;
    };

    void ExposedDropdownMenuBox(bool expanded, std::function<void(bool)> onExpandedChange, const ui::Modifier& modifier,
                                std::function<void(const ExposedDropdownMenuBoxScope&)> content);

    struct ExposedDropdownMenuOptions {
        foundation::ScrollState* scrollState = nullptr;   // null: remembered
        bool matchAnchorWidth = true;
        foundation::ShapePtr shape;                       // null: MenuDefaults::shape
        std::optional<ui::Color> containerColor;          // unset: MenuDefaults::containerColor
        ui::Dp tonalElevation{ 0 };                       // MenuDefaults.TonalElevation
        std::optional<ui::Dp> shadowElevation;            // unset: MenuDefaults::ShadowElevation
        std::optional<foundation::BorderStroke> border;
    };
    // ExposedDropdownMenuBoxScope.ExposedDropdownMenu: the menu, in a popup by the anchor.
    void ExposedDropdownMenu(const ExposedDropdownMenuBoxScope& scope, bool expanded, std::function<void()> onDismissRequest,
                             const ui::Modifier& modifier, const ExposedDropdownMenuOptions& options, std::function<void()> content);

    namespace ExposedDropdownMenuDefaults {
        // An arrow that points down, and up while expanded.
        void TrailingIcon(bool expanded, const ui::Modifier& modifier = {});
        TextFieldColors textFieldColors();
        TextFieldColors outlinedTextFieldColors();
        foundation::PaddingValues ItemContentPadding();
    }

    namespace internal {
        // calculateMaxHeight: the taller of the room above and below the anchor, within the
        // window less its vertical margin (all of it when the anchor is outside the window).
        int ExposedDropdownMenuMaxHeight(ui::IntRect windowBounds, std::optional<ui::Rect> anchorBounds, int verticalMargin);
        // ExposedDropdownMenuPositionProvider: below the anchor, else above it, else against the
        // window edge nearer the anchor (centred when taller than the window less margins).
        std::shared_ptr<const ui::PopupPositionProvider> MakeExposedDropdownMenuPositionProvider(ui::Density density, int topWindowInsets);
    }

}
