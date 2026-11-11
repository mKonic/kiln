#pragma once

// NavigationDrawer.kt: navigation drawers. A modal drawer slides in from the start edge over a
// scrim (a tap on it, Back, Escape or the dismiss action closes it); a dismissible drawer pushes
// the content aside; a permanent drawer always shows beside it. Modal and dismissible drawers
// open and close by drag and fling; their sheets follow the predictive back gesture. Drawer
// items are selectable tabs with an icon, a label and a badge.

#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    enum class DrawerValue : std::uint8_t { Closed, Open };

    class DrawerState {
    public:
        explicit DrawerState(DrawerValue initialValue, std::function<bool(DrawerValue)> confirmStateChange = {});

        // Reading these subscribes.
        bool IsOpen() const { return CurrentValue() == DrawerValue::Open; }
        bool IsClosed() const { return CurrentValue() == DrawerValue::Closed; }
        DrawerValue CurrentValue() const { return m_Anchored.SettledValue(); }
        DrawerValue TargetValue() const { return m_Anchored.TargetValue(); }
        bool IsAnimationRunning() const { return m_Anchored.IsAnimationRunning(); }
        // The sheet's offset in pixels (0 open, minus its width closed); NaN before it is laid out.
        float CurrentOffset() const { return m_Anchored.Offset(); }

        // Each animates only if confirmStateChange agrees, from the last drag velocity.
        rt::Task<> Open();
        rt::Task<> Close();
        rt::Task<> AnimateTo(DrawerValue target, anim::SpecPtr spec);
        rt::Task<> SnapTo(DrawerValue target);

        // Internal.
        bool Confirm(DrawerValue v) const { return !m_Confirm || m_Confirm(v); }
        foundation::AnchoredDraggableState<DrawerValue>& Anchored() { return m_Anchored; }
        const foundation::AnchoredDraggableState<DrawerValue>& Anchored() const { return m_Anchored; }
        anim::SpecPtr openDrawerMotionSpec, closeDrawerMotionSpec;

    private:
        std::function<bool(DrawerValue)> m_Confirm;
        foundation::AnchoredDraggableState<DrawerValue> m_Anchored;
    };

    // rememberDrawerState: the value survives re-creation (DrawerState.Saver).
    std::shared_ptr<DrawerState> RememberDrawerState(DrawerValue initialValue, std::function<bool(DrawerValue)> confirmStateChange = {});

    namespace DrawerDefaults {
        extern const ui::Dp ModalDrawerElevation, PermanentDrawerElevation, DismissibleDrawerElevation, MaximumDrawerWidth;
        foundation::ShapePtr shape();
        ui::Color scrimColor();
        ui::Color standardContainerColor();
        ui::Color modalContainerColor();
        // System bars and the display cutout, top, bottom and start.
        foundation::WindowInsets windowInsets();
    }

    struct ModalNavigationDrawerOptions {
        std::shared_ptr<DrawerState> drawerState;   // null: RememberDrawerState(Closed)
        bool gesturesEnabled = true;
        std::optional<ui::Color> scrimColor;         // unset: DrawerDefaults::scrimColor
    };
    void ModalNavigationDrawer(std::function<void()> drawerContent, const ui::Modifier& modifier, const ModalNavigationDrawerOptions& options,
                               std::function<void()> content);

    struct DismissibleNavigationDrawerOptions {
        std::shared_ptr<DrawerState> drawerState;   // null: RememberDrawerState(Closed)
        bool gesturesEnabled = true;
    };
    void DismissibleNavigationDrawer(std::function<void()> drawerContent, const ui::Modifier& modifier,
                                     const DismissibleNavigationDrawerOptions& options, std::function<void()> content);

    void PermanentNavigationDrawer(std::function<void()> drawerContent, const ui::Modifier& modifier, std::function<void()> content);

    // The drawer sheets. Unset fields take the kind's defaults: modal sheets have the rounded end
    // shape and modalContainerColor; dismissible and permanent ones are rectangles in
    // standardContainerColor.
    struct DrawerSheetOptions {
        foundation::ShapePtr drawerShape;
        std::optional<ui::Color> drawerContainerColor, drawerContentColor;
        std::optional<ui::Dp> drawerTonalElevation;
        std::optional<foundation::WindowInsets> windowInsets;   // unset: DrawerDefaults::windowInsets
    };
    // With a drawerState, the sheet follows the predictive back gesture and Back closes the drawer.
    void ModalDrawerSheet(const ui::Modifier& modifier, const DrawerSheetOptions& options, std::function<void()> content,
                          std::shared_ptr<DrawerState> drawerState = nullptr);
    void DismissibleDrawerSheet(const ui::Modifier& modifier, const DrawerSheetOptions& options, std::function<void()> content,
                                std::shared_ptr<DrawerState> drawerState = nullptr);
    void PermanentDrawerSheet(const ui::Modifier& modifier, const DrawerSheetOptions& options, std::function<void()> content);

    struct NavigationDrawerItemColors {
        ui::Color selectedIconColor, unselectedIconColor, selectedTextColor, unselectedTextColor, selectedContainerColor, unselectedContainerColor,
            selectedBadgeColor, unselectedBadgeColor;
        ui::Color IconColor(bool selected) const { return selected ? selectedIconColor : unselectedIconColor; }
        ui::Color TextColor(bool selected) const { return selected ? selectedTextColor : unselectedTextColor; }
        ui::Color BadgeColor(bool selected) const { return selected ? selectedBadgeColor : unselectedBadgeColor; }
        ui::Color ContainerColor(bool selected) const { return selected ? selectedContainerColor : unselectedContainerColor; }
        bool operator==(const NavigationDrawerItemColors&) const = default;
    };

    namespace NavigationDrawerItemDefaults {
        NavigationDrawerItemColors colors();
        // Around each item in a drawer: 12 dp each side.
        inline const foundation::PaddingValues ItemPadding{ ui::Dp(12), ui::Dp(0), ui::Dp(12), ui::Dp(0) };
    }

    struct NavigationDrawerItemOptions {
        std::function<void()> icon, badge;
        foundation::ShapePtr shape;                     // null: a full pill
        std::optional<NavigationDrawerItemColors> colors;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
    };
    void NavigationDrawerItem(std::function<void()> label, bool selected, std::function<void()> onClick, const ui::Modifier& modifier = {},
                              const NavigationDrawerItemOptions& options = {});

}
