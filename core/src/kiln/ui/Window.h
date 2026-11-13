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

}
