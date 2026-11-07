#include "kiln/material3/SeedColorScheme.h"

#include "cpp/scheme/scheme_monochrome.h"
#include "cpp/scheme/scheme_neutral.h"
#include "cpp/scheme/scheme_tonal_spot.h"
#include "cpp/scheme/scheme_vibrant.h"
#include "cpp/scheme/scheme_expressive.h"
#include "cpp/scheme/scheme_fidelity.h"
#include "cpp/scheme/scheme_content.h"
#include "cpp/scheme/scheme_rainbow.h"
#include "cpp/scheme/scheme_fruit_salad.h"

namespace kiln::material3 {

    namespace mcu = material_color_utilities;

    namespace {
        ColorScheme FromDynamicScheme(const mcu::DynamicScheme& d) {
            ColorScheme s;
            s.primary = Color(d.GetPrimary());
            s.onPrimary = Color(d.GetOnPrimary());
            s.primaryContainer = Color(d.GetPrimaryContainer());
            s.onPrimaryContainer = Color(d.GetOnPrimaryContainer());
            s.inversePrimary = Color(d.GetInversePrimary());
            s.secondary = Color(d.GetSecondary());
            s.onSecondary = Color(d.GetOnSecondary());
            s.secondaryContainer = Color(d.GetSecondaryContainer());
            s.onSecondaryContainer = Color(d.GetOnSecondaryContainer());
            s.tertiary = Color(d.GetTertiary());
            s.onTertiary = Color(d.GetOnTertiary());
            s.tertiaryContainer = Color(d.GetTertiaryContainer());
            s.onTertiaryContainer = Color(d.GetOnTertiaryContainer());
            s.background = Color(d.GetBackground());
            s.onBackground = Color(d.GetOnBackground());
            s.surface = Color(d.GetSurface());
            s.onSurface = Color(d.GetOnSurface());
            s.surfaceVariant = Color(d.GetSurfaceVariant());
            s.onSurfaceVariant = Color(d.GetOnSurfaceVariant());
            s.surfaceTint = Color(d.GetSurfaceTint());
            s.inverseSurface = Color(d.GetInverseSurface());
            s.inverseOnSurface = Color(d.GetInverseOnSurface());
            s.error = Color(d.GetError());
            s.onError = Color(d.GetOnError());
            s.errorContainer = Color(d.GetErrorContainer());
            s.onErrorContainer = Color(d.GetOnErrorContainer());
            s.outline = Color(d.GetOutline());
            s.outlineVariant = Color(d.GetOutlineVariant());
            s.scrim = Color(d.GetScrim());
            s.surfaceBright = Color(d.GetSurfaceBright());
            s.surfaceDim = Color(d.GetSurfaceDim());
            s.surfaceContainer = Color(d.GetSurfaceContainer());
            s.surfaceContainerHigh = Color(d.GetSurfaceContainerHigh());
            s.surfaceContainerHighest = Color(d.GetSurfaceContainerHighest());
            s.surfaceContainerLow = Color(d.GetSurfaceContainerLow());
            s.surfaceContainerLowest = Color(d.GetSurfaceContainerLowest());
            s.primaryFixed = Color(d.GetPrimaryFixed());
            s.primaryFixedDim = Color(d.GetPrimaryFixedDim());
            s.onPrimaryFixed = Color(d.GetOnPrimaryFixed());
            s.onPrimaryFixedVariant = Color(d.GetOnPrimaryFixedVariant());
            s.secondaryFixed = Color(d.GetSecondaryFixed());
            s.secondaryFixedDim = Color(d.GetSecondaryFixedDim());
            s.onSecondaryFixed = Color(d.GetOnSecondaryFixed());
            s.onSecondaryFixedVariant = Color(d.GetOnSecondaryFixedVariant());
            s.tertiaryFixed = Color(d.GetTertiaryFixed());
            s.tertiaryFixedDim = Color(d.GetTertiaryFixedDim());
            s.onTertiaryFixed = Color(d.GetOnTertiaryFixed());
            s.onTertiaryFixedVariant = Color(d.GetOnTertiaryFixedVariant());
            return s;
        }
    }

    ColorScheme ColorSchemeFromSeed(Color seed, bool dark, SchemeVariant variant, double contrastLevel) {
        const mcu::Hct hct(seed.argb);
        switch (variant) {
            case SchemeVariant::Monochrome: return FromDynamicScheme(mcu::SchemeMonochrome(hct, dark, contrastLevel));
            case SchemeVariant::Neutral: return FromDynamicScheme(mcu::SchemeNeutral(hct, dark, contrastLevel));
            case SchemeVariant::TonalSpot: return FromDynamicScheme(mcu::SchemeTonalSpot(hct, dark, contrastLevel));
            case SchemeVariant::Vibrant: return FromDynamicScheme(mcu::SchemeVibrant(hct, dark, contrastLevel));
            case SchemeVariant::Expressive: return FromDynamicScheme(mcu::SchemeExpressive(hct, dark, contrastLevel));
            case SchemeVariant::Fidelity: return FromDynamicScheme(mcu::SchemeFidelity(hct, dark, contrastLevel));
            case SchemeVariant::Content: return FromDynamicScheme(mcu::SchemeContent(hct, dark, contrastLevel));
            case SchemeVariant::Rainbow: return FromDynamicScheme(mcu::SchemeRainbow(hct, dark, contrastLevel));
            case SchemeVariant::FruitSalad: return FromDynamicScheme(mcu::SchemeFruitSalad(hct, dark, contrastLevel));
        }
        return FromDynamicScheme(mcu::SchemeTonalSpot(hct, dark, contrastLevel));
    }

}
