#pragma once

// Scaffold.kt: the screen layout that places a top bar, a bottom bar, a snackbar host and a
// floating action button around the content, and hands the content the padding those bars and
// the window insets take.

#include "kiln/foundation/Insets.h"
#include <kiln/Function.h>
#include "kiln/foundation/Layouts.h"
#include <cstdint>

#include <functional>
#include <optional>

namespace kiln::material3 {

    // FabPosition: Start, Center and End sit above the bottom bar; EndOverlay overlaps it.
    enum class FabPosition : std::uint8_t { Start, Center, End, EndOverlay };

    namespace ScaffoldDefaults {
        // contentWindowInsets: the system bars and the display cutout.
        foundation::WindowInsets contentWindowInsets();
    }

    struct ScaffoldOptions {
        kiln::Function<void()> topBar, bottomBar, snackbarHost, floatingActionButton;
        FabPosition floatingActionButtonPosition = FabPosition::End;
        std::optional<ui::Color> containerColor;       // unset: colorScheme.background
        std::optional<ui::Color> contentColor;         // unset: contentColorFor(containerColor)
        // Taken into account at the top / bottom only without a top / bottom bar (the bars
        // handle the insets); insets ancestors consumed are excluded.
        std::optional<foundation::WindowInsets> contentWindowInsets;   // unset: ScaffoldDefaults
    };

    // The content receives the padding to apply (and consume): the top bar's height or the top
    // inset, the bottom bar's height or the bottom inset, and the horizontal insets. It
    // recomposes when that padding changes, not on every measure.
    void Scaffold(const ui::Modifier& modifier, const ScaffoldOptions& options, kiln::Function<void(const foundation::PaddingValues&)> content);

}
