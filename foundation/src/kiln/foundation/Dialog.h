#pragma once

// Dialog (androidx.compose.ui.window.Dialog, AndroidDialog.android.kt): content in a real platform
// window above the app, sized to the content (or the platform's default dialog width) and centred,
// with the window behind it dimmed. Back, Escape and a press outside the content ask to dismiss
// it. The content composes with the caller's composition locals, in its own window owner.

#include <cmath>
#include <kiln/Function.h>

#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/ui/Window.h"

namespace kiln::foundation {

    using ui::SecureFlagPolicy;

    struct DialogProperties {
        bool dismissOnBackPress = true;
        bool dismissOnClickOutside = true;
        SecureFlagPolicy securePolicy = SecureFlagPolicy::Inherit;
        bool usePlatformDefaultWidth = true;
        bool decorFitsSystemWindows = true;
        std::string windowTitle;
        Dp blurBehindRadius = Dp::Unspecified();
        Dp backgroundBlurRadius = Dp::Unspecified();
        float scrimAlpha = NAN;   // NaN: the theme's dim
        // The platform's window enter / exit animation (Android's Animation.Dialog). Material's
        // sheets turn it off, as ModalBottomSheetDialog's theme does (windowAnimationStyle @null):
        // the sheet slides in itself from its first frame.
        bool windowAnimations = true;
        bool operator==(const DialogProperties& o) const {
            return dismissOnBackPress == o.dismissOnBackPress && dismissOnClickOutside == o.dismissOnClickOutside && securePolicy == o.securePolicy &&
                   usePlatformDefaultWidth == o.usePlatformDefaultWidth && decorFitsSystemWindows == o.decorFitsSystemWindows &&
                   windowTitle == o.windowTitle && blurBehindRadius == o.blurBehindRadius && backgroundBlurRadius == o.backgroundBlurRadius &&
                   (scrimAlpha == o.scrimAlpha || (std::isnan(scrimAlpha) && std::isnan(o.scrimAlpha))) && windowAnimations == o.windowAnimations;
        }
    };

    void Dialog(kiln::Function<void()> onDismissRequest, DialogProperties properties, kiln::Function<void()> content, Key key = {});
    // For dialogs that animate with the back gesture (Material's forks of Dialog): the window's
    // back callback is predictive. onBack runs once per gesture with its progress, as a
    // PredictiveBackHandler's, and decides what completing it does (usually onDismissRequest).
    void Dialog(kiln::Function<void()> onDismissRequest, DialogProperties properties, kiln::Function<rt::Task<>(BackProgress&)> onBack,
                kiln::Function<void()> content, Key key = {});

    // The same, with the caller's gate: held from when the window is made until it can show its
    // content (rt::GateOpen on it waits for that), as the content's own animations do.
    void GatedDialog(kiln::Function<void()> onDismissRequest, DialogProperties properties, kiln::Function<rt::Task<>(BackProgress&)> onBack,
                     kiln::Function<void()> content, std::shared_ptr<rt::FrameGate> gate, Key key = {});

}
