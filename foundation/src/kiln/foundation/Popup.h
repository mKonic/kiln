#pragma once

// Popup (androidx.compose.ui.window.Popup, AndroidPopup.android.kt): content shown above
// everything else in the window, placed by a PopupPositionProvider against the bounds of the
// layout it is called in. kiln draws it in the window's overlay layer (see kiln/ui/Popup.h for
// how it takes touches, key focus and dismissal). Its content composes with the caller's
// composition locals.

#include "kiln/foundation/Compose.h"
#include "kiln/ui/Popup.h"

#include <memory>

namespace kiln::foundation {

    using ui::AlignmentOffsetPositionProvider;
    using ui::PopupPositionProvider;
    using ui::PopupProperties;
    using ui::SecureFlagPolicy;

    // LocalIsInPopupLayout: true inside a popup's content (nested popups know it).
    inline rt::Local<bool> LocalIsInPopupLayout{ false };

    void Popup(std::shared_ptr<const PopupPositionProvider> popupPositionProvider, std::function<void()> onDismissRequest,
               PopupProperties properties, std::function<void()> content, Key key = {});

    // The popup's alignment point sits on the same point of the anchor, moved by `offset`
    // (pixels; x mirrored in RTL).
    void Popup(Alignment alignment, IntOffset offset, std::function<void()> onDismissRequest, PopupProperties properties,
               std::function<void()> content, Key key = {});
    inline void Popup(std::function<void()> content, Key key = {}) {
        Popup(Alignment::TopStart(), {}, {}, {}, std::move(content), key);
    }

}
