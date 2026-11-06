#pragma once

#include "kiln/TextBackend.h"

#include <memory>

namespace kiln::hostdev {
    // HarfBuzz + FreeType backend for the Linux development host (architecture 3.5: close to
    // Android, not identical). Greedy word wrap, one font plus an emoji fallback, no bidi yet.
    std::unique_ptr<TextBackend> CreateHostText(float density, float fontScale);
}
