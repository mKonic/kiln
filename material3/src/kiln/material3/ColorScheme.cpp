#include "kiln/material3/ColorScheme.h"
#include <kiln/Function.h>

#include "kiln/material3/tokens/ColorDarkTokens.h"
#include "kiln/material3/tokens/ColorLightTokens.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/PaletteTokens.h"

#include <cmath>

namespace kiln::material3 {

    namespace {
        template<typename F> ColorScheme Customized(ColorScheme s, F&& customize) {
            s.surfaceTint = Color::Unspecified();
            if (customize) customize(s);
            if (!s.surfaceTint.Specified()) s.surfaceTint = s.primary;   // surfaceTint: Color = primary
            return s;
        }
    }

    ColorScheme LightColorScheme(const kiln::Function<void(ColorScheme&)>& customize) {
        ColorScheme s;
        s.primary = tokens::ColorLightTokens::Primary;
        s.onPrimary = tokens::ColorLightTokens::OnPrimary;
        s.primaryContainer = tokens::ColorLightTokens::PrimaryContainer;
        s.onPrimaryContainer = tokens::ColorLightTokens::OnPrimaryContainer;
        s.inversePrimary = tokens::ColorLightTokens::InversePrimary;
        s.secondary = tokens::ColorLightTokens::Secondary;
        s.onSecondary = tokens::ColorLightTokens::OnSecondary;
        s.secondaryContainer = tokens::ColorLightTokens::SecondaryContainer;
        s.onSecondaryContainer = tokens::ColorLightTokens::OnSecondaryContainer;
        s.tertiary = tokens::ColorLightTokens::Tertiary;
        s.onTertiary = tokens::ColorLightTokens::OnTertiary;
        s.tertiaryContainer = tokens::ColorLightTokens::TertiaryContainer;
        s.onTertiaryContainer = tokens::ColorLightTokens::OnTertiaryContainer;
        s.background = tokens::ColorLightTokens::Background;
        s.onBackground = tokens::ColorLightTokens::OnBackground;
        s.surface = tokens::ColorLightTokens::Surface;
        s.onSurface = tokens::ColorLightTokens::OnSurface;
        s.surfaceVariant = tokens::ColorLightTokens::SurfaceVariant;
        s.onSurfaceVariant = tokens::ColorLightTokens::OnSurfaceVariant;
        s.inverseSurface = tokens::ColorLightTokens::InverseSurface;
        s.inverseOnSurface = tokens::ColorLightTokens::InverseOnSurface;
        s.error = tokens::ColorLightTokens::Error;
        s.onError = tokens::ColorLightTokens::OnError;
        s.errorContainer = tokens::ColorLightTokens::ErrorContainer;
        s.onErrorContainer = tokens::ColorLightTokens::OnErrorContainer;
        s.outline = tokens::ColorLightTokens::Outline;
        s.outlineVariant = tokens::ColorLightTokens::OutlineVariant;
        s.scrim = tokens::ColorLightTokens::Scrim;
        s.surfaceBright = tokens::ColorLightTokens::SurfaceBright;
        s.surfaceDim = tokens::ColorLightTokens::SurfaceDim;
        s.surfaceContainer = tokens::ColorLightTokens::SurfaceContainer;
        s.surfaceContainerHigh = tokens::ColorLightTokens::SurfaceContainerHigh;
        s.surfaceContainerHighest = tokens::ColorLightTokens::SurfaceContainerHighest;
        s.surfaceContainerLow = tokens::ColorLightTokens::SurfaceContainerLow;
        s.surfaceContainerLowest = tokens::ColorLightTokens::SurfaceContainerLowest;
        s.primaryFixed = tokens::ColorLightTokens::PrimaryFixed;
        s.primaryFixedDim = tokens::ColorLightTokens::PrimaryFixedDim;
        s.onPrimaryFixed = tokens::ColorLightTokens::OnPrimaryFixed;
        s.onPrimaryFixedVariant = tokens::ColorLightTokens::OnPrimaryFixedVariant;
        s.secondaryFixed = tokens::ColorLightTokens::SecondaryFixed;
        s.secondaryFixedDim = tokens::ColorLightTokens::SecondaryFixedDim;
        s.onSecondaryFixed = tokens::ColorLightTokens::OnSecondaryFixed;
        s.onSecondaryFixedVariant = tokens::ColorLightTokens::OnSecondaryFixedVariant;
        s.tertiaryFixed = tokens::ColorLightTokens::TertiaryFixed;
        s.tertiaryFixedDim = tokens::ColorLightTokens::TertiaryFixedDim;
        s.onTertiaryFixed = tokens::ColorLightTokens::OnTertiaryFixed;
        s.onTertiaryFixedVariant = tokens::ColorLightTokens::OnTertiaryFixedVariant;
        return Customized(s, customize);
    }

    ColorScheme DarkColorScheme(const kiln::Function<void(ColorScheme&)>& customize) {
        ColorScheme s;
        s.primary = tokens::ColorDarkTokens::Primary;
        s.onPrimary = tokens::ColorDarkTokens::OnPrimary;
        s.primaryContainer = tokens::ColorDarkTokens::PrimaryContainer;
        s.onPrimaryContainer = tokens::ColorDarkTokens::OnPrimaryContainer;
        s.inversePrimary = tokens::ColorDarkTokens::InversePrimary;
        s.secondary = tokens::ColorDarkTokens::Secondary;
        s.onSecondary = tokens::ColorDarkTokens::OnSecondary;
        s.secondaryContainer = tokens::ColorDarkTokens::SecondaryContainer;
        s.onSecondaryContainer = tokens::ColorDarkTokens::OnSecondaryContainer;
        s.tertiary = tokens::ColorDarkTokens::Tertiary;
        s.onTertiary = tokens::ColorDarkTokens::OnTertiary;
        s.tertiaryContainer = tokens::ColorDarkTokens::TertiaryContainer;
        s.onTertiaryContainer = tokens::ColorDarkTokens::OnTertiaryContainer;
        s.background = tokens::ColorDarkTokens::Background;
        s.onBackground = tokens::ColorDarkTokens::OnBackground;
        s.surface = tokens::ColorDarkTokens::Surface;
        s.onSurface = tokens::ColorDarkTokens::OnSurface;
        s.surfaceVariant = tokens::ColorDarkTokens::SurfaceVariant;
        s.onSurfaceVariant = tokens::ColorDarkTokens::OnSurfaceVariant;
        s.inverseSurface = tokens::ColorDarkTokens::InverseSurface;
        s.inverseOnSurface = tokens::ColorDarkTokens::InverseOnSurface;
        s.error = tokens::ColorDarkTokens::Error;
        s.onError = tokens::ColorDarkTokens::OnError;
        s.errorContainer = tokens::ColorDarkTokens::ErrorContainer;
        s.onErrorContainer = tokens::ColorDarkTokens::OnErrorContainer;
        s.outline = tokens::ColorDarkTokens::Outline;
        s.outlineVariant = tokens::ColorDarkTokens::OutlineVariant;
        s.scrim = tokens::ColorDarkTokens::Scrim;
        s.surfaceBright = tokens::ColorDarkTokens::SurfaceBright;
        s.surfaceDim = tokens::ColorDarkTokens::SurfaceDim;
        s.surfaceContainer = tokens::ColorDarkTokens::SurfaceContainer;
        s.surfaceContainerHigh = tokens::ColorDarkTokens::SurfaceContainerHigh;
        s.surfaceContainerHighest = tokens::ColorDarkTokens::SurfaceContainerHighest;
        s.surfaceContainerLow = tokens::ColorDarkTokens::SurfaceContainerLow;
        s.surfaceContainerLowest = tokens::ColorDarkTokens::SurfaceContainerLowest;
        s.primaryFixed = tokens::ColorDarkTokens::PrimaryFixed;
        s.primaryFixedDim = tokens::ColorDarkTokens::PrimaryFixedDim;
        s.onPrimaryFixed = tokens::ColorDarkTokens::OnPrimaryFixed;
        s.onPrimaryFixedVariant = tokens::ColorDarkTokens::OnPrimaryFixedVariant;
        s.secondaryFixed = tokens::ColorDarkTokens::SecondaryFixed;
        s.secondaryFixedDim = tokens::ColorDarkTokens::SecondaryFixedDim;
        s.onSecondaryFixed = tokens::ColorDarkTokens::OnSecondaryFixed;
        s.onSecondaryFixedVariant = tokens::ColorDarkTokens::OnSecondaryFixedVariant;
        s.tertiaryFixed = tokens::ColorDarkTokens::TertiaryFixed;
        s.tertiaryFixedDim = tokens::ColorDarkTokens::TertiaryFixedDim;
        s.onTertiaryFixed = tokens::ColorDarkTokens::OnTertiaryFixed;
        s.onTertiaryFixedVariant = tokens::ColorDarkTokens::OnTertiaryFixedVariant;
        return Customized(s, customize);
    }

    ColorScheme ExpressiveLightColorScheme() {
        return LightColorScheme([](ColorScheme& s) {
            s.onPrimaryContainer = tokens::PaletteTokens::Primary30;
            s.onSecondaryContainer = tokens::PaletteTokens::Secondary30;
            s.onTertiaryContainer = tokens::PaletteTokens::Tertiary30;
            s.onErrorContainer = tokens::PaletteTokens::Error30;
        });
    }

    Color ContentColorFor(const ColorScheme& s, Color b) {
        // `when` order: the first matching role wins.
        if (b == s.primary) return s.onPrimary;
        if (b == s.secondary) return s.onSecondary;
        if (b == s.tertiary) return s.onTertiary;
        if (b == s.background) return s.onBackground;
        if (b == s.error) return s.onError;
        if (b == s.primaryContainer) return s.onPrimaryContainer;
        if (b == s.secondaryContainer) return s.onSecondaryContainer;
        if (b == s.tertiaryContainer) return s.onTertiaryContainer;
        if (b == s.errorContainer) return s.onErrorContainer;
        if (b == s.inverseSurface) return s.inverseOnSurface;
        if (b == s.surface) return s.onSurface;
        if (b == s.surfaceVariant) return s.onSurfaceVariant;
        if (b == s.surfaceBright) return s.onSurface;
        if (b == s.surfaceContainer) return s.onSurface;
        if (b == s.surfaceContainerHigh) return s.onSurface;
        if (b == s.surfaceContainerHighest) return s.onSurface;
        if (b == s.surfaceContainerLow) return s.onSurface;
        if (b == s.surfaceContainerLowest) return s.onSurface;
        if (b == s.surfaceDim) return s.onSurface;
        if (b == s.primaryFixed) return s.onPrimaryFixed;
        if (b == s.primaryFixedDim) return s.onPrimaryFixed;
        if (b == s.secondaryFixed) return s.onSecondaryFixed;
        if (b == s.secondaryFixedDim) return s.onSecondaryFixed;
        if (b == s.tertiaryFixed) return s.onTertiaryFixed;
        if (b == s.tertiaryFixedDim) return s.onTertiaryFixed;
        return Color::Unspecified();
    }

    Color SurfaceColorAtElevation(const ColorScheme& s, ui::Dp elevation) {
        if (elevation == ui::Dp(0)) return s.surface;
        const float alpha = ((4.5f * std::log(elevation.value + 1)) + 2.0f) / 100.0f;
        return ui::CompositeOver(s.surfaceTint.WithAlpha(alpha), s.surface);
    }

    Color FromToken(const ColorScheme& s, tokens::ColorToken t) {
        namespace K = tokens::ColorSchemeKeyTokens;
        if (t == K::Primary) return s.primary;
        if (t == K::OnPrimary) return s.onPrimary;
        if (t == K::PrimaryContainer) return s.primaryContainer;
        if (t == K::OnPrimaryContainer) return s.onPrimaryContainer;
        if (t == K::InversePrimary) return s.inversePrimary;
        if (t == K::Secondary) return s.secondary;
        if (t == K::OnSecondary) return s.onSecondary;
        if (t == K::SecondaryContainer) return s.secondaryContainer;
        if (t == K::OnSecondaryContainer) return s.onSecondaryContainer;
        if (t == K::Tertiary) return s.tertiary;
        if (t == K::OnTertiary) return s.onTertiary;
        if (t == K::TertiaryContainer) return s.tertiaryContainer;
        if (t == K::OnTertiaryContainer) return s.onTertiaryContainer;
        if (t == K::Background) return s.background;
        if (t == K::OnBackground) return s.onBackground;
        if (t == K::Surface) return s.surface;
        if (t == K::OnSurface) return s.onSurface;
        if (t == K::SurfaceVariant) return s.surfaceVariant;
        if (t == K::OnSurfaceVariant) return s.onSurfaceVariant;
        if (t == K::SurfaceTint) return s.surfaceTint;
        if (t == K::InverseSurface) return s.inverseSurface;
        if (t == K::InverseOnSurface) return s.inverseOnSurface;
        if (t == K::Error) return s.error;
        if (t == K::OnError) return s.onError;
        if (t == K::ErrorContainer) return s.errorContainer;
        if (t == K::OnErrorContainer) return s.onErrorContainer;
        if (t == K::Outline) return s.outline;
        if (t == K::OutlineVariant) return s.outlineVariant;
        if (t == K::Scrim) return s.scrim;
        if (t == K::SurfaceBright) return s.surfaceBright;
        if (t == K::SurfaceDim) return s.surfaceDim;
        if (t == K::SurfaceContainer) return s.surfaceContainer;
        if (t == K::SurfaceContainerHigh) return s.surfaceContainerHigh;
        if (t == K::SurfaceContainerHighest) return s.surfaceContainerHighest;
        if (t == K::SurfaceContainerLow) return s.surfaceContainerLow;
        if (t == K::SurfaceContainerLowest) return s.surfaceContainerLowest;
        if (t == K::PrimaryFixed) return s.primaryFixed;
        if (t == K::PrimaryFixedDim) return s.primaryFixedDim;
        if (t == K::OnPrimaryFixed) return s.onPrimaryFixed;
        if (t == K::OnPrimaryFixedVariant) return s.onPrimaryFixedVariant;
        if (t == K::SecondaryFixed) return s.secondaryFixed;
        if (t == K::SecondaryFixedDim) return s.secondaryFixedDim;
        if (t == K::OnSecondaryFixed) return s.onSecondaryFixed;
        if (t == K::OnSecondaryFixedVariant) return s.onSecondaryFixedVariant;
        if (t == K::TertiaryFixed) return s.tertiaryFixed;
        if (t == K::TertiaryFixedDim) return s.tertiaryFixedDim;
        if (t == K::OnTertiaryFixed) return s.onTertiaryFixed;
        if (t == K::OnTertiaryFixedVariant) return s.onTertiaryFixedVariant;
        return Color::Unspecified();
    }

}
