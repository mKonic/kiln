#pragma once

// ToggleableState (ui state/ToggleableState.kt): the state of a tri-state toggle.

#include <cstdint>

namespace kiln::ui {

    enum class ToggleableState : std::uint8_t { Off = 0, On = 1, Indeterminate = 2 };

    // ToggleableState(value: Boolean): On or Off.
    constexpr ToggleableState ToggleableStateOf(bool value) { return value ? ToggleableState::On : ToggleableState::Off; }

}
