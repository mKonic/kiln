#pragma once

// internal/MappedInteractionSource.kt: one component's interactions repeated for another, with
// press positions moved by an offset (a navigation item's presses, mapped into its indicator pill
// so the ripple starts under the finger).

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>

#include <functional>
#include <memory>

namespace kiln::material3::internal {

    // A source that repeats `source`'s interactions from now on, each press at its position less
    // delta() (read when the press arrives). Remembered with `source`.
    std::shared_ptr<foundation::MutableInteractionSource> RememberMappedInteractionSource(
        const std::shared_ptr<foundation::MutableInteractionSource>& source, kiln::Function<ui::Offset()> delta);

}
