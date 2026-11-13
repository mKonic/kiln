#pragma once

// Pieces many samples repeat word for word.

#include <functional>
#include <kiln/Function.h>
#include <string>

namespace catalog::samples {
    // TooltipBox(rememberTooltipPositionProvider(Above), PlainTooltip(text), rememberTooltipState())
    // around `anchor`, with the samples' semantics workaround (b/496338253). shown false: no tooltip.
    void PlainTooltipAbove(const std::string& text, kiln::Function<void()> anchor, bool shown = true);
    // LoremIpsum().values.first(): the tooling's lorem ipsum, 500 words.
    const char* LoremIpsum();
}
