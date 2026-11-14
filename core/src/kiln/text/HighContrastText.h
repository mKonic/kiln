#pragma once

// High-contrast text (the accessibility setting), as Android draws it for every app's text:
// HWUI's DrawTextFunctor (frameworks/base libs/hwui/hwui/DrawTextFunctor.h) outlines each glyph
// in black or white and pushes the fill's colour away from the middle (in CIE Lab), and
// android.text.Layout (drawHighContrastBackground, API 36+) puts a translucent rounded
// rectangle behind each line's characters.

#include "kiln/ui/Geometry.h"

#include <algorithm>

namespace kiln::text {

    // Whether paragraphs are laid out with high-contrast outline glyphs (the setting is the
    // same for every window). The host sets it, then lays text out again.
    void SetHighContrastOutlines(bool enabled);
    bool HighContrastOutlines();

    // kHighContrastTextBorderWidth / kHighContrastTextBorderWidthFactor: the outline stroke
    // (stroke-and-fill, round joins) is max(4, 0.2 x text size) px wide.
    inline float HighContrastBorderWidth(float textSizePx) { return std::max(4.0f, 0.2f * textSizePx); }
    // shouldDarkenTextForHighContrast: Lab lightness at most 50. Dark text gets a white outline
    // and background, light text black ones.
    bool ShouldDarkenForHighContrast(ui::Color color);
    // adjustHighContrastInnerTextColor: the fill. Near-grey text goes black or white (to 20 or
    // 80 when it was near the middle); coloured text keeps its hue with lightness at most 20
    // (dark) or at least 90 (light). Opaque, as HWUI's simplified paint is.
    ui::Color HighContrastInnerColor(ui::Color color);
    // The outline colour.
    inline ui::Color HighContrastOutlineColor(ui::Color color) {
        return ShouldDarkenForHighContrast(color) ? ui::Color(0xFFFFFFFFu) : ui::Color(0xFF000000u);
    }
    // Layout's background: 70% white behind dark text, 70% black behind light text, corners
    // max(5 dp, 0.5 x text size), no padding.
    ui::Color HighContrastBackgroundColor(ui::Color textColor);
    inline float HighContrastBackgroundRadius(float textSizePx, float density) { return std::max(5.0f * density, 0.5f * textSizePx); }

}
