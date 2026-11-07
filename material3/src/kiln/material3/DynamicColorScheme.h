#pragma once

// Dynamic colour (material3 DynamicTonalPalette.android.kt): colour schemes from the platform's
// Material You colours. API 34+ reads the platform's scheme roles; API 31-33 builds the scheme
// from its tonal palette, synthesising the tones the palette lacks (Color.setLuminance). The
// colours come with the configuration, so callers recompose when the wallpaper colours change.

#include "kiln/material3/ColorScheme.h"
#include "kiln/material3/DynamicColorFields.h"
#include "kiln/ui/Configuration.h"

#include <optional>

namespace kiln::material3 {

    // dynamicTonalPalette: the platform palette with the synthesised tones; none before API 31.
    std::optional<TonalPalette> DynamicTonalPalette(const ui::Configuration& config);
    // dynamicLightColorScheme31 / dynamicDarkColorScheme31
    ColorScheme DynamicLightColorScheme31(const TonalPalette& palette);
    ColorScheme DynamicDarkColorScheme31(const TonalPalette& palette);

    // dynamicLightColorScheme / dynamicDarkColorScheme: none before API 31 (material3 requires
    // API 31; kiln lets the caller pick a fallback).
    std::optional<ColorScheme> DynamicLightColorScheme(const ui::Configuration& config);
    std::optional<ColorScheme> DynamicDarkColorScheme(const ui::Configuration& config);
    // The same for the composition's LocalConfiguration.
    std::optional<ColorScheme> DynamicLightColorScheme();
    std::optional<ColorScheme> DynamicDarkColorScheme();

}
