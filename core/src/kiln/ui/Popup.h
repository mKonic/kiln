#pragma once

// Popups (androidx.compose.ui.window.Popup): Compose shows each in its own sub-window; kiln draws
// them in an overlay layer of the owning window (architecture 3.4) with the same behaviour. A
// popup's layout node is a child of the owner's window root above the content root, placed where
// its position provider says. Touches inside a popup stay in it; a focusable popup takes every
// touch (it is touch modal) and the key focus, so the content under it is cut off; a press outside
// asks to dismiss it when dismissOnClickOutside (a non-focusable popup lets that press through, as
// ACTION_OUTSIDE does); Back and Escape dismiss a focusable popup when dismissOnBackPress.

#include "kiln/ui/Accessibility.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Geometry.h"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::ui {

    class LayoutNode;
    class FocusTargetNode;

    // PopupPositionProvider: where the popup goes for its anchor's bounds in the window, the
    // window's (visible) size and the popup's measured size, in pixels.
    class PopupPositionProvider {
    public:
        virtual ~PopupPositionProvider() = default;
        virtual IntOffset CalculatePosition(IntRect anchorBounds, IntSize windowSize, LayoutDirection layoutDirection,
                                            IntSize popupContentSize) const = 0;
    };

    // AlignmentOffsetPositionProvider: the popup's alignment point on the anchor's, then the offset
    // (its x mirrored in RTL).
    class AlignmentOffsetPositionProvider final : public PopupPositionProvider {
    public:
        AlignmentOffsetPositionProvider(Alignment alignment, IntOffset offset) : m_Alignment(alignment), m_Offset(offset) {}
        IntOffset CalculatePosition(IntRect anchor, IntSize, LayoutDirection dir, IntSize content) const override {
            const IntOffset anchorPoint = m_Alignment.Align({ 0, 0 }, { anchor.right - anchor.left, anchor.bottom - anchor.top }, dir);
            const IntOffset popupPoint = m_Alignment.Align({ 0, 0 }, content, dir);
            const IntOffset user{ m_Offset.x * (dir == LayoutDirection::Ltr ? 1 : -1), m_Offset.y };
            return { anchor.left + anchorPoint.x - popupPoint.x + user.x, anchor.top + anchorPoint.y - popupPoint.y + user.y };
        }
        bool operator==(const AlignmentOffsetPositionProvider& o) const {
            return m_Alignment.horizontal == o.m_Alignment.horizontal && m_Alignment.vertical == o.m_Alignment.vertical && m_Offset == o.m_Offset;
        }
    private:
        Alignment m_Alignment;
        IntOffset m_Offset;
    };

    // PopupProperties (the Android one). Popups share the window's surface: SecureOn makes the
    // window secure while the popup shows; SecureOff cannot lift the window's own flag.
    // scrimAlpha (NaN: none) dims the content behind the popup.
    struct PopupProperties {
        bool focusable = false;
        bool dismissOnBackPress = true;
        bool dismissOnClickOutside = true;
        SecureFlagPolicy securePolicy = SecureFlagPolicy::Inherit;
        bool excludeFromSystemGesture = true;
        bool clippingEnabled = true;
        bool usePlatformDefaultWidth = false;
        float scrimAlpha = NAN;
        bool operator==(const PopupProperties& o) const {
            return focusable == o.focusable && dismissOnBackPress == o.dismissOnBackPress &&
                   dismissOnClickOutside == o.dismissOnClickOutside && securePolicy == o.securePolicy &&
                   excludeFromSystemGesture == o.excludeFromSystemGesture && clippingEnabled == o.clippingEnabled &&
                   usePlatformDefaultWidth == o.usePlatformDefaultWidth &&
                   (scrimAlpha == o.scrimAlpha || (std::isnan(scrimAlpha) && std::isnan(o.scrimAlpha)));
        }
    };

    // One shown popup (PopupLayout). Its owner keeps them in show order; the last is on top.
    struct PopupLayer {
        LayoutNode* node = nullptr;              // the popup's root; its children are the content
        PopupProperties properties;
        std::function<void()> onDismissRequest;
        // The anchor's bounds in window pixels; nullopt while it is not placed (the popup hides).
        std::function<std::optional<IntRect>()> anchorBounds;
        std::shared_ptr<const PopupPositionProvider> positionProvider;
        LayoutDirection direction = LayoutDirection::Ltr;

        // Set by the owner.
        std::optional<IntRect> bounds;           // where it was placed, window pixels
        FocusTargetNode* parkedFocus = nullptr;  // the focus under it when it took focus
    };

}
