#pragma once

// preferredFrameRate (ui FrameRate.kt): content that asks the display for a frame rate while it
// draws (a rate in Hz, or a category). Votes of everything drawn in a frame combine as the
// platform's frame-rate voting does: the highest wins.

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    ui::Modifier PreferredFrameRate(ui::Modifier m, float frameRate);
    ui::Modifier PreferredFrameRate(ui::Modifier m, ui::FrameRateCategory frameRateCategory);

}
