#pragma once

#include "kiln/foundation/Interaction.h"

// Focus and key modifiers over kiln::ui's focus system (spec 03 req. 4-5, 06 req. 3).

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    using namespace kiln::ui;

    // focusTarget with a focusability and a state callback.
    Modifier Focusable(Modifier m, Focusability f = Focusability::Always,
                       std::function<void(FocusState)> onFocusChange = {},
                       std::shared_ptr<MutableInteractionSource> interactionSource = nullptr);
    // focusProperties { }: applies to the next focusable down the chain (or in children).
    Modifier FocusPropertiesMod(Modifier m, std::function<void(ui::FocusProperties&)> properties);
    // onFocusChanged: reports the state of the first focus target after it (deduplicated).
    Modifier OnFocusChanged(Modifier m, std::function<void(FocusState)> onChange);
    Modifier FocusRequesterModifier(Modifier m, FocusRequester& requester);
    // focusRestorer: a focus group that remembers which child had focus when focus left it and
    // gives it back when focus comes in again. `fallback` when that child is gone: null enters
    // the default child, FocusProperties::Cancel() keeps focus out.
    Modifier FocusRestorer(Modifier m, FocusRequester* fallback = nullptr);
    // focusGroup: a never-focusable focus target grouping its children (focus searches stay
    // inside first; focusProperties before it, such as onEnter / onExit, apply to the group).
    Modifier FocusGroup(Modifier m);
    // onKeyEvent / onPreviewKeyEvent: return true to consume.
    Modifier OnKeyEvent(Modifier m, std::function<bool(const KeyEvent&)> onKey,
                        std::function<bool(const KeyEvent&)> onPreview = {});
    // onInterceptKeyBeforeSoftKeyboard / onPreInterceptKeyBeforeSoftKeyboard: keys before the
    // IME sees them, when focused. Return true to consume.
    Modifier OnInterceptKeyBeforeSoftKeyboard(Modifier m, std::function<bool(const KeyEvent&)> onKey,
                                              std::function<bool(const KeyEvent&)> onPreKey = {});
    // onRotaryScrollEvent / onPreRotaryScrollEvent: rotary encoder turns, when focused.
    Modifier OnRotaryScrollEvent(Modifier m, std::function<bool(const RotaryScrollEvent&)> onEvent,
                                 std::function<bool(const RotaryScrollEvent&)> onPreEvent = {});

}
