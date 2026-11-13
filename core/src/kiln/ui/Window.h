#pragma once

// Real windows for dialogs (architecture 3.4; Compose's DialogWrapper): a dialog's content has
// its own Owner, shown by a platform dialog window the owner's DialogHost makes. The window sizes
// itself to the content (or to the platform's default dialog width), centres it, dims what is
// behind it, and asks for a dismissal on Back, Escape or a press outside the content.

#include "kiln/ui/Configuration.h"
#include <kiln/Function.h>
#include "kiln/ui/Geometry.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace kiln::ui {

    class Owner;

    // What the platform window needs of DialogProperties (the secure policy already resolved
    // against the parent window).
    struct DialogWindowProperties {
        bool dismissOnBackPress = true;
        bool dismissOnClickOutside = true;
        bool secure = false;
        bool usePlatformDefaultWidth = true;
        bool decorFitsSystemWindows = true;
        std::string windowTitle;
        float scrimAlpha = NAN;             // NaN: the theme's dim
        int blurBehindRadiusPx = -1;        // -1: unspecified
        int backgroundBlurRadiusPx = -1;
        LayoutDirection direction = LayoutDirection::Ltr;
        bool operator==(const DialogWindowProperties& o) const {
            return dismissOnBackPress == o.dismissOnBackPress && dismissOnClickOutside == o.dismissOnClickOutside &&
                   secure == o.secure && usePlatformDefaultWidth == o.usePlatformDefaultWidth &&
                   decorFitsSystemWindows == o.decorFitsSystemWindows && windowTitle == o.windowTitle &&
                   (scrimAlpha == o.scrimAlpha || (std::isnan(scrimAlpha) && std::isnan(o.scrimAlpha))) &&
                   blurBehindRadiusPx == o.blurBehindRadiusPx && backgroundBlurRadiusPx == o.backgroundBlurRadiusPx &&
                   direction == o.direction;
        }
    };

    // The platform dialog showing a content owner; destroying it dismisses the window.
    class PlatformDialog {
    public:
        virtual ~PlatformDialog() = default;
        virtual void Update(const DialogWindowProperties& properties) = 0;
    };

    // Makes dialog windows for content owners (show). `onDismissRequest` runs for Back, Escape or
    // a press outside the content when the properties allow it.
    struct DialogHost {
        kiln::Function<std::unique_ptr<PlatformDialog>(Owner& content, const DialogWindowProperties& properties,
                                                       kiln::Function<void()> onDismissRequest)>
            create;
    };

    // Real windows for popups (PopupLayout: a TYPE_APPLICATION_SUB_PANEL window per popup), for
    // owners whose window does not cover the display: a dialog's (it wraps its content), a popup's,
    // a KilnView placed in an app's own layout. A popup there would otherwise be cut to that window.
    // The main window keeps popups in its overlay layer (architecture 3.4).
    struct PopupWindowProperties {
        bool focusable = false;
        bool dismissOnBackPress = true;
        bool dismissOnClickOutside = true;
        bool secure = false;                // the secure policy resolved against the parent window
        bool clippingEnabled = true;        // false: FLAG_LAYOUT_NO_LIMITS
        float scrimAlpha = NAN;             // NaN or 0: none (FLAG_DIM_BEHIND otherwise)
        LayoutDirection direction = LayoutDirection::Ltr;
        bool operator==(const PopupWindowProperties& o) const {
            return focusable == o.focusable && dismissOnBackPress == o.dismissOnBackPress &&
                   dismissOnClickOutside == o.dismissOnClickOutside && secure == o.secure && clippingEnabled == o.clippingEnabled &&
                   (scrimAlpha == o.scrimAlpha || (std::isnan(scrimAlpha) && std::isnan(o.scrimAlpha))) && direction == o.direction;
        }
    };

    // The platform window showing a popup's content owner; destroying it removes the window.
    class PlatformPopup {
    public:
        virtual ~PlatformPopup() = default;
        virtual void Update(const PopupWindowProperties& properties) = 0;
        // Where the window goes, in screen pixels (the first call shows it).
        virtual void Place(IntRect boundsOnScreen) = 0;
    };

    struct PopupHost {
        // `onDismissRequest` runs for a press outside the window (or Escape) when the properties
        // allow it.
        kiln::Function<std::unique_ptr<PlatformPopup>(Owner& content, const PopupWindowProperties& properties,
                                                      kiln::Function<void()> onDismissRequest)>
            create;
        // Where the owner's window is on the screen (window pixel 0,0), in screen pixels.
        kiln::Function<IntOffset()> windowOrigin;
        // getWindowVisibleDisplayFrame (clipping) or the window manager's bounds, screen pixels.
        kiln::Function<IntRect(bool clippingEnabled)> displayBounds;
    };

}
