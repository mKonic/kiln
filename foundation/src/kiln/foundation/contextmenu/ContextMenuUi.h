#pragma once

// The context menu's look (androidx.compose.foundation.contextmenu: ContextMenuUi.kt,
// ContextMenuPopupPositionProvider.kt): a column of 48 dp items in a focusable popup at a point,
// flipped to stay inside the window.

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/foundation/Popup.h"

#include <functional>
#include <string>
#include <vector>

namespace kiln::foundation {

    namespace ContextMenuSpec {
        inline constexpr float ContainerWidthMinDp = 112, ContainerWidthMaxDp = 280, ListItemHeightDp = 48;
        inline constexpr float MenuContainerElevationDp = 3, CornerRadiusDp = 4;
        inline constexpr float HorizontalPaddingDp = 12, VerticalPaddingDp = 8, IconSizeDp = 24;
        inline constexpr float DividerHeightDp = 1, DividerVerticalPaddingDp = 8;
        TextStyle LabelTextStyle(Color color);   // 14 sp medium, 20 sp lines, 0.1 sp spacing
    }

    struct ContextMenuColors {
        Color backgroundColor, textColor, iconColor, disabledTextColor, disabledIconColor;
        bool operator==(const ContextMenuColors&) const = default;
    };
    // White with black text; disabled at 38 % alpha.
    ContextMenuColors DefaultContextMenuColors();
    // computeContextMenuColors: the platform popup menu theme's colours (inside a composition;
    // recomputed when the configuration changes), the defaults without a host.
    ContextMenuColors ComputeContextMenuColors();

    // ContextMenuScope: the menu's items and separators, in order.
    class ContextMenuScope {
    public:
        // leadingIcon draws the icon (24 dp box) in the colour it is given.
        void Item(std::string label, kiln::Function<void()> onClick, bool enabled = true, Modifier modifier = {},
                  kiln::Function<void(Color)> leadingIcon = {});
        void Separator();
        void Content(const ContextMenuColors& colors) const;
    private:
        struct Entry {
            bool separator = false;
            std::string label;
            bool enabled = true;
            Modifier modifier;
            kiln::Function<void(Color)> leadingIcon;
            kiln::Function<void()> onClick;
        };
        std::vector<Entry> m_Entries;
    };

    void ContextMenuColumnBuilder(const Modifier& modifier, const ContextMenuColors& colors,
                                  const kiln::Function<void(ContextMenuScope&)>& builder);
    void ContextMenuItem(const std::string& label, bool enabled, const ContextMenuColors& colors, const Modifier& modifier,
                         const kiln::Function<void(Color)>& leadingIcon, kiln::Function<void()> onClick);
    // ContextMenuPopup: the column in a focusable popup.
    void ContextMenuPopup(std::shared_ptr<const PopupPositionProvider> positionProvider, kiln::Function<void()> onDismiss,
                          const Modifier& modifier, kiln::Function<void(ContextMenuScope&)> builder, Key key = {});

    // alignPopupAxis: the popup's start at the position if it fits towards the end, else its end
    // there, else against the window edge (closeAffinity: the start is the near side).
    int AlignPopupAxis(int position, int popupLength, int windowLength, bool closeAffinity = true);

    // ContextMenuPopupPositionProvider: the menu at a point in the anchor's coordinates.
    class ContextMenuPopupPositionProvider final : public PopupPositionProvider {
    public:
        explicit ContextMenuPopupPositionProvider(kiln::Function<IntOffset()> anchorPosition,
                                                  kiln::Function<void(IntOffset, ui::IntRect)> onPositionCalculated = {})
            : m_AnchorPosition(std::move(anchorPosition)), m_OnPositionCalculated(std::move(onPositionCalculated)) {}
        IntOffset CalculatePosition(ui::IntRect anchorBounds, ui::IntSize windowSize, LayoutDirection dir,
                                    ui::IntSize popupContentSize) const override;
    private:
        kiln::Function<IntOffset()> m_AnchorPosition;
        kiln::Function<void(IntOffset, ui::IntRect)> m_OnPositionCalculated;
    };

}
