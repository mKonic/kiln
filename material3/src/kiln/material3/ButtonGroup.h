#pragma once

// ButtonGroup.kt: buttons in a row that widen while pressed, taking the space from their
// neighbours (no further than the neighbour's end content padding). Items that do not fit move
// into a DropdownMenu behind an overflow indicator. Connected groups use the
// connected*ButtonShapes with ConnectedSpaceBetween.

#include "kiln/foundation/Arrangement.h"
#include <kiln/Function.h>
#include "kiln/foundation/Interaction.h"
#include "kiln/material3/IconButton.h"
#include "kiln/material3/ToggleButton.h"
#include "kiln/runtime/Saveable.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::material3 {

    // ButtonGroupMenuState: whether the overflow menu shows.
    class ButtonGroupMenuState {
    public:
        explicit ButtonGroupMenuState(bool initialIsShowing = false) : m_Showing(initialIsShowing) {}
        bool IsShowing() const { return m_Showing.Get(); }   // observable
        void Dismiss() { m_Showing.Set(false); }
        void Show() { m_Showing.Set(true); }
        // Saver: whether it shows, so the menu stays open across a configuration change.
        static rt::Saver<std::shared_ptr<ButtonGroupMenuState>> Saver() {
            return { [](const std::shared_ptr<ButtonGroupMenuState>& s) { return rt::SavedValue(s->IsShowing()); },
                     [](const rt::SavedValue& v) -> std::optional<std::shared_ptr<ButtonGroupMenuState>> {
                         const bool* showing = std::get_if<bool>(&v.v);
                         if (!showing) return std::nullopt;
                         return std::make_shared<ButtonGroupMenuState>(*showing);
                     } };
        }

    private:
        rt::State<bool> m_Showing;
    };

    namespace internal { struct ButtonGroupItem; }

    // ButtonGroupScope: what the content lambda adds items with, and the modifiers its custom
    // items use.
    class ButtonGroupScope {
    public:
        explicit ButtonGroupScope(anim::SpecPtr animationSpec) : m_Spec(std::move(animationSpec)) {}
        // weight: a share of the width left after the unweighted items (> 0).
        ui::Modifier Weight(ui::Modifier m, float weight) const;
        // animateWidth: widen while the source is pressed; `compressionLimit` caps how far the
        // neighbours give way (unset: ButtonDefaults.ContentPadding's end).
        ui::Modifier AnimateWidth(ui::Modifier m, std::shared_ptr<foundation::MutableInteractionSource> interactionSource,
                                  std::optional<ui::Dp> compressionLimit = std::nullopt) const;
        ui::Modifier Align(ui::Modifier m, ui::AlignmentV alignment) const;

        // A Button; in the overflow menu a DropdownMenuItem that also closes the menu.
        void ClickableItem(kiln::Function<void()> onClick, std::string label, kiln::Function<void()> icon = {}, float weight = NAN,
                           bool enabled = true);
        // A ToggleButton; in the menu an item that flips it.
        void ToggleableItem(bool checked, std::string label, kiln::Function<void(bool)> onCheckedChange, kiln::Function<void()> icon = {},
                            float weight = NAN, bool enabled = true);
        void CustomItem(kiln::Function<void()> buttonGroupContent, kiln::Function<void(ButtonGroupMenuState&)> menuContent);

        const std::vector<std::shared_ptr<internal::ButtonGroupItem>>& Items() const { return m_Items; }

    private:
        anim::SpecPtr m_Spec;
        std::vector<std::shared_ptr<internal::ButtonGroupItem>> m_Items;
    };

    namespace ButtonGroupDefaults {
        inline constexpr float ExpandedRatio = 0.15f;
        foundation::Arrangement HorizontalArrangement();
        extern const ui::Dp ConnectedSpaceBetween;
        foundation::ShapePtr connectedLeadingButtonShape();
        foundation::ShapePtr connectedLeadingButtonPressShape();
        foundation::ShapePtr connectedTrailingButtonShape();
        foundation::ShapePtr connectedTrailingButtonPressShape();
        foundation::ShapePtr connectedButtonCheckedShape();
        foundation::ShapePtr connectedMiddleButtonPressShape();
        // Null arguments take the defaults above (the middle shape: ShapeDefaults.Small).
        ToggleButtonShapes connectedLeadingButtonShapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr,
                                                        foundation::ShapePtr checkedShape = nullptr);
        ToggleButtonShapes connectedMiddleButtonShapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr,
                                                       foundation::ShapePtr checkedShape = nullptr);
        ToggleButtonShapes connectedTrailingButtonShapes(foundation::ShapePtr shape = nullptr, foundation::ShapePtr pressedShape = nullptr,
                                                         foundation::ShapePtr checkedShape = nullptr);

        // A filled icon button ("More options", with its tooltip) that opens and closes the menu.
        struct OverflowIndicatorOptions {
            bool enabled = true;
            foundation::ShapePtr shape;                 // null: IconButtonDefaults.filledShape
            std::optional<IconButtonColors> colors;     // unset: filledIconButtonColors
            std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        };
        void OverflowIndicator(ButtonGroupMenuState& menuState, const ui::Modifier& modifier = {}, const OverflowIndicatorOptions& options = {});
    }

    struct ButtonGroupOptions {
        float expandedRatio = ButtonGroupDefaults::ExpandedRatio;   // how much a pressed item widens (>= 0)
        std::optional<foundation::Arrangement> horizontalArrangement;   // unset: HorizontalArrangement
        ui::AlignmentV verticalAlignment = ui::AlignmentV::Top();
    };
    void ButtonGroup(kiln::Function<void(ButtonGroupMenuState&)> overflowIndicator, const ui::Modifier& modifier,
                     kiln::Function<void(ButtonGroupScope&)> content, const ButtonGroupOptions& options = {});

}
