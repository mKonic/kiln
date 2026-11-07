#pragma once

// Colour schemes from a seed colour (material-color-utilities: DynamicScheme and its scheme
// variants), with every ColorScheme role from MaterialDynamicColors (spec 08 req. 3).

#include "kiln/material3/ColorScheme.h"

namespace kiln::material3 {

    // material_color_utilities::Variant
    enum class SchemeVariant { Monochrome, Neutral, TonalSpot, Vibrant, Expressive, Fidelity, Content, Rainbow, FruitSalad };

    // The scheme of `variant` for `seed`; contrastLevel -1 (reduced) to 1 (high), 0 standard.
    ColorScheme ColorSchemeFromSeed(Color seed, bool dark, SchemeVariant variant = SchemeVariant::TonalSpot, double contrastLevel = 0.0);

}
