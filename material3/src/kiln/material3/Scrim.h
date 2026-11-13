#pragma once

// Scrim.kt: a full-size scrim that obscures the content behind a modal surface and, when it has
// an onClick, dismisses it on a tap or an accessibility click.

#include "kiln/foundation/Layouts.h"
#include <kiln/Function.h>

#include <functional>
#include <optional>
#include <string>

namespace kiln::material3 {

    namespace ScrimDefaults {
        ui::Color color();   // the scrim colour at 32%
    }

    struct ScrimOptions {
        kiln::Function<void()> onClick = [] {};   // null: no click and no click semantics
        kiln::Function<float()> alpha;            // unset: 1; coerced into 0..1
        std::optional<ui::Color> color;          // unset: ScrimDefaults::color; Unspecified draws nothing
    };
    // contentDescription: what the scrim dismisses, for accessibility services.
    void Scrim(std::optional<std::string> contentDescription, const ui::Modifier& modifier, const ScrimOptions& options);

}
